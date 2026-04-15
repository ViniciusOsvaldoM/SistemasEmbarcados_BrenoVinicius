/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gptimer.h"
#include "esp_log.h"

static const char *TAG = "example";

typedef struct {
    uint64_t contagem_atual;  
    uint64_t valor_do_alarme;   
} acumulador;

typedef struct {
    int horas;
    int minutos;
    int segundos;
} relogio;


QueueHandle_t fila_contador = NULL;

static bool IRAM_ATTR example_timer_on_alarm_cb_v3(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    static uint64_t contagem = 0;
    uint64_t alarme = edata->valor_do_alarme;

    contagem += alarme;
    acumulador ele = {
        .contagem_atual = contagem;
        .valor_do_alarme = alarme;
        };
        
    BaseType_t high_task_awoken = pdFALSE;
    // Retrieve count value and send to queue

    xQueueSendFromISR(queue, &ele, &high_task_awoken);
    // reconfigure alarm value
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = edata->valor_do_alarme + 1000000, // alarm in next 1s
    };
    gptimer_set_alarm_action(timer, &alarm_config);
    // return whether we need to yield at the end of ISR
    return (high_task_awoken == pdTRUE);
}


static void timer_task(void* arg)
{
    relogio clock = {0};
    uint64_t ultimo_log = 0;
    uint32_t dado;
    acumulador recebido;

    timer_config_t config = {
        .divider = 80
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN
        .auto_reload = false;
    }

    /*  timer_init(TIMER_GROUP_0, TIMER_0,&config);
    timer_set_counter_value(TIMER_GROUP_0,TIMER_0,0);
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0,100000);
    timer_enable_intr(TIMER_GROUP_0, TIMER_0)
    timer_isr_callback_add(TIMER_GROUP_0,TIMER_0,timer_isr_callback_add,NULL,0)
    timer_start(TIMER_GROUP_0,TIMER_0)
    */
   
    relogio clock = {0, 0, 0};

    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &dado, portMAX_DELAY)) {
            gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
            uint64_t segundos_totais = dado.contagem / 1000000;
            relogio.hora = (segundos_totais/3600) % 24; 
            relogio.minutos = (segundos_totais / 60) % 60;
            relogio.segundos = segundos_totais % 60;
 
            if(segundos_totais != ultimo_log) {
                ultimo_log = segundo_totais;
                ESP_LOGI(TAG, "Hora: %02d: %02d: %02 | Contagem: %llu | Alarme: %llu",
                relogio.hora, relogio.minutos, relogio.segundos,
                dado.contagem, dado.valor_do_alarme);

            }
        }
            
            //start gpio task
        
        }
    }



void app_main(void)
{


        /* -------------------------------- Chip information ------------------------------------------------ */

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


    /* -------------------------------- Configuração do I/O ------------------------------------------------ */
    
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

    /* -------------------------------- Iniicialização da tarefa ------------------------------------------------ */


    xTaskCreate(timer_task, "Tarefa para o timer", 2048, NULL, 10, NULL);

    example_queue_element_t ele;
    QueueHandle_t queue = xQueueCreate(10, sizeof(example_queue_element_t));
    if (!queue) {
        ESP_LOGE(TAG, "Creating queue failed");
        return;
    }

    ESP_LOGI(TAG, "Create timer handle");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = example_timer_on_alarm_cb_v1,
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, queue));

    ESP_LOGI(TAG, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Start timer, stop it at alarm event");
    gptimer_alarm_config_t alarm_config1 = {
        .alarm_count = 1000000, // period = 1s
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config1));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    if (xQueueReceive(queue, &ele, pdMS_TO_TICKS(2000))) {
        ESP_LOGI(TAG, "Timer stopped, count=%llu", ele.event_count);
    } else {
        ESP_LOGW(TAG, "Missed one count event");
    }

    ESP_LOGI(TAG, "Set count value");
    ESP_ERROR_CHECK(gptimer_set_raw_count(gptimer, 100));
    ESP_LOGI(TAG, "Get count value");
    uint64_t count;
    ESP_ERROR_CHECK(gptimer_get_raw_count(gptimer, &count));
    ESP_LOGI(TAG, "Timer count value=%llu", count);

    // before updating the alarm callback, we should make sure the timer is not in the enable state
    ESP_LOGI(TAG, "Disable timer");
    ESP_ERROR_CHECK(gptimer_disable(gptimer));
    // set a new callback function
    cbs.on_alarm = example_timer_on_alarm_cb_v2;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, queue));
    ESP_LOGI(TAG, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Start timer, auto-reload at alarm event");
    gptimer_alarm_config_t alarm_config2 = {
        .reload_count = 0,
        .alarm_count = 1000000, // period = 1s
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config2));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    int record = 4;
    while (record) {
        if (xQueueReceive(queue, &ele, pdMS_TO_TICKS(2000))) {
            ESP_LOGI(TAG, "Timer reloaded, count=%llu", ele.event_count);
            record--;
        } else {
            ESP_LOGW(TAG, "Missed one count event");
        }
    }
    ESP_LOGI(TAG, "Stop timer");
    ESP_ERROR_CHECK(gptimer_stop(gptimer));

    ESP_LOGI(TAG, "Disable timer");
    ESP_ERROR_CHECK(gptimer_disable(gptimer));
    cbs.on_alarm = example_timer_on_alarm_cb_v3;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, queue));
    ESP_LOGI(TAG, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG, "Start timer, update alarm value dynamically");
    gptimer_alarm_config_t alarm_config3 = {
        .alarm_count = 1000000, // period = 1s
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config3));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
    record = 4;
    while (record) {
        if (xQueueReceive(queue, &ele, pdMS_TO_TICKS(2000))) {
            ESP_LOGI(TAG, "Timer alarmed, count=%llu", ele.event_count);
            record--;
        } else {
            ESP_LOGW(TAG, "Missed one count event");
        }
    }

    ESP_LOGI(TAG, "Stop timer");
    ESP_ERROR_CHECK(gptimer_stop(gptimer));
    ESP_LOGI(TAG, "Disable timer");
    ESP_ERROR_CHECK(gptimer_disable(gptimer));
    ESP_LOGI(TAG, "Delete timer");
    ESP_ERROR_CHECK(gptimer_del_timer(gptimer));

    vQueueDelete(queue);
}
