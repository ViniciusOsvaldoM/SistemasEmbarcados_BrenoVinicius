// CEFET-MG - ENGENHARIA ELÉTRICA
// Disciplina de Sistemas Embarcados
// Professor: Túlio Charles
// Alunos: Breno Guimarães
//         Vinícius Osvaldo

// PRÁTICA 4
// Objetivo: fazer um controle PWM utilizando 3 botões por meio de interrupções, timers, filas e semáforos.

/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
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
#include "soc/soc_caps.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
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
#include "esp_sleep.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"
#include "esp_sntp.h"
#include <json_generator.h>



#define CONFIG_SNTP_TIME_SERVER "pool.ntp.org"
//------------------------sntp-------------------------------
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

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

//----------------------- GPIO -------------------------------
#define BOTAO1 21
#define BOTAO2 22
#define BOTAO3 23
#define GPIO_INPUT_PIN_SEL ((1ULL << BOTAO1) | (1ULL << BOTAO2) | (1ULL << BOTAO3))
#define LED_ESP 2
#define GPIO_OUTPUT_PIN_SEL (1ULL << LED_ESP)

#define ESP_INTR_FLAG_DEFAULT 0 // Flag para GPIO
         // Frequency in Hertz. Set frequency at 5 kHz

//----------------------- TAGS ESPLOGS -------------------------------
static const char *TAG = "boot";
static const char *TAG2 = "botoes";
static const char *TAG3 = "RELOGIO";
const static char *TAG4 = "EXAMPLE";
static const char *TAG5 = "Mqtt";
static const char *TAG6 = "sntp";

//----------------------- Filas -------------------------------
static QueueHandle_t fila_gpio = NULL;
static QueueHandle_t fila_contador = NULL;
static QueueHandle_t fila_I2C = NULL;
static QueueHandle_t fila_Mqtt_cor = NULL;
static QueueHandle_t fila_registro = NULL;

//----------------------- Semáforos -------------------------------
static SemaphoreHandle_t semaphore_registro = NULL;

typedef struct
{
    uint64_t contagem_atual;
    uint64_t valor_do_alarme;
} acumulador_t;

typedef struct
{
    int horas;
    int minutos;
    int segundos;
} relogio_t;

typedef struct
{
    char nome[20];
    char hora[20];
} registro_t;

typedef struct {
    char buf[256];
    size_t offset;
} json_gen_test_result_t;       //Struct para gerar o JSON


//----------------------- JSON Generator -------------------------------

static void flush_str(char *buf, void *priv)
{
    json_gen_test_result_t *result = (json_gen_test_result_t *)priv;
    if (result) {
        if (strlen(buf) > sizeof(result->buf) - result->offset) {
            printf("Result Buffer too small\r\n");
            return;
        }
        memcpy(result->buf + result->offset, buf, strlen(buf));
        result->offset += strlen(buf);
    }
}


static int json_gen_perform_test(json_gen_test_result_t *result, const char *expected)
{
	char buf[20];
    memset(result, 0, sizeof(json_gen_test_result_t));
	json_gen_str_t jstr;
	json_gen_str_start(&jstr, buf, sizeof(buf), flush_str, result);
	json_gen_start_object(&jstr);
	json_gen_obj_set_string(&jstr, "Nome", registro.nome);
	json_gen_obj_set_string(&jstr, "Hora", registro.hora);
	json_gen_end_object(&jstr);
	json_gen_str_end(&jstr);
    if (strcmp(expected, result->buf) == 0) {
        return 0;
    } else {
        return -1;
    }
}


//-----------------------sntp-------------------------------
/* LwIP SNTP example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


/* Variable holding number of times ESP32 restarted since first boot.
 * It is placed into RTC memory using RTC_DATA_ATTR and
 * maintains its value when ESP32 wakes from deep sleep.
 */

RTC_DATA_ATTR static int boot_count = 0;

static void obtain_time(void);

#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_CUSTOM
void sntp_sync_time(struct timeval *tv)
{
   settimeofday(tv, NULL);
   ESP_LOGI(TAG, "Time is synchronized from custom code");
   sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}
#endif

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG6, "Notification of a time synchronization event");
}


static void print_servers(void)
{
    ESP_LOGI(TAG6, "List of configured NTP servers:");

    for (uint8_t i = 0; i < SNTP_MAX_SERVERS; ++i){
        if (esp_sntp_getservername(i)){
            ESP_LOGI(TAG6, "server %d: %s", i, esp_sntp_getservername(i));
        } else {
            // we have either IPv4 or IPv6 address, let's print it
            char buff[INET6_ADDRSTRLEN];
            ip_addr_t const *ip = esp_sntp_getserver(i);
            if (ipaddr_ntoa_r(ip, buff, INET6_ADDRSTRLEN) != NULL)
                ESP_LOGI(TAG6, "server %d: %s", i, buff);
        }
    }
}

static void obtain_time(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

#if LWIP_DHCP_GET_NTP_SRV
    /**
     * NTP server address could be acquired via DHCP,
     * see following menuconfig options:
     * 'LWIP_DHCP_GET_NTP_SRV' - enable STNP over DHCP
     * 'LWIP_SNTP_DEBUG' - enable debugging messages
     *
     * NOTE: This call should be made BEFORE esp acquires IP address from DHCP,
     * otherwise NTP option would be rejected by default.
     */
    ESP_LOGI(TAG6, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
    config.start = false;                       // start SNTP service explicitly (after connecting)
    config.server_from_dhcp = true;             // accept NTP offers from DHCP server, if any (need to enable *before* connecting)
    config.renew_servers_after_new_IP = true;   // let esp-netif update configured SNTP server(s) after receiving DHCP lease
    config.index_of_first_server = 1;           // updates from server num 1, leaving server 0 (from DHCP) intact
    // configure the event on which we renew servers
#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
#else
    config.ip_event_to_renew = IP_EVENT_ETH_GOT_IP;
#endif
    config.sync_cb = time_sync_notification_cb; // only if we need the notification function
    esp_netif_sntp_init(&config);

#endif /* LWIP_DHCP_GET_NTP_SRV */

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

#if LWIP_DHCP_GET_NTP_SRV
    ESP_LOGI(TAG6, "Starting SNTP");
    esp_netif_sntp_start();
#if LWIP_IPV6 && SNTP_MAX_SERVERS > 2
    /* This demonstrates using IPv6 address as an additional SNTP server
     * (statically assigned IPv6 address is also possible)
     */
    ip_addr_t ip6;
    if (ipaddr_aton("2a01:3f7::1", &ip6)) {    // ipv6 ntp source "ntp.netnod.se"
        esp_sntp_setserver(2, &ip6);
    }
#endif  /* LWIP_IPV6 */

#else
    ESP_LOGI(TAG6, "Initializing and starting SNTP");
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    /* This demonstrates configuring more than one server
     */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
                               ESP_SNTP_SERVER_LIST(CONFIG_SNTP_TIME_SERVER, "pool.ntp.org" ) );
#else
    /*
     * This is the basic default config with one server and starting the service
     */
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
#endif
    config.sync_cb = time_sync_notification_cb;     // Note: This is only needed if we want
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    config.smooth_sync = true;
#endif

    esp_netif_sntp_init(&config);
#endif

    print_servers();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 15;
    while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && ++retry < retry_count) {
        ESP_LOGI(TAG6, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    }
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_ERROR_CHECK( example_disconnect() );
    esp_netif_sntp_deinit();

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

        msg_id = esp_mqtt_client_subscribe(client, "green", 0);
        ESP_LOGI(TAG6, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "blue", 0);
        ESP_LOGI(TAG6, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "red", 0);
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
        // converte event->data para inteiro e envia para a fila
        // respeitar o event->data_len para evitar problemas de buffer overflow
        // como distinguir os tópicos? usar event->topic para isso

        char nome [20];

        if (strncmp(event->data, "breno123", event->data_len) == 0)
        {
            strcpy(nome, "breno");
//            printf("Green: %d\n", cor.green);
            xQueueSend(fila_registro, &nome, 10);    
        }   

        else if (strncmp(event->data, "vinicius123", event->data_len) == 0)
        {
            strcpy(nome, "vinicius");
            xQueueSend(fila_registro, &nome, 10);
        }

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


//------------------------ I2C Display -------------------------------
// To use LV_COLOR_FORMAT_I1, we need an extra buffer to hold the converted data
static uint8_t oled_buffer[EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES / 8];
// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;
lv_obj_t *label;
lv_obj_t *label2;

void example_lvgl_demo_ui(lv_display_t *disp)
{

    lv_obj_t *scr = lv_display_get_screen_active(disp);
    label = lv_label_create(scr);
    label2 = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, "00:00:00  ");
    lv_obj_set_width(label, lv_display_get_horizontal_resolution(disp));
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align(label2, LV_ALIGN_BOTTOM_MID, 0, 0);
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

    char buff[64];
    entrada_t parametro;
    relogio_t clock;
    while (1)
    {
        if (xQueueReceive(fila_registro, &parametro, 10))
        {
            snprintf(buff, sizeof(buff), "Bem-vindo, %d mV", parametro.nome);
            _lock_acquire(&lvgl_api_lock);
            lv_label_set_text(label2, buff);
            _lock_release(&lvgl_api_lock);
        }

        if (xQueueReceive(fila_I2C, &clock, 10))
        {
            snprintf(buff, sizeof(buff), " %d:%d:%d", clock.horas, clock.minutos, clock.segundos);
            _lock_acquire(&lvgl_api_lock);
            lv_label_set_text(label, buff);
            _lock_release(&lvgl_api_lock);
        }
    }
}

//----------------------- Interrupção GPIO -------------------------------
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(fila_gpio, &gpio_num, NULL);
}

/* ----------------------- Tarefa GPIO  ------------------------------- */
static void gpio_task(void *arg)
{
    gpio_config_t io_conf = {};
    // disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    // bit mask of the pin 2
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    // disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);

    // interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    // bit mask of the pins 21,22,23
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    // set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    // enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    // change gpio interrupt type for one pin
    gpio_set_intr_type(LED_ESP, GPIO_INTR_ANYEDGE);

    // Inicia gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    // hook isr handler for specific gpio pin
    gpio_isr_handler_add(BOTAO1, gpio_isr_handler, (void *)BOTAO1);
    gpio_isr_handler_add(BOTAO2, gpio_isr_handler, (void *)BOTAO2);
    gpio_isr_handler_add(BOTAO3, gpio_isr_handler, (void *)BOTAO3);

    uint32_t io_num;
    int LED_STATE = 0;

    for (;;)
    {
        if (xQueueReceive(fila_gpio, &io_num, 10))
        {

            if (io_num == BOTAO1)
            {
                gpio_set_level(LED_ESP, 1);
                ESP_LOGI(TAG2, "\n\nBotao 1 pressionado");
                ESP_LOGI(TAG2, "Digite a senha: \n");
            }
        }

        xQueueSend(fila_registro, &io_num, NULL);
    }
}

/* ----------------------- Alarme do timer ------------------------------- */

static bool IRAM_ATTR timer_alarme(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    static uint64_t contagem = 0;
    uint64_t alarme = edata->count_value;

    contagem += alarme;
    acumulador_t ele = {
        .contagem_atual = contagem,
        .valor_do_alarme = alarme};

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
static void timer_task(void *arg)
{
    uint64_t ultimo_log = 0;
    acumulador_t dado;
    dado.contagem_atual = 0;
    dado.valor_do_alarme = 0;

    ESP_LOGI(TAG3, "Create timer handle");
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1MHz, 1 tick=1us
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_alarme,
    };

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_LOGI(TAG3, "Enable timer");
    ESP_ERROR_CHECK(gptimer_enable(gptimer));

    ESP_LOGI(TAG3, "Start timer, update alarm value dynamically");
    gptimer_alarm_config_t alarm_config3 = {
        .alarm_count = 1000000, // period = 1s
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config3));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    relogio_t clock = {0, 0, 0};
    uint64_t segundos_totais = 0;
    tensao_t tensao;
    static int contador = 0;

    for (;;)
    {
        if (xQueueReceive(fila_contador, &dado, portMAX_DELAY))
        {
            segundos_totais = dado.contagem_atual / 1000000;
            clock.horas = (segundos_totais / 3600) % 24;
            clock.minutos = (segundos_totais / 60) % 60;
            clock.segundos = segundos_totais % 60;

            if (segundos_totais != ultimo_log)
            {
                if (contador % 10 == 0)
                {
                    ultimo_log = segundos_totais;
                    ESP_LOGI(TAG3, "Hora: %02d: %02d: %02d | Contagem: %llu | Alarme: %llu",
                             clock.horas, clock.minutos, clock.segundos,
                             dado.contagem_atual, dado.valor_do_alarme);
                }
            }
        }

        xSemaphoreGive(semaphore_registro);
    }
}


//----------------------- Main -------------------------------
void app_main(void)
{
    json_gen_test_result_t result; // Struct para gerar o JSON

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

    ESP_LOGI(TAG6, "[APP] Startup..");
    ESP_LOGI(TAG6, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG6, "[APP] IDF version: %s", esp_get_idf_version());

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
    fila_gpio = xQueueCreate(10, sizeof(uint32_t));
    fila_contador = xQueueCreate(10, sizeof(uint32_t));
    fila_I2C = xQueueCreate(10, sizeof(char));
    fila_Mqtt_cor = xQueueCreate(10, sizeof(cor_t));
    fila_registro = xQueueCreate(10, sizeof(char));

    // Criação do semáforo
    semaphore_registro = xSemaphoreCreateBinary();

    // Criação das tarefas

    xTaskCreate(gpio_task, "Tarefa para o GPIO", 4096, NULL, 10, NULL);
    xTaskCreate(timer_task, "Tarefa para o timer", 4096, NULL, 10, NULL);
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);
    xTaskCreate(example_display_port_task, "LVGL do Display", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    // MQTT

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

    // esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
//     /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
//     esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
//     esp_mqtt_client_start(client);


    // Protocolo SNTP

    ++boot_count;
        ESP_LOGI(TAG6, "Boot count: %d", boot_count);

        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        // Is time set? If not, tm_year will be (1970 - 1900).
        if (timeinfo.tm_year < (2016 - 1900)) {
            ESP_LOGI(TAG6, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
            obtain_time();
            // update 'now' variable with current time
            time(&now);
        }
    #ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
        else {
            // add 500 ms error to the current system time.
            // Only to demonstrate a work of adjusting method!
            {
                ESP_LOGI(TAG6, "Add a error for test adjtime");
                struct timeval tv_now;
                gettimeofday(&tv_now, NULL);
                int64_t cpu_time = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;
                int64_t error_time = cpu_time + 500 * 1000L;
                struct timeval tv_error = { .tv_sec = error_time / 1000000L, .tv_usec = error_time % 1000000L };
                settimeofday(&tv_error, NULL);
            }

            ESP_LOGI(TAG6, "Time was set, now just adjusting it. Use SMOOTH SYNC method.");
            obtain_time();
            // update 'now' variable with current time
            time(&now);
        }
    #endif

        char strftime_buf[64];

        // Set timezone to Brasil Standard Time
        setenv("TZ", "BRT3", 1);
        tzset();
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG6, "The current date/time in Brazil is: %s", strftime_buf);

        if (sntp_get_sync_mode() == SNTP_SYNC_MODE_SMOOTH) {
            struct timeval outdelta;
            while (sntp_get_sync_status() == SNTP_SYNC_STATUS_IN_PROGRESS) {
                adjtime(NULL, &outdelta);
                ESP_LOGI(TAG6, "Waiting for adjusting time ... outdelta = %jd sec: %li ms: %li us",
                            (intmax_t)outdelta.tv_sec,
                            outdelta.tv_usec/1000,
                            outdelta.tv_usec%1000);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
            }
        }

        const int deep_sleep_sec = 10;
        ESP_LOGI(TAG6, "Entering deep sleep for %d seconds", deep_sleep_sec);
        esp_deep_sleep(1000000LL * deep_sleep_sec);

}
