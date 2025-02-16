#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "hardware/i2c.h"
#include "ssd1306.h"

// Configurações do Wi-Fi e API
#define WIFI_SSID "Lamarck"
#define WIFI_PASS "99712005"
#define API_HOST "embarcatech-api-3b0da5fbc57e.herokuapp.com"
#define API_PORT 80

// Pino do LED
#define LED_PIN 25

// Variáveis globais
struct tcp_pcb *tcp_client_pcb = NULL;
ip_addr_t server_ip;
bool tcp_connected = false;
ssd1306_t disp;

// Função para exibir uma mensagem no OLED
void exibir_mensagem_no_oled(const char *mensagem, int linha) {
    ssd1306_clear(&disp);
    ssd1306_draw_string(&disp, 0, linha, 1, mensagem);
    ssd1306_show(&disp);
}

// Callback quando recebe resposta da API
static err_t http_recv_callback(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (p == NULL) {
        printf("Conexão fechada pelo servidor.\n");
        tcp_close(tpcb);
        tcp_client_pcb = NULL;
        tcp_connected = false;
        return ERR_OK;
    }

    // Exibe a resposta recebida
    printf("Resposta recebida:\n");
    printf("%.*s\n", p->len, (char *)p->payload);

    // Libera o buffer
    pbuf_free(p);
    return ERR_OK;
}

// Callback quando a conexão TCP é estabelecida
static err_t http_connected_callback(void *arg, struct tcp_pcb *tpcb, err_t err) {
    if (err != ERR_OK) {
        printf("Erro na conexão TCP: %d\n", err);
        return err;
    }

    printf("Conectado à API!\n");
    tcp_connected = true;

    // Obtém os parâmetros passados para a função
    int device_id = *(int *)arg;
    int is_on = *((int *)arg + 1);

    // Cria o corpo da requisição PUT dinamicamente
    char json_body[64];
    snprintf(json_body, sizeof(json_body), "{\"is_on\": %d}", is_on);

    // Envia a requisição PUT
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
    if (write_err != ERR_OK) {
        printf("Erro ao escrever no TCP: %d\n", write_err);
        return write_err;
    }

    err_t output_err = tcp_output(tpcb);
    if (output_err != ERR_OK) {
        printf("Erro ao enviar dados TCP: %d\n", output_err);
        return output_err;
    }

    printf("Requisição PUT enviada.\n");
    return ERR_OK;
}

// Resolver DNS e conectar ao servidor
static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    if (ipaddr) {
        printf("Endereço IP da API: %s\n", ipaddr_ntoa(ipaddr));
        tcp_client_pcb = tcp_new();
        if (!tcp_client_pcb) {
            printf("Erro ao criar PCB.\n");
            return;
        }
        tcp_recv(tcp_client_pcb, http_recv_callback); // Configura o callback de recebimento
        tcp_arg(tcp_client_pcb, callback_arg); // Passa os argumentos para o callback
        tcp_connect(tcp_client_pcb, ipaddr, API_PORT, http_connected_callback);
    } else {
        printf("Falha na resolução de DNS\n");
    }
}


// Função para tentar resolver o DNS da API
bool resolver_dns_api(int device_id, int is_on) {
    printf("Tentando resolver DNS da API...\n");

    // Passa os parâmetros para o callback
    int params[2] = {device_id, is_on};

    // Tenta resolver o DNS da API
    err_t err = dns_gethostbyname(API_HOST, &server_ip, dns_callback, params);
    if (err == ERR_OK) {
        printf("DNS da API resolvido! Endereço IP: %s\n", ipaddr_ntoa(&server_ip));
        return true;
    } else if (err == ERR_INPROGRESS) {
        // Aguarda a resolução DNS
        printf("Aguardando resolução DNS da API...\n");
        for (int i = 0; i < 20; i++) { // Aguarda até 20 segundos
            sleep_ms(1000);
            if (server_ip.addr != 0) {
                printf("DNS da API resolvido! Endereço IP: %s\n", ipaddr_ntoa(&server_ip));
                return true;
            }
        }
        printf("Tempo esgotado. Falha ao resolver DNS da API.\n");
        return false;
    } else {
        printf("Falha ao resolver DNS da API. Erro: %d\n", err);
        return false;
    }
}

// Função principal para conectar e enviar a requisição PUT
void enviar_requisicao_put(int device_id, int is_on) {
    // Inicializa o Wi-Fi
    printf("Inicializando Wi-Fi...\n");
    exibir_mensagem_no_oled("Iniciando Wi-Fi...", 24); // Exibe status no OLED
    if (cyw43_arch_init()) {
        printf("Falha ao iniciar Wi-Fi\n");
        return;
    }

    // Habilita o modo station (conectar a uma rede Wi-Fi)
    cyw43_arch_enable_sta_mode();
    printf("Conectando ao Wi-Fi...\n");
    exibir_mensagem_no_oled("Conectando...", 24); // Exibe status no OLED

    // Tenta conectar ao Wi-Fi
    int tentativas = 0;
    while (tentativas < 3) { // Tenta conectar até 3 vezes
        printf("Tentativa %d...\n", tentativas + 1);
        if (cyw43_arch_wifi_connect_blocking(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_MIXED_PSK) == 0) {
            printf("Wi-Fi conectado!\n");
            exibir_mensagem_no_oled("Wi-Fi OK!", 24); // Exibe sucesso no OLED
            break;
        } else {
            printf("Falha ao conectar ao Wi-Fi. Tentando novamente...\n");
            tentativas++;
            sleep_ms(5000); // Aguarda 5 segundos antes de tentar novamente
        }
    }

    if (tentativas >= 3) {
        printf("Falha ao conectar ao Wi-Fi após 3 tentativas.\n");
        return;
    }

    sleep_ms(2000); // Pausa para visualização

    // Tenta resolver o DNS da API
    if (!resolver_dns_api(device_id, is_on)) {
        return; // Sai se não conseguir resolver o DNS
    }
    sleep_ms(2000); // Pausa para visualização

    // Aguarda a conexão ser estabelecida
    while (!tcp_connected) {
        sleep_ms(100);
    }
}

// Inicialização do OLED
void inicializar_oled() {
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(14, GPIO_FUNC_I2C);
    gpio_set_function(15, GPIO_FUNC_I2C);
    gpio_pull_up(14);
    gpio_pull_up(15);
    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    ssd1306_clear(&disp);
    ssd1306_show(&disp);
}

int main() {
    stdio_init_all();
    inicializar_oled();

    // Configura o pino do LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0); // Inicia com o LED apagado

    // Envia a requisição PUT com valores dinâmicos
    enviar_requisicao_put(2, 1); // Exemplo: device_id = 3, is_on = 0

    while (true) {
        sleep_ms(1000); // Mantém o programa rodando
    }
}