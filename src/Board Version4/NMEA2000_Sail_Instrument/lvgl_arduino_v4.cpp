#include "lvgl_arduino_v4.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "gt911_rev4.h"
#include "rev4_board.h"

namespace {
constexpr uint32_t kScreenWidth = 480;
constexpr uint32_t kScreenHeight = 480;
constexpr uint32_t kTickPeriodMs = 2;
constexpr uint32_t kTaskMaxDelayMs = 3;
constexpr uint32_t kTaskMinDelayMs = 3;
constexpr uint32_t kTaskStackSize = 10 * 1024;
constexpr UBaseType_t kTaskPriority = 10;
constexpr bool kTouchSwapXY = false;
constexpr bool kTouchMirrorX = false;
constexpr bool kTouchMirrorY = false;
constexpr BaseType_t kTaskCore = 0;
constexpr gpio_num_t kI2cSda = GPIO_NUM_15;
constexpr gpio_num_t kI2cScl = GPIO_NUM_7;
constexpr uint8_t kCh32Addr = 0x24;
constexpr uint8_t kCh32RegOutput = 0x02;
constexpr uint8_t kCh32RegDirection = 0x03;
constexpr uint8_t kCh32OutputSafe = 0xBF;
constexpr uint8_t kCh32DirectionFactory = 0x3A;
constexpr uint8_t kCh32DirectionRuntime = 0x7A;
constexpr uint32_t kGt911BootDelayMs = 100;
constexpr size_t kBufferPixels = kScreenWidth * kScreenHeight;

// Tuneable RGB timing parameters.
// These are the values to experiment with when tuning panel stability / artifacts.
uint32_t g_rgb_pclk_hz = 12'000'000;
bool g_rgb_hsync_polarity = true;
uint16_t g_rgb_hsync_front_porch = 5;
uint16_t g_rgb_hsync_pulse_width = 8;
uint16_t g_rgb_hsync_back_porch = 50;
bool g_rgb_vsync_polarity = true;
uint16_t g_rgb_vsync_front_porch = 10;
uint16_t g_rgb_vsync_pulse_width = 8;
uint16_t g_rgb_vsync_back_porch = 30;
bool g_rgb_pclk_active_neg = false;

SemaphoreHandle_t g_lvgl_mux = nullptr;
TaskHandle_t g_lvgl_task_handle = nullptr;
GT911Rev4 g_touch;
lv_disp_draw_buf_t g_draw_buf;
lv_disp_drv_t g_disp_drv;
lv_indev_drv_t g_indev_drv;
lv_disp_t *g_disp = nullptr;
lv_indev_t *g_indev = nullptr;

Arduino_DataBus *g_bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 42 /* CS */, 2 /* SCK */, 1 /* MOSI */, GFX_NOT_DEFINED /* MISO */);

Arduino_ESP32RGBPanel *g_rgbpanel = nullptr;
Arduino_RGB_Display *g_gfx = nullptr;

bool ch32WriteQuick(uint8_t reg, uint8_t value) {
  if (!Rev4Board::I2cLock()) return false;
  Wire.beginTransmission(kCh32Addr);
  Wire.write(reg);
  Wire.write(value);
  const bool ok = Wire.endTransmission() == 0;
  Rev4Board::I2cUnlock();
  return ok;
}

void lvglTick(void *arg) {
  (void)arg;
#if LV_TICK_CUSTOM == 0
  lv_tick_inc(kTickPeriodMs);
#endif
}

void lvglFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  const uint32_t w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t h = static_cast<uint32_t>(area->y2 - area->y1 + 1);
#if (LV_COLOR_16_SWAP != 0)
  g_gfx->draw16bitBeRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(&color_p->full), w, h);
#else
  g_gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(&color_p->full), w, h);
#endif
  lv_disp_flush_ready(disp);
}

void lvglTouchRead(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  (void)indev_driver;
  int16_t x[5] = {0};
  int16_t y[5] = {0};
  const uint8_t touched = g_touch.readPoints(x, y, 5);
  if (touched > 0) {
    int16_t tx = x[0];
    int16_t ty = y[0];

    if (kTouchSwapXY) {
      const int16_t tmp = tx;
      tx = ty;
      ty = tmp;
    }
    if (kTouchMirrorX) {
      tx = static_cast<int16_t>((LV_HOR_RES - 1) - tx);
    }
    if (kTouchMirrorY) {
      ty = static_cast<int16_t>((LV_VER_RES - 1) - ty);
    }

    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx >= LV_HOR_RES) tx = LV_HOR_RES - 1;
    if (ty >= LV_VER_RES) ty = LV_VER_RES - 1;

    data->state = LV_INDEV_STATE_PR;
    data->point.x = static_cast<lv_coord_t>(tx);
    data->point.y = static_cast<lv_coord_t>(ty);
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void lvglTask(void *arg) {
  (void)arg;
  while (true) {
    uint32_t delay_ms = kTaskMaxDelayMs;
    if (lvgl_port_lock(-1)) {
      delay_ms = lv_timer_handler();
      lvgl_port_unlock();
    }
    if (delay_ms > kTaskMaxDelayMs) delay_ms = kTaskMaxDelayMs;
    if (delay_ms < kTaskMinDelayMs) delay_ms = kTaskMinDelayMs;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

bool initTouchThenDisplay() {
  g_rgbpanel = new Arduino_ESP32RGBPanel(
      40 /* DE */, 39 /* VSYNC */, 38 /* HSYNC */, 41 /* PCLK */,
      46 /* R0 */, 3 /* R1 */, 8 /* R2 */, 18 /* R3 */, 17 /* R4 */,
      14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
      5 /* B0 */, 45 /* B1 */, 48 /* B2 */, 47 /* B3 */, 21 /* B4 */,
      g_rgb_hsync_polarity, g_rgb_hsync_front_porch, g_rgb_hsync_pulse_width, g_rgb_hsync_back_porch,
      g_rgb_vsync_polarity, g_rgb_vsync_front_porch, g_rgb_vsync_pulse_width, g_rgb_vsync_back_porch,
      g_rgb_pclk_active_neg, g_rgb_pclk_hz);
  if (g_rgbpanel == nullptr) return false;

  g_gfx = new Arduino_RGB_Display(
      kScreenWidth, kScreenHeight, g_rgbpanel, 0 /* MUST stay 0 on Rev 4 */, true /* auto_flush */,
      g_bus, GFX_NOT_DEFINED /* RST */, st7701_type1_init_operations, sizeof(st7701_type1_init_operations));
  if (g_gfx == nullptr) return false;

  Serial.println("RGB timing parameters:");
  Serial.printf("  pclk_hz=%lu\n", static_cast<unsigned long>(g_rgb_pclk_hz));
  Serial.printf("  hsync: pol=%u fp=%u pw=%u bp=%u\n", g_rgb_hsync_polarity, g_rgb_hsync_front_porch, g_rgb_hsync_pulse_width, g_rgb_hsync_back_porch);
  Serial.printf("  vsync: pol=%u fp=%u pw=%u bp=%u\n", g_rgb_vsync_polarity, g_rgb_vsync_front_porch, g_rgb_vsync_pulse_width, g_rgb_vsync_back_porch);
  Serial.printf("  pclk_active_neg=%u\n", g_rgb_pclk_active_neg);

  Wire.begin(kI2cSda, kI2cScl, 100000U);
  if (!ch32WriteQuick(kCh32RegOutput, kCh32OutputSafe)) return false;
  if (!ch32WriteQuick(kCh32RegDirection, kCh32DirectionFactory)) return false;
  delay(kGt911BootDelayMs);

  uint8_t gt_addr = 0;
  if (!GT911Rev4::scanForAddress(Wire, gt_addr)) return false;
  if (!g_touch.begin(Wire, gt_addr)) return false;

  char product_id[5] = {0};
  uint8_t status = 0;
  if (g_touch.readProductId(product_id)) {
    Serial.print("GT911 product: ");
    Serial.println(product_id);
  } else {
    Serial.println("GT911 product read failed");
  }
  if (g_touch.readStatus(status)) {
    Serial.print("GT911 status @ init: 0x");
    Serial.println(status, HEX);
  } else {
    Serial.println("GT911 status read failed");
  }

    // Apply the same Rev 4 GT911 configuration step that worked in the standalone test sketch.
  if (!g_touch.applyRev4Config()) {
    Serial.println("GT911 config apply failed");
    return false;
  }

  if (g_touch.readStatus(status)) {
    Serial.print("GT911 status after config: 0x");
    Serial.println(status, HEX);
  }

  if (!ch32WriteQuick(kCh32RegOutput, kCh32OutputSafe)) return false;
  if (!ch32WriteQuick(kCh32RegDirection, kCh32DirectionRuntime)) return false;

  Wire.begin(kI2cSda, kI2cScl, 400000U);
  return g_gfx->begin();
}

} // namespace

bool lvgl_port_init(void) {
  if (!initTouchThenDisplay()) {
    return false;
  }

  lv_init();

  lv_color_t *buf1 = static_cast<lv_color_t *>(
      heap_caps_malloc(kBufferPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  lv_color_t *buf2 = static_cast<lv_color_t *>(
      heap_caps_malloc(kBufferPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if ((buf1 == nullptr) || (buf2 == nullptr)) {
    return false;
  }

  lv_disp_draw_buf_init(&g_draw_buf, buf1, buf2, kBufferPixels);

  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = kScreenWidth;
  g_disp_drv.ver_res = kScreenHeight;
  g_disp_drv.flush_cb = lvglFlush;
  g_disp_drv.draw_buf = &g_draw_buf;
  g_disp_drv.full_refresh = 1;  // 1 = full screen refresh, 0 = partial refresh
  g_disp_drv.sw_rotate = 0;

  g_disp = lv_disp_drv_register(&g_disp_drv);
  if (g_disp == nullptr) {
    return false;
  }

  // Keep LVGL unrotated here to avoid the software rotation cost on this application.

  lv_indev_drv_init(&g_indev_drv);
  g_indev_drv.type = LV_INDEV_TYPE_POINTER;
  g_indev_drv.read_cb = lvglTouchRead;

  // gesture detection parameters
  g_indev_drv.gesture_min_velocity = 1;
  g_indev_drv.gesture_limit = 8;

  g_indev = lv_indev_drv_register(&g_indev_drv);
  if (g_indev == nullptr) {
    return false;
  }

  // Poll the touch driver more often so fast swipes are less likely to be missed
  if (g_indev->driver && g_indev->driver->read_timer) {
    lv_timer_set_period(g_indev->driver->read_timer, 10);
  }

  g_lvgl_mux = xSemaphoreCreateRecursiveMutex();
  if (g_lvgl_mux == nullptr) {
    return false;
  }

  const esp_timer_create_args_t tick_args = {
      .callback = &lvglTick,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "lvgl_tick",
      .skip_unhandled_events = false};

  esp_timer_handle_t tick_timer = nullptr;
  if (esp_timer_create(&tick_args, &tick_timer) != ESP_OK) {
    return false;
  }

  if (esp_timer_start_periodic(tick_timer, kTickPeriodMs * 1000U) != ESP_OK) {
    return false;
  }

  if (xTaskCreatePinnedToCore(
          lvglTask,
          "lvgl",
          kTaskStackSize,
          nullptr,
          kTaskPriority,
          &g_lvgl_task_handle,
          kTaskCore) != pdPASS) {
    return false;
  }

  return true;
}

bool lvgl_port_lock(int timeout_ms) {
  if (g_lvgl_mux == nullptr) return false;
  const TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(g_lvgl_mux, ticks) == pdTRUE;
}

bool lvgl_port_unlock(void) {
  if (g_lvgl_mux == nullptr) return false;
  xSemaphoreGiveRecursive(g_lvgl_mux);
  return true;
}
