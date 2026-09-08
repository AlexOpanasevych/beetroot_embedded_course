/*
 * Module 6.6 — Forest logger: deep-sleep routine checks, an instant tamper
 * alarm, and a non-blocking "active" data-transmission phase.
 *
 * The device spends its life in three phases:
 *
 *   1. "Глибока сплячка" (Routine): every SLEEP_TIME_SEC (10 s) a timer
 *      wakes the chip from deep sleep, it reads the analog sensor (ADC),
 *      appends the reading to an RTC-memory log and increments the global
 *      "planned checks" counter, then goes straight back to deep sleep.
 *      Deep sleep wipes normal RAM and re-runs app_main() from scratch on
 *      every wake-up — only RTC_DATA_ATTR globals survive, which is why
 *      the counter and the ADC log live there.
 *
 *   2. "Тривога" (Tamper event): the digital trigger pin is wired to an
 *      EXT1 wake source, so a press wakes the chip immediately, without
 *      waiting for the 10 s timer. app_main() logs how many routine checks
 *      happened since the last report (using the same global counter) and
 *      resets it to zero.
 *
 *   3. "Передача даних" (Active/Wait): app_main() spawns a dedicated
 *      FreeRTOS task to report the data, then blocks on a task
 *      notification (itself not a busy loop) until that task is done.
 *      The reporting task prints "Встановлення з'єднання...", blocks for
 *      exactly 4 seconds on vTaskDelay() — the CPU goes to the FreeRTOS
 *      idle task / tickless sleep, it does NOT spin — prints the checks
 *      count and every logged ADC sample, then "Дані успішно відправлені!".
 *      Unlike phase 1, this 4 s pause is a plain RTOS delay, not deep
 *      sleep: the task's stack and local variables survive untouched and
 *      execution resumes on the very next line, and app_main() is not
 *      restarted. Only once that task signals completion does app_main()
 *      arm the wake sources again and go back to deep sleep.
 *
 * Board: ESP32-S3-DevKitC-1
 *   Trigger (tamper) button -> GPIO0 (BOOT button, active-LOW, board pull-up)
 *   Analog sensor            -> GPIO1 (ADC1_CH0, potentiometer/LDR wiper)
 *
 * Note on hardware wake sources: esp_sleep_enable_ext0_wakeup() only
 * exists on the original ESP32. ESP32-S3 has no EXT0 unit, so the trigger
 * uses esp_sleep_enable_ext1_wakeup() (ANY_LOW mode) instead.
 *
 * Optional harder variant: set ENABLE_STM32_UART to 1 below to also ping
 * an STM32 over UART1 at the start of the 4 s wait and log whatever
 * confirmation byte(s) it sends back during that window (received
 * asynchronously by the UART driver, so it never shortens the 4 s delay).
 * With it left at 0, phase 3 only prints to the serial monitor.
 */

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"

#define ENABLE_STM32_UART 0

#if ENABLE_STM32_UART
#include "driver/uart.h"
#define UART_PORT       UART_NUM_1
#define UART_TX_PIN     GPIO_NUM_17
#define UART_RX_PIN     GPIO_NUM_18
#define UART_BAUD_RATE  115200
#endif

// ── Hardware ─────────────────────────────────────────────────────────────
#define TRIGGER_PIN         GPIO_NUM_0   // BOOT button, active-LOW
#define SLEEP_TIME_SEC      10           // planned check interval (Phase 1)
#define TX_WAIT_MS          4000         // "wait for a response" (Phase 3)

#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_0 // GPIO1
#define ADC_ATTEN           ADC_ATTEN_DB_12
#define ADC_BITWIDTH        ADC_BITWIDTH_12

#define ADC_LOG_CAPACITY    64 // samples kept between two transmissions

static const char *TAG = "FOREST_LOGGER";

// ── Data that must survive deep sleep (main RAM is wiped on every wake) ──
RTC_DATA_ATTR static uint32_t routine_checks = 0;
RTC_DATA_ATTR static uint16_t adc_log[ADC_LOG_CAPACITY];
RTC_DATA_ATTR static uint32_t adc_log_count = 0;

// Snapshot handed to the transmission task; app_main() blocks until the
// task is finished with it, so a static buffer (no heap, no dangling
// stack) is enough.
typedef struct {
    uint32_t checks;
    uint16_t samples[ADC_LOG_CAPACITY];
    uint32_t sample_count;
    TaskHandle_t requester;
} tx_report_t;

static tx_report_t s_report;

// ── Phase 3: separate FreeRTOS task that "sends" the collected data ─────
static void tx_task(void *pvParameters) {
    tx_report_t *report = static_cast<tx_report_t *>(pvParameters);

    printf("Встановлення з'єднання...\n");

#if ENABLE_STM32_UART
    const uint8_t ping[] = "PING\n";
    uart_write_bytes(UART_PORT, (const char *)ping, sizeof(ping) - 1);
#endif

    // Exactly 4 seconds, and the CPU is genuinely idle for all of it:
    // vTaskDelay() blocks this task and lets the FreeRTOS scheduler run
    // the idle task (tickless idle), it never spins on an empty loop.
    vTaskDelay(pdMS_TO_TICKS(TX_WAIT_MS));

#if ENABLE_STM32_UART
    // The UART driver receives bytes into its own ring buffer via
    // interrupt regardless of what this task was doing, so checking now
    // does not shorten the 4 s wait above.
    size_t available = 0;
    uart_get_buffered_data_len(UART_PORT, &available);
    if (available > 0) {
        uint8_t rx_buf[32];
        int len = uart_read_bytes(UART_PORT, rx_buf, sizeof(rx_buf) - 1,
                                   0 /* don't block, data is already there */);
        if (len > 0) {
            rx_buf[len] = '\0';
            ESP_LOGI(TAG, "STM32 підтвердив прослуховування: %s", (char *)rx_buf);
        }
    } else {
        ESP_LOGW(TAG, "STM32 не відповів за 4с, відправляємо дані все одно");
    }
#endif

    printf("Кількість пройдених планових перевірок: %" PRIu32 "\n", report->checks);
    printf("Дані АЦП (%" PRIu32 " відліків):", report->sample_count);
    for (uint32_t i = 0; i < report->sample_count; i++) {
        printf(" %u", report->samples[i]);
    }
    printf("\n");

    printf("Дані успішно відправлені!\n");

    // Wake app_main() up (it is blocked on ulTaskNotifyTake, not polling)
    // and clean up this task.
    xTaskNotifyGive(report->requester);
    vTaskDelete(NULL);
}

// ── Phase 1: one routine check — read the ADC, log it, bump the counter ─
static void do_routine_check(adc_oneshot_unit_handle_t adc) {
    int raw = 0;
    if (adc_oneshot_read(adc, ADC_CHANNEL, &raw) == ESP_OK) {
        if (adc_log_count < ADC_LOG_CAPACITY) {
            adc_log[adc_log_count++] = (uint16_t)raw;
        } else {
            ESP_LOGW(TAG, "Лог АЦП переповнений, найстаріший відлік втрачено");
        }
    }
    routine_checks++;
    ESP_LOGI(TAG, "Планова перевірка №%" PRIu32 ", АЦП=%d", routine_checks, raw);
}

// ── Phase 2: tamper alarm — report the counter, reset it, hand off to Phase 3
static void handle_tamper_event(void) {
    ESP_LOGW(TAG, "Увага, тригер! До цього моменту система пройшла %" PRIu32
                  " планових перевірок", routine_checks);

    s_report.checks = routine_checks;
    s_report.sample_count = adc_log_count;
    for (uint32_t i = 0; i < adc_log_count; i++) {
        s_report.samples[i] = adc_log[i];
    }
    s_report.requester = xTaskGetCurrentTaskHandle();

    routine_checks = 0;
    adc_log_count = 0;

    // Re-init the trigger pin as a plain digital input (deep sleep resets
    // regular GPIO config) and wait out the release + debounce so the
    // still-held button can't immediately re-trigger the next sleep.
    gpio_reset_pin(TRIGGER_PIN);
    gpio_set_direction(TRIGGER_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(TRIGGER_PIN, GPIO_PULLUP_ONLY);
    while (gpio_get_level(TRIGGER_PIN) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    xTaskCreate(tx_task, "TxTask", 4096, &s_report, 5, NULL);

    // Block (no busy loop) until tx_task finishes sending, then this
    // function returns and app_main() goes back to deep sleep.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

static adc_oneshot_unit_handle_t adc_init(void) {
    adc_oneshot_unit_handle_t adc = NULL;
    const adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, ADC_CHANNEL, &chan_cfg));
    return adc;
}

#if ENABLE_STM32_UART
static void uart_init(void) {
    const uart_config_t cfg = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 256, 256, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}
#endif

extern "C" void app_main(void) {
    adc_oneshot_unit_handle_t adc = adc_init();
#if ENABLE_STM32_UART
    uart_init();
#endif

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER:
            do_routine_check(adc);
            break;

        case ESP_SLEEP_WAKEUP_EXT1:
            handle_tamper_event();
            break;

        default:
            ESP_LOGI(TAG, "Холодний старт. Ініціалізація лісового логера.");
            routine_checks = 0;
            adc_log_count = 0;
            break;
    }

    // Arm both wake sources for the next sleep cycle.
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_TIME_SEC * 1000000ULL));
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup(1ULL << TRIGGER_PIN, ESP_EXT1_WAKEUP_ANY_LOW));
    rtc_gpio_pullup_en(TRIGGER_PIN);
    rtc_gpio_pulldown_dis(TRIGGER_PIN);

    ESP_LOGI(TAG, "Засинаємо на %d с...", SLEEP_TIME_SEC);
    fflush(stdout);
    esp_deep_sleep_start();
}
