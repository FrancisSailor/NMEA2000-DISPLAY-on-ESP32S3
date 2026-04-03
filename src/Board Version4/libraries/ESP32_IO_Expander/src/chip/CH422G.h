/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "driver/i2c.h"
#include "esp_err.h"

#include "../ESP_IOExpander.h"

/**
 * Pin mapping:
 *
 * | Pin Number   | 0   | 1   | 2   | 3   | 4   | 5   | 6   | 7   | 8   | 9   | 10  | 11  |
 * | ------------ | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
 * | Function     | IO0 | IO1 | IO2 | IO3 | IO4 | IO5 | IO6 | IO7 | OC0 | OC1 | OC2 | OC3 |
 */
class ESP_IOExpander_CH422G: public ESP_IOExpander {
public:
    /**
     * @brief Constructor to create ESP_IOExpander object
     *
     * @note  After using this function, call `init()` will initialize I2C bus.
     *
     * @param id I2C port number
     * @param address I2C device address. Just to keep the same with other IO expanders, but it is ignored.
     * @param config Pointer to I2C bus configuration
     */
    ESP_IOExpander_CH422G(i2c_port_t id, uint8_t address, const i2c_config_t *config): ESP_IOExpander(id, 0xFF, config) { };

    /**
     * @brief Constructor to create ESP_IOExpander object
     *
     * @note  After using this function, call `init()` will initialize I2C bus.
     *
     * @param id I2C port number
     * @param address I2C device address. Just to keep the same with other IO expanders, but it is ignored.
     * @param scl SCL pin number
     * @param sda SDA pin number
     */
    ESP_IOExpander_CH422G(i2c_port_t id, uint8_t address, int scl, int sda): ESP_IOExpander(id, 0xFF, scl, sda) { };

    /**
     * @brief Constructor to create ESP_IOExpander object
     *
     * @note  If use this function, should initialize I2C bus before call `init()`.
     *
     * @param id I2C port number
     * @param address I2C device address. Just to keep the same with other IO expanders, but it is ignored.
     */
    ESP_IOExpander_CH422G(i2c_port_t id, uint8_t address): ESP_IOExpander(id, 0xFF) { };

    /**
     * @brief Destructor
     *
     * @note  This function will delete I2C driver if it is initialized by ESP_IOExpander and delete ESP_IOExpander object.
     */
    ~ESP_IOExpander_CH422G() override;

    /**
     * @brief Begin IO expander
     *
     * @note  The driver initialization by default sets CH422G's IO0-7 to output high-level mode.
     *
     */
    void begin(void) override;

    /**
     * @brief Enable OC0-OC3 output open-drain
     *
     */
    void enableOC_OpenDrain(void);

    /**
     * @brief Enable OC0-OC3 output push-pull (default mode when power-on)
     *
     */
    void enableOC_PushPull(void);

    /**
     * @brief Enable IO0-7 input mode
     *
     * @note  The driver initialization by default sets CH422G's IO0-7 to output high-level mode.
     * @note  Since the input/output mode of CH422G's IO0-7 must remain consistent, the driver will only set IO0-7 to
     *        input mode when it determines that all pins are configured as input.
     *
     */
    void enableAllIO_Input(void);

    /**
     * @brief Enable IO0-7 output mode
     *
     */
    void enableAllIO_Output(void);
};

/**
 * @brief I2C address of the ch422g. Just to keep the same with other IO expanders, but it is ignored.
 *
 */
#define ESP_IO_EXPANDER_I2C_CH422G_ADDRESS    (0x24)
