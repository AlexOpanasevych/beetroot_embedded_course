#include <Arduino.h>
#include <variant>
#include "StateMachine.h"

// ═══════════════════════════════════════════════════════════════════════════════
// Active scenario — comment one line to switch:
//   (nothing)          → Scenario A: polling
//   #define SCENARIO_B → Scenario B: interrupt-driven
// ═══════════════════════════════════════════════════════════════════════════════
#define SCENARIO_B

// ── Shared configuration ───────────────────────────────────────────────────────
struct LedConfig {
    static constexpr int      LED_PIN        = 4;
    static constexpr int      BUTTON_PIN     = 46;

    // Scenario A
    static constexpr uint32_t SLOW_INTERVAL  = 500;
    static constexpr uint32_t FAST_INTERVAL  = 100;
    static constexpr uint32_t COUNT_INTERVAL = 200;
    static constexpr uint8_t  BLINK_COUNT    = 3;

    // Scenario B
    static constexpr uint32_t BLINK_INTERVAL = 500;
    static constexpr uint32_t DEBOUNCE_MS    = 50;

    // Shared
    static constexpr uint32_t SERIAL_BAUD    = 115200;
    static constexpr uint32_t PROFILER_EVERY = 1000;
};

// ── Shared: context & profiler ────────────────────────────────────────────────
struct LedContext {
    bool     btnPressed = false;
    uint32_t now        = 0;
};

class LoopProfiler {
public:
    void tick() {
        const uint32_t now = micros();
        if (_count > 0) {
            const uint32_t elapsed = now - _prev;
            if (elapsed < _minUs) _minUs = elapsed;
            if (elapsed > _maxUs) _maxUs = elapsed;
            _sumUs += elapsed;
            if (_count % LedConfig::PROFILER_EVERY == 0) {
                Serial.printf("[profiler] min=%luus  max=%luus  avg=%luus\n",
                    (unsigned long)_minUs,
                    (unsigned long)_maxUs,
                    (unsigned long)(_sumUs / LedConfig::PROFILER_EVERY));
                _minUs = UINT32_MAX;
                _maxUs = 0;
                _sumUs = 0;
            }
        }
        _prev = now;
        ++_count;
    }
private:
    uint32_t _prev  = 0;
    uint32_t _count = 0;
    uint32_t _minUs = UINT32_MAX;
    uint32_t _maxUs = 0;
    uint64_t _sumUs = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// SCENARIO A — Polling
// Button state is read with digitalRead() on every loop() tick.
// Cycle: Idle → CountedBlink (×3) → SlowBlink → FastBlink → Idle
// ═══════════════════════════════════════════════════════════════════════════════
#ifndef SCENARIO_B

struct IdleState;
struct CountedBlinkState;
struct SlowBlinkState;
struct FastBlinkState;
struct LedNext;

struct IdleState {
    void    onEnter(LedContext&) { digitalWrite(LedConfig::LED_PIN, LOW); }
    void    onExit (LedContext&) {}
    LedNext update (LedContext& ctx);
};

struct CountedBlinkState {
    uint32_t _lastToggle = 0;
    uint8_t  _toggles    = 0;
    bool     _ledOn      = false;

    void    onEnter(LedContext& ctx) { _lastToggle = ctx.now; _toggles = 0; _ledOn = false; }
    void    onExit (LedContext&)     { digitalWrite(LedConfig::LED_PIN, LOW); }
    LedNext update (LedContext& ctx);
};

struct SlowBlinkState {
    uint32_t _lastToggle = 0;
    bool     _ledOn      = false;

    void    onEnter(LedContext& ctx) { _lastToggle = ctx.now; _ledOn = false; }
    void    onExit (LedContext&)     { digitalWrite(LedConfig::LED_PIN, LOW); }
    LedNext update (LedContext& ctx);
};

struct FastBlinkState {
    uint32_t _lastToggle = 0;
    bool     _ledOn      = false;

    void    onEnter(LedContext& ctx) { _lastToggle = ctx.now; _ledOn = false; }
    void    onExit (LedContext&)     { digitalWrite(LedConfig::LED_PIN, LOW); }
    LedNext update (LedContext& ctx);
};

struct LedNext {
    using Var = std::variant<IdleState, CountedBlinkState, SlowBlinkState, FastBlinkState>;
    template <typename T> LedNext(T s) : state(std::move(s)) {}
    operator Var() &&             { return std::move(state); }
    operator const Var&() const & { return state; }
    Var state;
};

LedNext IdleState::update(LedContext& ctx) {
    if (ctx.btnPressed) return CountedBlinkState{};
    return *this;
}
LedNext CountedBlinkState::update(LedContext& ctx) {
    if (ctx.now - _lastToggle >= LedConfig::COUNT_INTERVAL) {
        _ledOn = !_ledOn;
        digitalWrite(LedConfig::LED_PIN, _ledOn);
        _lastToggle = ctx.now;
        ++_toggles;
    }
    if (_toggles >= LedConfig::BLINK_COUNT * 2) return SlowBlinkState{};
    return *this;
}
LedNext SlowBlinkState::update(LedContext& ctx) {
    if (ctx.btnPressed) return FastBlinkState{};
    if (ctx.now - _lastToggle >= LedConfig::SLOW_INTERVAL) {
        _ledOn = !_ledOn;
        digitalWrite(LedConfig::LED_PIN, _ledOn);
        _lastToggle = ctx.now;
    }
    return *this;
}
LedNext FastBlinkState::update(LedContext& ctx) {
    if (ctx.btnPressed) return IdleState{};
    if (ctx.now - _lastToggle >= LedConfig::FAST_INTERVAL) {
        _ledOn = !_ledOn;
        digitalWrite(LedConfig::LED_PIN, _ledOn);
        _lastToggle = ctx.now;
    }
    return *this;
}

LedContext   ledCtx;
LoopProfiler profiler;
StateMachine<LedContext, IdleState, CountedBlinkState, SlowBlinkState, FastBlinkState>
    sm(ledCtx, IdleState{});

static bool lastBtn = false;

void setup() {
    Serial.begin(LedConfig::SERIAL_BAUD);
    pinMode(LedConfig::LED_PIN,    OUTPUT);
    pinMode(LedConfig::BUTTON_PIN, INPUT_PULLUP);
    Serial.println("[A] Polling. Idle → CountedBlink → SlowBlink → FastBlink");
}

void loop() {
    profiler.tick();
    ledCtx.now        = millis();
    const bool btn    = !digitalRead(LedConfig::BUTTON_PIN);
    ledCtx.btnPressed = btn && !lastBtn;
    lastBtn           = btn;
    sm.update();
}

#endif // !SCENARIO_B

// ═══════════════════════════════════════════════════════════════════════════════
// SCENARIO B — Interrupt-driven
// ISR sets a volatile flag; loop() consumes it with software debounce.
// Cycle: Blink → AlwaysOn → AlwaysOff → Blink
// ═══════════════════════════════════════════════════════════════════════════════
#ifdef SCENARIO_B

// Written only from ISR; read and cleared in loop().
volatile bool g_btnFlag = false;

// IRAM_ATTR — ESP32 requires ISR bodies in IRAM (not flash).
// Rule: set the flag and return immediately; no Serial, no delay, no logic.
void IRAM_ATTR onButtonISR() {
    g_btnFlag = true;
}

struct BlinkState;
struct AlwaysOnState;
struct AlwaysOffState;
struct LedNext;

// Blinks at BLINK_INTERVAL; button → AlwaysOn
struct BlinkState {
    uint32_t _lastToggle = 0;
    bool     _ledOn      = false;

    void    onEnter(LedContext& ctx) { _lastToggle = ctx.now; _ledOn = false; }
    void    onExit (LedContext&)     { digitalWrite(LedConfig::LED_PIN, LOW); }
    LedNext update (LedContext& ctx);
};

// LED constantly on; button → AlwaysOff
struct AlwaysOnState {
    void    onEnter(LedContext&) { digitalWrite(LedConfig::LED_PIN, HIGH); }
    void    onExit (LedContext&) {}
    LedNext update (LedContext& ctx);
};

// LED constantly off; button → Blink
struct AlwaysOffState {
    void    onEnter(LedContext&) { digitalWrite(LedConfig::LED_PIN, LOW); }
    void    onExit (LedContext&) {}
    LedNext update (LedContext& ctx);
};

struct LedNext {
    using Var = std::variant<BlinkState, AlwaysOnState, AlwaysOffState>;
    template <typename T> LedNext(T s) : state(std::move(s)) {}
    operator Var() &&             { return std::move(state); }
    operator const Var&() const & { return state; }
    Var state;
};

LedNext BlinkState::update(LedContext& ctx) {
    if (ctx.btnPressed) return AlwaysOnState{};
    if (ctx.now - _lastToggle >= LedConfig::BLINK_INTERVAL) {
        _ledOn = !_ledOn;
        digitalWrite(LedConfig::LED_PIN, _ledOn);
        _lastToggle = ctx.now;
    }
    return *this;
}
LedNext AlwaysOnState::update(LedContext& ctx) {
    if (ctx.btnPressed) return AlwaysOffState{};
    return *this;
}
LedNext AlwaysOffState::update(LedContext& ctx) {
    if (ctx.btnPressed) return BlinkState{};
    return *this;
}

LedContext   ledCtx;
LoopProfiler profiler;
StateMachine<LedContext, BlinkState, AlwaysOnState, AlwaysOffState>
    sm(ledCtx, BlinkState{});

static uint32_t lastDebounce = 0;

void setup() {
    Serial.begin(LedConfig::SERIAL_BAUD);
    pinMode(LedConfig::LED_PIN,    OUTPUT);
    pinMode(LedConfig::BUTTON_PIN, INPUT_PULLUP);
    // FALLING edge: active-low button (INPUT_PULLUP → LOW when pressed)
    attachInterrupt(digitalPinToInterrupt(LedConfig::BUTTON_PIN), onButtonISR, FALLING);
    Serial.println("[B] Interrupt. Blink → AlwaysOn → AlwaysOff → Blink");
}

void loop() {
    profiler.tick();
    ledCtx.now = millis();

    // Consume the ISR flag here — never inside the ISR.
    // Debounce: ignore re-triggers within DEBOUNCE_MS of the last accepted press.
    ledCtx.btnPressed = false;
    if (g_btnFlag) {
        g_btnFlag = false;
        if (ledCtx.now - lastDebounce >= LedConfig::DEBOUNCE_MS) {
            ledCtx.btnPressed = true;
            lastDebounce      = ledCtx.now;
        }
    }

    sm.update();
}

#endif // SCENARIO_B
