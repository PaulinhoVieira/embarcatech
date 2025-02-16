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

// Definições de pinos e constantes
#define I2C_PORT i2c1          // Porta I2C utilizada
#define PINO_SCL 14            // Pino para o clock do I2C
#define PINO_SDA 15            // Pino para o dado do I2C
#define BTN_B 6                // Pino do botão B
#define BTN_SAIR 5             // Pino do botão de sair
#define MAX_ITENS_NA_TELA 3    // Número máximo de itens visíveis na tela

const int SW = 22;             // Pino do botão SW
const uint BLUE_LED_PIN = 12;  // Pino do LED azul
const uint RED_LED_PIN = 13;   // Pino do LED vermelho
const uint GREEN_LED_PIN = 11; // Pino do LED verde
const uint BUZZER_PIN = 21;    // Pino do buzzer
#define OLED_ADDR 0x3C         // Endereço do display OLED

// Variáveis globais
ssd1306_t disp;                // Estrutura do display OLED
uint menu_option = 1;          // Opção atual do menu
bool running = false;          // Estado de execução do menu
const uint DEAD_ZONE = 128;    // Zona morta para leitura do ADC

#define ADC_READINGS 10        // Número de leituras do ADC para média
uint16_t adc_buffer[ADC_READINGS]; // Buffer para armazenar leituras do ADC
uint16_t adc_index = 0;        // Índice atual do buffer do ADC

// Estrutura para representar um componente (ex: luz)
typedef struct
{
    int id;                    // ID do componente
    char nome[20];             // Nome do componente
    bool estado;               // Estado do componente (ligado/desligado)
} Componente;

// Estrutura para representar um comando (ex: "Random Luz")
typedef struct
{
    int id;                    // ID do comando
    char nome[20];             // Nome do comando
    int *referencia;           // Array de IDs dos componentes referenciados
    int num_referencias;       // Número de referências
} Comando;

// Array de componentes (luzes)
Componente componentes[] = {
    {1, "luz_1", false},
    {2, "luz_2", false},
    {3, "luz_3", false},
    {4, "luz_4", false},
    {5, "luz_5", false},
    {6, "luz_6", false},
    {7, "luz_7", false}};

// Array de referências para o comando "Random Luz"
int referencias_random_luz[] = {1, 2, 3, 4, 5, 6, 7};

// Array de comandos disponíveis
Comando comandos[] = {
    {1, "Random Luz", referencias_random_luz, 7}};

// Variáveis para armazenar o estado anterior dos botões
bool sw_estado_anterior = true;
bool btn_b_estado_anterior = true;
bool btn_sair_estado_anterior = true;

// Função para inicializar o hardware
void inicializa()
{
    stdio_init_all();          // Inicializa a comunicação serial
    adc_init();                // Inicializa o ADC
    adc_gpio_init(26);         // Configura o pino 26 como entrada do ADC
    adc_gpio_init(27);         // Configura o pino 27 como entrada do ADC
    i2c_init(I2C_PORT, 400 * 1000); // Inicializa o I2C com frequência de 400 kHz
    gpio_set_function(PINO_SCL, GPIO_FUNC_I2C); // Configura o pino SCL como I2C
    gpio_set_function(PINO_SDA, GPIO_FUNC_I2C); // Configura o pino SDA como I2C
    gpio_pull_up(PINO_SCL);    // Habilita pull-up no pino SCL
    gpio_pull_up(PINO_SDA);    // Habilita pull-up no pino SDA
    disp.external_vcc = false; // Configura o display para usar VCC interno
    ssd1306_init(&disp, 128, 64, OLED_ADDR, I2C_PORT); // Inicializa o display OLED

    // Configuração dos botões
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

// Função para detectar borda de descida (botão pressionado)
bool detectar_borda_baixa(uint gpio, bool *estado_anterior)
{
    bool estado_atual = gpio_get(gpio); // Lê o estado atual do pino
    if (*estado_anterior && !estado_atual) // Verifica se houve uma borda de descida
    {
        *estado_anterior = estado_atual; // Atualiza o estado anterior
        return true; // Retorna true se houve borda de descida
    }
    *estado_anterior = estado_atual; // Atualiza o estado anterior
    return false; // Retorna false se não houve borda de descida
}

// Função para exibir o menu principal
void print_menu()
{
    ssd1306_clear(&disp); // Limpa o display
    ssd1306_draw_string(&disp, 52, 2, 1, "Menu"); // Exibe o título "Menu"
    ssd1306_draw_string(&disp, 6, 18, 1.5, "Componentes"); // Exibe a opção "Componentes"
    ssd1306_draw_string(&disp, 6, 30, 1.5, "Comandos"); // Exibe a opção "Comandos"
    ssd1306_draw_empty_square(&disp, 2, 12 + (menu_option - 1) * 12, 120, 18); // Desenha um quadrado ao redor da opção selecionada
    ssd1306_show(&disp); // Atualiza o display
}

// Função para exibir itens na tela (componentes ou comandos)
void print_itens(void *itens, int num_itens, int start_index, const char *titulo, bool mostrar_estado, bool is_comando, int max_itens_na_tela)
{
    ssd1306_clear(&disp); // Limpa o display
    ssd1306_draw_string(&disp, 40, 2, 1, titulo); // Exibe o título da tela

    // Loop para exibir os itens visíveis na tela
    for (int i = 0; i < max_itens_na_tela; i++)
    {
        int item_index = start_index + i;
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
                if (mostrar_estado) // Se for para mostrar o estado
                {
                    char estado_str[10];
                    snprintf(estado_str, sizeof(estado_str), "|%d|", componente[item_index].estado ? 1 : 0); // Formata o estado
                    ssd1306_draw_string(&disp, 90, 18 + i * 12, 1.5, estado_str); // Exibe o estado
                }
            }
        }
    }

    // Desenha o quadrado ao redor do item selecionado
    int cursor_pos = menu_option - start_index;
    if (cursor_pos >= 0 && cursor_pos < max_itens_na_tela)
    {
        ssd1306_draw_empty_square(&disp, 2, 12 + cursor_pos * 12, 120, 18);
    }

    ssd1306_show(&disp); // Atualiza o display
}

// Função para alternar o estado de um componente
void toggle_estado(Componente *componente)
{
    componente->estado = !componente->estado; // Inverte o estado do componente
    // chame aqui o enviar_requisicao_put(componente->id, componente->estado);
}

// Função para exibir e navegar pelos itens (componentes ou comandos)
void menu_itens(void *itens, int num_itens, const char *titulo, bool mostrar_estado, bool is_comando, int max_itens_na_tela)
{
    menu_option = 0;     // Cursor começa no primeiro item
    int start_index = 0; // Índice do primeiro item visível na tela

    while (running) // Loop enquanto o menu estiver ativo
    {
        print_itens(itens, num_itens, start_index, titulo, mostrar_estado, is_comando, max_itens_na_tela); // Exibe os itens
        adc_select_input(0); // Seleciona o canal do ADC para leitura
        uint adc_y_raw = adc_read(); // Lê o valor do ADC

        // Navegação para baixo
        if (adc_y_raw < 1500)
        {
            if (menu_option < num_itens - 1) // Verifica se ainda há itens abaixo
            {
                menu_option++; // Move o cursor para baixo

                // Rola a lista se o cursor ultrapassar o último item visível
                if (menu_option >= start_index + max_itens_na_tela)
                {
                    start_index++;
                }
            }
            busy_wait_us_32(250000); // Debounce
        }
        // Navegação para cima
        else if (adc_y_raw > 2500)
        {
            if (menu_option > 0) // Verifica se ainda há itens acima
            {
                menu_option--; // Move o cursor para cima

                // Rola a lista se o cursor ultrapassar o primeiro item visível
                if (menu_option < start_index)
                {
                    start_index--;
                }
            }
            busy_wait_us_32(250000); // Debounce
        }

        // Ação ao pressionar o botão
        if (detectar_borda_baixa(SW, &sw_estado_anterior) || detectar_borda_baixa(BTN_B, &btn_b_estado_anterior))
        {
            int item_selecionado = menu_option; // Obtém o índice do item selecionado

            if (is_comando) // Se for um comando
            {
                Comando *comando = (Comando *)itens;
                if (strcmp(comando[item_selecionado].nome, "Random Luz") == 0) // Verifica se é o comando "Random Luz"
                {
                    int luz_aleatoria = rand() % 5; // Escolhe uma luz aleatória
                    toggle_estado(&componentes[luz_aleatoria]); // Alterna o estado da luz

                    // Exibe uma mensagem no display
                    ssd1306_clear(&disp);
                    ssd1306_draw_string(&disp, 20, 2, 1, "Luz Alterada:");
                    char msg[20];
                    snprintf(msg, sizeof(msg), "%s %s", componentes[luz_aleatoria].nome, componentes[luz_aleatoria].estado ? "Ligada" : "Desligada");
                    ssd1306_draw_string(&disp, 6, 18, 1.5, msg);
                    ssd1306_show(&disp);
                    busy_wait_us_32(1000000); // Espera 1 segundo antes de voltar ao menu
                }
            }
            else // Se for um componente
            {
                Componente *componente = (Componente *)itens;
                toggle_estado(&componente[item_selecionado]); // Alterna o estado do componente
            }
        }

        // Ação ao pressionar o botão de sair
        if (detectar_borda_baixa(BTN_SAIR, &btn_sair_estado_anterior))
        {
            running = false; // Sai do menu
            menu_option = 0; // Volta ao menu principal com o cursor no primeiro item
        }
    }
}

// Função principal
int main()
{
    inicializa(); // Inicializa o hardware
    srand(time(NULL)); // Inicializa o gerador de números aleatórios

    while (true) // Loop infinito
    {
        print_menu(); // Exibe o menu principal
        adc_select_input(0); // Seleciona o canal do ADC para leitura
        uint adc_y_raw = adc_read(); // Lê o valor do ADC

        // Navegação no menu principal
        if (adc_y_raw < 1500 && menu_option < 2)
        {
            menu_option++; // Move o cursor para baixo
            busy_wait_us_32(250000); // Debounce
        }
        else if (adc_y_raw > 2500 && menu_option > 1)
        {
            menu_option--; // Move o cursor para cima
            busy_wait_us_32(250000); // Debounce
        }

        // Ação ao pressionar o botão
        if (detectar_borda_baixa(SW, &sw_estado_anterior) || detectar_borda_baixa(BTN_B, &btn_b_estado_anterior))
        {
            running = true; // Ativa o menu de itens
            switch (menu_option)
            {
            case 1:
                menu_itens(componentes, sizeof(componentes) / sizeof(componentes[0]), "Componentes", true, false, MAX_ITENS_NA_TELA); // Exibe o menu de componentes
                break;
            case 2:
                menu_itens(comandos, sizeof(comandos) / sizeof(comandos[0]), "Comandos", false, true, MAX_ITENS_NA_TELA); // Exibe o menu de comandos
                break;
            }
            running = false; // Desativa o menu de itens
        }
    }
}