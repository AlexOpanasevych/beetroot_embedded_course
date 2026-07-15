#include <stdio.h>
#include <stddef.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"

static const char *TAG = "BUZZER";

// ── Buzzer PWM (LEDC) config ────────────────────────────────────────────────
#define BUZZER_GPIO      GPIO_NUM_41
#define BUZZER_TIMER     LEDC_TIMER_0
#define BUZZER_CHANNEL   LEDC_CHANNEL_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define LEDC_MAX_DUTY    ((1 << 10) - 1)   // 1023
#define BUZZER_DUTY_ON   (LEDC_MAX_DUTY / 2)  // 50% square wave

// ── Player tick ──────────────────────────────────────────────────────────────
#define TICK_MS  50

// ── Note frequencies (4th/5th octave, equal temperament) ────────────────────
#define NOTE_REST  0
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587

typedef struct {
    uint16_t freq_hz;
    uint8_t  ticks;
} note_step_t;

// "Древо" / "Смарагдове небо" — top (melody) voice of the piano score's
// chorus, bars 15-18 (the "ff" hook over Em-Am-C-D), transcribed from the
// provided sheet music. The score repeats this 4-bar phrase, so the tick
// engine's index wrap loops it the same way.
static const note_step_t s_melody[] = {
    { NOTE_B4, 6 },
    { NOTE_B4, 3 }, { NOTE_A4, 3 }, { NOTE_B4, 3 }, { NOTE_E4, 3 },
    { NOTE_E4, 3 }, { NOTE_B4, 3 }, { NOTE_B4, 3 },
    { NOTE_B4, 3 }, { NOTE_A4, 3 }, { NOTE_B4, 3 }, { NOTE_D5, 3 },
    { NOTE_C5, 3 }, { NOTE_C5, 3 }, { NOTE_B4, 3 }, { NOTE_C5, 3 },
    { NOTE_C5, 3 }, { NOTE_C5, 3 }, { NOTE_C5, 3 }, { NOTE_C5, 3 },
    { NOTE_C5, 3 }, { NOTE_B4, 3 }, { NOTE_C5, 3 }, { NOTE_C5, 3 },
    { NOTE_B4, 3 },
};
#define MELODY_LEN  (sizeof(s_melody) / sizeof(s_melody[0]))

static esp_timer_handle_t s_tick_timer;
static size_t             s_note_idx    = 0;
static uint32_t           s_ticks_left  = 0;

// ── Drive the buzzer with one note (0 Hz = silence) ─────────────────────────
static void play_note(const note_step_t *note)
{
    if (note->freq_hz == NOTE_REST) {
        ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, 0);
    } else {
        ledc_set_freq(LEDC_MODE, BUZZER_TIMER, note->freq_hz);
        ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, BUZZER_DUTY_ON);
    }
    ledc_update_duty(LEDC_MODE, BUZZER_CHANNEL);
    ESP_LOGI(TAG, "note %zu: %u Hz, %u ticks", s_note_idx, note->freq_hz, note->ticks);
}

// ── 50 ms tick callback — advances the melody without blocking anything ────
static void tick_cb(void *arg)
{
    if (s_ticks_left == 0) {
        play_note(&s_melody[s_note_idx]);
        s_ticks_left = s_melody[s_note_idx].ticks;
        s_note_idx   = (s_note_idx + 1) % MELODY_LEN;
    }
    s_ticks_left--;
}

// ── LEDC init ─────────────────────────────────────────────────────────────
static void buzzer_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = BUZZER_TIMER,
        .freq_hz         = NOTE_C4,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    const ledc_channel_config_t channel_cfg = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = BUZZER_CHANNEL,
        .timer_sel  = BUZZER_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
}

// ── Entry point ───────────────────────────────────────────────────────────────
extern "C" void app_main(void)
{
    buzzer_init();

    const esp_timer_create_args_t timer_args = {
        .callback = &tick_cb,
        .name     = "buzzer_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_tick_timer, TICK_MS * 1000));

    ESP_LOGI(TAG, "Player started: %u ms tick, %zu-note melody, GPIO%d",
             TICK_MS, MELODY_LEN, BUZZER_GPIO);

    // Setup is done — playback runs entirely from the periodic esp_timer
    // callback above, so app_main returns without a task loop or delay.
}
