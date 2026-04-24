#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace Rev4Board {

bool StartupPrepare();
bool Begin();
bool StartBeeperTask(BaseType_t core_id = 1, UBaseType_t priority = 1, uint32_t stack_words = 2048);

void BeeperStart(uint32_t on_ms, uint32_t off_ms, uint16_t repeat);
void BeeperStop();
void BeepShort();

bool SetBacklightPwm(uint8_t pwm);
uint8_t SliderValueToBacklightPwm(int16_t slider_value);

bool I2cLock(TickType_t timeout_ticks = pdMS_TO_TICKS(50));
void I2cUnlock();

bool TouchResetPulse();

} // namespace Rev4Board
