#pragma once

#include <Arduino.h>
#include <unordered_map>

class PinService {
public:
    void setInput(uint8_t pin);
    void setInputPullup(uint8_t pin);
    void setInputPullDown(uint8_t pin);
    void setOutput(uint8_t pin);
    void setHigh(uint8_t pin);
    void setLow(uint8_t pin);
    bool read(uint8_t pin);
    void togglePullup(uint8_t pin);
    int readAnalog(uint8_t pin);
    bool setupPwm(uint8_t pin, uint32_t freq, uint8_t dutyPercent);
    void setServoAngle(uint8_t pin, uint8_t angle);
private:
    bool isPwmFeasible(uint32_t freq, uint8_t resolutionBits);
    std::unordered_map<uint8_t, bool> pullupState; // true = INPUT_PULLUP, false = INPUT
};
