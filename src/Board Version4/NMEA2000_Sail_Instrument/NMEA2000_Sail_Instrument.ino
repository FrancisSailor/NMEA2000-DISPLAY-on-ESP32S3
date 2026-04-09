// ====================================================================================================================================================================
// Do NOT export the "ui_img_manager.h" file and the "ui_img_manager.cpp" from squareline studio, this will break the code and result in compiler errors !!!!!!!!!!!!!!
//    Keep the "ui_img_manager.h" file and the "ui_img_manager.cpp" files supplied together with the sketch!!!
// particularities of the program: 
// - images are loaded in Ffat under directory assets. For this you need another program, A Ffat uploader. Do not compile with the erase all flash on flashing since this will erase the Ffat also
// - On startup the sketch loads all images from Ffat to PSram for better lvgl speed. (library lv_port_fs_ffat.h)
// - The lvgl task runs on core 0 (can be set in lvgl_port_v8.h), the NMEA2000 loop runs on core 1. This helps performance of the NMEA2000 decoding (less lost sentences)
// - In lvgl_port_v8.h we set the priority of the task to 4 or higher, to optimise speed of display and touch handling
// - for control and visualising of the raymarine EVO pilot, proprietary raymarine pgn's are used
// - unit settings are stored in flash
// ====================================================================================================================================================================

#include <Arduino.h>

#include <string.h>

#include "freertos/FreeRTOS.h" // included these 2 lines for the beepr task
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <math.h>
#include "driver/i2c.h"
#include "esp_err.h"
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "lvgl_port_v8.h"
#include <ui.h>
#include <FFat.h>
#include "lv_port_fs_ffat.h"
#include "ui_img_manager.h"
#define ESP32_CAN_RX_PIN GPIO_NUM_0 // gpio pins for can bus
#define ESP32_CAN_TX_PIN GPIO_NUM_6 // gpio pins for can bus
#include <NMEA2000_esp32_twai.h>
#include <NMEA2000.h>
tNMEA2000 &NMEA2000 = *(new NMEA2000_esp32_twai());
#include <n2k.h>

static void ui_update_timer_cb(lv_timer_t * timer); // create a task for all display related functions with a refresh timer in millisec (in lvgl_port_v8.h this task is pinned to core 0 on line 53)
static void BacklightDialog_CancelPending(const char * reason = nullptr);
static void BacklightDialog_GestureGuard(lv_event_t * e);
static void RegisterBacklightDialogGuards();

uint32_t Touch_Button_Clamp = 1000, Button_Clamp_millis; //Touch_Button_Clamp clamps the touchbutton state for the number of millis specified, to give time at the AP to conform the status set by the buttons

// contain the abbreviations of the units chosen in the settings panel (avoid Arduino String to reduce heap churn)
char WindUnitStr[8] = "";
char WindUnitStr2[3] = "";      // first two characters of WindUnitStr (used where the original code did substring(0,2))
char SpeedUnitStr[8] = "";
char DepthUnitStr[4] = "";
char DistanceUnitStr[4] = "";
N2K::ApMode Old_Auto_Mode;

uint32_t Beep_on_millis, Beep_off_millis, TimeOut_millis;  // timing variables for dialog (warning) screen
uint32_t Beep_on_timeout = 100, Beep_off_timeout = 300, TimeOut_millis_timeout = 8000;  // timeout variables for dialog (warning) screen in pretrack mode

bool BacklightDialog_Active = false, oldBacklightDialog_Active = false;
uint32_t BacklightDialog_Timeout;

static bool BacklightDialog_RequestPending = false;
static uint32_t BacklightDialog_RequestMillis = 0;
static uint32_t BacklightDialog_GestureBusyUntil = 0;
static lv_obj_t *BacklightDialog_RequestScreen = nullptr;
static constexpr uint32_t BacklightDialog_TouchSettleMs = 120;
static constexpr uint32_t BacklightDialog_GestureGuardMs = 850;

// V4 helper MCU backend for the Waveshare V4 board.
// The beeper is driven by CH32V003 register 0x02 bit 6. Register 0x03 = 0x3A
// is written once during init only; subsequent beeper pulses do read-modify-write
// on 0x02 so only the buzzer bit changes.
static constexpr i2c_port_t V4_EXPANDER_I2C_PORT = I2C_NUM_0;
static constexpr uint8_t V4_EXPANDER_I2C_ADDRESS = 0x24;
static constexpr uint8_t V4_BEEPER_PULSE_REG = 0x02;
static constexpr uint8_t V4_BEEPER_CONFIG_REG = 0x03;
static constexpr uint8_t V4_BEEPER_CONFIG_VALUE = 0x3A;
static constexpr uint8_t V4_BEEPER_BIT_MASK = 0x40;     // bit 6
static constexpr uint32_t V4_BEEP_SHORT_MS = 30;        // Matches the original keypress beep behaviour.
static constexpr uint32_t V4_STARTUP_BEEP_MS = 120;
static constexpr uint8_t V4_BACKLIGHT_REG = 0x05;
static TaskHandle_t V4_BeeperTaskHandle = nullptr;
static portMUX_TYPE V4_BeeperMux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t V4_ExpanderMutex = nullptr;
static int16_t V4_LastBacklightSliderValue = -1;

static volatile bool V4_BeeperStartReq = false;
static volatile bool V4_BeeperStopReq = false;
static volatile uint32_t V4_BeeperReqOnMs = 0;
static volatile uint32_t V4_BeeperReqOffMs = 0;
static volatile uint16_t V4_BeeperReqRepeat = 0;

static bool v4_expander_write_reg_unlocked(uint8_t reg, uint8_t value) {
  uint8_t buf[2] = {reg, value};
  esp_err_t err = i2c_master_write_to_device(V4_EXPANDER_I2C_PORT, V4_EXPANDER_I2C_ADDRESS, buf, sizeof(buf), pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    Serial.printf("V4 expander write failed: reg=0x%02X value=0x%02X err=%s\n", reg, value, esp_err_to_name(err));
    return false;
  }
  return true;
}

static bool v4_expander_read_reg_unlocked(uint8_t reg, uint8_t &value) {
  esp_err_t err = i2c_master_write_read_device(V4_EXPANDER_I2C_PORT, V4_EXPANDER_I2C_ADDRESS, &reg, 1, &value, 1, pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    Serial.printf("V4 expander read failed: reg=0x%02X err=%s\n", reg, esp_err_to_name(err));
    return false;
  }
  return true;
}

static bool v4_expander_write_reg(uint8_t reg, uint8_t value) {
  if (V4_ExpanderMutex != nullptr) {
    if (xSemaphoreTake(V4_ExpanderMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      Serial.printf("V4 expander mutex timeout: reg=0x%02X value=0x%02X\n", reg, value);
      return false;
    }
  }

  bool ok = v4_expander_write_reg_unlocked(reg, value);

  if (V4_ExpanderMutex != nullptr) {
    xSemaphoreGive(V4_ExpanderMutex);
  }

  return ok;
}

static bool v4_beeper_update_bit(bool active) {
  if (V4_ExpanderMutex != nullptr) {
    if (xSemaphoreTake(V4_ExpanderMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      Serial.println("V4 beeper mutex timeout");
      return false;
    }
  }

  uint8_t current = 0;
  bool ok = v4_expander_read_reg_unlocked(V4_BEEPER_PULSE_REG, current);
  if (ok) {
    const uint8_t updated = active ? (current | V4_BEEPER_BIT_MASK) : (current & ~V4_BEEPER_BIT_MASK);
    if (updated != current) {
      ok = v4_expander_write_reg_unlocked(V4_BEEPER_PULSE_REG, updated);
    }
  }

  if (V4_ExpanderMutex != nullptr) {
    xSemaphoreGive(V4_ExpanderMutex);
  }

  return ok;
}

static bool v4_beeper_init() {
  bool ok = v4_expander_write_reg(V4_BEEPER_CONFIG_REG, V4_BEEPER_CONFIG_VALUE);
  if (ok) {
    ok = v4_beeper_update_bit(false);
  }
  Serial.printf("V4 beeper init: %s, config=0x%02X, mask=0x%02X\n", ok ? "ok" : "failed", V4_BEEPER_CONFIG_VALUE, V4_BEEPER_BIT_MASK);
  return ok;
}

static bool v4_beeper_pulse_once(uint32_t on_ms) {
  if (!v4_beeper_update_bit(true)) {
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(on_ms));
  return v4_beeper_update_bit(false);
}

void Beeper_Start(uint32_t on_ms, uint32_t off_ms, uint16_t repeat) {
  portENTER_CRITICAL(&V4_BeeperMux);
  V4_BeeperReqOnMs = on_ms;
  V4_BeeperReqOffMs = off_ms;
  V4_BeeperReqRepeat = repeat;
  V4_BeeperStartReq = true;
  portEXIT_CRITICAL(&V4_BeeperMux);
}

void Beeper_Stop() {
  portENTER_CRITICAL(&V4_BeeperMux);
  V4_BeeperStopReq = true;
  portEXIT_CRITICAL(&V4_BeeperMux);
}

static void V4_BeeperTask(void *pvParameters) {
  (void)pvParameters;
  v4_beeper_init();

  uint32_t curOnMs = 0, curOffMs = 0;
  uint16_t curRepeat = 0;  // 0 => infinite
  uint16_t remaining = 0;
  bool active = false;

  for (;;) {
    bool stopReq = false, startReq = false;
    uint32_t reqOnMs = 0, reqOffMs = 0;
    uint16_t reqRepeat = 0;

    portENTER_CRITICAL(&V4_BeeperMux);
    stopReq = V4_BeeperStopReq;
    startReq = V4_BeeperStartReq;
    reqOnMs = V4_BeeperReqOnMs;
    reqOffMs = V4_BeeperReqOffMs;
    reqRepeat = V4_BeeperReqRepeat;
    if (stopReq) V4_BeeperStopReq = false;
    if (startReq) V4_BeeperStartReq = false;
    portEXIT_CRITICAL(&V4_BeeperMux);

    if (stopReq) {
      active = false;
      v4_beeper_update_bit(false);
    }

    if (startReq) {
      if (reqOnMs == 0) {
        active = false;
        v4_beeper_update_bit(false);
      } else {
        if (!(active && curOnMs == reqOnMs && curOffMs == reqOffMs && curRepeat == reqRepeat)) {
          curOnMs = reqOnMs;
          curOffMs = reqOffMs;
          curRepeat = reqRepeat;
          remaining = reqRepeat;  // 0 => infinite
          active = true;
        }
      }
    }

    if (!active) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    if (!v4_beeper_pulse_once(curOnMs)) {
      active = false;
      continue;
    }

    if (curRepeat != 0) {
      if (remaining > 0) {
        remaining--;
      }
      if (remaining == 0) {
        active = false;
        continue;
      }
    }

    if (curOffMs > 0) {
      vTaskDelay(pdMS_TO_TICKS(curOffMs));
    }
  }
}

void Beep(){
  Beeper_Start(V4_BEEP_SHORT_MS, 0, 1); // short keypress beep
}

byte Dialog_Status; // global variable to store the Do_Show_Dialog screen Warning_Type (determines behaviour of the screen)
lv_obj_t *Return_Screen;

static void BacklightDialog_CancelPending(const char * reason) {
  if (BacklightDialog_RequestPending && (reason != nullptr)) {
    Serial.printf("Backlight dialog request cancelled: %s\n", reason);
  }
  BacklightDialog_RequestPending = false;
  BacklightDialog_RequestMillis = 0;
  BacklightDialog_GestureBusyUntil = 0;
  BacklightDialog_RequestScreen = nullptr;
}

static void BacklightDialog_GestureGuard(lv_event_t * e) {
  (void)e;
  BacklightDialog_GestureBusyUntil = millis() + BacklightDialog_GestureGuardMs;
  if (BacklightDialog_RequestPending && !BacklightDialog_Active) {
    BacklightDialog_CancelPending("gesture overlap");
  }
}

static void RegisterBacklightDialogGuards() {
  lv_obj_add_event_cb(ui_ScrWind, BacklightDialog_GestureGuard, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(ui_ScrAutopilot, BacklightDialog_GestureGuard, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(ui_ScrXTE, BacklightDialog_GestureGuard, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(ui_ScrNav, BacklightDialog_GestureGuard, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(ui_ScrAppWind, BacklightDialog_GestureGuard, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(ui_ScrTruWind, BacklightDialog_GestureGuard, LV_EVENT_GESTURE, NULL);
  lv_obj_add_event_cb(ui_ScrInfo, BacklightDialog_GestureGuard, LV_EVENT_GESTURE, NULL);
}

#include <Preferences.h>  // esp32 library to write variables to flash
Preferences FlashStorage; // to store calibrations in Flash
int Unit_Initialise, Unit_Speed, Unit_Position, Unit_Distance, Unit_Wind, Unit_Depth, Unit_Wind_Damping, Unit_Heading_Damping;
bool Unit_Silence_Alarm;

void setup() {
    Serial.begin(115200);
    vTaskDelay(200);

    V4_ExpanderMutex = xSemaphoreCreateMutex();
    if (V4_ExpanderMutex == nullptr) {
      Serial.println("V4 expander mutex creation failed");
    }

    ESP_Panel *panel = new ESP_Panel();
    panel->init();
#if LVGL_PORT_AVOID_TEAR
    ESP_PanelBus_RGB *rgb_bus = static_cast<ESP_PanelBus_RGB *>(panel->getLcd()->getBus()); // When avoid tearing function is enabled, configure the RGB bus according to the LVGL configuration
    rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
    rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);
#endif
    panel->begin();

    lvgl_port_init(panel->getLcd(), panel->getTouch());

    Serial.println("Mount FFat");   // this routine loads all images in Ffat to PSram, images need to be stored in a folder named assets in Ffat
    if (!FFat.begin(false, "/ffat", 10, "ffat")) {
        Serial.println("FFat mount FAILED");
    } else {
      lv_port_fs_ffat_init();
    }
    
    lvgl_port_lock(-1); /* Lock the mutex due to the LVGL APIs are not thread-safe */
    ui_init();
    RegisterBacklightDialogGuards();
    if (!ui_assets_all_ok()) {
        while (true) { delay(1000); }
    }
    lv_timer_handler();
    lv_timer_create(ui_update_timer_cb, 100, NULL); // this sets refresh timing for all screens to display
    lvgl_port_unlock(); /* Release the mutex */
    
    // Basic NMEA2000 node setup (talker + listener)
    NMEA2000.SetN2kCANReceiveFrameBufSize(200);
    NMEA2000.SetN2kCANMsgBufSize(32);

    NMEA2000.SetProductInformation(
      "00000001",    // Model code
      100,           // Product code
      "Ray_N2K Node", // Model ID
      "0.4.0",       // Software version
      "0.4.0"        // Model version
    );

    NMEA2000.SetDeviceInformation(
      1,   // Unique number 
      130, // Function (display)
      120, // Class (sensor/communication)
      2046 // Industry group
    );

    // Let this node both talk and listen
    NMEA2000.SetMode(tNMEA2000::N2km_ListenAndNode, 51);

    // Attach Ray_N2K smoothing + pilot helpers WITHOUT changing mode/product info
    N2K::attach(NMEA2000);

    // N2K timeout: too long makes values go stale sluggish only after the delay and state detection can also be influenced, too short and we get random stale values
    N2K::setTimeoutMs(2000);

    NMEA2000.Open();

    read_UnitSettings2Flash();
    if (Unit_Initialise != 444) {   // variables were not initialized in flash, so we will initialize them
    // initialize my variables
    Unit_Initialise = 444;
    Unit_Speed = 0;
    Unit_Position = 0;
    Unit_Distance = 0;
    Unit_Wind = 0;
    Unit_Depth = 0;
    Unit_Wind_Damping = 50;
    Unit_Heading_Damping = 50;
    write_UnitSettings2Flash();
  }
  Update_Unit_Labels();
  lv_obj_add_state(ui_Track, LV_STATE_DISABLED);
  lv_arc_set_change_rate (ui_CircularScale,1); // max change is 1 degrees per second

  if (xTaskCreatePinnedToCore(V4_BeeperTask, "v4Beep", 2048, NULL, 1, &V4_BeeperTaskHandle, 1) != pdPASS) {
    Serial.println("V4 beeper task creation failed");
  }

  Serial.println("Setup Finished");
}

void loop() {
    N2K::process(NMEA2000); // Always service N2K, This does all the NMEA decoding and also sends commands to the AP
    delay(1); // yield to lower-priority tasks (LVGL, WiFi, etc.)
}

void write_UnitSettings2Flash(){
  // Namespace to access variables in flash, true = read only, false = read / write
  if (FlashStorage.begin("Units_Space", false)) {
    FlashStorage.putInt("Unit_Initialise", 444);
    FlashStorage.putInt("Unit_Speed", Unit_Speed);
    FlashStorage.putInt("Unit_Position", Unit_Position);
    FlashStorage.putInt("Unit_Distance", Unit_Distance);
    FlashStorage.putInt("Unit_Wind", Unit_Wind);
    FlashStorage.putInt("Unit_Depth", Unit_Depth);
    FlashStorage.putInt("Unit_Wnd_Damp", Unit_Wind_Damping);
    //FlashStorage.putInt("Unit_Hdng_Damp", Unit_Heading_Damping);
    FlashStorage.putBool("Unit_Sil_Alm", Unit_Silence_Alarm);
    FlashStorage.end();
  }
}

void Update_Unit_Labels() { // updates all labels on all screens when units are read back (or have been changed)

    switch (Unit_Wind) {
      case 0: 
        strncpy(WindUnitStr, "knots", sizeof(WindUnitStr));
        break;
      case 1: 
        strncpy(WindUnitStr, "m/s", sizeof(WindUnitStr));
        break;
      case 2: 
        strncpy(WindUnitStr, "km/h", sizeof(WindUnitStr));
        break;
      case 3: 
        strncpy(WindUnitStr, "Bft", sizeof(WindUnitStr));
        break;
    }

    WindUnitStr[sizeof(WindUnitStr) - 1] = '\0';
    // Maintain original behavior of WindUnitStr.substring(0,2)
    WindUnitStr2[0] = WindUnitStr[0];
    WindUnitStr2[1] = WindUnitStr[1] ? WindUnitStr[1] : '\0';
    WindUnitStr2[2] = '\0';

    switch (Unit_Speed) {
      case 0: 
        strncpy(SpeedUnitStr, "kn", sizeof(SpeedUnitStr));
        break;
      case 1: 
        strncpy(SpeedUnitStr, "m/s", sizeof(SpeedUnitStr));
        break;
      case 2: 
        strncpy(SpeedUnitStr, "km/h", sizeof(SpeedUnitStr));
        break;
    }

    SpeedUnitStr[sizeof(SpeedUnitStr) - 1] = '\0';

    switch (Unit_Depth) {
      case 0: 
        strncpy(DepthUnitStr, "m", sizeof(DepthUnitStr));
        break;
      case 1: 
        strncpy(DepthUnitStr, "ft", sizeof(DepthUnitStr));
        break;
    }

    DepthUnitStr[sizeof(DepthUnitStr) - 1] = '\0';

    switch (Unit_Position) {
      case 0: 
        lv_label_set_text(ui_PosLbl, "DDM: Degrees Decimal Minutes");
        break;
      case 1: 
        lv_label_set_text(ui_PosLbl, "DD: Decimal Degrees");
        break;
      case 2: 
        lv_label_set_text(ui_PosLbl, "DMS: Degrees Minutes Seconds");
        break;
    }

    switch (Unit_Distance) {
      case 0: 
        strncpy(DistanceUnitStr, "nm", sizeof(DistanceUnitStr));
        break;
      case 1: 
        strncpy(DistanceUnitStr, "km", sizeof(DistanceUnitStr));
        break;
      case 2: 
        strncpy(DistanceUnitStr, "mi", sizeof(DistanceUnitStr));
        break;
    }

    DistanceUnitStr[sizeof(DistanceUnitStr) - 1] = '\0';

  N2K::A_HDG = float(Unit_Heading_Damping) / 1000.0 + 0.01; // adjust smoothing factors
  N2K::A_WIND = float(Unit_Wind_Damping) / 1000.0 + 0.01;
  N2K::A_RDR = 0.25f;
  N2K::A_SPD = 0.05f;
}

void read_UnitSettings2Flash(){
  // Namespace to access variables in flash, true = read only, false = read / write
  if (FlashStorage.begin("Units_Space", true)) {
    Unit_Initialise = FlashStorage.getInt("Unit_Initialise",0);
    Unit_Speed = FlashStorage.getInt("Unit_Speed",0);
    Unit_Position = FlashStorage.getInt("Unit_Position",0);
    Unit_Distance = FlashStorage.getInt("Unit_Distance",0);
    Unit_Wind = FlashStorage.getInt("Unit_Wind",0);
    Unit_Depth = FlashStorage.getInt("Unit_Depth",0);
    Unit_Wind_Damping = FlashStorage.getInt("Unit_Wnd_Damp",0);
    Unit_Heading_Damping = FlashStorage.getInt("Unit_Hdng_Damp",0);
    Unit_Silence_Alarm = FlashStorage.getBool("Unit_Sil_Alm", false); 
    FlashStorage.end();
  }
  Update_Unit_Labels();
}

void CancelSettings(lv_event_t * e) { // Cancel button press event on settings screen, just beeps
  Beep();
}

void StoreUnitSettings(lv_event_t * e) { // OK button press event on settings screen: Store Unit Settings in Flash memory
    Beep();
    Unit_Initialise = 444;
    Unit_Speed = lv_dropdown_get_selected(ui_SpeedDropdown);
    Unit_Position = lv_dropdown_get_selected(ui_PostionDropdown);
    Unit_Distance = lv_dropdown_get_selected(ui_DistanceDropdown);
    Unit_Wind = lv_dropdown_get_selected(ui_WindDropdown);
    Unit_Depth = lv_dropdown_get_selected(ui_DepthDropdown);
    Unit_Wind_Damping = lv_slider_get_value(ui_WindDampingSlider);
    Unit_Heading_Damping = lv_slider_get_value(ui_HeadingDampingSlider);
    if (lv_obj_has_state(ui_AutoSilenceNoPilot, LV_STATE_CHECKED)) Unit_Silence_Alarm = true;
      else                                                         Unit_Silence_Alarm = false;
    write_UnitSettings2Flash();
    Update_Unit_Labels();
}

void DoInitSettingsScr(lv_event_t * e){  // is called before displaying the settings screen
  read_UnitSettings2Flash();
  lv_dropdown_set_selected(ui_SpeedDropdown, Unit_Speed);
  lv_dropdown_set_selected(ui_PostionDropdown, Unit_Position);
  lv_dropdown_set_selected(ui_DistanceDropdown, Unit_Distance);
  lv_dropdown_set_selected(ui_WindDropdown, Unit_Wind);
  lv_dropdown_set_selected(ui_DepthDropdown, Unit_Depth);
  lv_slider_set_value(ui_WindDampingSlider, Unit_Wind_Damping, LV_ANIM_OFF);
  lv_slider_set_value(ui_HeadingDampingSlider, Unit_Heading_Damping, LV_ANIM_OFF);
  if (Unit_Silence_Alarm == true)
    lv_obj_add_state(ui_AutoSilenceNoPilot, LV_STATE_CHECKED);
  else
    lv_obj_clear_state(ui_AutoSilenceNoPilot, LV_STATE_CHECKED);

  lv_disp_load_scr(ui_SettingsScr);
  Beep();
}

const char* ConvertSpeed2UnitSettings(float SpeedVal, int Speed_Setting){
  static char BufStr[24];
  BufStr[0] = '\0';
  switch (Speed_Setting) {
    case 0: // knots
      if (SpeedVal < 20)
        snprintf(BufStr, sizeof(BufStr), "%.1f", SpeedVal);
      else
        snprintf(BufStr, sizeof(BufStr), "%.0f", SpeedVal);
      break;
    case 1: // m/s
      snprintf(BufStr, sizeof(BufStr), "%.1f", (double)SpeedVal * 0.5144444);
      break;
    case 2: // km/h
      snprintf(BufStr, sizeof(BufStr), "%.0f", (double)SpeedVal * 1.852);
      break;
    case 3: {// Beaufort
      double BufVal = pow(SpeedVal/1.625 , 0.6667);
      snprintf(BufStr, sizeof(BufStr), "%.0f", BufVal);
      break;}
  }
  return BufStr;
}

const char* ConvertDepth2UnitSettings(float DepthVal, int Speed_Setting){
  static char BufStr[24];
  BufStr[0] = '\0';
  switch (Speed_Setting) {
    case 0: // meters
      if (DepthVal < 50) snprintf(BufStr, sizeof(BufStr), "%.1f", DepthVal);
      else snprintf(BufStr, sizeof(BufStr), "%.0f", DepthVal);
      break;
    case 1: // feet
      snprintf(BufStr, sizeof(BufStr), "%.0f", (double)DepthVal * 3.2808);
      break;
    Default: 
      snprintf(BufStr, sizeof(BufStr), "%f", DepthVal);
      break;  
  }
  return BufStr;
}

const char* ConvertPosition2UnitSettings(double PositionVal, char LatLon, int Postion_Setting) {
  // UTF-8 degree symbol (note: in String.length() it counts as 2 bytes)
  static const char* DEG = "\xC2\xB0";

  const bool isLat = (LatLon == 'L' || LatLon == 'l');
  const int degDigits = isLat ? 2 : 3;
  const int maxDeg = isLat ? 90 : 180;

  // Hemisphere from sign; output uses absolute value (no minus sign)
  const char hemi = isLat ? (PositionVal < 0.0 ? 'S' : 'N')
                          : (PositionVal < 0.0 ? 'W' : 'E');

  double absVal = fabs(PositionVal);
  if (absVal > (double)maxDeg) absVal = (double)maxDeg;  // clamp out-of-range inputs

  static char buf[32];

  switch (Postion_Setting) {
    case 1: { // Decimal Degrees (DD): 4 decimals, padded degrees
      // Width = degDigits + '.' + 4 decimals
      const int width = degDigits + 1 + 4;  // e.g. lat: 7 => "00.0000", lon: 8 => "000.0000"
      // If rounding pushes slightly over max, clamp again
      if (absVal > (double)maxDeg) absVal = (double)maxDeg;
      snprintf(buf, sizeof(buf), "%0*.*f%s%c", width, 4, absVal, DEG, hemi);
      break;
    }

    case 0: { // Degrees Decimal Minutes (DDM): minutes with 2 decimals
      int deg = (int)floor(absVal);
      double minutes = (absVal - (double)deg) * 60.0;

      // Round to 2 decimals, then handle rollover to degrees
      minutes = round(minutes * 100.0) / 100.0;
      if (minutes >= 60.0) { minutes = 0.0; deg += 1; }

      if (deg > maxDeg) { deg = maxDeg; minutes = 0.0; }

      // Format: DD°MM.mm'H  (lat=10) / DDD°MM.mm'H (lon=11)
      snprintf(buf, sizeof(buf), "%0*d%s%05.2f'%c", degDigits, deg, DEG, minutes, hemi);
      break;
    }

    case 2: { // Degrees Minutes Seconds (DMS): integer seconds
      int deg = (int)floor(absVal);
      double totalMin = (absVal - (double)deg) * 60.0;
      int min = (int)floor(totalMin);
      double sec = (totalMin - (double)min) * 60.0;

      // Round to nearest whole second
      int secInt = (int)llround(sec);

      // Rollover handling
      if (secInt >= 60) { secInt = 0; min += 1; }
      if (min >= 60)    { min = 0; deg += 1; }

      if (deg > maxDeg) { deg = maxDeg; min = 0; secInt = 0; }

      // Format: DD°MM'SS"H  (lat=10) / DDD°MM'SS"H (lon=11)
      snprintf(buf, sizeof(buf), "%0*d%s%02d'%02d\"%c", degDigits, deg, DEG, min, secInt, hemi);
      break;
    }

    default: { // fallback: treat as DD
      const int width = degDigits + 1 + 4;
      snprintf(buf, sizeof(buf), "%0*.*f%s%c", width, 4, absVal, DEG, hemi);
      break;
    }
  }

  return buf;
}

const char* ConvertDistance2UnitSettings(float DistanceVal, int Distance_Setting){
  static char BufStr[24];
  BufStr[0] = '\0';
  float BufVal;

  switch (Distance_Setting) {
    case 0:  BufVal = DistanceVal; break;          // nm
    case 1:  BufVal = DistanceVal * 1.852f; break; // km
    case 2:  BufVal = DistanceVal * 1.1508f; break;// mi
    default: BufVal = DistanceVal; break;
  }
  if (BufVal < 100) snprintf(BufStr, sizeof(BufStr), "%.1f", BufVal);
  else              snprintf(BufStr, sizeof(BufStr), "%.0f", BufVal);
  return BufStr;
}

void Do_Update_ScrXTE(){
  char BufStr[64];
  float XTEdistScr, XTEangleScr;

  snprintf(BufStr, sizeof(BufStr), "XTE:                                     %s", DistanceUnitStr);
  lv_label_set_text(ui_DistanceLbl, BufStr);
  snprintf(BufStr, sizeof(BufStr), "DTW:                                   %s", DistanceUnitStr);
  lv_label_set_text(ui_DistanceLbl1, BufStr);

  snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::btwSmoothed());
  lv_label_set_text(ui_BearingWpt, BufStr);
  
  lv_label_set_text(ui_DistanceWpt, ConvertDistance2UnitSettings(N2K::dtwNmSmoothed(), Unit_Distance));
  
  snprintf(BufStr, sizeof(BufStr), "%.0f:%.0f", (double)N2K::etwHours(), (double)N2K::etwMinutes());
  lv_label_set_text(ui_ETW, BufStr);
  if (N2K::etwDays() != 0) {
    snprintf(BufStr, sizeof(BufStr), "Day + %.0f", (double)N2K::etwDays()); // only display ETW days if it's value is different from zero
  }
  else {
    strncpy(BufStr, " ", sizeof(BufStr));
    BufStr[sizeof(BufStr) - 1] = '\0';
  }
  lv_label_set_text(ui_ETWday, BufStr);

  lv_label_set_text(ui_XteWpt, ConvertDistance2UnitSettings(N2K::xtcNmSmoothed(), Unit_Distance));
  
  if (N2K::hasFreshXTC()) {
    if (N2K::xtcNmSmoothed() < 0) {
      lv_obj_clear_flag(ui_XTEleft, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_XTEright, LV_OBJ_FLAG_HIDDEN);

    } 
    if (N2K::xtcNmSmoothed() > 0) {
      lv_obj_add_flag(ui_XTEleft, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_XTEright, LV_OBJ_FLAG_HIDDEN);
    }  
    if (fabs(N2K::xtcNmSmoothed()) <= 0.1) {
      lv_obj_clear_flag(ui_XTEleft, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_XTEright, LV_OBJ_FLAG_HIDDEN);
    }  
  }
  else {
      lv_obj_add_flag(ui_XTEleft, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_XTEright, LV_OBJ_FLAG_HIDDEN);
  }

  if (N2K::hasFreshXTC() && N2K::hasFreshCOG() && N2K::hasFreshBTW()){
    XTEdistScr = N2K::xtcNmSmoothed() * 50; // this displaces the boat image completely to the left for -5 nm and to the right for the same nm positive
    XTEangleScr = (N2K::cogSmoothed() - N2K::btwSmoothed())*10;
    lv_obj_set_x(ui_BoatImg, XTEdistScr);
    lv_img_set_angle(ui_BoatImg, XTEangleScr);
  }

  // if there are no valid values, fill in with default text
  if (!isfinite(N2K::btwSmoothed())) lv_label_set_text(ui_BearingWpt, "---°");
  if (!isfinite(N2K::dtwNmSmoothed())) lv_label_set_text(ui_DistanceWpt, "--.-");
  if (!isfinite(N2K::etwHours())) lv_label_set_text(ui_ETW, "--:--");
  if (!isfinite(N2K::etwDays())) lv_label_set_text(ui_ETWday, "Day + --");
  if (!isfinite(N2K::xtcNmSmoothed())) lv_label_set_text(ui_XteWpt, "--.-");
  if (!isfinite(N2K::xtcNmSmoothed())) lv_obj_add_flag(ui_XTEright, LV_OBJ_FLAG_HIDDEN);
  if (!isfinite(N2K::xtcNmSmoothed())) lv_obj_add_flag(ui_XTEleft, LV_OBJ_FLAG_HIDDEN);
}

const char* apModeToStr(N2K::ApMode m){  // helper function
  switch (m) {
    case N2K::AP_MODE_STANDBY: return "STANDBY";
    case N2K::AP_MODE_AUTO:    return "AUTO";
    case N2K::AP_MODE_WIND:    return "WIND";
    case N2K::AP_MODE_PRE_TRACK:   return "Pre TRACK";
    case N2K::AP_MODE_TRACK:   return "TRACK";
    default:                   return "NO PILOT";
  }
} 

void Pilot_min10(lv_event_t * e) { // detect button press of the auto button
  N2K::ApMode mode = N2K::apMode();
  if (!(mode == N2K::AP_MODE_WIND)) N2K::apMinus10();
  else                              N2K::apPlus10();
  Beep();
}

void Pilot_min1(lv_event_t * e) { // detect button press of the auto button
  N2K::ApMode mode = N2K::apMode();
  if (!(mode == N2K::AP_MODE_WIND)) N2K::apMinus1();
  else                              N2K::apPlus1();
  Beep();
}

void Pilot_plus10(lv_event_t * e) { // detect button press of the auto button
  N2K::ApMode mode = N2K::apMode();
  if (!(mode == N2K::AP_MODE_WIND)) N2K::apPlus10();
  else                              N2K::apMinus10();
  Beep();
}

void Pilot_plus1(lv_event_t * e) { // detect button press of the auto button
  N2K::ApMode mode = N2K::apMode();
  if (!(mode == N2K::AP_MODE_WIND)) N2K::apPlus1();
  else                              N2K::apMinus1();
  Beep();
}

void Auto_pressed(lv_event_t * e) { // detect button press of the auto button
	if (!lv_obj_has_state(ui_Auto, LV_STATE_USER_1)) { // if the auto button is checked
    lv_obj_add_state(ui_Auto, LV_STATE_USER_1);
    N2K::apSetModeAuto();
    Button_Clamp_millis = millis(); // clamp this value for Touch_Button_Clamp millis
  } else if (!lv_obj_has_state(ui_Wind, LV_STATE_USER_1) && (!lv_obj_has_state(ui_Track, LV_STATE_USER_1))){ // if Auto is not active (else if) and if wind, track and nodrift also are not active
    N2K::apSetModeStandby();
  }
  Beep();
}

void Wind_pressed(lv_event_t * e) { // detect button press of the track button
	if (!lv_obj_has_state(ui_Wind, LV_STATE_USER_1)) { // if the wind button is checked (on wind mode)
    lv_obj_add_state(ui_Wind, LV_STATE_USER_1);
    N2K::apSetModeWind();
    Button_Clamp_millis = millis(); // clamp this value for Touch_Button_Clamp millis
  } else if (!lv_obj_has_state(ui_Auto, LV_STATE_USER_1) && (!lv_obj_has_state(ui_Track, LV_STATE_USER_1))){ // if Wind is not active (else if) and if auto, track and nodrift also are not active
    N2K::apSetModeStandby();
  }
  Beep();
}

void Do_Show_Dialog (bool OK_Visible, bool Cancel_Visible, const char* Warning_Text, byte Warning_Type) {
 
  BacklightDialog_CancelPending("dialog took priority");

  lv_obj_t *ActScr = lv_scr_act();
  if (ActScr != ui_DialogScr)    // the return screen can never be the warning screen
    Return_Screen = lv_scr_act(); // Store the screen which was active before calling the dialog screen

  Dialog_Status = Warning_Type; // Store the warning type for later use (distinguish eg track dialog from error dialog and behave accordingly)

  lv_obj_clear_flag(ui_WarningPanel, LV_OBJ_FLAG_HIDDEN); // make WarningPanel visible
  lv_obj_add_flag(ui_BacklightPanel, LV_OBJ_FLAG_HIDDEN); // and hide Backlightpanel

  lv_scr_load(ui_DialogScr);
  lv_label_set_text(ui_WarningText1, Warning_Text);
  if (OK_Visible)     lv_obj_clear_flag(ui_WarningOK, LV_OBJ_FLAG_HIDDEN);
    else              lv_obj_add_flag(ui_WarningOK, LV_OBJ_FLAG_HIDDEN);
  if (Cancel_Visible) lv_obj_clear_flag(ui_WarningCancel, LV_OBJ_FLAG_HIDDEN);
    else              lv_obj_add_flag(ui_WarningCancel, LV_OBJ_FLAG_HIDDEN);

  Beeper_Start(Beep_on_timeout, Beep_off_timeout, 0);   // Repeat forever while warning dialog is active (will be stopped when exiting the dialog screen)
  
  switch (Warning_Type) {
    case 0: 
      lv_img_set_src(ui_WarningSymbol, &ui_img_error_png); 
      lv_label_set_text(ui_WarningText, "Alarm");
      lv_obj_set_style_bg_color(ui_WarningPanel,lv_color_hex(0xFC5C5C),LV_PART_MAIN);
      lv_obj_set_style_bg_color(ui_WarningSymbol,lv_color_hex(0xFF0000),LV_PART_MAIN); // 0xFFFF8D
      break;
    case 1: 
      lv_img_set_src(ui_WarningSymbol, &ui_img_warning_png);
      lv_label_set_text(ui_WarningText, "Warning");
      lv_obj_set_style_bg_color(ui_WarningPanel,lv_color_hex(0xFFFF00),LV_PART_MAIN);
      lv_obj_set_style_bg_color(ui_WarningSymbol,lv_color_hex(0xFFFF00),LV_PART_MAIN);
      break;
    case 2: 
      lv_img_set_src(ui_WarningSymbol, &ui_img_error2_png); 
      lv_label_set_text(ui_WarningText, "Critical Error");
      lv_obj_set_style_bg_color(ui_WarningPanel,lv_color_hex(0xFC5C5C),LV_PART_MAIN);
      lv_obj_set_style_bg_color(ui_WarningSymbol,lv_color_hex(0xFF0000),LV_PART_MAIN);
      break;
    case 3: 
      lv_img_set_src(ui_WarningSymbol, &ui_img_turn_port_png);
      lv_label_set_text(ui_WarningText, "Confirm Track Mode");
      lv_obj_set_style_bg_color(ui_WarningPanel,lv_color_hex(0x9BC6FF),LV_PART_MAIN);
      lv_obj_set_style_bg_color(ui_WarningSymbol,lv_color_hex(0x9BC6FF),LV_PART_MAIN);
      break;
    case 4: 
      lv_img_set_src(ui_WarningSymbol, &ui_img_turn_starboard_png);
      lv_label_set_text(ui_WarningText, "Confirm Track Mode");
      lv_obj_set_style_bg_color(ui_WarningPanel,lv_color_hex(0x9BC6FF),LV_PART_MAIN);
      lv_obj_set_style_bg_color(ui_WarningSymbol,lv_color_hex(0x9BC6FF),LV_PART_MAIN);
      break;
  }
}

void DoSetBacklight(lv_event_t * e){  // is triggered by long press event on a screen (regardless which screen)
  (void)e;

  if (BacklightDialog_Active || BacklightDialog_RequestPending) {
    return;
  }

  lv_obj_t *ActScr = lv_scr_act();
  if ((ActScr == ui_DialogScr) || (ActScr == ui_SplashScr)) {
    return;
  }

  BacklightDialog_RequestScreen = ActScr;

  lv_indev_t * indev = lv_indev_get_act();
  if (indev != NULL) {
    lv_indev_wait_release(indev);
  }

  BacklightDialog_RequestPending = true;
  BacklightDialog_RequestMillis = millis();
  BacklightDialog_GestureBusyUntil = BacklightDialog_RequestMillis + BacklightDialog_TouchSettleMs;
}

void Do_Show_SetBacklight() {
  //Beep();   //This beep interacts with other i2c traffic and causes occasional problems
  oldBacklightDialog_Active = true; // block successive calls to this routine
  BacklightDialog_RequestPending = false;
  BacklightDialog_RequestMillis = 0;
  BacklightDialog_GestureBusyUntil = 0;
  V4_LastBacklightSliderValue = lv_slider_get_value(ui_BacklightSlider);
  if ((BacklightDialog_RequestScreen != nullptr) && (BacklightDialog_RequestScreen != ui_DialogScr)) {
    Return_Screen = BacklightDialog_RequestScreen; // return to the stable screen from which the request originated
  } else {
    Return_Screen = lv_scr_act(); // fallback if there is no stored request screen
  }
  BacklightDialog_RequestScreen = nullptr;
  lv_obj_add_flag(ui_WarningPanel, LV_OBJ_FLAG_HIDDEN); // hide WarningPanel
  lv_obj_clear_flag(ui_BacklightPanel, LV_OBJ_FLAG_HIDDEN); // show Backlightpanel

  lv_scr_load(ui_DialogScr);
}

void ScrDialog_Track(){  // helper for Do_Update_ScrDialog in case of dialog for track confirmation
    if (millis()-TimeOut_millis >= TimeOut_millis_timeout){ // as long as we are in the timeout window, we execute this code block. after this timeout, the dialog window should close (else statement)
      TimeOut_millis = millis();       // here after we define the actions after timeout of the warning window has occured
      // beeper hardware path disabled for V4 bring-up
      lv_disp_load_scr(Return_Screen);
      Dialog_Status = 10; // this is an undefined status which will be detected in other routines
      lv_obj_clear_state(ui_Track, LV_STATE_USER_1); //revert the AP to it's previous state, since track has not been confirmed
      switch (Old_Auto_Mode) {  // and close the dialog window
        case N2K::AP_MODE_STANDBY: N2K::apSetModeStandby(); break;
        case N2K::AP_MODE_AUTO:    N2K::apSetModeAuto(); break;
        case N2K::AP_MODE_WIND:    N2K::apSetModeWind(); break;
        case N2K::AP_MODE_PRE_TRACK:   N2K::apSetModeStandby(); break;
        case N2K::AP_MODE_TRACK: N2K::apSetModeStandby(); break;
        default:                   N2K::apSetModeStandby(); break;
      }
    }
}

void Do_Update_ScrDialog() { // called cyclically to update the dialog screen if we are requesting track mode
  switch (Dialog_Status) {
    case 3:
    case 4:
      ScrDialog_Track();
      break;
    default:
      break;  
  }
}

void Track_pressed(lv_event_t * e) { // detect button press of the track button
  char Bufstr[64];
  const char* TackDirection;
  float CapChange;
  int CapImage;

  Old_Auto_Mode = N2K::apMode(); // remember the pilot mode when track was pressed. if we cancel on the dialog screen we return to this mode
  if (!lv_obj_has_state(ui_Track, LV_STATE_USER_1)) { // if the track button is not checked (on track mode)
    Button_Clamp_millis = millis(); // clamp this value for Touch_Button_Clamp millis
    if (isfinite(N2K::btwSmoothed()) && isfinite(N2K::xtcNmSmoothed())) { // if we want to engage track mode we should have valid values for the navigation data
      CapChange = N2K::apTrackHeading();

      if (CapChange <0){
        TackDirection = "P";
        CapChange = fabs(CapChange);
        CapImage = 3;
      }
      else if (CapChange >0) {
        TackDirection = "S";
        CapImage = 4;
      }
      else {
        TackDirection = "";
        CapImage = 1;
      }
      N2K::apSetModeTrack();
      lv_obj_add_state(ui_Track, LV_STATE_USER_1);
      snprintf(Bufstr, sizeof(Bufstr), "Will engage turn %.0f°%s", (double)round(CapChange), TackDirection);
      Do_Show_Dialog (true,true, Bufstr, CapImage);

    }
    else {
      Do_Show_Dialog (true,true, "No Navigation Data", 4);
      lv_obj_clear_state(ui_Track, LV_STATE_USER_1);
    }
  }
  else if (!lv_obj_has_state(ui_Auto, LV_STATE_USER_1) && (!lv_obj_has_state(ui_Wind, LV_STATE_USER_1))){ // if Track is not active (else if) and if auto and wind also are not active
    N2K::apSetModeStandby();
    lv_obj_clear_state(ui_Track, LV_STATE_USER_1);
  }
  Beep();
  Beep_on_millis = millis();
  Beep_off_millis = millis();
  TimeOut_millis = millis();  
}

void DialogOK(lv_event_t * e) { // detect button press of the ok button on the dialog screen
  Beep();
  lv_disp_load_scr(Return_Screen); // display the screen which was active before the dialog screen was displayed
  Beeper_Stop();
  switch (Dialog_Status) {
    case 0: // actions when dialogscr displays an error
    case 1: // actions when dialogscr displays a warning
    case 2: // actions when dialogscr displays a critical error
      N2K::Silence_Alarm();
      Dialog_Status = 10; // this is an undefined status which will be detected in other routines
      break;
    case 3:
    case 4:
      if (lv_obj_has_state(ui_Track, LV_STATE_USER_1)) { // if the track button is checked (on track mode)
        N2K::apSetModeTrack();
        Button_Clamp_millis = millis(); // clamp this value for Touch_Button_Clamp millis
      } else if (!lv_obj_has_state(ui_Auto, LV_STATE_USER_1) && (!lv_obj_has_state(ui_Wind, LV_STATE_USER_1))){ // if Track is not active (else if) and if auto and wind also are not active
        N2K::apSetModeStandby();
      }
      break;
  }

}

void DialogCancel(lv_event_t * e) { // detect button press of the cancel button on the dialog screen
  Beep();
  lv_disp_load_scr(Return_Screen);
  Beeper_Stop();
  Dialog_Status = 10; // this is an undefined alarm or warning status which will be detected in other routines
  lv_obj_clear_state(ui_Track, LV_STATE_USER_1);
  switch (Old_Auto_Mode) {
    case N2K::AP_MODE_STANDBY: N2K::apSetModeStandby(); break;
    case N2K::AP_MODE_AUTO:    N2K::apSetModeAuto(); break;
    case N2K::AP_MODE_WIND:    N2K::apSetModeWind(); break;
    case N2K::AP_MODE_PRE_TRACK:   N2K::apSetModeStandby(); break;
    case N2K::AP_MODE_TRACK: N2K::apSetModeStandby(); break;
    default:                   N2K::apSetModeStandby(); break;
  }
}

void Do_Update_ScrAutopilot(){
  char BufStr[32];
  const char* Tack;
  float Windangle;

  N2K::ApMode mode = N2K::apMode();

  if (N2K::hasFreshRudder()) lv_bar_set_value(ui_Rudder2, int(N2K::rudderSmoothed()), LV_ANIM_OFF);
      else lv_bar_set_value(ui_Rudder2, 0, LV_ANIM_OFF);

  if ((uint32_t)(millis() - Button_Clamp_millis) >= Touch_Button_Clamp){ // this clamping is to give the autopilot time to change status after one of the buttons is pressed
    if (mode == N2K::AP_MODE_WIND) {  // make wind panel visible and the other nav panel invisible
      lv_obj_add_flag(ui_APheadingPanel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_APwindPanel, LV_OBJ_FLAG_HIDDEN);
    }
    else {                            // else do the inverse
      lv_obj_add_flag(ui_APwindPanel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_APheadingPanel, LV_OBJ_FLAG_HIDDEN); 
      if (N2K::hasFreshHDG()) {  // update the fields in the panel
        lv_img_set_angle(ui_Compass, -10 * N2K::hdgMagSmoothed());
        snprintf(BufStr, sizeof(BufStr), "%.0f", (double)N2K::hdgMagSmoothed());
        lv_label_set_text(ui_Heading1, BufStr); }
      else {
        lv_img_set_angle(ui_Compass, 0);
        lv_label_set_text(ui_Heading1, "---°");}
    }

    lv_label_set_text(ui_APmode, apModeToStr(mode)); // display the actual autopilot mode

    if (N2K::hasFreshBTW() && N2K::hasFreshXTC())              // AUTOPILOT has navigation/track information on NMEA2000 
      lv_obj_clear_state(ui_Track, LV_STATE_DISABLED); // so we can enable Track mode
    else
      lv_obj_add_state(ui_Track, LV_STATE_DISABLED); // otherwise no nav info, we must disable Track mode

    switch (mode) {
      case N2K::AP_MODE_AUTO: {
        snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::apHeadingTarget());
        lv_label_set_text(ui_AutopilotHeading, BufStr);
        lv_obj_add_state(ui_Auto, LV_STATE_USER_1);  // detect ap auto mode and set button accordingly
        if (lv_obj_has_state(ui_Wind, LV_STATE_USER_1))  lv_obj_clear_state(ui_Wind, LV_STATE_USER_1); // if we are in auto clear the other buttons
        if (lv_obj_has_state(ui_Track, LV_STATE_USER_1)) lv_obj_clear_state(ui_Track, LV_STATE_USER_1);// if we are in auto clear the other buttons
        } break;

      case N2K::AP_MODE_WIND: {
        Windangle = N2K::apWindAngleTarget();
        if (Windangle > 0) Tack = "S";
        else if (Windangle < 0) Tack = "P";
        else Tack="";

        snprintf(BufStr, sizeof(BufStr), "%.0f%s", (double)fabs(Windangle), Tack);
        lv_label_set_text(ui_AutopilotHeading, BufStr);
        
        Windangle = N2K::awaSmoothed();
        if (Windangle > 180) {
          Windangle = Windangle -360;
          lv_obj_set_style_arc_color(ui_CircularScale, lv_color_hex(0xFF0000), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        else
          lv_obj_set_style_arc_color(ui_CircularScale, lv_color_hex(0x00FF00), LV_PART_INDICATOR | LV_STATE_DEFAULT);

        if (N2K::hasFreshAWA()) lv_arc_set_value(ui_CircularScale, Windangle);
          else                  lv_arc_set_value(ui_CircularScale, 0);
        lv_obj_add_state(ui_Wind, LV_STATE_USER_1); // detect ap wind mode and set button accordingly
        if (lv_obj_has_state(ui_Track, LV_STATE_USER_1)) lv_obj_clear_state(ui_Track, LV_STATE_USER_1); // if we are in wind clear the other buttons
        if (lv_obj_has_state(ui_Auto, LV_STATE_USER_1))  lv_obj_clear_state(ui_Auto, LV_STATE_USER_1); // if we are in wind clear the other buttons
        } break; 

      case N2K::AP_MODE_PRE_TRACK: 
      case N2K::AP_MODE_TRACK: 
      {
        snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::btwSmoothed()); // Bearing to waypoint
        lv_label_set_text(ui_AutopilotHeading, BufStr);
        lv_obj_add_state(ui_Track, LV_STATE_USER_1);  // detect ap track mode and set button accordingly
        if (lv_obj_has_state(ui_Wind, LV_STATE_USER_1)) lv_obj_clear_state(ui_Wind, LV_STATE_USER_1);// if we are in track clear the other buttons
        if (lv_obj_has_state(ui_Auto, LV_STATE_USER_1)) lv_obj_clear_state(ui_Auto, LV_STATE_USER_1);// if we are in track clear the other buttons
        } break;

      case N2K::AP_MODE_STANDBY:
      case N2K::AP_MODE_UNKNOWN : { // detect ap standby mode OR unknown (=no autopilot) and set buttons accordingly
        lv_label_set_text(ui_AutopilotHeading, "---°");
        if (lv_obj_has_state(ui_Auto, LV_STATE_USER_1) | lv_obj_has_state(ui_Wind, LV_STATE_USER_1) | lv_obj_has_state(ui_Track, LV_STATE_USER_1)) // if we are in standby and one of the autopilot buttons is active
          lv_obj_clear_state(ui_Wind, LV_STATE_USER_1); // if Auto is active then wind is not active
          lv_obj_clear_state(ui_Track, LV_STATE_USER_1); // if Auto is active then track is not active
          lv_obj_clear_state(ui_Auto, LV_STATE_USER_1);
      } break;
    }
  }
}

void Do_Update_ScrWind(){
    char BufStr[64];
    
    if (N2K::hasFreshAWA()) 
      lv_img_set_angle(ui_AppWindImg, 10 * N2K::awaSmoothed());
    else 
      lv_img_set_angle(ui_AppWindImg, 0);

    if (N2K::hasFreshDepth() && (N2K::depth()<=100)  && isfinite(N2K::depth()) )  snprintf(BufStr, sizeof(BufStr), "Depth %s%s", ConvertDepth2UnitSettings(N2K::depth(), Unit_Depth), DepthUnitStr);
      else                                                                        snprintf(BufStr, sizeof(BufStr), "Depth --.-%s", DepthUnitStr);
    lv_label_set_text(ui_Depth1, BufStr);

    if (N2K::hasFreshTWA()) lv_img_set_angle(ui_TrueWindImg, 10 * N2K::twaSmoothed());
      else                  lv_img_set_angle(ui_TrueWindImg, 0);
    
    if (N2K::hasFreshHDG()) {
        lv_img_set_angle(ui_CompassRoseImg, -10 * N2K::hdgMagSmoothed());
        snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::hdgMagSmoothed());
        lv_label_set_text(ui_Heading, BufStr); 
    }
      else {
        lv_img_set_angle(ui_CompassRoseImg, 0);
        lv_label_set_text(ui_Heading, "---°");
      }
    
    if (N2K::hasFreshRudder()) lv_bar_set_value(ui_Rudder, int(N2K::rudderSmoothed()), LV_ANIM_OFF);
      else lv_bar_set_value(ui_Rudder, 0, LV_ANIM_OFF);
    
    if (N2K::hasFreshAWS()) lv_label_set_text(ui_AppWindSpeed, ConvertSpeed2UnitSettings(N2K::awsSmoothed(), Unit_Wind));
      else                  lv_label_set_text(ui_AppWindSpeed, "---");
    
    if (N2K::hasFreshSTW()) snprintf(BufStr, sizeof(BufStr), "STW %s%s", ConvertSpeed2UnitSettings(N2K::stwSmoothed(), Unit_Speed), SpeedUnitStr);
      else                  snprintf(BufStr, sizeof(BufStr), "STW --- %s", SpeedUnitStr);
    lv_label_set_text(ui_Waterspeed, BufStr); 

    if (N2K::hasFreshSOG())
        snprintf(BufStr, sizeof(BufStr), "SOG %s%s", ConvertSpeed2UnitSettings(N2K::sogSmoothed(), Unit_Speed), SpeedUnitStr);
    else snprintf(BufStr, sizeof(BufStr), "SOG ---%s", SpeedUnitStr);
    lv_label_set_text(ui_GroundSpeed2, BufStr); 

    if (N2K::hasFreshTWS())
        snprintf(BufStr, sizeof(BufStr), "TWS %s%s", ConvertSpeed2UnitSettings(N2K::twsSmoothed(), Unit_Wind), WindUnitStr2);
    else snprintf(BufStr, sizeof(BufStr), "TWS ---%s", WindUnitStr2);
    lv_label_set_text(ui_TruWindSpeed, BufStr);   
}

void Do_Update_ScrAppWind() {
    char BufStr[48];
    const char* ShortSpeedUnitStr;

    if (strcmp(SpeedUnitStr, "knots") == 0) ShortSpeedUnitStr = "kn";
    else ShortSpeedUnitStr = SpeedUnitStr;

    if (N2K::hasFreshTWA())
      if (N2K::twaSmoothed() < 180) snprintf(BufStr, sizeof(BufStr), "TWA %.0fS", (double)N2K::twaSmoothed());
      else                          snprintf(BufStr, sizeof(BufStr), "TWA %.0fP", (double)fabs(360-N2K::twaSmoothed()));
    else strncpy(BufStr, "TWA ---°-", sizeof(BufStr));
    BufStr[sizeof(BufStr)-1] = '\0';
    lv_label_set_text(ui_TruWindAngle2, BufStr); 

    if (N2K::hasFreshDepth() && (N2K::depth()<=100)  && isfinite(N2K::depth()) )  snprintf(BufStr, sizeof(BufStr), "Depth %s%s", ConvertDepth2UnitSettings(N2K::depth(), Unit_Depth), DepthUnitStr);
      else                                                                        snprintf(BufStr, sizeof(BufStr), "Depth --.-%s", DepthUnitStr);
    lv_label_set_text(ui_Depth3, BufStr);

    if (N2K::hasFreshSTW()) snprintf(BufStr, sizeof(BufStr), "STW %s%s", ConvertSpeed2UnitSettings(N2K::stwSmoothed(), Unit_Speed), ShortSpeedUnitStr);
      else                  snprintf(BufStr, sizeof(BufStr), "STW ---%s", ShortSpeedUnitStr);
    lv_label_set_text(ui_WaterSpeed2, BufStr); 

    if (N2K::hasFreshSOG()) snprintf(BufStr, sizeof(BufStr), "SOG %s%s", ConvertSpeed2UnitSettings(N2K::sogSmoothed(), Unit_Speed), ShortSpeedUnitStr);
      else                  snprintf(BufStr, sizeof(BufStr), "SOG ---%s", ShortSpeedUnitStr);
    lv_label_set_text(ui_GroundSpeed, BufStr); 

    if (N2K::hasFreshAWA()) lv_img_set_angle(ui_AWAneedle, 10 * N2K::awaSmoothed());
      else                  lv_img_set_angle(ui_AWAneedle, 0);

    if (N2K::hasFreshAWS()) lv_label_set_text(ui_AWS, ConvertSpeed2UnitSettings(N2K::awsSmoothed(), Unit_Wind));
      else                  lv_label_set_text(ui_AWS, "---");
}

void Do_Update_ScrTruWind() {
    char BufStr[48];
    const char* ShortSpeedUnitStr;

    if (strcmp(SpeedUnitStr, "knots") == 0) ShortSpeedUnitStr = "kn";
    else ShortSpeedUnitStr = SpeedUnitStr;

    if (N2K::hasFreshAWA()) 
      if (N2K::awaSmoothed() < 180) snprintf(BufStr, sizeof(BufStr), "AWA %.0fS", (double)N2K::awaSmoothed());
      else                        snprintf(BufStr, sizeof(BufStr), "AWA %.0fP", (double)fabs(360-N2K::awaSmoothed()));
    else strncpy(BufStr, "AWA ---°-", sizeof(BufStr));
    BufStr[sizeof(BufStr)-1] = '\0';
    lv_label_set_text(ui_AppWindAngle1, BufStr);

    if (N2K::hasFreshSTW()) snprintf(BufStr, sizeof(BufStr), "STW %s%s", ConvertSpeed2UnitSettings(N2K::stwSmoothed(), Unit_Speed), ShortSpeedUnitStr);
      else                  snprintf(BufStr, sizeof(BufStr), "STW ---%s", ShortSpeedUnitStr);
    lv_label_set_text(ui_WaterSpeed3, BufStr); 

    if (N2K::hasFreshDepth() && (N2K::depth()<=100)  && isfinite(N2K::depth()) )  snprintf(BufStr, sizeof(BufStr), "Depth %s%s", ConvertDepth2UnitSettings(N2K::depth(), Unit_Depth), DepthUnitStr);
      else                                                                        snprintf(BufStr, sizeof(BufStr), "Depth --.-%s", DepthUnitStr);
    lv_label_set_text(ui_Depth4, BufStr);

    if (N2K::hasFreshSOG()) snprintf(BufStr, sizeof(BufStr), "SOG %s%s", ConvertSpeed2UnitSettings(N2K::sogSmoothed(), Unit_Speed), ShortSpeedUnitStr);
      else                  snprintf(BufStr, sizeof(BufStr), "SOG ---%s", ShortSpeedUnitStr);
    lv_label_set_text(ui_GroundSpeed1, BufStr); 

    if (N2K::hasFreshTWA()) lv_img_set_angle(ui_TWAneedle, 10 * N2K::twaSmoothed());
      else                  lv_img_set_angle(ui_TWAneedle, 0);
    
    if (N2K::hasFreshTWS()) lv_label_set_text(ui_TWS, ConvertSpeed2UnitSettings(N2K::twsSmoothed(), Unit_Wind));
      else                  lv_label_set_text(ui_TWS, "---");
}

void Do_Update_ScrNav(){
  char BufStr[64];
  N2K::ApMode mode = N2K::apMode();

  snprintf(BufStr, sizeof(BufStr), "STW:                                     %s", SpeedUnitStr);
  lv_label_set_text(ui_SpeedLbl1, BufStr);

  if (N2K::hasFreshLatitude()) lv_label_set_text(ui_Lat, ConvertPosition2UnitSettings(N2K::latitude(), 'L', Unit_Position));
  else                         lv_label_set_text(ui_Lat, "--.----°-");
  
  if (N2K::hasFreshLongitude()) lv_label_set_text(ui_Lon, ConvertPosition2UnitSettings(N2K::longitude(), 'O', Unit_Position));
  else                          lv_label_set_text(ui_Lon, "---.----°-");

  if (mode == N2K::AP_MODE_AUTO) {
    snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::apHeadingTarget());
    lv_label_set_text(ui_AutopilotHeading1, BufStr);
    lv_label_set_text(ui_APmode1, apModeToStr(mode));
  }
  else if (mode == N2K::AP_MODE_WIND) {
    snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::apWindAngleTarget());
    lv_label_set_text(ui_AutopilotHeading1, BufStr);
    lv_label_set_text(ui_APmode1, apModeToStr(mode));
  }
  else if (mode == N2K::AP_MODE_PRE_TRACK) {
    snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::btwSmoothed()); // Bearing to waypoint
    lv_label_set_text(ui_AutopilotHeading1, BufStr);
    lv_label_set_text(ui_APmode1, apModeToStr(mode));
  }
  else if ((mode == N2K::AP_MODE_STANDBY) || (mode == N2K::AP_MODE_UNKNOWN)) { // detect ap standby mode OR unknown (=no autopilot) and set buttons accordingly
    lv_label_set_text(ui_AutopilotHeading1, "---°");
    lv_label_set_text(ui_APmode1, apModeToStr(mode));
  }
  
  if (N2K::hasFreshHDG()) {
    snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::hdgMagSmoothed());
    lv_label_set_text(ui_HDG, BufStr);
  } else lv_label_set_text(ui_HDG, "---°");

  if (N2K::hasFreshSTW()) {
    snprintf(BufStr, sizeof(BufStr), "%.1f", (double)N2K::stwSmoothed());
    lv_label_set_text(ui_STW, BufStr);
  }
    else lv_label_set_text(ui_STW, "--.-");
}

void Do_Update_ScrInfo(){
  char BufStr[64];

  snprintf(BufStr, sizeof(BufStr), "Depth:                                  %s", DepthUnitStr);
  lv_label_set_text(ui_DepthLbl, BufStr);
  snprintf(BufStr, sizeof(BufStr), "SOG:                                     %s", SpeedUnitStr);
  lv_label_set_text(ui_SpeedLbl, BufStr);

  if (N2K::hasFreshDepth() && (N2K::depth()<=100)  && isfinite(N2K::depth()) )  lv_label_set_text(ui_Depth, ConvertDepth2UnitSettings(N2K::depth(), Unit_Depth));
    else                                                                        lv_label_set_text(ui_Depth, "--.-");

  if (N2K::hasFreshRudder()) lv_bar_set_value(ui_Rudder1, int(N2K::rudderSmoothed()), LV_ANIM_OFF);
    else                     lv_bar_set_value(ui_Rudder1, 0, LV_ANIM_OFF);

  if (N2K::hasFreshCOG()) snprintf(BufStr, sizeof(BufStr), "%.0f°", (double)N2K::cogSmoothed());
    else                  strncpy(BufStr, "---°", sizeof(BufStr));
  BufStr[sizeof(BufStr)-1] = '\0';
  lv_label_set_text(ui_COG, BufStr); 

  if (N2K::hasFreshSOG()) lv_label_set_text(ui_SOG, ConvertSpeed2UnitSettings(N2K::sogSmoothed(), Unit_Speed));
    else                  lv_label_set_text(ui_SOG, "--.-");

  if (N2K::hasFreshAWA()) {
    if (N2K::awaSmoothed() < 180) snprintf(BufStr, sizeof(BufStr), "%.0fS", (double)N2K::awaSmoothed());
      else                        snprintf(BufStr, sizeof(BufStr), "%.0fP", (double)fabs(360-N2K::awaSmoothed()));
    lv_label_set_text(ui_AWA, BufStr);
  }
  else {
    lv_label_set_text(ui_AWA, "---");
  }
}

static void ui_update_timer_cb(lv_timer_t * timer) {
  N2K::setUseCalculatedTrueWind(true);  // true means that calculated true wind TWS and TWA are calculated in the library, false means they are coming from NMEA2000 pgn's

  lv_obj_t *ActScr = lv_scr_act();
  if (ActScr == ui_ScrAutopilot) {
      Do_Update_ScrAutopilot();
  } else if (ActScr == ui_ScrWind) {
      Do_Update_ScrWind();
  } else if (ActScr == ui_ScrNav) {
      Do_Update_ScrNav();
  } else if (ActScr == ui_ScrXTE) {
      Do_Update_ScrXTE();
  } else if (ActScr == ui_ScrAppWind) {
      Do_Update_ScrAppWind();
  } else if (ActScr == ui_ScrTruWind){
      Do_Update_ScrTruWind();       
  } else if (ActScr == ui_ScrInfo){
      Do_Update_ScrInfo();   
  } else if (ActScr == ui_DialogScr){
      Do_Update_ScrDialog();       
  }

  if (N2K::apHasAlarm()) {
    if ((N2K::apAlarmCode() == 0x22) && Unit_Silence_Alarm) // if we get a "no pilot" alarm and we choose to silence it automatically in the settings
      N2K::Silence_Alarm();  // then we silence it
    else if (ActScr != ui_DialogScr) // else if the dialog screen for alarm is not shown, show it
      Do_Show_Dialog (true, false, N2K::apAlarmText(), 0);  // displays alarm dialog when an alarm is detected on the NMEA2000 network
  }
  else if (N2K::apHasWarning()) {
    if (ActScr != ui_DialogScr)
      Do_Show_Dialog (true, false, N2K::apWarningText(), 1);  // displays warning dialog when a warning is detected on the NMEA2000 network
  }

  if (BacklightDialog_RequestPending) {
    if (N2K::apHasAlarm() || N2K::apHasWarning()) {
      BacklightDialog_CancelPending("alarm/warning took priority");
    } else if (millis() >= BacklightDialog_GestureBusyUntil) {
      lv_obj_t *CurrentScr = lv_scr_act();
      if ((BacklightDialog_RequestScreen != nullptr) && (CurrentScr != BacklightDialog_RequestScreen)) {
        BacklightDialog_CancelPending("screen changed before dialog open");
      } else {
        BacklightDialog_Active = true;
        oldBacklightDialog_Active = false;
      }
    }
  }

  if (!BacklightDialog_Active){
    if ((ActScr == ui_DialogScr) && (Dialog_Status !=3) && (Dialog_Status !=4) &&  !(N2K::apHasAlarm()) && !(N2K::apHasWarning()))  { // if the alarmscreen is displayed (with alarm or warning status i) but there is no active alarm anymore, revert to the previous screen and change dialog status
      Dialog_Status = 10; // this is an undefined status which will be detected in other routines
      lv_disp_load_scr(Return_Screen); // display the screen which was active before the dialog screen was displayed
      Beeper_Stop(); // assure buzzer ends LOW via beeper task  
    }
  }
  else {
    if (!oldBacklightDialog_Active) { // Backlight dialog was not visble, so we make it visible
      Do_Show_SetBacklight();
      BacklightDialog_Timeout = millis();
    }
    else { // here the backlight dialog was already active, in this pasrt we set the screen brightness and watch a timeout of this dialog
      if (millis() >= (BacklightDialog_Timeout + 5000)) {// brightness dialog stays on screen for 5 seconds
        BacklightDialog_Active = false; // we deactivate the backlight doialog and return to the main screen
        lv_disp_load_scr(Return_Screen); // display the screen which was active before the dialog screen was displayed
      }
      int16_t sliderValue = lv_slider_get_value(ui_BacklightSlider);
      if (sliderValue != V4_LastBacklightSliderValue) {
        V4_LastBacklightSliderValue = sliderValue;
        v4_expander_write_reg(V4_BACKLIGHT_REG, 255 - uint8_t(sliderValue)); // only update brightness when the slider value changes
      }
    }
  }
}

