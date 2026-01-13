/**
 * Controle de ar condicionado via PWM com proteção de Watchdog
 * Integra controle IR com sistema de recuperação automática
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#include "hardware/i2c.h"
#include "lib/custom_ir.h"
#include "lib/ssd1306.h"

// ===================== PINOS BITDOGLAB =====================
#define LED_BOOT_RED     13   // LED vermelho: indica boot/reset
#define LED_OK_GREEN     11   // LED verde: operação normal
#define LED_TRAVA_BLUE   12   // LED azul: falha/travamento
#define BOTAO_A           5   // Botão A: falha proposital
#define BOTAO_B          6   // Botão B: comandos IR

// ===================== PINOS IR =====================
#define IR_PIN           16   // Pino para saída IR
#define LED_PIN          25   // LED onboard do Pico

// ===================== DISPLAY =====================
#define I2C_PORT_DISP    i2c1
#define SDA_DISP         14
#define SCL_DISP         15
#define DISPLAY_ADDR     0x3C

// ===================== WATCHDOG =====================
// Timeout ajustado para latência das operações IR (transmissão + resposta AC)
// Operação IR típica: ~500ms, adicionamos margem para UART e display
#define WDT_TIMEOUT_MS   5000  // 5 segundos de margem segura

// Códigos de falha nos scratch registers
#define FALHA_BOTAO_A    0x01  // Falha induzida manualmente (loop infinito)
#define FALHA_TEMP_22C   0x02  // Falha no comando de temperatura 22°C

// ===================== ESTADOS DO SISTEMA =====================
typedef enum {
    STATE_OFF,
    STATE_ON,
    STATE_TEMP_20,
    STATE_TEMP_22,
    STATE_FAN_1,
    STATE_FAN_2,
    STATE_MAX
} system_state_t;

static system_state_t current_state = STATE_OFF;
static system_state_t last_display_state = STATE_MAX; // força atualização inicial

// ===================== VARIÁVEIS GLOBAIS =====================
static ssd1306_t ssd;
static uint32_t last_operation_time = 0;
static bool ir_operation_pending = false;

// ===================== HELPERS GPIO =====================
static void init_gpio(void) {
    // LEDs de diagnóstico
    gpio_init(LED_BOOT_RED);
    gpio_init(LED_OK_GREEN);
    gpio_init(LED_TRAVA_BLUE);
    gpio_set_dir(LED_BOOT_RED, GPIO_OUT);
    gpio_set_dir(LED_OK_GREEN, GPIO_OUT);
    gpio_set_dir(LED_TRAVA_BLUE, GPIO_OUT);

    // LED onboard
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // Botões com pull-up
    gpio_init(BOTAO_A);
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_A);
    gpio_pull_up(BOTAO_B);
}

// ===================== HELPERS DISPLAY =====================
static void init_display(ssd1306_t *ssd) {
    i2c_init(I2C_PORT_DISP, 400 * 1000);
    gpio_set_function(SDA_DISP, GPIO_FUNC_I2C);
    gpio_set_function(SCL_DISP, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_DISP);
    gpio_pull_up(SCL_DISP);

    ssd1306_init(ssd, WIDTH, HEIGHT, false, DISPLAY_ADDR, I2C_PORT_DISP);
    ssd1306_config(ssd);
}

static void draw_frame_base(ssd1306_t *ssd, bool cor) {
    ssd1306_fill(ssd, !cor);
    ssd1306_rect(ssd, 3, 3, 122, 60, cor, !cor);
    ssd1306_line(ssd, 3, 25, 123, 25, cor);
    ssd1306_line(ssd, 3, 37, 123, 37, cor);
}

// Tela de diagnóstico de boot
static void show_boot_diag(ssd1306_t *ssd, bool reboot_wdt, uint32_t count, uint32_t fault) {
    char line[22];
    draw_frame_base(ssd, true);

    ssd1306_draw_string(ssd, "IR + WDT SYSTEM", 6, 6);
    ssd1306_draw_string(ssd, reboot_wdt ? "RESET WATCHDOG" : "RESET NORMAL", 10, 16);

    snprintf(line, sizeof(line), "COUNT: %lu", (unsigned long)count);
    ssd1306_draw_string(ssd, line, 10, 28);
    
    snprintf(line, sizeof(line), "FAULT: 0x%02lX", (unsigned long)fault);
    ssd1306_draw_string(ssd, line, 10, 40);
    
    snprintf(line, sizeof(line), "TIMEOUT: %dms", WDT_TIMEOUT_MS);
    ssd1306_draw_string(ssd, line, 10, 52);

    ssd1306_send_data(ssd);
}

// Tela de operação mostrando estado do AC
static void show_running_state(ssd1306_t *ssd, system_state_t state) {
    char line[22];
    draw_frame_base(ssd, true);

    ssd1306_draw_string(ssd, "AC CONTROL+WDT", 12, 6);
    
    // Mostra estado atual do AC
    switch (state) {
        case STATE_OFF:
            ssd1306_draw_string(ssd, "AC: DESLIGADO", 10, 16);
            break;
        case STATE_ON:
            ssd1306_draw_string(ssd, "AC: LIGADO", 10, 16);
            break;
        case STATE_TEMP_20:
            ssd1306_draw_string(ssd, "AC: 20C", 10, 16);
            break;
        case STATE_TEMP_22:
            ssd1306_draw_string(ssd, "AC: 22C", 10, 16);
            break;
        case STATE_FAN_1:
            ssd1306_draw_string(ssd, "AC: FAN NIVEL 1", 10, 16);
            break;
        case STATE_FAN_2:
            ssd1306_draw_string(ssd, "AC: FAN NIVEL 2", 10, 16);
            break;
        default:
            ssd1306_draw_string(ssd, "AC: UNKNOWN", 10, 16);
            break;
    }

    ssd1306_draw_string(ssd, "BTN A=FALHA", 10, 28);
    ssd1306_draw_string(ssd, "BTN B=NEXT CMD", 10, 40);
    ssd1306_draw_string(ssd, "WDT: ATIVO", 10, 52);

    ssd1306_send_data(ssd);
}

// Tela de falha
static void show_fault_mode(ssd1306_t *ssd, const char* msg) {
    draw_frame_base(ssd, true);

    ssd1306_draw_string(ssd, "FALHA INDUZIDA", 12, 6);
    ssd1306_draw_string(ssd, msg, 10, 16);
    ssd1306_draw_string(ssd, "Sem feed WDT", 10, 28);
    ssd1306_draw_string(ssd, "Aguard. reset", 10, 40);
    ssd1306_draw_string(ssd, "em ~5 seg...", 10, 52);

    ssd1306_send_data(ssd);
}

// ===================== CONTROLE IR COM PROTEÇÃO =====================
// Executa comando IR com proteção de watchdog
static bool execute_ir_command_safe(system_state_t new_state) {
    ir_operation_pending = true;
    last_operation_time = to_ms_since_boot(get_absolute_time());
    
    printf("Executando comando IR para estado: %d\n", new_state);
    
    // Feed do watchdog ANTES da operação IR
    watchdog_update();
    
    // ===== DEFEITO 2: TEMPERATURA 22°C =====
    // Simula falha ao tentar configurar 22°C
    if (new_state == STATE_TEMP_22) {
        printf("\n!!! FALHA NO COMANDO 22C !!!\n");
        printf("Sistema travara ao processar temperatura 22C\n");
        
        watchdog_hw->scratch[1] = FALHA_TEMP_22C;
        show_fault_mode(&ssd, "CMD 22C FALHOU");
        
        // Loop infinito SEM watchdog_update()
        while (true) {
            gpio_put(LED_TRAVA_BLUE, 1);
            gpio_put(LED_PIN, 1);
            sleep_ms(150);
            gpio_put(LED_TRAVA_BLUE, 0);
            gpio_put(LED_PIN, 0);
            sleep_ms(150);
        }
    }
    
    // Executa comando IR apropriado para os demais estados
    switch (new_state) {
        case STATE_OFF:
            printf("Comando: DESLIGAR AC\n");
            turn_off_ac();
            gpio_put(LED_PIN, 0);
            break;
            
        case STATE_ON:
            printf("Comando: LIGAR AC\n");
            turn_on_ac();
            gpio_put(LED_PIN, 1);
            break;
            
        case STATE_TEMP_20:
            printf("Comando: TEMPERATURA 20C\n");
            set_temp_20c();
            gpio_put(LED_PIN, 1);
            break;
            
        case STATE_FAN_1:
            printf("Comando: VENTILADOR NIVEL 1\n");
            set_fan_level_1();
            gpio_put(LED_PIN, 1);
            break;
            
        case STATE_FAN_2:
            printf("Comando: VENTILADOR NIVEL 2\n");
            set_fan_level_2();
            gpio_put(LED_PIN, 1);
            break;
            
        default:
            printf("Estado invalido\n");
            ir_operation_pending = false;
            return false;
    }
    
    // Feed do watchdog APÓS a operação IR
    watchdog_update();
    
    // Delay para garantir transmissão completa
    sleep_ms(100);
    
    ir_operation_pending = false;
    current_state = new_state;
    
    printf("Comando IR executado com sucesso\n");
    return true;
}

// ===================== PROCESSAMENTO DE UART =====================
static void process_uart_input() {
    int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT) {
        return;
    }
    
    printf("%c\n", ch);
    
    system_state_t new_state = current_state;
    
    switch (ch) {
        case '1':
            new_state = STATE_ON;
            break;
        case '2':
            new_state = STATE_OFF;
            break;
        case '3':
            new_state = STATE_TEMP_22;  // ? ACIONARÁ DEFEITO 2
            break;
        case '4':
            new_state = STATE_TEMP_20;
            break;
        case '5':
            new_state = STATE_FAN_1;
            break;
        case '6':
            new_state = STATE_FAN_2;
            break;
        case '0':
            printf("\n=== MENU IR + WATCHDOG ===\n");
            printf("1-Ligar\n 2-Desligar\n");
            printf("3-22C(FALHA!)\n 4-20C\n");
            printf("5-Fan1\n 6-Fan2\n");
            printf("0-Menu\n");
            return;
        default:
            return;
    }
    
    execute_ir_command_safe(new_state);
}

// ===================== MAIN =====================
int main() {
    stdio_init_all();
    sleep_ms(2000);

    printf("\n\n=== SISTEMA IR + WATCHDOG ===\n");
    printf("Raspberry Pi Pico - Protocolo IR com Protecao WDT\n\n");

    // 1) Inicializa GPIOs
    init_gpio();

    // 2) Inicializa display OLED
    init_display(&ssd);

    // 3) Indicação visual de boot (3 piscadas)
    for (int i = 0; i < 3; i++) {
        gpio_put(LED_BOOT_RED, 1);
        sleep_ms(120);
        gpio_put(LED_BOOT_RED, 0);
        sleep_ms(120);
    }

    // ===== DIAGNÓSTICO DE REBOOT =====
    // 4) Verifica causa do último reset
    bool reboot_wdt = watchdog_caused_reboot();

    if (reboot_wdt) {
        watchdog_hw->scratch[0] = watchdog_hw->scratch[0] + 1;
        printf("AVISO: Sistema recuperado de reset por WATCHDOG!\n");
    } else {
        watchdog_hw->scratch[0] = 0;
        watchdog_hw->scratch[1] = 0;
        printf("Boot normal (primeira execucao ou reset manual)\n");
    }

    uint32_t count = watchdog_hw->scratch[0];
    uint32_t fault = watchdog_hw->scratch[1];

    printf("Resets por WDT: %lu\n", (unsigned long)count);
    printf("Codigo falha: 0x%02lX\n", (unsigned long)fault);
    if (fault == FALHA_BOTAO_A) {
        printf("Ultima falha: Botao A (loop infinito)\n");
    } else if (fault == FALHA_TEMP_22C) {
        printf("Ultima falha: Comando 22C (travamento)\n");
    }

    // 5) Mostra diagnóstico no OLED
    show_boot_diag(&ssd, reboot_wdt, count, fault);
    sleep_ms(3000);

    // 6) Inicializa sistema IR
    printf("Inicializando sistema IR...\n");
    if (!custom_ir_init(IR_PIN)) {
        printf("ERRO: Falha ao inicializar sistema IR!\n");
        
        while (1) {
            gpio_put(LED_BOOT_RED, 1);
            sleep_ms(100);
            gpio_put(LED_BOOT_RED, 0);
            sleep_ms(100);
        }
    }
    printf("Sistema IR inicializado com sucesso\n");

    // ===== HABILITA WATCHDOG =====
    // 7) Ativa watchdog com timeout ajustado para operações IR
    printf("Habilitando Watchdog (timeout: %dms)...\n", WDT_TIMEOUT_MS);
    watchdog_enable(WDT_TIMEOUT_MS, true);
    printf("Watchdog ativo!\n\n");

    // Mostra menu inicial
    printf("=== MENU IR + WATCHDOG ===\n");
    printf("1-Ligar 2-Desligar\n");
    printf("3-22C(FALHA!) 4-20C\n");
    printf("5-Fan1 6-Fan2\n");
    printf("0-Menu\n\n");

    // ===== LOOP PRINCIPAL =====
    absolute_time_t next_display = make_timeout_time_ms(1000);
    absolute_time_t next_led = make_timeout_time_ms(500);
    bool led_state = false;

    // Debounce para botões
    static uint32_t last_button_a = 0;
    static uint32_t last_button_b = 0;

    while (true) {
        uint32_t current_time = to_ms_since_boot(get_absolute_time());

        // ===== DEFEITO 1: GATILHO DE FALHA - BOTÃO A =====
        if (gpio_get(BOTAO_A) == 0 && (current_time - last_button_a) > 300) {
            last_button_a = current_time;
            
            printf("\n!!! FALHA INDUZIDA PELO BOTAO A !!!\n");
            printf("Sistema entrara em loop infinito sem feed do WDT\n");
            
            watchdog_hw->scratch[1] = FALHA_BOTAO_A;
            show_fault_mode(&ssd, "BOTAO A");

            // Loop infinito SEM watchdog_update()
            while (true) {
                gpio_put(LED_TRAVA_BLUE, 1);
                sleep_ms(200);
                gpio_put(LED_TRAVA_BLUE, 0);
                sleep_ms(200);
            }
        }

        // ===== BOTÃO B - AVANÇAR ESTADO DO AC =====
        if (gpio_get(BOTAO_B) == 0 && (current_time - last_button_b) > 300) {
            last_button_b = current_time;
            
            system_state_t new_state = (current_state + 1) % STATE_MAX;
            printf("\nBotao B pressionado - mudando para estado %d\n", new_state);
            execute_ir_command_safe(new_state);
        }

        // ===== PROCESSA COMANDOS UART =====
        process_uart_input();

        // ===== LED DE HEARTBEAT (operação normal) =====
        if (absolute_time_diff_us(get_absolute_time(), next_led) <= 0) {
            led_state = !led_state;
            gpio_put(LED_OK_GREEN, led_state);
            next_led = make_timeout_time_ms(500);
        }

        // ===== ATUALIZA DISPLAY PERIODICAMENTE =====
        if (absolute_time_diff_us(get_absolute_time(), next_display) <= 0 || 
            last_display_state != current_state) {
            
            show_running_state(&ssd, current_state);
            last_display_state = current_state;
            next_display = make_timeout_time_ms(1000);
            
            // Feed adicional após atualizar display (I2C pode demorar)
            watchdog_update();
        }

        // ===== FEED DO WATCHDOG - PONTO ESTRATÉGICO =====
        // Este é o ponto crítico: se o código travar em qualquer lugar
        // acima (IR, I2C, processamento), o watchdog não será alimentado
        // e o sistema resetará automaticamente
        watchdog_update();

        // Pequena pausa para não sobrecarregar
        sleep_ms(10);
    }

    return 0;
}