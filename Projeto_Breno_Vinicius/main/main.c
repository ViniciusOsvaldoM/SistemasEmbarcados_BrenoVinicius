// CEFET-MG - ENGENHARIA ELÉTRICA
// Disciplina de Sistemas Embarcados

// Professor: Túlio Charles
// Alunos: Breno Guimarães
//         Vinícius Osvaldo

//PRÁTICA 4 - Objetivos:


#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "esp_err.h"


#define BOTAO1    21
#define BOTAO2    22
#define BOTAO3    23
#define GPIO_INPUT_PIN_SEL  ((1ULL<<BOTAO1) | (1ULL<<BOTAO2) | (BOTAO3))
#define LED    2
#define GPIO_OUTPUT_PIN_SEL  (1ULL<<LED)

#define ESP_INTR_FLAG_DEFAULT 0

static const char* TAG = "boot";
static const char* TAG2 = "botoes";
static const char* TAG3 = "RELOGIO";

static QueueHandle_t gpio_evt_queue = NULL;
QueueHandle_t fila_contador = NULL;
QueueHandle_t fila_pwm = NULL;

static SemaphoreHandle_t semaphore_pwm = NULL; 

typedef struct {
    uint64_t contagem_atual;  
    uint64_t valor_do_alarme;   
} tAcumulador;

typedef struct {
    int horas;
    int minutos;
    int segundos;
} relogio_t;


static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

/* ----------------------- Tarefa GPIO  ------------------------------- */
static void gpio_task_example(void* arg)
{
    gpio_config_t io_conf = {};
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    //change gpio interrupt type for one pin
    gpio_set_intr_type(LED, GPIO_INTR_ANYEDGE);


    // Inicia gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    // hook isr handler for specific gpio pin
    gpio_isr_handler_add(BOTAO1, gpio_isr_handler, (void*) BOTAO1);
    // hook isr handler for specific gpio pin
    gpio_isr_handler_add(BOTAO2, gpio_isr_handler, (void*) BOTAO2);
    
    gpio_isr_handler_add(BOTAO3, gpio_isr_handler, (void*) BOTAO3);


    uint32_t io_num;
    int LED_STATE = 0;

    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int level = gpio_get_level(io_num);
            if(io_num == BOTAO1) {
                gpio_set_level(LED, 1);
                ESP_LOGI(TAG2, "Botao 1 pressionado");
                
            }
            else if(io_num == BOTAO2) {
                gpio_set_level(LED, 0);
                ESP_LOGI(TAG2, "Botao 2 pressionado");
            }
            else if(io_num == BOTAO3) {
                LED_STATE = !LED_STATE;
                gpio_set_level(LED, LED_STATE);
                ESP_LOGI(TAG2, "Botao 3 pressionado");
            }

        }

        xQueueSendFromISR(fila_pwm, &io_num, NULL);

    }
}

/* ----------------------- Alarme do timer ------------------------------- */

static bool IRAM_ATTR example_timer_on_alarm_cb_v3(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    static uint64_t contagem = 0;
    uint64_t alarme = edata->count_value;

    contagem += alarme;
    tAcumulador ele = {
        .contagem_atual = contagem,
        .valor_do_alarme = alarme
    };
        
    BaseType_t high_task_awoken = pdFALSE;
    
    xQueueSendFromISR(fila_contador, &ele, &high_task_awoken);
    
    // reconfigure alarm value
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->count_value + 100000, // alarm in next 1s
    };
    gptimer_set_alarm_action(timer, &alarm_config);
    // return whether we need to yield at the end of ISR
    return (high_task_awoken == pdTRUE);
}

/* ----------------------- Tarefa do gptimer ------------------------------- */
static void timer_task(void* arg)
{
    uint64_t ultimo_log = 0;
    tAcumulador dado;

    ESP_LOGI(TAG, "Create timer handle");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));
    
   gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb_v3,
    };

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_LOGI(TAG, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Start timer, update alarm value dynamically");
    gptimer_alarm_config_t alarm_config3 = {
        .alarm_count = 1000000, // period = 1s
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config3));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    relogio_t clock = {0, 0, 0};
  uint64_t segundos_totais = 0;
    for (;;) {
        if (xQueueReceive(fila_contador, &dado, portMAX_DELAY)) {
            segundos_totais = dado.contagem_atual / 1000000;
            clock.horas = (segundos_totais/3600) % 24; 
            clock.minutos = (segundos_totais / 60) % 60;
            clock.segundos = segundos_totais % 60;

            if(segundos_totais != ultimo_log) {
                ultimo_log = segundos_totais;
                ESP_LOGI(TAG3, "Hora: %02d: %02d: %02d | Contagem: %llu | Alarme: %llu",
                clock.horas, clock.minutos, clock.segundos,
                dado.contagem_atual, dado.valor_do_alarme);

            }
        }
        //xSemaphoreGive(semaphore_pwm);
            
        }

        }



#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LED                     (16) // Define the output GPIO
#define OSCILOSCOPIO            (33) // Define the output GPIO
#define LED_CHANNEL             LEDC_CHANNEL_0
#define OSCILOSCOPIO_CHANNEL    LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (4096) // Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_FREQUENCY          (5000) // Frequency in Hertz. Set frequency at 4 kHz


/* ----------------------- Tarefa do PWM ------------------------------- */
static void pwm_task(void* arg)
{
    
    int duty;
    bool manual;
    int intensidade;
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t led_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };

    
    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t osciloscopio_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_1,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = OSCILOSCOPIO,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };

    for (;;) {
    xSemaphoreTake(semaphore_pwm, portMAX_DELAY);
    ESP_ERROR_CHECK(ledc_channel_config(&led_channel));
    ESP_ERROR_CHECK(ledc_channel_config(&osciloscopio_channel));
    xQueueReceive(fila_pwm, &duty, portMAX_DELAY);

    if (duty == 1){
        manual = false;
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, 8000);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
        
    }
    else if (duty == 2){
        intensidade = 0;
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, intensidade);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
        manual = true;
    }
    else if (duty == 3 && manual == true){
        intensidade += 100;
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL_0, intensidade);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL_0);
    }

    }
}   


void app_main(void)
{

   
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG,"Este é um microcontrolador %s com %d núcleo(s), %s%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
           (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "\n");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    ESP_LOGI(TAG,"silicon revision v%d.%d, \n", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        ESP_LOGE(TAG,"Falha na Obtenção da Memória Flash!");
       return;
    }

    printf("teste\n");
    ESP_LOGI(TAG,"Memória Flash: %" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
          (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    ESP_LOGI(TAG,"Free heap: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG,"Versão do ESP-IDF: %s\n", IDF_VER);


    // Criação das filas
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    fila_contador = xQueueCreate(10,sizeof(uint32_t));
    fila_pwm = xQueueCreate(10,sizeof(uint32_t));
    
    // Criação do semáforo
    semaphore_pwm = xSemaphoreCreateBinary();

    // xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);
    xTaskCreate(timer_task, "Tarefa para o timer", 4096, NULL, 10, NULL);

}



 