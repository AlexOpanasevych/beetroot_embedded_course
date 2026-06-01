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