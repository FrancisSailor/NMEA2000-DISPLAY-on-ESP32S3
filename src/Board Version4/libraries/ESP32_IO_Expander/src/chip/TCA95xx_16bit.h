/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "driver/i2c.h"
#include "esp_err.h"

#include "../ESP_IOExpander.h"

class ESP_IOExpander_TCA95xx_16bit: public ESP_IOExpander {
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
    ESP_IOExpander_TCA95xx_16bit(i2c_port_t id, uint8_t address, const i2c_config_t *config): ESP_IOExpander(id, address, config) { };

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
    ESP_IOExpander_TCA95xx_16bit(i2c_port_t id, uint8_t address, int scl, int sda): ESP_IOExpander(id, address, scl, sda) { };

    /**
     * @brief Constructor to create ESP_IOExpander object
     *
     * @note  If use this function, should initialize I2C bus before call `init()`.
     *
     * @param id I2C port number
     * @param address I2C device address. Should be like `ESP_IO_EXPANDER_I2C_*`.
     *                Can be found in the header file of each IO expander.h.
     */
    ESP_IOExpander_TCA95xx_16bit(i2c_port_t id, uint8_t address): ESP_IOExpander(id, address) { };

    /**
     * @brief Destructor
     *
     * @note  This function will delete I2C driver if it is initialized by ESP_IOExpander and delete ESP_IOExpander object.
     */
    ~ESP_IOExpander_TCA95xx_16bit() override;

    /**
     * @brief Begin IO expander
     *
     */
    void begin(void) override;
};

/**
 * @brief I2C address of the TCA9539 or TCA9555
 *
 * The 8-bit address format for the TCA9539 is as follows:
 *
 *                (Slave Address)
 *     ┌─────────────────┷─────────────────┐
 *  ┌─────┐─────┐─────┐─────┐─────┐─────┐─────┐─────┐
 *  |  1  |  1  |  1  |  0  |  1  | A1  | A0  | R/W |
 *  └─────┘─────┘─────┘─────┘─────┘─────┘─────┘─────┘
 *     └────────┯──────────────┘     └──┯──┘
 *           (Fixed)        (Hareware Selectable)
 *
 * The 8-bit address format for the TCA9555 is as follows:
 *
 *                (Slave Address)
 *     ┌─────────────────┷─────────────────┐
 *  ┌─────┐─────┐─────┐─────┐─────┐─────┐─────┐─────┐
 *  |  0  |  1  |  0  |  0  | A2  | A1  | A0  | R/W |
 *  └─────┘─────┘─────┘─────┘─────┘─────┘─────┘─────┘
 *     └────────┯────────┘     └─────┯──────┘
 *           (Fixed)        (Hareware Selectable)
 *
 * And the 7-bit slave address is the most important data for users.
 * For example, if a TCA9555 chip's A0,A1,A2 are connected to GND, it's 7-bit slave address is 0b0100000.
 * Then users can use `ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000` to init it.
 */
enum esp_io_expander_tca_95xx_16bit_address {
    ESP_IO_EXPANDER_I2C_TCA9539_ADDRESS_00 = 0b1110100,
    ESP_IO_EXPANDER_I2C_TCA9539_ADDRESS_01 = 0b1110101,
    ESP_IO_EXPANDER_I2C_TCA9539_ADDRESS_10 = 0b1110110,
    ESP_IO_EXPANDER_I2C_TCA9539_ADDRESS_11 = 0b1110111,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000 = 0b0100000,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_001 = 0b0100001,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_010 = 0b0100010,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_011 = 0b0100011,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_100 = 0b0100000,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_101 = 0b0100101,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_110 = 0b0100110,
    ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_111 = 0b0100111,
};
