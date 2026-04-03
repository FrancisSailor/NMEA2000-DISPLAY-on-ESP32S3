[![Arduino Lint](https://github.com/esp-arduino-libs/ESP32_IO_Expander/actions/workflows/arduino_lint.yml/badge.svg)](https://github.com/esp-arduino-libs/ESP32_IO_Expander/actions/workflows/arduino_lint.yml) [![pre-commit](https://github.com/esp-arduino-libs/ESP32_IO_Expander/actions/workflows/pre-commit.yml/badge.svg)](https://github.com/esp-arduino-libs/ESP32_IO_Expander/actions/workflows/pre-commit.yml) [![Build Test Apps](https://github.com/esp-arduino-libs/ESP32_IO_Expander/actions/workflows/build_test.yml/badge.svg)](https://github.com/esp-arduino-libs/ESP32_IO_Expander/actions/workflows/build_test.yml)

# ESP32_IO_Expander

ESP32_IO_Expander is an Arduino library designed for driving [IO expander chips](#supported-drivers) using ESP32 SoCs.

ESP32_IO_Expander encapsulates various components from the [Espressif Components Registry](https://components.espressif.com/). It is developed based on [arduino-esp32](https://github.com/espressif/arduino-esp32) and can be easily downloaded and integrated into the Arduino IDE.

## Features

* Supports various IO expander chips.
* Supports controlling individual IO pin (using pinMode(), digitalRead(), and digitalWrite() functions).
* Supports controlling multiple IO pins simultaneously.

## Supported Drivers

|                                               **Driver**                                               | **Version** |
| ------------------------------------------------------------------------------------------------------ | ----------- |
| [esp_io_expander](https://components.espressif.com/components/espressif/esp_io_expander)               | 1.0.1       |
| [TCA95xx (8bit)](https://components.espressif.com/components/espressif/esp_io_expander_tca9554)        | 1.0.1       |
| [TCA95xx (16bit)](https://components.espressif.com/components/espressif/esp_io_expander_tca95xx_16bit) | 1.0.0       |
| [HT8574](https://components.espressif.com/components/espressif/esp_io_expander_ht8574)                 | 1.0.0       |
| CH422G                                                                                                 | x           |

## Dependencies Version

|                          **Name**                           | **Version** |
| ----------------------------------------------------------- | ----------- |
| [arduino-esp32](https://github.com/espressif/arduino-esp32) | >= v2.0.9   |

## How to Use

For information on how to use the library in the Arduino IDE, please refer to the documentation for [Arduino IDE v1.x.x](https://docs.arduino.cc/software/ide-v1/tutorials/installing-libraries) or [Arduino IDE v2.x.x](https://docs.arduino.cc/software/ide-v2/tutorials/ide-v2-installing-a-library).

### Examples

* [Test Functions](examples/TestFunctions): Demonstrates how to use ESP32_IO_Expander and test all functions.

### Detailed Usage

```cpp
#include <ESP_IOExpander_Library.h>

// Create an ESP_IOExpander object according to the chip type
ESP_IOExpander *expander = new ESP_IOExpander_TCA95xx_8bit(EXAMPLE_I2C_NUM_0, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000,
                                                           EXAMPLE_I2C_SCL_PIN, EXAMPLE_I2C_SDA_PIN);

// Control a single pin (0-31)
expander->pinMode(0, OUTPUT);
expander->digitalWrite(0, HIGH);
expander->digitalWrite(0, LOW);
expander->pinMode(0, INPUT);
int level = expander->digitalRead(0);

// Control multiple pins (IO_EXPANDER_PIN_NUM_0 - IO_EXPANDER_PIN_NUM_31)
expander->multiPinMode(IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, OUTPUT);
expander->multiDigitalWrite(IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, HIGH);
expander->multiDigitalWrite(IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, LOW);
expander->multiPinMode(IO_EXPANDER_PIN_NUM_0 | IO_EXPANDER_PIN_NUM_1, INPUT);
uint32_t level = expander->multiDigitalRead(IO_EXPANDER_PIN_NUM_2 | IO_EXPANDER_PIN_NUM_3);

// Release the ESP_IOExpander object
delete expander;
```
