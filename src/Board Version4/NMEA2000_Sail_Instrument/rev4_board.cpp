#include "rev4_board.h"

#include <Wire.h>
#include "freertos/semphr.h"

namespace Rev4Board {
namespace {

static constexpr gpio_num_t kSdaPin = GPIO_NUM_15;
static constexpr gpio_num_t kSclPin = GPIO_NUM_7;
static constexpr uint8_t kI2cAddress = 0x24;

static constexpr uint8_t kOutputReg = 0x02;
static constexpr uint8_t kDirectionReg = 0x03;
static constexpr uint8_t kBacklightReg = 0x05;

static constexpr uint8_t kOutputSafe = 0xBF;       // all HIGH except buzzer bit LOW
static constexpr uint8_t kDirectionSafe = 0x3A;    // TP_RST, LCD_RST, SDCS, SYS_EN as outputs
static constexpr uint8_t kDirectionRuntime = 0x7A; // same + BEE_EN as output after startup
static constexpr uint8_t kBeeperBitMask = 0x40;    // bit 6
static constexpr uint32_t kBeepShortMs = 30;
static constexpr uint8_t kDefaultBacklightPwm = 72; // ~80% brightness per Rev 4 guide

static constexpr uint8_t kBitLcdRst = 3;

static const uint16_t kLcdResetLowMs = 100;
static const uint16_t kLcdPostResetDelayMs = 500;

SemaphoreHandle_t g_expander_mutex = nullptr;
SemaphoreHandle_t g_i2c_mutex = nullptr;
TaskHandle_t g_beeper_task_handle = nullptr;
portMUX_TYPE g_beeper_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool g_beeper_start_req = false;
volatile bool g_beeper_stop_req = false;
volatile uint32_t g_beeper_req_on_ms = 0;
volatile uint32_t g_beeper_req_off_ms = 0;
volatile uint16_t g_beeper_req_repeat = 0;

bool writeRegUnlocked(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kI2cAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegUnlocked(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(kI2cAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(kI2cAddress), 1) != 1) {
    return false;
  }
  value = static_cast<uint8_t>(Wire.read());
  return true;
}

bool writeReg(uint8_t reg, uint8_t value) {
  if (!I2cLock()) {
    return false;
  }
  if ((g_expander_mutex != nullptr) && (xSemaphoreTake(g_expander_mutex, pdMS_TO_TICKS(50)) != pdTRUE)) {
    I2cUnlock();
    return false;
  }

  const bool ok = writeRegUnlocked(reg, value);

  if (g_expander_mutex != nullptr) {
    xSemaphoreGive(g_expander_mutex);
  }
  I2cUnlock();

  return ok;
}

bool updateBeeperBit(bool active) {
  if (!I2cLock()) {
    return false;
  }
  if ((g_expander_mutex != nullptr) && (xSemaphoreTake(g_expander_mutex, pdMS_TO_TICKS(50)) != pdTRUE)) {
    I2cUnlock();
    return false;
  }

  uint8_t current = 0;
  bool ok = readRegUnlocked(kOutputReg, current);
  if (ok) {
    const uint8_t updated = active ? static_cast<uint8_t>(current | kBeeperBitMask)
                                   : static_cast<uint8_t>(current & ~kBeeperBitMask);
    if (updated != current) {
      ok = writeRegUnlocked(kOutputReg, updated);
    }
  }

  if (g_expander_mutex != nullptr) {
    xSemaphoreGive(g_expander_mutex);
  }
  I2cUnlock();

  return ok;
}

bool beeperInit() {
  bool ok = writeReg(kOutputReg, kOutputSafe);
  ok &= writeReg(kDirectionReg, kDirectionRuntime);
  ok &= writeReg(kOutputReg, kOutputSafe);
  ok &= updateBeeperBit(false);
  return ok;
}

bool beeperPulseOnce(uint32_t on_ms) {
  if (!updateBeeperBit(true)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(on_ms));
  return updateBeeperBit(false);
}

void beeperTask(void *pvParameters) {
  (void)pvParameters;
  vTaskDelay(pdMS_TO_TICKS(250));
  beeperInit();

  uint32_t cur_on_ms = 0;
  uint32_t cur_off_ms = 0;
  uint16_t cur_repeat = 0;
  uint16_t remaining = 0;
  bool active = false;

  for (;;) {
    bool stop_req = false;
    bool start_req = false;
    uint32_t req_on_ms = 0;
    uint32_t req_off_ms = 0;
    uint16_t req_repeat = 0;

    portENTER_CRITICAL(&g_beeper_mux);
    stop_req = g_beeper_stop_req;
    start_req = g_beeper_start_req;
    req_on_ms = g_beeper_req_on_ms;
    req_off_ms = g_beeper_req_off_ms;
    req_repeat = g_beeper_req_repeat;
    if (stop_req) g_beeper_stop_req = false;
    if (start_req) g_beeper_start_req = false;
    portEXIT_CRITICAL(&g_beeper_mux);

    if (stop_req) {
      active = false;
      updateBeeperBit(false);
    }

    if (start_req) {
      if (req_on_ms == 0) {
        active = false;
        updateBeeperBit(false);
      } else if (!(active && cur_on_ms == req_on_ms && cur_off_ms == req_off_ms && cur_repeat == req_repeat)) {
        cur_on_ms = req_on_ms;
        cur_off_ms = req_off_ms;
        cur_repeat = req_repeat;
        remaining = req_repeat;
        active = true;
      }
    }

    if (!active) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    if (!beeperPulseOnce(cur_on_ms)) {
      active = false;
      continue;
    }

    if (cur_repeat != 0) {
      if (remaining > 0) {
        remaining--;
      }
      if (remaining == 0) {
        active = false;
        continue;
      }
    }

    if (cur_off_ms > 0) {
      vTaskDelay(pdMS_TO_TICKS(cur_off_ms));
    }
  }
}

} // namespace

bool StartupPrepare() {
  Wire.begin(kSdaPin, kSclPin, 100000U);

  bool ok = true;
  ok &= writeRegUnlocked(kOutputReg, kOutputSafe);
  ok &= writeRegUnlocked(kDirectionReg, kDirectionSafe);
  vTaskDelay(pdMS_TO_TICKS(20));

  const uint8_t out_with_reset_low = static_cast<uint8_t>(kOutputSafe & ~(1u << kBitLcdRst));
  ok &= writeRegUnlocked(kOutputReg, out_with_reset_low);
  vTaskDelay(pdMS_TO_TICKS(kLcdResetLowMs));
  ok &= writeRegUnlocked(kOutputReg, kOutputSafe);

  vTaskDelay(pdMS_TO_TICKS(kLcdPostResetDelayMs));

  ok &= writeRegUnlocked(kBacklightReg, kDefaultBacklightPwm);
  ok &= writeRegUnlocked(kOutputReg, kOutputSafe);
  ok &= writeRegUnlocked(kDirectionReg, kDirectionSafe);

  return ok;
}

bool Begin() {
  if (g_expander_mutex == nullptr) {
    g_expander_mutex = xSemaphoreCreateMutex();
  }
  if (g_i2c_mutex == nullptr) {
    g_i2c_mutex = xSemaphoreCreateMutex();
  }
  return (g_expander_mutex != nullptr) && (g_i2c_mutex != nullptr);
}



bool TouchResetPulse() {
  const uint8_t kBitTpRst = 1;
  const uint8_t out_reset_low = static_cast<uint8_t>(kOutputSafe & ~(1u << kBitTpRst));
  if (!writeReg(kOutputReg, out_reset_low)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(10));
  if (!writeReg(kOutputReg, kOutputSafe)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(50));
  return true;
}

bool StartBeeperTask(BaseType_t core_id, UBaseType_t priority, uint32_t stack_words) {
  if (g_beeper_task_handle != nullptr) {
    return true;
  }
  return xTaskCreatePinnedToCore(beeperTask, "v4Beep", stack_words, nullptr, priority, &g_beeper_task_handle, core_id) == pdPASS;
}

void BeeperStart(uint32_t on_ms, uint32_t off_ms, uint16_t repeat) {
  portENTER_CRITICAL(&g_beeper_mux);
  g_beeper_req_on_ms = on_ms;
  g_beeper_req_off_ms = off_ms;
  g_beeper_req_repeat = repeat;
  g_beeper_start_req = true;
  portEXIT_CRITICAL(&g_beeper_mux);
}

void BeeperStop() {
  portENTER_CRITICAL(&g_beeper_mux);
  g_beeper_stop_req = true;
  portEXIT_CRITICAL(&g_beeper_mux);
}

void BeepShort() {
  BeeperStart(kBeepShortMs, 0, 1);
}

bool SetBacklightPwm(uint8_t pwm) {
  if (pwm > 247) {
    pwm = 247;
  }
  return writeReg(kBacklightReg, pwm);
}

uint8_t SliderValueToBacklightPwm(int16_t slider_value) {
  uint8_t pwm = static_cast<uint8_t>(255 - static_cast<uint8_t>(slider_value));
  if (pwm > 247) {
    pwm = 247;
  }
  return pwm;
}

bool I2cLock(TickType_t timeout_ticks) {
  if (g_i2c_mutex == nullptr) return true;
  return xSemaphoreTake(g_i2c_mutex, timeout_ticks) == pdTRUE;
}

void I2cUnlock() {
  if (g_i2c_mutex != nullptr) {
    xSemaphoreGive(g_i2c_mutex);
  }
}

} // namespace Rev4Board
