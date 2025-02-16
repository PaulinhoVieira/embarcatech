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

const int SW = 22; // Pino do botão de seleção
const uint BLUE_LED_PIN = 12; // Pino do LED azul
const uint RED_LED_PIN = 13; // Pino do LED vermelho
const uint GREEN_LED_PIN = 11; // Pino do LED verde
const uint BUZZER_PIN = 21; // Pino do buzzer
#define OLED_ADDR 0x3C // Endereço I2C do display OLED

// Configurações do Wi-Fi e API
#define WIFI_SSID "Lamarck" // Nome da rede Wi-Fi
#define WIFI_PASS "99712005" // Senha da rede Wi-Fi
#define API_HOST "embarcatech-api-3b0da5fbc57e.herokuapp.com" // Host da API
#define API_PORT 80 // Porta da API

// Variáveis globais
struct tcp_pcb *tcp_client_pcb = NULL; // Estrutura para controle da conexão TCP
ssd1306_t disp; // Estrutura para controle do display OLED
uint menu_option = 1; // Opção selecionada no menu
bool running = false; // Estado de execução do menu
const uint DEAD_ZONE = 128; // Zona morta para leitura do ADC
bool tcp_connected = false; // Estado da conexão TCP
ip_addr_t server_ip; // Endereço IP do servidor
int saved_menu_option = 0; // Salva a opção selecionada no menu
int saved_start_index = 0; // Salva o índice de início da exibição
int start_index = 0; // Índice de início da exibição atual

#define ADC_READINGS 10 // Número de leituras do ADC para suavização
uint16_t adc_buffer[ADC_READINGS]; // Buffer para armazenar leituras do ADC
uint16_t adc_index = 0; // Índice atual no buffer do ADC

// Estruturas para componentes e comandos
typedef struct
{
    int id; // ID do componente
    char nome[20]; // Nome do componente
    bool estado; // Estado atual do componente (ligado/desligado)
    bool carregando; // Indica se o componente está carregando (aguardando resposta da API)
} Componente;

typedef struct
{
    int id; // ID do comando
    char nome[20]; // Nome do comando
    int *referencia; // Referência aos componentes que o comando afeta
    int num_referencias; // Número de referências
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

int referencias_random_luz[] = {1, 2, 3, 4, 5, 6, 7}; // Referências para o comando "Random Luz"

Comando comandos[] = {
    {1, "Random Luz", referencias_random_luz, 7}}; // Comando para alternar o estado de uma luz aleatória

bool sw_estado_anterior = true; // Estado anterior do botão SW
bool btn_b_estado_anterior = true; // Estado anterior do botão B
bool btn_sair_estado_anterior = true; // Estado anterior do botão SAIR

// Função para exibir uma mensagem no OLED
void exibir_mensagem_no_oled(const char *mensagem, int linha)
{
    ssd1306_clear(&disp); // Limpa o display
    ssd1306_draw_string(&disp, 0, linha, 1, mensagem); // Exibe a mensagem na linha especificada
    ssd1306_show(&disp); // Atualiza o display
}

// Função para desconectar o Wi-Fi
void desconectar_wifi()
{
    printf("Desconectando da rede Wi-Fi...\n");
    exibir_mensagem_no_oled("Desconectando...", 24); // Exibe mensagem no OLED
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA); // Desconecta da rede Wi-Fi
    printf("Desconectado da rede Wi-Fi!\n");
    exibir_mensagem_no_oled("Wi-Fi OFF!", 24); // Exibe mensagem no OLED
}

// Função para inicializar o hardware
void inicializa()
{
    stdio_init_all(); // Inicializa a comunicação serial
    adc_init(); // Inicializa o ADC
    adc_gpio_init(26); // Configura o pino 26 como entrada do ADC
    adc_gpio_init(27); // Configura o pino 27 como entrada do ADC
    i2c_init(I2C_PORT, 400 * 1000); // Inicializa o I2C com frequência de 400 kHz
    gpio_set_function(PINO_SCL, GPIO_FUNC_I2C); // Configura o pino SCL para função I2C
    gpio_set_function(PINO_SDA, GPIO_FUNC_I2C); // Configura o pino SDA para função I2C
    gpio_pull_up(PINO_SCL); // Habilita pull-up no pino SCL
    gpio_pull_up(PINO_SDA); // Habilita pull-up no pino SDA
    disp.external_vcc = false; // Define que o display não usa VCC externo
    ssd1306_init(&disp, 128, 64, OLED_ADDR, I2C_PORT); // Inicializa o display OLED

    gpio_init(SW); // Inicializa o pino do botão SW
    gpio_set_dir(SW, GPIO_IN); // Configura o pino SW como entrada
    gpio_pull_up(SW); // Habilita pull-up no pino SW

    gpio_init(BTN_B); // Inicializa o pino do botão B
    gpio_set_dir(BTN_B, GPIO_IN); // Configura o pino B como entrada
    gpio_pull_up(BTN_B); // Habilita pull-up no pino B

    gpio_init(BTN_SAIR); // Inicializa o pino do botão SAIR
    gpio_set_dir(BTN_SAIR, GPIO_IN); // Configura o pino SAIR como entrada
    gpio_pull_up(BTN_SAIR); // Habilita pull-up no pino SAIR
}

// Função para detectar borda de descida
bool detectar_borda_baixa(uint gpio, bool *estado_anterior)
{
    bool estado_atual = gpio_get(gpio); // Lê o estado atual do pino
    if (*estado_anterior && !estado_atual) // Verifica se houve uma borda de descida
    {
        *estado_anterior = estado_atual; // Atualiza o estado anterior
        return true; // Retorna verdadeiro se detectou borda de descida
    }
    *estado_anterior = estado_atual; // Atualiza o estado anterior
    return false; // Retorna falso se não detectou borda de descida
}

// Função para exibir o menu principal
void print_menu()
{
    ssd1306_clear(&disp); // Limpa o display
    ssd1306_draw_string(&disp, 52, 2, 1, "Menu"); // Exibe o título "Menu"
    ssd1306_draw_string(&disp, 6, 18, 1.5, "Componentes"); // Exibe a opção "Componentes"
    ssd1306_draw_string(&disp, 6, 30, 1.5, "Comandos"); // Exibe a opção "Comandos"
    ssd1306_draw_empty_square(&disp, 2, 12 + (menu_option - 1) * 12, 120, 18); // Desenha um quadrado vazio ao redor da opção selecionada
    ssd1306_show(&disp); // Atualiza o display
}

// Função para exibir itens na tela
void print_itens(void *itens, int num_itens, int start_index, const char *titulo, bool mostrar_estado, bool is_comando, int max_itens_na_tela)
{
    ssd1306_clear(&disp); // Limpa o display
    ssd1306_draw_string(&disp, 40, 2, 1, titulo); // Exibe o título da tela

    for (int i = 0; i < max_itens_na_tela; i++) // Itera sobre os itens a serem exibidos
    {
        int item_index = start_index + i; // Calcula o índice do item atual
        if (item_index < num_itens) // Verifica se o índice é válido
        {
            if (is_comando) // Se for um comando
            {
                Comando *comando = (Comando *)itens;
                ssd1306_draw_string(&disp, 6, 18 + i * 12, 1.5, comando[item_index].nome); // Exibe o nome do comando
            }
            else // Se for um componente
            {
                Componente *componente = (Componente *)itens;
                ssd1306_draw_string(&disp, 6, 18 + i * 12, 1.5, componente[item_index].nome); // Exibe o nome do componente
                if (mostrar_estado) // Se deve mostrar o estado
                {
                    char estado_str[15];
                    if (componente[item_index].carregando) // Se o componente está carregando
                    {
                        snprintf(estado_str, sizeof(estado_str), "|Carregando|"); // Exibe "Carregando"
                    }
                    else
                    {
                        snprintf(estado_str, sizeof(estado_str), "|%d|", componente[item_index].estado ? 1 : 0); // Exibe o estado (0 ou 1)
                    }
                    ssd1306_draw_string(&disp, 90, 18 + i * 12, 1.5, estado_str); // Exibe o estado ao lado do nome
                }
            }
        }
    }

    int cursor_pos = menu_option - start_index; // Calcula a posição do cursor
    if (cursor_pos >= 0 && cursor_pos < max_itens_na_tela) // Verifica se o cursor está dentro dos limites
    {
        ssd1306_draw_empty_square(&disp, 2, 12 + cursor_pos * 12, 120, 18); // Desenha um quadrado vazio ao redor do item selecionado
    }

    // Exibe a mensagem de estado na parte inferior do display
    if (!is_comando) // Se não for um comando
    {
        Componente *componente = (Componente *)itens;
        char estado_msg[30];
        if (componente[menu_option].carregando) // Se o componente está carregando
        {
            snprintf(estado_msg, sizeof(estado_msg), "%s: Carregando...", componente[menu_option].nome); // Exibe "Carregando..."
        }
        else
        {
            snprintf(estado_msg, sizeof(estado_msg), "%s: %s", componente[menu_option].nome, componente[menu_option].estado ? "Ligado" : "Desligado"); // Exibe o estado atual
        }
        ssd1306_draw_string(&disp, 6, 54, 1, estado_msg); // Exibe a mensagem na última linha do display
    }

    ssd1306_show(&disp); // Atualiza o display
}

// Callback quando recebe resposta da API
static err_t http_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (p == NULL) // Se a conexão foi fechada pelo servidor
    {
        printf("Conexão fechada pelo servidor.\n");
        tcp_close(tpcb); // Fecha a conexão TCP
        tcp_client_pcb = NULL; // Limpa o ponteiro para a conexão
        tcp_connected = false; // Indica que a conexão foi encerrada
        return ERR_OK;
    }

    printf("Resposta recebida:\n");
    printf("%.*s\n", p->len, (char *)p->payload); // Exibe a resposta recebida

    pbuf_free(p); // Libera o buffer de recebimento

    // Define o estado de "carregando" como falso após a resposta ser recebida
    for (int i = 0; i < sizeof(componentes) / sizeof(componentes[0]); i++)
    {
        if (componentes[i].carregando)
        {
            componentes[i].carregando = false; // Atualiza o estado de carregando para falso
        }
    }

    return ERR_OK;
}

// Callback quando a conexão TCP é estabelecida
static err_t http_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    if (err != ERR_OK) // Se houve erro na conexão
    {
        printf("Erro na conexão TCP: %d\n", err);
        return err;
    }

    printf("Conectado à API!\n");
    tcp_connected = true; // Indica que a conexão foi estabelecida

    int device_id = *(int *)arg; // Obtém o ID do dispositivo
    int is_on = *((int *)arg + 1); // Obtém o estado (ligado/desligado)

    char json_body[64];
    snprintf(json_body, sizeof(json_body), "{\"is_on\": %d}", is_on); // Cria o corpo da requisição JSON

    char request[256];
    snprintf(request, sizeof(request),
             "PUT /api/devices/%d HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             device_id, API_HOST, strlen(json_body), json_body); // Cria a requisição HTTP PUT

    printf("Enviando requisição:\n%s\n", request); // Exibe a requisição no console

    err_t write_err = tcp_write(tpcb, request, strlen(request), TCP_WRITE_FLAG_COPY); // Envia a requisição
    if (write_err != ERR_OK) // Se houve erro ao escrever
    {
        printf("Erro ao escrever no TCP: %d\n", write_err);
        return write_err;
    }

    err_t output_err = tcp_output(tpcb); // Envia os dados para a rede
    if (output_err != ERR_OK) // Se houve erro ao enviar
    {
        printf("Erro ao enviar dados TCP: %d\n", output_err);
        return output_err;
    }

    printf("Requisição PUT enviada.\n");

    // Define o estado de "carregando" como falso após a requisição ser enviada
    for (int i = 0; i < sizeof(componentes) / sizeof(componentes[0]); i++)
    {
        if (componentes[i].id == device_id) // Encontra o componente correspondente
        {
            componentes[i].carregando = false; // Atualiza o estado de carregando para falso
            break;
        }
    }

    return ERR_OK;
}

// Resolver DNS e conectar ao servidor
static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    if (ipaddr) // Se o DNS foi resolvido com sucesso
    {
        printf("Endereço IP da API: %s\n", ipaddr_ntoa(ipaddr)); // Exibe o endereço IP
        tcp_client_pcb = tcp_new(); // Cria uma nova conexão TCP
        if (!tcp_client_pcb) // Se não foi possível criar a conexão
        {
            printf("Erro ao criar PCB.\n");
            return;
        }
        tcp_recv(tcp_client_pcb, http_recv_callback); // Configura o callback de recebimento
        tcp_arg(tcp_client_pcb, callback_arg); // Passa os argumentos para o callback
        tcp_connect(tcp_client_pcb, ipaddr, API_PORT, http_connected_callback); // Conecta ao servidor
    }
    else // Se o DNS não foi resolvido
    {
        printf("Falha na resolução de DNS\n");
    }
}

// Função para tentar resolver o DNS da API
bool resolver_dns_api(int device_id, int is_on)
{
    printf("Tentando resolver DNS da API...\n");

    int params[2] = {device_id, is_on}; // Parâmetros para o callback

    err_t err = dns_gethostbyname(API_HOST, &server_ip, dns_callback, params); // Tenta resolver o DNS
    if (err == ERR_OK) // Se o DNS foi resolvido imediatamente
    {
        printf("DNS da API resolvido! Endereço IP: %s\n", ipaddr_ntoa(&server_ip)); // Exibe o endereço IP
        return true;
    }
    else if (err == ERR_INPROGRESS) // Se o DNS está sendo resolvido
    {
        printf("Aguardando resolução DNS da API...\n");
        for (int i = 0; i < 20; i++) // Tenta por 20 segundos
        {
            uint64_t start_time = time_us_64(); // Tempo inicial em microssegundos
            while (time_us_64() - start_time < 1000000) {
                // Espera 1 segundo (1000000 microssegundos)
            }
            if (server_ip.addr != 0) // Se o DNS foi resolvido
            {
                printf("DNS da API resolvido! Endereço IP: %s\n", ipaddr_ntoa(&server_ip)); // Exibe o endereço IP
                return true;
            }
        }
        printf("Tempo esgotado. Falha ao resolver DNS da API.\n"); // Se o tempo esgotou
        return false;
    }
    else // Se houve erro ao resolver o DNS
    {
        printf("Falha ao resolver DNS da API. Erro: %d\n", err);
        return false;
    }
}

// Função para conectar ao Wi-Fi
bool conectar_wifi()
{
    printf("Inicializando Wi-Fi...\n");
    exibir_mensagem_no_oled("Iniciando Wi-Fi...", 24); // Exibe mensagem no OLED
    if (cyw43_arch_init()) // Inicializa o Wi-Fi
    {
        printf("Falha ao iniciar Wi-Fi\n");
        return false;
    }

    cyw43_arch_enable_sta_mode(); // Habilita o modo estação
    printf("Conectando ao Wi-Fi...\n");
    exibir_mensagem_no_oled("Conectando...", 24); // Exibe mensagem no OLED

    int tentativas = 0;
    while (tentativas < 3) // Tenta conectar 3 vezes
    {
        printf("Tentativa %d...\n", tentativas + 1);
        if (cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_MIXED_PSK) == 0) // Tenta conectar
        {
            printf("Wi-Fi conectado!\n");
            exibir_mensagem_no_oled("Wi-Fi OK!", 24); // Exibe mensagem no OLED
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
        tcp_close(tcp_client_pcb); // Fecha a conexão
        tcp_client_pcb = NULL; // Limpa o ponteiro
        tcp_connected = false; // Indica que a conexão foi encerrada
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
        if (componentes[i].id == device_id) // Encontra o componente correspondente
        {
            componentes[i].carregando = false; // Atualiza o estado de carregando para falso
            break;
        }
    }
}

void atualizar_estado_no_display(Componente *componente)
{
    char msg[30];
    if (componente->carregando) // Se o componente está carregando
    {
        snprintf(msg, sizeof(msg), "%s: Carregando...", componente->nome); // Exibe "Carregando..."
    }
    else
    {
        snprintf(msg, sizeof(msg), "%s: %s", componente->nome, componente->estado ? "Ligado" : "Desligado"); // Exibe o estado atual
    }
    ssd1306_draw_string(&disp, 6, 18, 1.5, msg); // Atualiza apenas a linha do componente
    ssd1306_show(&disp); // Envia apenas a parte alterada para o display
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
    menu_option = saved_menu_option; // Restaura a opção selecionada
    int start_index = saved_start_index; // Restaura o índice de início

    while (running) // Enquanto o menu estiver em execução
    {
        print_itens(itens, num_itens, start_index, titulo, mostrar_estado, is_comando, max_itens_na_tela); // Exibe os itens
        adc_select_input(0); // Seleciona o canal do ADC para leitura
        uint adc_y_raw = adc_read(); // Lê o valor do ADC

        if (adc_y_raw < 1500) // Se o joystick foi movido para baixo
        {
            if (menu_option < num_itens - 1) // Se não está no último item
            {
                menu_option++; // Move para o próximo item

                if (menu_option >= start_index + max_itens_na_tela) // Se o item está fora da tela
                {
                    start_index++; // Move o índice de início
                }
            }
            busy_wait_us_32(250000); // Espera 250ms para evitar leituras múltiplas
        }
        else if (adc_y_raw > 2500) // Se o joystick foi movido para cima
        {
            if (menu_option > 0) // Se não está no primeiro item
            {
                menu_option--; // Move para o item anterior

                if (menu_option < start_index) // Se o item está fora da tela
                {
                    start_index--; // Move o índice de início
                }
            }
            busy_wait_us_32(250000); // Espera 250ms para evitar leituras múltiplas
        }

        if (detectar_borda_baixa(SW, &sw_estado_anterior) || detectar_borda_baixa(BTN_B, &btn_b_estado_anterior)) // Se o botão foi pressionado
        {
            int item_selecionado = menu_option; // Obtém o item selecionado

            if (is_comando) // Se é um comando
            {
                Comando *comando = (Comando *)itens;
                if (strcmp(comando[item_selecionado].nome, "Random Luz") == 0) // Se o comando é "Random Luz"
                {
                    int luz_aleatoria = rand() % 5; // Escolhe uma luz aleatória
                    toggle_estado(&componentes[luz_aleatoria], start_index); // Alterna o estado da luz

                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 2, 1, "Luz Alterada:");
                    char msg[20];
                    if (componentes[luz_aleatoria].carregando) // Se a luz está carregando
                    {
                        snprintf(msg, sizeof(msg), "%s Carregando...", componentes[luz_aleatoria].nome); // Exibe "Carregando..."
                    }
                    else
                    {
                        snprintf(msg, sizeof(msg), "%s %s", componentes[luz_aleatoria].nome, componentes[luz_aleatoria].estado ? "Ligando" : "Desligando"); // Exibe o estado atual
                    }
                    ssd1306_draw_string(&disp, 6, 18, 1.5, msg);
                    ssd1306_show(&disp);
                    busy_wait_us_32(1000000); // Espera 1 segundo
                }
            }
            else // Se é um componente
            {
                Componente *componente = (Componente *)itens;
                toggle_estado(&componente[item_selecionado], start_index); // Alterna o estado do componente
            }
        }

        if (detectar_borda_baixa(BTN_SAIR, &btn_sair_estado_anterior)) // Se o botão SAIR foi pressionado
        {
            running = false; // Sai do menu
            menu_option = 0; // Reseta a opção do menu
        }
    }
}

// Função principal
int main()
{
    inicializa(); // Inicializa o hardware
    srand(time(NULL)); // Inicializa o gerador de números aleatórios

    // Inicializar o campo carregando como falso
    for (int i = 0; i < sizeof(componentes) / sizeof(componentes[0]); i++)
    {
        componentes[i].carregando = false; // Define o estado de carregando como falso
    }

    // Inicializar as variáveis de estado do menu
    saved_menu_option = 0;
    saved_start_index = 0;

    // Conectar ao Wi-Fi uma vez no início
    if (!conectar_wifi()) // Tenta conectar ao Wi-Fi
    {
        printf("Falha ao conectar ao Wi-Fi. Encerrando...\n");
        return 1;
    }

    while (true) // Loop principal
    {
        print_menu(); // Exibe o menu principal
        adc_select_input(0); // Seleciona o canal do ADC para leitura
        uint adc_y_raw = adc_read(); // Lê o valor do ADC

        if (adc_y_raw < 1500 && menu_option < 2) // Se o joystick foi movido para baixo e não está na última opção
        {
            menu_option++; // Move para a próxima opção
            busy_wait_us_32(250000); // Espera 250ms para evitar leituras múltiplas
        }
        else if (adc_y_raw > 2500 && menu_option > 1) // Se o joystick foi movido para cima e não está na primeira opção
        {
            menu_option--; // Move para a opção anterior
            busy_wait_us_32(250000); // Espera 250ms para evitar leituras múltiplas
        }

        if (detectar_borda_baixa(SW, &sw_estado_anterior) || detectar_borda_baixa(BTN_B, &btn_b_estado_anterior)) // Se o botão foi pressionado
        {
            running = true; // Inicia o menu
            switch (menu_option) // Verifica a opção selecionada
            {
            case 1:
                menu_itens(componentes, sizeof(componentes) / sizeof(componentes[0]), "Componentes", true, false, MAX_ITENS_NA_TELA); // Exibe o menu de componentes
                break;
            case 2:
                menu_itens(comandos, sizeof(comandos) / sizeof(comandos[0]), "Comandos", false, true, MAX_ITENS_NA_TELA); // Exibe o menu de comandos
                break;
            }
            running = false; // Sai do menu
        }
    }

    // Desconectar o Wi-Fi no final
    desconectar_wifi(); // Desconecta da rede Wi-Fi
    return 0;
}