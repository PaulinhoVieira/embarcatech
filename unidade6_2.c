#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ssd1306.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "hardware/timer.h" // Para usar time_us_64()

// Definições de pinos e constantes
#define I2C_PORT i2c1
#define PINO_SCL 14
#define PINO_SDA 15
#define BTN_B 6
#define BTN_SAIR 5
#define MAX_ITENS_NA_TELA 3

const int SW = 22;
const uint BLUE_LED_PIN = 12;
const uint RED_LED_PIN = 13;
const uint GREEN_LED_PIN = 11;
const uint BUZZER_PIN = 21;
#define OLED_ADDR 0x3C

// Configurações do Wi-Fi e API
#define WIFI_SSID "Lamarck"
#define WIFI_PASS "99712005"
#define API_HOST "embarcatech-api-3b0da5fbc57e.herokuapp.com"
#define API_PORT 80

// Variáveis globais
struct tcp_pcb *tcp_client_pcb = NULL;
ssd1306_t disp;
uint menu_option = 1;
bool running = false;
const uint DEAD_ZONE = 128;
bool tcp_connected = false;
ip_addr_t server_ip;
int saved_menu_option = 0; // Salva a opção selecionada no menu
int saved_start_index = 0; // Salva o índice de início da exibição
int start_index = 0;       // Índice de início da exibição atual

#define ADC_READINGS 10
uint16_t adc_buffer[ADC_READINGS];
uint16_t adc_index = 0;

// Estruturas para componentes e comandos
typedef struct
{
    int id;
    char nome[20];
    bool estado;
    bool carregando; // Novo campo para indicar se está carregando
} Componente;

typedef struct
{
    int id;
    char nome[20];
    int *referencia;
    int num_referencias;
} Comando;

// Inicialização dos componentes e comandos
Componente componentes[] = {
    {1, "luz_1", false, false},
    {2, "luz_2", false, false},
    {3, "luz_3", false, false},
    {4, "luz_4", false, false},
    {5, "luz_5", false, false},
    {6, "luz_6", false, false},
    {7, "luz_7", false, false}};

int referencias_random_luz[] = {1, 2, 3, 4, 5, 6, 7};

Comando comandos[] = {
    {1, "Random Luz", referencias_random_luz, 7}};

bool sw_estado_anterior = true;
bool btn_b_estado_anterior = true;
bool btn_sair_estado_anterior = true;

// Função para exibir uma mensagem no OLED
void exibir_mensagem_no_oled(const char *mensagem, int linha)
{
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, linha, 1, mensagem);
    ssd1306_show(&disp);
}

// Função para desconectar o Wi-Fi
void desconectar_wifi()
{
    printf("Desconectando da rede Wi-Fi...\n");
    exibir_mensagem_no_oled("Desconectando...", 24);
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    printf("Desconectado da rede Wi-Fi!\n");
    exibir_mensagem_no_oled("Wi-Fi OFF!", 24);
}

// Função para inicializar o hardware
void inicializa()
{
    stdio_init_all();
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(PINO_SCL, GPIO_FUNC_I2C);
    gpio_set_function(PINO_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(PINO_SCL);
    gpio_pull_up(PINO_SDA);
    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, OLED_ADDR, I2C_PORT);

    gpio_init(SW);
    gpio_set_dir(SW, GPIO_IN);
    gpio_pull_up(SW);

    gpio_init(BTN_B);
    gpio_set_dir(BTN_B, GPIO_IN);
    gpio_pull_up(BTN_B);

    gpio_init(BTN_SAIR);
    gpio_set_dir(BTN_SAIR, GPIO_IN);
    gpio_pull_up(BTN_SAIR);
}

// Função para detectar borda de descida
bool detectar_borda_baixa(uint gpio, bool *estado_anterior)
{
    bool estado_atual = gpio_get(gpio);
    if (*estado_anterior && !estado_atual)
    {
        *estado_anterior = estado_atual;
        return true;
    }
    *estado_anterior = estado_atual;
    return false;
}

// Função para exibir o menu principal
void print_menu()
{
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 52, 2, 1, "Menu");
    ssd1306_draw_string(&disp, 6, 18, 1.5, "Componentes");
    ssd1306_draw_string(&disp, 6, 30, 1.5, "Comandos");
    ssd1306_draw_empty_square(&disp, 2, 12 + (menu_option - 1) * 12, 120, 18);
    ssd1306_show(&disp);
}

// Função para exibir itens na tela
void print_itens(void *itens, int num_itens, int start_index, const char *titulo, bool mostrar_estado, bool is_comando, int max_itens_na_tela)
{
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 40, 2, 1, titulo);

    for (int i = 0; i < max_itens_na_tela; i++)
    {
        int item_index = start_index + i;
        if (item_index < num_itens)
        {
            if (is_comando)
            {
                Comando *comando = (Comando *)itens;
                ssd1306_draw_string(&disp, 6, 18 + i * 12, 1.5, comando[item_index].nome);
            }
            else
            {
                Componente *componente = (Componente *)itens;
                ssd1306_draw_string(&disp, 6, 18 + i * 12, 1.5, componente[item_index].nome);
                if (mostrar_estado)
                {
                    char estado_str[15];
                    if (componente[item_index].carregando)
                    {
                        snprintf(estado_str, sizeof(estado_str), "|Carregando|");
                    }
                    else
                    {
                        snprintf(estado_str, sizeof(estado_str), "|%d|", componente[item_index].estado ? 1 : 0);
                    }
                    ssd1306_draw_string(&disp, 90, 18 + i * 12, 1.5, estado_str);
                }
            }
        }
    }

    int cursor_pos = menu_option - start_index;
    if (cursor_pos >= 0 && cursor_pos < max_itens_na_tela)
    {
        ssd1306_draw_empty_square(&disp, 2, 12 + cursor_pos * 12, 120, 18);
    }

    // Exibe a mensagem de estado na parte inferior do display
    if (!is_comando)
    {
        Componente *componente = (Componente *)itens;
        char estado_msg[30];
        if (componente[menu_option].carregando)
        {
            snprintf(estado_msg, sizeof(estado_msg), "%s: Carregando...", componente[menu_option].nome);
        }
        else
        {
            snprintf(estado_msg, sizeof(estado_msg), "%s: %s", componente[menu_option].nome, componente[menu_option].estado ? "Ligado" : "Desligado");
        }
        ssd1306_draw_string(&disp, 6, 54, 1, estado_msg); // Linha 54 (última linha)
    }

    ssd1306_show(&disp);
}

// Callback quando recebe resposta da API
static err_t http_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (p == NULL)
    {
        printf("Conexão fechada pelo servidor.\n");
        tcp_close(tpcb);
        tcp_client_pcb = NULL;
        tcp_connected = false;
        return ERR_OK;
    }

    printf("Resposta recebida:\n");
    printf("%.*s\n", p->len, (char *)p->payload);

    pbuf_free(p);

    // Define o estado de "carregando" como falso após a resposta ser recebida
    for (int i = 0; i < sizeof(componentes) / sizeof(componentes[0]); i++)
    {
        if (componentes[i].carregando)
        {
            componentes[i].carregando = false;
        }
    }

    return ERR_OK;
}

// Callback quando a conexão TCP é estabelecida
static err_t http_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    if (err != ERR_OK)
    {
        printf("Erro na conexão TCP: %d\n", err);
        return err;
    }

    printf("Conectado à API!\n");
    tcp_connected = true;

    int device_id = *(int *)arg;
    int is_on = *((int *)arg + 1);

    char json_body[64];
    snprintf(json_body, sizeof(json_body), "{\"is_on\": %d}", is_on);

    char request[256];
    snprintf(request, sizeof(request),
             "PUT /api/devices/%d HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             device_id, API_HOST, strlen(json_body), json_body);

    printf("Enviando requisição:\n%s\n", request);

    err_t write_err = tcp_write(tpcb, request, strlen(request), TCP_WRITE_FLAG_COPY);
    if (write_err != ERR_OK)
    {
        printf("Erro ao escrever no TCP: %d\n", write_err);
        return write_err;
    }

    err_t output_err = tcp_output(tpcb);
    if (output_err != ERR_OK)
    {
        printf("Erro ao enviar dados TCP: %d\n", output_err);
        return output_err;
    }

    printf("Requisição PUT enviada.\n");

    // Define o estado de "carregando" como falso após a requisição ser enviada
    for (int i = 0; i < sizeof(componentes) / sizeof(componentes[0]); i++)
    {
        if (componentes[i].id == device_id)
        {
            componentes[i].carregando = false;
            break;
        }
    }

    return ERR_OK;
}

// Resolver DNS e conectar ao servidor
static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    if (ipaddr)
    {
        printf("Endereço IP da API: %s\n", ipaddr_ntoa(ipaddr));
        tcp_client_pcb = tcp_new();
        if (!tcp_client_pcb)
        {
            printf("Erro ao criar PCB.\n");
            return;
        }
        tcp_recv(tcp_client_pcb, http_recv_callback);
        tcp_arg(tcp_client_pcb, callback_arg);
        tcp_connect(tcp_client_pcb, ipaddr, API_PORT, http_connected_callback);
    }
    else
    {
        printf("Falha na resolução de DNS\n");
    }
}

// Função para tentar resolver o DNS da API
bool resolver_dns_api(int device_id, int is_on)
{
    printf("Tentando resolver DNS da API...\n");

    int params[2] = {device_id, is_on};

    err_t err = dns_gethostbyname(API_HOST, &server_ip, dns_callback, params);
    if (err == ERR_OK)
    {
        printf("DNS da API resolvido! Endereço IP: %s\n", ipaddr_ntoa(&server_ip));
        return true;
    }
    else if (err == ERR_INPROGRESS)
    {
        printf("Aguardando resolução DNS da API...\n");
        for (int i = 0; i < 20; i++)
        {
            uint64_t start_time = time_us_64(); // Tempo inicial em microssegundos
            while (time_us_64() - start_time < 1000000) {
                // Espera 1 segundo (1000000 microssegundos)
            }
            if (server_ip.addr != 0)
            {
                printf("DNS da API resolvido! Endereço IP: %s\n", ipaddr_ntoa(&server_ip));
                return true;
            }
        }
        printf("Tempo esgotado. Falha ao resolver DNS da API.\n");
        return false;
    }
    else
    {
        printf("Falha ao resolver DNS da API. Erro: %d\n", err);
        return false;
    }
}

// Função para conectar ao Wi-Fi
bool conectar_wifi()
{
    printf("Inicializando Wi-Fi...\n");
    exibir_mensagem_no_oled("Iniciando Wi-Fi...", 24);
    if (cyw43_arch_init())
    {
        printf("Falha ao iniciar Wi-Fi\n");
        return false;
    }

    cyw43_arch_enable_sta_mode();
    printf("Conectando ao Wi-Fi...\n");
    exibir_mensagem_no_oled("Conectando...", 24);

    int tentativas = 0;
    while (tentativas < 3)
    {
        printf("Tentativa %d...\n", tentativas + 1);
        if (cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_MIXED_PSK) == 0)
        {
            printf("Wi-Fi conectado!\n");
            exibir_mensagem_no_oled("Wi-Fi OK!", 24);
            return true;
        }
        else
        {
            printf("Falha ao conectar ao Wi-Fi. Tentando novamente...\n");
            tentativas++;
        }
    }

    printf("Falha ao conectar ao Wi-Fi após 3 tentativas.\n");
    return false;
}

void enviar_requisicao_put(int device_id, int is_on)
{
    // Fecha a conexão existente, se houver
    if (tcp_client_pcb != NULL)
    {
        tcp_close(tcp_client_pcb);
        tcp_client_pcb = NULL;
        tcp_connected = false;
    }

    // Tenta resolver o DNS e conectar ao servidor
    if (!resolver_dns_api(device_id, is_on))
    {
        return;
    }

    // Aguarda até que a conexão seja estabelecida
    while (!tcp_connected)
    {
        uint64_t start_time = time_us_64(); // Tempo inicial em microssegundos
        while (time_us_64() - start_time < 100000) {
            // Espera 100 milissegundos (100000 microssegundos)
        }
    }

    // Define o estado de "carregando" como falso após a requisição ser concluída
    for (int i = 0; i < sizeof(componentes) / sizeof(componentes[0]); i++)
    {
        if (componentes[i].id == device_id)
        {
            componentes[i].carregando = false;
            break;
        }
    }
}

void atualizar_estado_no_display(Componente *componente)
{
    char msg[30];
    if (componente->carregando)
    {
        snprintf(msg, sizeof(msg), "%s: Carregando...", componente->nome);
    }
    else
    {
        snprintf(msg, sizeof(msg), "%s: %s", componente->nome, componente->estado ? "Ligado" : "Desligado");
    }
    ssd1306_draw_string(&disp, 6, 18, 1.5, msg); // Atualiza apenas a linha do componente
    ssd1306_show(&disp);                         // Envia apenas a parte alterada para o display
}

void toggle_estado(Componente *componente, int start_index)
{
    // Salva o estado atual do menu
    saved_menu_option = menu_option;
    saved_start_index = start_index;

    // Limpa a tela e exibe "Carregando..."
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 40, 24, 1, "Carregando...");
    ssd1306_show(&disp);

    // Define o estado como "carregando"
    componente->carregando = true;

    // Alterna o estado do componente
    componente->estado = !componente->estado;

    // Envia a requisição PUT para o servidor
    enviar_requisicao_put(componente->id, componente->estado ? 1 : 0);

    // Aguarda um pouco para simular o tempo de carregamento
    busy_wait_us_32(1000000); // 1 segundo de espera

    // Define o estado como "carregando" como falso após a requisição ser concluída
    componente->carregando = false;

    // Restaura o estado do menu
    menu_option = saved_menu_option;
    start_index = saved_start_index;

    // Exibe novamente os itens do menu
    print_itens(componentes, sizeof(componentes) / sizeof(componentes[0]), start_index, "Componentes", true, false, MAX_ITENS_NA_TELA);
}

// Função para exibir e navegar pelos itens
void menu_itens(void *itens, int num_itens, const char *titulo, bool mostrar_estado, bool is_comando, int max_itens_na_tela)
{
    menu_option = saved_menu_option;     // Restaura a opção selecionada
    int start_index = saved_start_index; // Restaura o índice de início

    while (running)
    {
        print_itens(itens, num_itens, start_index, titulo, mostrar_estado, is_comando, max_itens_na_tela);
        adc_select_input(0);
        uint adc_y_raw = adc_read();

        if (adc_y_raw < 1500)
        {
            if (menu_option < num_itens - 1)
            {
                menu_option++;

                if (menu_option >= start_index + max_itens_na_tela)
                {
                    start_index++;
                }
            }
            busy_wait_us_32(250000);
        }
        else if (adc_y_raw > 2500)
        {
            if (menu_option > 0)
            {
                menu_option--;

                if (menu_option < start_index)
                {
                    start_index--;
                }
            }
            busy_wait_us_32(250000);
        }

        if (detectar_borda_baixa(SW, &sw_estado_anterior) || detectar_borda_baixa(BTN_B, &btn_b_estado_anterior))
        {
            int item_selecionado = menu_option;

            if (is_comando)
            {
                Comando *comando = (Comando *)itens;
                if (strcmp(comando[item_selecionado].nome, "Random Luz") == 0)
                {
                    int luz_aleatoria = rand() % 5;
                    toggle_estado(&componentes[luz_aleatoria], start_index); // Passa start_index como argumento

                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 2, 1, "Luz Alterada:");
                    char msg[20];
                    if (componentes[luz_aleatoria].carregando)
                    {
                        snprintf(msg, sizeof(msg), "%s Carregando...", componentes[luz_aleatoria].nome);
                    }
                    else
                    {
                        snprintf(msg, sizeof(msg), "%s %s", componentes[luz_aleatoria].nome, componentes[luz_aleatoria].estado ? "Ligando" : "Desligando");
                    }
                    ssd1306_draw_string(&disp, 6, 18, 1.5, msg);
                    ssd1306_show(&disp);
                    busy_wait_us_32(1000000);
                }
            }
            else
            {
                Componente *componente = (Componente *)itens;
                toggle_estado(&componente[item_selecionado], start_index); // Passa start_index como argumento
            }
        }

        if (detectar_borda_baixa(BTN_SAIR, &btn_sair_estado_anterior))
        {
            running = false;
            menu_option = 0;
        }
    }
}

// Função principal
int main()
{
    inicializa();
    srand(time(NULL));

    // Inicializar o campo carregando como falso
    for (int i = 0; i < sizeof(componentes) / sizeof(componentes[0]); i++)
    {
        componentes[i].carregando = false;
    }

    // Inicializar as variáveis de estado do menu
    saved_menu_option = 0;
    saved_start_index = 0;

    // Conectar ao Wi-Fi uma vez no início
    if (!conectar_wifi())
    {
        printf("Falha ao conectar ao Wi-Fi. Encerrando...\n");
        return 1;
    }

    while (true)
    {
        print_menu();
        adc_select_input(0);
        uint adc_y_raw = adc_read();

        if (adc_y_raw < 1500 && menu_option < 2)
        {
            menu_option++;
            busy_wait_us_32(250000);
        }
        else if (adc_y_raw > 2500 && menu_option > 1)
        {
            menu_option--;
            busy_wait_us_32(250000);
        }

        if (detectar_borda_baixa(SW, &sw_estado_anterior) || detectar_borda_baixa(BTN_B, &btn_b_estado_anterior))
        {
            running = true;
            switch (menu_option)
            {
            case 1:
                menu_itens(componentes, sizeof(componentes) / sizeof(componentes[0]), "Componentes", true, false, MAX_ITENS_NA_TELA);
                break;
            case 2:
                menu_itens(comandos, sizeof(comandos) / sizeof(comandos[0]), "Comandos", false, true, MAX_ITENS_NA_TELA);
                break;
            }
            running = false;
        }
    }

    // Desconectar o Wi-Fi no final
    desconectar_wifi();
    return 0;
}