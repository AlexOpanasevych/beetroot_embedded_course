#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "pin.h"
#include "StateMachine.h"

Pin<GPIO_NUM_4> red;
Pin<GPIO_NUM_6> green;
Pin<GPIO_NUM_5> yellow;

template<typename TRed, typename TGreen, typename TYellow>
struct Output
{
    TRed& red;
    TGreen& green;
    TYellow& yellow;
    Output(TRed& r, TGreen& g, TYellow& y) : red(r), green(g), yellow(y) {}
};

using TrafficOutput = Output<Pin<GPIO_NUM_4>, Pin<GPIO_NUM_6>, Pin<GPIO_NUM_5>>;

struct RunContext
{
    TrafficOutput& output;
    int64_t entry_time = 0;
};

// Forward-declare so states can use it as a return type before it is fully defined
struct TrafficLightStates;

struct YellowBlinkingState {
    static constexpr uint16_t timeout = 5000; // ms
    void onEnter(RunContext& context);
    void onExit(RunContext& context);
    TrafficLightStates update(RunContext& context);
};

struct AllRedState {
    static constexpr uint16_t timeout = 3000; // ms
    void onEnter(RunContext& context);
    void onExit(RunContext& context);
    TrafficLightStates update(RunContext& context);
};

struct GreenState {
    static constexpr uint16_t timeout = 8000; // ms
    void onEnter(RunContext& context);
    void onExit(RunContext& context);
    TrafficLightStates update(RunContext& context);
};

struct GreenBlinkingState {
    static constexpr uint16_t timeout = 3000; // ms
    void onEnter(RunContext& context);
    void onExit(RunContext& context);
    TrafficLightStates update(RunContext& context);
};

struct YellowState {
    static constexpr uint16_t timeout = 3000; // ms
    void onEnter(RunContext& context);
    void onExit(RunContext& context);
    TrafficLightStates update(RunContext& context);
};

struct RedYellowState {
    static constexpr uint16_t timeout = 3000; // ms
    void onEnter(RunContext& context);
    void onExit(RunContext& context);
    TrafficLightStates update(RunContext& context);
};

struct RedState {
    static constexpr uint16_t timeout = 8000; // ms
    void onEnter(RunContext& context);
    void onExit(RunContext& context);
    TrafficLightStates update(RunContext& context);
};

// All state types are now complete — safe to instantiate the variant.
// Order must exactly match the StateMachine template arguments so the
// derived-to-base conversion in the visit lambda compiles.
struct TrafficLightStates
    : std::variant<YellowBlinkingState, RedState, GreenState, GreenBlinkingState, YellowState, RedYellowState, AllRedState>
{
    using std::variant<YellowBlinkingState, RedState, GreenState, GreenBlinkingState, YellowState, RedYellowState, AllRedState>::variant;
};

// Method bodies — TrafficLightStates is complete here

void YellowBlinkingState::onEnter(RunContext& context)
{
    context.entry_time = esp_timer_get_time();
    context.output.yellow.set_level(1);
}
void YellowBlinkingState::onExit(RunContext& context)
{
    context.output.yellow.set_level(0);
}
TrafficLightStates YellowBlinkingState::update(RunContext& context)
{
    if ((esp_timer_get_time() - context.entry_time) >= (int64_t(timeout) * 1000))
        return AllRedState{};
    context.output.yellow.toggle();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return *this;
}

void AllRedState::onEnter(RunContext& context)
{
    context.entry_time = esp_timer_get_time();
    context.output.red.set_level(1);
}
void AllRedState::onExit(RunContext& context)
{
    context.output.red.set_level(0);
}
TrafficLightStates AllRedState::update(RunContext& context)
{
    if ((esp_timer_get_time() - context.entry_time) >= (int64_t(timeout) * 1000))
        return GreenState{};
    return *this;
}

void RedState::onEnter(RunContext& context)
{
    context.entry_time = esp_timer_get_time();
    context.output.red.set_level(1);
}
void RedState::onExit(RunContext& context)
{
}
TrafficLightStates RedState::update(RunContext& context)
{
    if ((esp_timer_get_time() - context.entry_time) >= (int64_t(timeout) * 1000))
        return RedYellowState{};
    return *this;
}

void GreenState::onEnter(RunContext& context)
{
    context.entry_time = esp_timer_get_time();
    context.output.green.set_level(1);
}
void GreenState::onExit(RunContext& context)
{
    context.output.green.set_level(0);
}
TrafficLightStates GreenState::update(RunContext& context)
{
    if ((esp_timer_get_time() - context.entry_time) >= (int64_t(timeout) * 1000))
        return GreenBlinkingState{};
    return *this;
}

void GreenBlinkingState::onEnter(RunContext& context)
{
    context.entry_time = esp_timer_get_time();
    context.output.green.set_level(1);
}
void GreenBlinkingState::onExit(RunContext& context)
{
    context.output.green.set_level(0);
}
TrafficLightStates GreenBlinkingState::update(RunContext& context)
{
    if ((esp_timer_get_time() - context.entry_time) >= (int64_t(timeout) * 1000))
        return YellowState{};
    context.output.green.toggle();
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    return *this;
}

void YellowState::onEnter(RunContext& context)
{
    context.entry_time = esp_timer_get_time();
    context.output.yellow.set_level(1);
}

void YellowState::onExit(RunContext& context)
{
    context.output.yellow.set_level(0);
}

TrafficLightStates YellowState::update(RunContext& context)
{
    if ((esp_timer_get_time() - context.entry_time) >= (int64_t(timeout) * 1000))
        return RedState{};
    return *this;
}

void RedYellowState::onEnter(RunContext& context)
{
    context.entry_time = esp_timer_get_time();
    context.output.red.set_level(1);
    context.output.yellow.set_level(1);
}
void RedYellowState::onExit(RunContext& context)
{
    context.output.red.set_level(0);
    context.output.yellow.set_level(0);
}
TrafficLightStates RedYellowState::update(RunContext& context)
{
    if ((esp_timer_get_time() - context.entry_time) >= (int64_t(timeout) * 1000))
        return GreenState{};
    return *this;
}

extern "C" void app_main()
{
    Output outputs(red, green, yellow);
    RunContext context{outputs};

    StateMachine<RunContext, YellowBlinkingState, RedState, GreenState, GreenBlinkingState, YellowState, RedYellowState, AllRedState> sm(context, YellowBlinkingState{});

    while (1)
    {
        sm.update();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
