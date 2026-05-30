/*
 * Module 2.5 — Exhaust Fan Timer Controller
 *
 * Board : ESP32-S3-DevKitC-1
 * Relay : GPIO_NUM_2  (active-HIGH relay module; HIGH = fan ON)
 * LED   : GPIO_NUM_47 (status indicator, mirrors relay state)
 *
 * Schedule: fan turns ON every FAN_PERIOD_US and stays ON for FAN_ON_TIME_US,
 * then turns off automatically. All timing runs inside the esp_timer task —
 * fully independent of app_main's loop. No delay() is used for timing.
 *
 * ── Timings ──────────────────────────────────────────────────────────────
 *   Production : 1 h period, 15 min on-time  (ratio 1:4)
 *   Debug      : 10 s period,  2.5 s on-time (same 1:4 ratio)
 *   Swap the two #define blocks below to switch modes.
 *
 * ── Optional watchdog ────────────────────────────────────────────────────
 *   Enable CONFIG_ESP_TASK_WDT_EN + CONFIG_ESP_TASK_WDT_TIMEOUT_S in
 *   sdkconfig (or via menuconfig), then set USE_WATCHDOG 1.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
// #include "esp_task_wdt.h"   // uncomment together with USE_WATCHDOG
#include "pin.h"

// ── Pin assignments ───────────────────────────────────────────────────────
Pin<GPIO_NUM_15> FAN_PIN;   // relay coil / motor driver IN
Pin<GPIO_NUM_4> LED_PIN;  // status LED

// ── Timings — PRODUCTION (1 h / 15 min) ──────────────────────────────────
// #define FAN_PERIOD_US   (60ULL * 60ULL * 1000000ULL)   // 1 hour
// #define FAN_ON_TIME_US  (15ULL * 60ULL * 1000000ULL)   // 15 minutes

// ── Timings — DEBUG (10 s / 2.5 s, ratio 1:4 preserved) ──────────────────
#define FAN_PERIOD_US   (10ULL * 1000000ULL)   // 10 seconds
#define FAN_ON_TIME_US  (2500ULL * 1000ULL)    // 2.5 seconds

// Set to 1 to run the first ON-cycle immediately at boot (otherwise the
// first trigger fires only after one full period has elapsed).
#define FIRE_ON_BOOT  1

// Set to 1 to enable task watchdog on the idle loop (requires sdkconfig).
#define USE_WATCHDOG  0

// ─────────────────────────────────────────────────────────────────────────

static const char *TAG = "FAN";

static volatile bool      g_fan_on  = false;
static esp_timer_handle_t s_off_timer;

// ── Internal helpers ──────────────────────────────────────────────────────

static void set_fan(bool on)
{
    if (g_fan_on == on) return;   // guard redundant transitions
    g_fan_on = on;
    FAN_PIN.set_level(on ? 1 : 0);
    LED_PIN.set_level(on ? 1 : 0);
    ESP_LOGI(TAG, "Fan %-3s  [relay=%d led=%d]", on ? "ON" : "OFF", on, on);
}

// ── Timer callbacks — run in esp_timer task, not in app_main ─────────────

static void fan_off_cb(void * /*arg*/)
{
    set_fan(false);
    ESP_LOGI(TAG, "Cycle complete. Next ON in %.1f s",
             (double)FAN_PERIOD_US / 1e6);
}

static void fan_on_cb(void * /*arg*/)
{
    if (g_fan_on) {
        // Period elapsed while on-time was still running — skip this trigger.
        // This can happen if FAN_ON_TIME_US >= FAN_PERIOD_US (misconfiguration).
        ESP_LOGW(TAG, "Re-trigger skipped — fan still ON, check timings");
        return;
    }
    set_fan(true);
    ESP_LOGI(TAG, "Fan ON for %.1f s", (double)FAN_ON_TIME_US / 1e6);
    ESP_ERROR_CHECK(esp_timer_start_once(s_off_timer, FAN_ON_TIME_US));
}

// ── Entry point ───────────────────────────────────────────────────────────

extern "C" void app_main(void)
{
    // GPIO: relay + LED as push-pull outputs, default OFF
    gpio_config_t cfg = {};
    cfg.pin_bit_mask  = (1ULL << FAN_PIN.get_num()) | (1ULL << LED_PIN.get_num());
    cfg.mode          = GPIO_MODE_OUTPUT;
    cfg.pull_up_en    = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type     = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&cfg));

    // One-shot timer: turns fan OFF after FAN_ON_TIME_US
    esp_timer_create_args_t off_args = {};
    off_args.callback = fan_off_cb;
    off_args.name     = "fan_off";
    ESP_ERROR_CHECK(esp_timer_create(&off_args, &s_off_timer));

    // Periodic timer: starts a new ON-cycle every FAN_PERIOD_US
    esp_timer_handle_t on_timer;
    esp_timer_create_args_t on_args = {};
    on_args.callback = fan_on_cb;
    on_args.name     = "fan_on";
    ESP_ERROR_CHECK(esp_timer_create(&on_args, &on_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(on_timer, FAN_PERIOD_US));

    ESP_LOGI(TAG, "Scheduler ready — period=%.0f s, on-time=%.1f s",
             (double)FAN_PERIOD_US / 1e6, (double)FAN_ON_TIME_US / 1e6);

#if FIRE_ON_BOOT
    fan_on_cb(nullptr);   // immediate first cycle without waiting one period
#endif

#if USE_WATCHDOG
    // esp_task_wdt_add(nullptr);   // register this task with the WDT
#endif

    // Idle loop — all fan logic lives in the esp_timer callbacks above.
    // vTaskDelay here is not for timing; it just yields the CPU.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5000));
#if USE_WATCHDOG
        // esp_task_wdt_reset();
#endif
    }
}
