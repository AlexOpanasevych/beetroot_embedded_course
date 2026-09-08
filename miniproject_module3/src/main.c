#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

#include "lwip/sockets.h"

/*
 * Two 15 W subwoofers driven by an external I2S DAC + class-D amp board.
 * Audio arrives over Wi-Fi as raw 16-bit stereo PCM (see tools/pc_audio_sender.py),
 * not Bluetooth: ESP32-S3 has no classic-BT/A2DP radio and no LE Audio (ISO
 * channel) support, so a UDP PCM link is the real substitute here.
 */

#define SAMPLE_RATE_HZ    44100

#define I2S_BCLK_PIN       GPIO_NUM_4
#define I2S_WS_PIN         GPIO_NUM_5
#define I2S_DOUT_PIN       GPIO_NUM_6
#define STATUS_LED_PIN     GPIO_NUM_2
#define AMP_ENABLE_PIN     GPIO_NUM_7
#define VOLUME_ADC_CHANNEL ADC_CHANNEL_0 /* GPIO1 on ESP32-S3 */

#define WIFI_CONNECTED_BIT BIT0
#define AUDIO_BUF_SAMPLES  512 /* interleaved L/R int16 samples per UDP packet */
#define VOLUME_UNITY_Q8    256
#define VOLUME_MAX_Q8      230 /* headroom below unity gain, avoids digital clipping */

static const char *TAG = "subwoofer_audio";

static EventGroupHandle_t s_wifi_event_group;
static i2s_chan_handle_t s_tx_chan;
static adc_oneshot_unit_handle_t s_adc_handle;

static volatile int32_t s_volume_q8 = 180; /* default ~70% of full scale */
static volatile TickType_t s_last_packet_tick = 0;

static void gpio_init(void)
{
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << STATUS_LED_PIN) | (1ULL << AMP_ENABLE_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));
    gpio_set_level(STATUS_LED_PIN, 0);
    gpio_set_level(AMP_ENABLE_PIN, 0); /* keep amp muted until the pipeline is up */
}

static void volume_adc_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, VOLUME_ADC_CHANNEL, &chan_cfg));
}

static void i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED, /* PCM5102 uses its internal PLL, SCK tied to GND */
            .bclk = I2S_BCLK_PIN,
            .ws = I2S_WS_PIN,
            .dout = I2S_DOUT_PIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying");
        gpio_set_level(AMP_ENABLE_PIN, 0);
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID '%s'...", CONFIG_WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

static void volume_task(void *arg)
{
    while (1) {
        int raw = 0;
        if (adc_oneshot_read(s_adc_handle, VOLUME_ADC_CHANNEL, &raw) == ESP_OK) {
            s_volume_q8 = (raw * VOLUME_MAX_Q8) / 4095;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void led_status_task(void *arg)
{
    while (1) {
        if ((xTaskGetTickCount() - s_last_packet_tick) < pdMS_TO_TICKS(1000)) {
            gpio_set_level(STATUS_LED_PIN, 1); /* solid: audio actively streaming */
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            gpio_set_level(STATUS_LED_PIN, 1);
            vTaskDelay(pdMS_TO_TICKS(400));
            gpio_set_level(STATUS_LED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(400)); /* slow blink: waiting for a sender */
        }
    }
}

static void udp_audio_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(CONFIG_AUDIO_UDP_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Listening for PCM audio on UDP port %d", CONFIG_AUDIO_UDP_PORT);
    gpio_set_level(AMP_ENABLE_PIN, 1); /* pipeline is up, unmute the amp */

    static int16_t rx_buf[AUDIO_BUF_SAMPLES];
    while (1) {
        int len = recv(sock, rx_buf, sizeof(rx_buf), 0);
        if (len <= 0) {
            continue;
        }

        int samples = len / (int) sizeof(int16_t);
        int32_t vol = s_volume_q8;
        for (int i = 0; i < samples; i++) {
            rx_buf[i] = (int16_t) (((int32_t) rx_buf[i] * vol) >> 8);
        }

        s_last_packet_tick = xTaskGetTickCount();

        size_t written = 0;
        i2s_channel_write(s_tx_chan, rx_buf, samples * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_init();
    volume_adc_init();
    i2s_init();
    wifi_init_sta();

    xTaskCreatePinnedToCore(udp_audio_task, "udp_audio", 4096, NULL, 10, NULL, 1);
    xTaskCreate(volume_task, "volume", 2048, NULL, 5, NULL);
    xTaskCreate(led_status_task, "led_status", 2048, NULL, 3, NULL);
}
