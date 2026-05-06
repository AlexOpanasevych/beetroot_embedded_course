#include <Arduino.h>
#include "USB_MIDI.h"
#include <math.h>

#define POT_NOTE_PIN     10
#define POT_LFO_PIN      11
#define ADC_MAX          4095
#define ADC_SAMPLES      4
#define NOTE_MIN         48       // C3
#define NOTE_MAX         72       // C5
#define LFO_RATE_MIN     0.3f     // Hz
#define LFO_RATE_MAX     8.0f     // Hz
#define PITCH_BEND_DEPTH 2500     // semitone depth (out of 8191 max)

static int8_t  g_active_note = -1;
static float   g_lfo_phase   = 0.0f;
static unsigned long g_last_ms = 0;

static int readSmoothed(uint8_t pin, int n) {
    uint32_t sum = 0;
    for (int i = 0; i < n; i++) sum += analogRead(pin);
    return (int)(sum / n);
}

static void readPots(int &rawNote, int &rawLfo) {
    rawNote = readSmoothed(POT_NOTE_PIN, ADC_SAMPLES);
    rawLfo  = readSmoothed(POT_LFO_PIN,  ADC_SAMPLES);
}

static float advanceLFO(float rateHz, unsigned long dtMs) {
    g_lfo_phase += (float)(2.0 * M_PI) * rateHz * (dtMs / 1000.0f);
    if (g_lfo_phase >= (float)(2.0 * M_PI)) g_lfo_phase -= (float)(2.0 * M_PI);
    return sinf(g_lfo_phase);
}

static void sendPitchBend(float sineVal, int depth) {
    int bend = 8192 + (int)(sineVal * depth);
    bend = constrain(bend, 0, 16383);
    uint8_t msg[3] = {
        0xE0,
        (uint8_t)(bend & 0x7F),
        (uint8_t)((bend >> 7) & 0x7F)
    };
    if (tud_midi_mounted()) tud_midi_stream_write(0, msg, 3);
}

static void updateNote(uint8_t note) {
    if ((int8_t)note != g_active_note) {
        if (g_active_note >= 0) sendMidiNoteOff((byte)g_active_note);
        sendMidiNoteOn(note, 100);
        g_active_note = (int8_t)note;
    }
}

void setup() {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    usbMidiSetup();
    g_last_ms = millis();
}

void loop() {
    unsigned long now = millis();
    unsigned long dt  = now - g_last_ms;
    g_last_ms = now;

    int rawNote, rawLfo;
    readPots(rawNote, rawLfo);

    uint8_t note  = (uint8_t)(NOTE_MIN + (rawNote * (NOTE_MAX - NOTE_MIN)) / ADC_MAX);
    float lfoRate = LFO_RATE_MIN + (rawLfo / (float)ADC_MAX) * (LFO_RATE_MAX - LFO_RATE_MIN);

    float sineVal = advanceLFO(lfoRate, dt);
    updateNote(note);
    sendPitchBend(sineVal, PITCH_BEND_DEPTH);

    delay(20);
}
