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