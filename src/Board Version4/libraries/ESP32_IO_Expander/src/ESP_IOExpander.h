/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESP_IOEXPANDER_H
#define ESP_IOEXPANDER_H

#include <stdint.h>

#include "driver/i2c.h"

#include "base/esp_io_expander.h"

// Refer to `esp32-hal-gpio.h`
#ifndef INPUT
#define INPUT             0x01
#endif
#ifndef OUTPUT
#define OUTPUT            0x03
#endif
#ifndef LOW
#define LOW               0x0
#endif
#ifndef HIGH
#define HIGH              0x1
#endif

#define EXPANDER_I2C_CONFIG_DEFAULT(scl, sda)                   \
    {                                                           \
        .mode = I2C_MODE_MASTER,                                \
        .sda_io_num = sda,                                      \
        .scl_io_num = scl,                                      \
        .sda_pullup_en = GPIO_PULLUP_ENABLE,                    \
        .scl_pullup_en = GPIO_PULLUP_ENABLE,                    \
        .master = {                                             \
            .clk_speed = 400000,                                \
        },                                                      \
        .clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL,               \
    }

/**
 * @brief ESP_IOExpander class.
 *
 */
class ESP_IOExpander {
public:
    /**
     * @brief Constructor to create ESP_IOExpander object
     *
     * @note  After using this function, call `init()` will initialize I2C bus.
     *
     * @param id I2C port number
     * @param address I2C device address. Should be like `ESP_IO_EXPANDER_I2C_*`.
     *                Can be found in the header file of each IO expander.h.
     * @param config Pointer to I2C bus configuration
     */
    ESP_IOExpander(i2c_port_t id, uint8_t address, const i2c_config_t *config);

    /**
     * @brief Constructor to create ESP_IOExpander object
     *
     * @note  After using this function, call `init()` will initialize I2C bus.
     *
     * @param id I2C port number
     * @param address I2C device address. Should be like `ESP_IO_EXPANDER_I2C_*`.
     *                Can be found in the header file of each IO expander.h.
     * @param scl SCL pin number
     * @param sda SDA pin number
     */
    ESP_IOExpander(i2c_port_t id, uint8_t address, int scl, int sda);

    /**
     * @brief Constructor to create ESP_IOExpander object
     *
     * @note  If use this function, should initialize I2C bus before call `init()`.
     *
     * @param id I2C port number
     * @param address I2C device address. Should be like `ESP_IO_EXPANDER_I2C_*`.
     *                Can be found in the header file of each IO expander.h.
     */
    ESP_IOExpander(i2c_port_t id, uint8_t address);

    /**
     * @brief Initialize IO expander
     *
     * @note  This function will initialize I2C bus if it is not initialized.
     *
     */
    void init(void);

    /**
     * @brief Reset IO expander
     *
     */
    void reset(void);

    /**
     * @brief Delete IO expander object
     *
     */
    void del(void);

    /**
     * @brief Get IO expander handle
     *
     * @return IO expander handle, which can be use to call `esp_io_expander_*` functions
     *
     */
    esp_io_expander_handle_t getHandle(void);

    /**
     * @brief Set pin mode
     *
     * @note  This function is same as Arduino's `pinMode()`.
     *
     * @param pin Pin number (0-31)
     * @param mode Pin mode (INPUT/OUTPUT)
     */
    void pinMode(uint8_t pin, uint8_t mode);

    /**
     * @brief Set pin level
     *
     * @note  This function is same as Arduino's `digitalWrite()`.
     *
     * @param pin Pin number (0-31)
     * @param val Pin level (HIGH/LOW)
     */
    void digitalWrite(uint8_t pin, uint8_t val);

    /**
     * @brief Read pin level
     *
     * @note  This function is same as Arduino's `digitalRead()`.
     *
     * @param pin Pin number (0-31)
     * @return Pin level (HIGH/LOW)
     */
    int digitalRead(uint8_t pin);

    /**
     * @brief Set multiple pin modes
     *
     * @param pin_mask Pin mask (Bitwise OR of `IO_EXPANDER_PIN_NUM_*`)
     * @param mode Mode to set (INPUT/OUTPUT)
     */
    void multiPinMode(uint32_t pin_mask, uint8_t mode);

    /**
     * @brief Write to multiple pins
     *
     * @param pin_mask Pin mask (Bitwise OR of `IO_EXPANDER_PIN_NUM_*`)
     * @param value Value to write (HIGH/LOW)
     */
    void multiDigitalWrite(uint32_t pin_mask, uint8_t value);

    /**
     * @brief Read multiple pin levels
     *
     * @param pin_mask Pin mask (Bitwise OR of `IO_EXPANDER_PIN_NUM_*`)
     * @return Pin levels, every bit represents a pin (HIGH/LOW)
     */
    uint32_t multiDigitalRead(uint32_t pin_mask);

    /**
     * @brief Print IO expander status, include pin index, direction, input level and output level
     *
     */
    void printStatus(void);

    /**
     * @brief Virtual destructor
     */
    virtual ~ESP_IOExpander() = default;

    /**
     * @brief Begin IO expander
     *
     */
    virtual void begin(void) = 0;


protected:
    esp_io_expander_handle_t handle;    /*!< IO expander handle */

    // I2C
    i2c_port_t i2c_id;          /*!< I2C port number */
    i2c_config_t i2c_config;    /*!< I2C bus configuration */
    uint8_t i2c_address;        /*!< I2C device address */
    bool i2c_need_init;         /*!< Whether need to initialize I2C bus */

};

#endif
