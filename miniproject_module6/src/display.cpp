#include "display.h"
#include <stdio.h>
#include "ssd1306.h"
#include "esp_log.h"

static const char *TAG = "DISPLAY";
static ssd1306_handle_t s_disp;
static bool s_ready = false;

esp_err_t display_init(i2c_master_bus_handle_t bus)
{
    ssd1306_config_t cfg = I2C_SSD1306_128x64_CONFIG_DEFAULT;
    esp_err_t err = ssd1306_init(bus, &cfg, &s_disp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ssd1306_init failed: %s (display will stay blank)", esp_err_to_name(err));
        return ESP_OK; // non-fatal: keep the rest of the system running
    }

    ssd1306_clear_display(s_disp, false);
    s_ready = true;
    ESP_LOGI(TAG, "ready");
    return ESP_OK;
}

void display_render(const display_msg_t *msg)
{
    if (!s_ready) return;

    char line[32];

    if (msg->sample.valid) {
        snprintf(line, sizeof(line), "T:%5.1fC H:%4.1f%%", msg->sample.temperature_c, msg->sample.humidity_pct);
    } else {
        snprintf(line, sizeof(line), "SENSOR ERROR");
    }
    ssd1306_display_text(s_disp, 0, line, false);

    snprintf(line, sizeof(line), "P:%6.1f hPa", msg->sample.pressure_hpa);
    ssd1306_display_text(s_disp, 1, line, false);

    snprintf(line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u",
             2000 + msg->sample.year, msg->sample.month, msg->sample.day,
             msg->sample.hour, msg->sample.minute, msg->sample.second);
    ssd1306_display_text(s_disp, 2, line, false);

    snprintf(line, sizeof(line), "LOG:%s FLASH:%3u%%",
             msg->logging_enabled ? "ON " : "OFF", msg->flash_pct);
    ssd1306_display_text(s_disp, 3, line, false);

    if (msg->flash_full) {
        ssd1306_display_text(s_disp, 5, "!! FLASH FULL !!", true);
    } else {
        ssd1306_clear_display_page(s_disp, 5, false);
    }
}
