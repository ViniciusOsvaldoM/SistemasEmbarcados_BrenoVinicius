// CEFET-MG - ENGENHARIA ELÉTRICA
// Disciplina de Sistemas Embarcados
// Professor: Túlio Charles
// Alunos: Breno Guimarães
//         Vinícius Osvaldo

// Trabalho final - Jogo da cobrinha


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
#include "esp_err.h"
#include "soc/soc_caps.h"
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "driver/i2c_master.h"
#include "lvgl.h"
#include <stdint.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include <time.h>
#include <sys/time.h>
#include "esp_attr.h"



//----------------------- I2C -------------------------------

#include "esp_lcd_panel_vendor.h"

#define I2C_BUS_PORT 0

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (400 * 1000)
#define EXAMPLE_PIN_NUM_SDA 19
#define EXAMPLE_PIN_NUM_SCL 18
#define EXAMPLE_PIN_NUM_RST -1
#define EXAMPLE_I2C_HW_ADDR 0x3C

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES 128
#define EXAMPLE_LCD_V_RES 64
// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8

#define EXAMPLE_LVGL_TICK_PERIOD_MS 5
#define EXAMPLE_LVGL_TASK_STACK_SIZE (4 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY 2
#define EXAMPLE_LVGL_PALETTE_SIZE 8
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ

#define TAMANHO_COBRA 5     //numero de segmentos

//---------------------------- TAGS ESPLOGS --------------------------------

static const char *TAG = "boot"; // Boot
static const char *TAG5 = "I2C"; // Display
static const char *TAG6 = "MQTT"; // MQTT

//--------------------------------- FILAS -------------------------------

static QueueHandle_t fila_I2C = NULL; // Fila para o display


//------------------------ I2C Display -------------------------------
// To use LV_COLOR_FORMAT_I1, we need an extra buffer to hold the converted data
static uint8_t oled_buffer[EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 8];
// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;
lv_obj_t *label;
lv_obj_t *label2;
lv_obj_t *cobra; // Variável global para a cobra
int largura = 10;
lv_obj_t *semente; // Variável global para a semente


void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    //label = lv_label_create(scr);
   // label2 = lv_label_create(scr);
    //lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);
    //lv_label_set_text(label2, "Jogo da cobrinha");
    //lv_obj_align(label2, LV_ALIGN_CENTER, 0, 0);
   // lv_obj_set_width(label2, lv_display_get_horizontal_resolution(disp));
    //lv_obj_align(label2, LV_ALIGN_TOP_MID, 0, 0);
   // lv_obj_align(label2, LV_ALIGN_BOTTOM_MID, 0, 0);


    // Cria a cobra usando a variável global
    cobra = lv_obj_create(scr);
    lv_obj_set_size(cobra, largura, 10); //proporção da cobra 
    lv_obj_set_style_bg_color(cobra, lv_color_black(), 0);
    lv_obj_align(cobra, LV_ALIGN_CENTER, 0, 0);
    
    //cria a semente
    semente = lv_obj_create(scr);
    lv_obj_set_size(semente, 7, 7);
    lv_obj_set_style_bg_color(semente, lv_color_black(), 0);
    lv_obj_set_pos(semente, 20, 20); //posição inicial da semente

   
}

void reposicionar_semente()
{
    int max_x = EXAMPLE_LCD_H_RES - 5;
    int max_y = EXAMPLE_LCD_V_RES - 5;

    int novo_x = (rand() % max_x);
    int novo_y = (rand() % max_y);

    lv_obj_set_pos(semente, novo_x, novo_y);
}


static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t io_panel, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);

    // This is necessary because LVGL reserves 2 x 4 bytes in the buffer, as these are assumed to be used as a palette. Skip the palette here
    // More information about the monochrome, please refer to https://docs.lvgl.io/9.2/porting/display.html#monochrome-displays
    px_map += EXAMPLE_LVGL_PALETTE_SIZE;

    uint16_t hor_res = lv_display_get_physical_horizontal_resolution(disp);
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;

    for (int y = y1; y <= y2; y++)
    {
        for (int x = x1; x <= x2; x++)
        {
            /* The order of bits is MSB first
                        MSB           LSB
               bits      7 6 5 4 3 2 1 0
               pixels    0 1 2 3 4 5 6 7
                        Left         Right
            */
            bool chroma_color = (px_map[(hor_res >> 3) * y + (x >> 3)] & 1 << (7 - x % 8));

            /* Write to the buffer as required for the display.
             * It writes only 1-bit for monochrome displays mapped vertically.*/
            uint8_t *buf = oled_buffer + hor_res * (y >> 3) + (x);
            if (chroma_color)
            {
                (*buf) &= ~(1 << (y % 8));
            }
            else
            {
                (*buf) |= (1 << (y % 8));
            }
        }
    }
    // pass the draw buffer to the driver
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, oled_buffer);
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG5, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1)
    {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, EXAMPLE_LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, EXAMPLE_LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}


static void example_display_port_task(void *arg)
{
    ESP_LOGI(TAG5, "Initialize I2C bus");
    i2c_master_bus_handle_t i2c_bus = NULL;
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .i2c_port = I2C_BUS_PORT,
        .sda_io_num = EXAMPLE_PIN_NUM_SDA,
        .scl_io_num = EXAMPLE_PIN_NUM_SCL,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &i2c_bus));

    ESP_LOGI(TAG5, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = EXAMPLE_I2C_HW_ADDR,
        .scl_speed_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .control_phase_bytes = 1,               // According to SSD1306 datasheet
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,   // According to SSD1306 datasheet
        .lcd_param_bits = EXAMPLE_LCD_CMD_BITS, // According to SSD1306 datasheet
        .dc_bit_offset = 6,                     // According to SSD1306 datasheet

    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    ESP_LOGI(TAG5, "Install SSD1306 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = EXAMPLE_PIN_NUM_RST,
    };

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = EXAMPLE_LCD_V_RES,
    };
    panel_config.vendor_config = &ssd1306_config;
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG5, "Initialize LVGL");
    lv_init();
    // create a lvgl display
    lv_display_t *display = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    // associate the i2c panel handle to the display
    lv_display_set_user_data(display, panel_handle);
    // create draw buffer
    void *buf = NULL;
    ESP_LOGI(TAG5, "Allocate separate LVGL draw buffers");
    // LVGL reserves 2 x 4 bytes in the buffer, as these are assumed to be used as a palette.
    size_t draw_buffer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 8 + EXAMPLE_LVGL_PALETTE_SIZE;
    buf = heap_caps_calloc(1, draw_buffer_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buf);

    // LVGL9 suooprt new monochromatic format.
    lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_FULL);
    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, example_lvgl_flush_cb);

    ESP_LOGI(TAG5, "Register io panel event callback for LVGL flush ready notification");
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = example_notify_lvgl_flush_ready,
    };
    /* Register done callback */
    esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display);

    ESP_LOGI(TAG5, "Use esp_timer as LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG5, "Create LVGL task");
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG5, "Display LVGL Scroll Text");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    _lock_acquire(&lvgl_api_lock);
    example_lvgl_demo_ui(display);
    _lock_release(&lvgl_api_lock);


   
    
    int x = 0, y = 0;

    // Se a direção pressionada for direta:   incrementar x até 64 e voltar para -64 
    //                              esquerda: decrementar x até -32 e voltar para 32 
    //                              cima:     decrementar y até -32 e voltar para 32 
    //                              baixo:    incrementar y até 32 e voltar para -32 
    
    //lv_obj_t *scr = lv_display_get_screen_active(display);
   // lv_obj_clean(scr); // Limpa a tela antes de desenhar novamente
    while (1)
    {   
        char direcao[5];
        lv_area_t area_cobra, area_semente;
        lv_obj_get_coords(cobra, &area_cobra);
        lv_obj_get_coords(semente, &area_semente);
 
        //verifica colisão
        if (!(area_cobra.x2 < area_semente.x1 || 
              area_cobra.x1 > area_semente.x2 || 
              area_cobra.y2 < area_semente.y1 || 
              area_cobra.y1 > area_semente.y2))
        {
            //colisão derectada
            largura += 5; // aumenta o tamanho da cobra
            lv_obj_set_size(cobra, largura, 7); // atualiza o tamanho da cobra
            
            ESP_LOGI(TAG5, "Colisão detectada!");
            reposicionar_semente();
        }

        _lock_acquire(&lvgl_api_lock);
        lv_obj_set_pos(cobra, x, y);
        _lock_release(&lvgl_api_lock);

        xQueueReceive(fila_I2C, &direcao, 10);
        
        ESP_LOGI(TAG5, "Direcao recebida: %s", direcao);
        ESP_LOGI(TAG5, "x = %d, y = %d", x, y);
            
            if (strcmp(direcao, "up") == 0)
            {
                y = y - 5; // move verticalmente para cima
                if (y <= -32)
                    y = 32;
            vTaskDelay(pdMS_TO_TICKS(200));
            }
            else if (strcmp(direcao, "down") == 0)
            {
                y = y + 5; // move verticalmente para baixo
                if (y >= 32)
                    y = -32;

            vTaskDelay(pdMS_TO_TICKS(200));
            }
            else if (strcmp(direcao, "left") == 0)
            {
                x = x - 5; // move horizontalmente para a esquerda
                if (x <= -64)
                    x = 64;

            vTaskDelay(pdMS_TO_TICKS(200));
            }
            else if (strcmp(direcao, "right") == 0)
            {
                x = x + 5; // move horizontalmente
                if (x >= 64)
                    x = -64;

            vTaskDelay(pdMS_TO_TICKS(200));
            }
        

    }
    

}


//----------------------- Mqtt -------------------------------
/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/



static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG6, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */

 static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG6, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG6, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "/topic/qos1", "data_3", 0, 1, 0);
        ESP_LOGI(TAG6, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "up", 0);
        ESP_LOGI(TAG6, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "down", 0);
        ESP_LOGI(TAG6, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "left", 0);
        ESP_LOGI(TAG6, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "right", 0);
        ESP_LOGI(TAG6, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG6, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");

        ESP_LOGI(TAG6, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;

        // case MQTT_EVENT_DISCONNECTED:
        //     ESP_LOGI(TAG6, "MQTT_EVENT_DISCONNECTED");
        //     break;

        // case MQTT_EVENT_SUBSCRIBED:
        //     ESP_LOGI(TAG6, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        //     msg_id = esp_mqtt_client_publish(client, "green", "data", 0, 0, 0);
        //     ESP_LOGI(TAG6, "sent publish successful, msg_id=%d", msg_id);

        //     msg_id = esp_mqtt_client_publish(client, "blue", "data", 0, 0, 0);
        //     ESP_LOGI(TAG6, "sent publish successful, msg_id=%d", msg_id);
        //     msg_id = esp_mqtt_client_publish(client, "red", "data", 0, 0, 0);
        //     ESP_LOGI(TAG6, "sent publish successful, msg_id=%d", msg_id);

        //    break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG6, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
        // case MQTT_EVENT_PUBLISHED:
        //     ESP_LOGI(TAG6, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        //     break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG6, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        // Fazer o envio da direção para a fila I2C para atualizar o display
        // Fila I2C: Fila que recebe a string de direção
        // As direçoes serão comparadas com os tópicos dos botões: up, down, left, right
 
        char direcao[5];
        strncpy(direcao, event->topic, event->topic_len);
        ESP_LOGI(TAG6, "Direcao: %s", direcao);
        xQueueSend(fila_I2C, &direcao, 10);

        break;
        case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG6, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG6, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
        default:
        ESP_LOGI(TAG6, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://g5device:g5device@node02.myqtthub.com:1883", // CONFIG_BROKER_URL: username, senha
        .credentials.client_id = "g5device",                                       // ID
    };
#if CONFIG_BROKER_URL_FROM_STDIN
    char line[128];

    if (strcmp(mqtt_cfg.broker.address.uri, "FROM_STDIN") == 0)
    {
        int count = 0;
        printf("Please enter url of mqtt broker\n");
        while (count < 128)
        {
            int c = fgetc(stdin);
            if (c == '\n')
            {
                line[count] = '\0';
                break;
            }
            else if (c > 0 && c < 127)
            {
                line[count] = c;
                ++count;
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        mqtt_cfg.broker.address.uri = line;
        printf("Broker url: %s\n", line);
    }
    else
    {
        ESP_LOGE(TAG6, "Configuration mismatch: wrong broker url");
        abort();
    }
#endif /* CONFIG_BROKER_URL_FROM_STDIN */

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}




//---------------------------------- Main -----------------------------------------

void app_main(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Este é um microcontrolador %s com %d núcleo(s), %s%s%s%s ",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi/" : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
             (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "\n");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    ESP_LOGI(TAG, "silicon revision v%d.%d, \n", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha na Obtenção da Memória Flash!");
        return;
    }

    ESP_LOGI(TAG, "Memória Flash: %" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

    ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG, "Versão do ESP-IDF: %s\n", IDF_VER);

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

       //-------Mqtt Logs Config-------//
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();

    // Criação das filas
    fila_I2C = xQueueCreate(10, sizeof(char[5]));
 

    // Criação das tarefas

    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);
    xTaskCreate(example_display_port_task, "LVGL do Display", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);
    
}