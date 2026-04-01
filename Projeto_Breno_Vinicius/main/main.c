// CEFET-MG - ENGENHARIA ELÉTRICA
// Disciplina de Sistemas Embarcados

// Professor: Túlio Charles
// Alunos: Breno Guimarães
//         Vinícius Osvaldo

//PRÁTICA 1 - Objetivos:
//      - Conhecer o ambiente de desenvolvimento 
//      - Introdução ao RTOS 
//      - Entender ESP_LOG 

// -----------FreeRTOS--------------
// Descrição da tarefa Função "vTaskDelay()": 
// Função da Biblioteca RTOS que bloqueia a tarefa atual pelo tempo inserido, 
// podendo trabalhar em outra tarefa durante este tempo. 


#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"

static const char* TAG = "boot";
static const char* TAG2 = "botoes";


#define BOTAO1    21
#define BOTAO2    22
#define BOTAO3    23
#define GPIO_INPUT_PIN_SEL  ((1ULL<<BOTAO1) | (1ULL<<BOTAO2) | (BOTAO3))

#define LED    2
#define GPIO_INPUT_PIN_SEL  (1ULL<<LED)


#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task_example(void* arg)
{
    uint32_t io_num;
    int LED_STATE = 0;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int level = gpio_get_level(io_num);
            if(io_num == BOTAO1) {
                gpio_set_level(LED, 1);
                ESPLOGI(TAG2, "Botao 1 pressionado");
            }
            else if(io_num == BOTAO2) {
                gpio_set_level(LED, 0);
                ESPLOGI(TAG2, "Botao 2 pressionado");
            }
            else if(io_num == BOTAO3) {
                LED_STATE = !LED_STATE;
                gpio_set_level(LED, LED_STATE);
                ESPLOGI(TAG2, "Botao 3 pressionado");
            }
        }
    }
}


void app_main(void)
{
    /* Print chip information */
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

    ESP_LOGI(TAG,"Memória Flash: %" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    ESP_LOGI(TAG,"Free heap: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG,"Versão do ESP-IDF: %s\n", IDF_VER);

        //Configuração do LED
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
    gpio_set_intr_type(GPIO_INPUT_IO_0, GPIO_INTR_ANYEDGE);

    // Cria a fila e a task
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //Inicia a task GPIO
    xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);

    //Inicia gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(BOTAO1, gpio_isr_handler, (void*) BOTAO1);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(BOTAO2, gpio_isr_handler, (void*) BOTAO2);
    //
    gpio_isr_handler_add(BOTAO3, gpio_isr_handler, (void*) BOTAO3);


    //remove isr handler for gpio number.
    gpio_isr_handler_remove(BOTAO1);
    //hook isr handler for specific gpio pin again
    gpio_isr_handler_add(BOTAO1, gpio_isr_handler, (void*) GPIO_INPUT_IO_0);


}
