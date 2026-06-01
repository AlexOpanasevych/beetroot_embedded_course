#include "driver/gpio.h"

template<gpio_num_t N>
class Pin {
public:
    Pin() {
        gpio_reset_pin(N);
        gpio_set_direction(N, GPIO_MODE_OUTPUT);
        set_level(0);
    }

    void set_level(int level) {
        _level = level;
        gpio_set_level(N, _level);
    }

    int get_level() const
    {
        return _level;
    }

    gpio_num_t get_num() const
    {
        return N;
    }

    void toggle() {
        set_level(!_level);
    }

private:
    int _level = 0;
};