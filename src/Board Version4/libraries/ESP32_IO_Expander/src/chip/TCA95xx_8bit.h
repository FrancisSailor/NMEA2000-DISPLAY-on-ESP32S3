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

class ESP_IOExpander_TCA95xx_8bit: public ESP_IOExpander {
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
    ESP_IOExpander_TCA95xx_8bit(i2c_port_t id, uint8_t address, const i2c_config_t *config): ESP_IOExpander(id, address, config) { };

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
    ESP_IOExpander_TCA95xx_8bit(i2c_port_t id, uint8_t address, int scl, int sda): ESP_IOExpander(id, address, scl, sda) { };

    /**
     * @brief Constructor to create ESP_IOExpander object
     *
     * @note  If use this function, should initialize I2C bus before call `init()`.
     *
     * @param id I2C port number
     * @param address I2C device address. Should be like `ESP_IO_EXPANDER_I2C_*`.
     *                Can be found in the header file of each IO expander.h.
     */
    ESP_IOExpander_TCA95xx_8bit(i2c_port_t id, uint8_t address): ESP_IOExpander(id, address) { };

    /**
     * @brief Destructor
     *
     * @note  This function will delete I2C driver if it is initialized by ESP_IOExpander and delete ESP_IOExpander object.
     */
    ~ESP_IOExpander_TCA95xx_8bit() override;

    /**
     * @brief Begin IO expander
     *
     */
    void begin(void) override;
};

/**
 * @brief I2C address of the TCA9554
 *
 * The 8-bit address format is as follows:
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
 * For example, if a chip's A0,A1,A2 are connected to GND, it's 7-bit slave address is 0100000b(0x20).
 * Then users can use `ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000` to init it.
 */
#define ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000    (0x20)
#define ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_001    (0x21)
#define ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_010    (0x22)
#define ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_011    (0x23)
#define ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_100    (0x24)
#define ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_101    (0x25)
#define ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_110    (0x26)
#define ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_111    (0x27)


/**
 * @brief I2C address of the TCA9554A
 *
 * The 8-bit address format is as follows:
 *
 *                (Slave Address)
 *     ┌─────────────────┷─────────────────┐
 *  ┌─────┐─────┐─────┐─────┐─────┐─────┐─────┐─────┐
 *  |  0  |  1  |  1  |  1  | A2  | A1  | A0  | R/W |
 *  └─────┘─────┘─────┘─────┘─────┘─────┘─────┘─────┘
 *     └────────┯────────┘     └─────┯──────┘
 *           (Fixed)        (Hareware Selectable)
 *
 * And the 7-bit slave address is the most important data for users.
 * For example, if a chip's A0,A1,A2 are connected to GND, it's 7-bit slave address is 0111000b(0x38).
 * Then users can use `ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_000` to init it.
 */
#define ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_000    (0x38)
#define ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_001    (0x39)
#define ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_010    (0x3A)
#define ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_011    (0x3B)
#define ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_100    (0x3C)
#define ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_101    (0x3D)
#define ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_110    (0x3E)
#define ESP_IO_EXPANDER_I2C_TCA9554A_ADDRESS_111    (0x3F)
