# NMEA2000 Sail Instrument — Rev 4 cleaned runtime

This project now runs on:
- Arduino IDE
- LVGL 8.4
- Arduino_GFX
- direct Rev 4 board layer (`rev4_board.*`)
- direct GT911 touch driver (`gt911_rev4.*`)

It no longer depends on:
- `ESP32_Display_Panel` at runtime
- `ESP_Panel_Library`
- `lvgl_port_v8`
- `ESP32_IO_Expander`

## Core project files

- `NMEA2000_Sail_Instrument.ino` — application entry point
- `lvgl_arduino_v4.cpp/.h` — LVGL + Arduino_GFX display integration
- `gt911_rev4.cpp/.h` — Rev 4 touch driver
- `rev4_board.cpp/.h` — Rev 4 helper MCU, backlight, buzzer, shared I2C ownership

## Notes

- UI tasking and NMEA tasking remain split as in the working migrated build.
- RGB timing parameters are intentionally left exposed near the top of `lvgl_arduino_v4.cpp` for further tuning if needed.
- Images are loaded from FFat and copied to PSRAM at startup.
