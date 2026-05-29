#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

constexpr gpio_num_t LED1_PIN = GPIO_NUM_4;
constexpr gpio_num_t LED2_PIN = GPIO_NUM_5;
constexpr gpio_num_t LED3_PIN = GPIO_NUM_6;

static uint64_t millis() {
    return esp_timer_get_time() / 1000ULL;
}

template<gpio_num_t N>
class Pin {
public:
    Pin() {
        gpio_reset_pin(N);
        gpio_set_direction(N, GPIO_MODE_OUTPUT);
        set_level(0);
    }

    void set_level(int level) {
        gpio_set_level(N, level);
    }

    int get_level() 
    {
        return gpio_get_level(N);
    }

    gpio_num_t get_num() const 
    {
        return N;
    }

private:
};

template<typename PinType>
class Led {
public:
    Led(uint32_t interval) : interval(interval), prevTime(0), state(0) {}

    void update() {
        uint64_t now = millis();
        if (now - prevTime >= interval) {
            prevTime = now;
            state ^= 1;
            pin.set_level(state);
        }
    }

private:
    uint32_t interval;
    uint64_t prevTime;
    int      state;
    PinType  pin;
};

extern "C" void app_main() {
    Led<Pin<LED1_PIN>> led1(200);
    Led<Pin<LED2_PIN>> led2(500);
    Led<Pin<LED3_PIN>> led3(1000);

    auto update_all = [](auto&... leds) { (leds.update(), ...); };

    while (1)
    {
        update_all(led1, led2, led3);
    }
}
