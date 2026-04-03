/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "driver/i2c.h"

#include "private/CheckResult.h"
#include "ESP_IOExpander.h"

// Check whether it is a valid pin number
#define IS_VALID_PIN(pin_num)   (pin_num < IO_COUNT_MAX)

static const char *TAG = "ESP_IOExpander";

ESP_IOExpander::ESP_IOExpander(i2c_port_t id, uint8_t address, const i2c_config_t *config):
    handle(NULL),
    i2c_id(id),
    i2c_config(*config),
    i2c_address(address),
    i2c_need_init(true)
{
}

ESP_IOExpander::ESP_IOExpander(i2c_port_t id, uint8_t address, int scl, int sda):
    handle(NULL),
    i2c_id(id),
    i2c_config((i2c_config_t)EXPANDER_I2C_CONFIG_DEFAULT(scl, sda)),
    i2c_address(address),
    i2c_need_init(true)
{
}

ESP_IOExpander::ESP_IOExpander(i2c_port_t id, uint8_t address):
    handle(NULL),
    i2c_id(id),
    i2c_address(address),
    i2c_need_init(false)
{
}

void ESP_IOExpander::init(void)
{
    if (i2c_need_init) {
        CHECK_ERROR_RETURN(i2c_param_config(i2c_id, &i2c_config));
        CHECK_ERROR_RETURN(i2c_driver_install(i2c_id, i2c_config.mode, 0, 0, 0));
    }
}

void ESP_IOExpander::reset(void)
{
    CHECK_ERROR_RETURN(esp_io_expander_reset(handle));
}

void ESP_IOExpander::del(void)
{
    CHECK_ERROR_RETURN(esp_io_expander_del(handle));
    handle = NULL;
}

esp_io_expander_handle_t ESP_IOExpander::getHandle(void)
{
    CHECK_NULL_GOTO(handle, err);
err:
    return handle;
}

void ESP_IOExpander::pinMode(uint8_t pin, uint8_t mode)
{
    CHECK_FALSE_RETURN(IS_VALID_PIN(pin));
    CHECK_FALSE_RETURN(mode == INPUT || mode == OUTPUT);

    esp_io_expander_dir_t dir = (mode == INPUT) ? IO_EXPANDER_INPUT : IO_EXPANDER_OUTPUT;
    CHECK_ERROR_RETURN(esp_io_expander_set_dir(handle, BIT64(pin), dir));
}

void ESP_IOExpander::digitalWrite(uint8_t pin, uint8_t val)
{
    CHECK_FALSE_RETURN(IS_VALID_PIN(pin));
    CHECK_ERROR_RETURN(esp_io_expander_set_level(handle, BIT64(pin), val));
}

int ESP_IOExpander::digitalRead(uint8_t pin)
{
    uint32_t level = 0;
    CHECK_FALSE_GOTO(IS_VALID_PIN(pin), err);

    CHECK_ERROR_GOTO(esp_io_expander_get_level(handle, BIT64(pin), &level), err);
err:
    return (level & BIT64(pin)) ? HIGH : LOW;
}

void ESP_IOExpander::multiPinMode(uint32_t pin_mask, uint8_t mode)
{
    CHECK_FALSE_RETURN(mode == INPUT || mode == OUTPUT);

    esp_io_expander_dir_t dir = (mode == INPUT) ? IO_EXPANDER_INPUT : IO_EXPANDER_OUTPUT;
    CHECK_ERROR_RETURN(esp_io_expander_set_dir(handle, pin_mask, dir));
}

void ESP_IOExpander::multiDigitalWrite(uint32_t pin_mask, uint8_t value)
{
    CHECK_ERROR_RETURN(esp_io_expander_set_level(handle, pin_mask, value));
}

uint32_t ESP_IOExpander::multiDigitalRead(uint32_t pin_mask)
{
    uint32_t level = 0;
    CHECK_ERROR_GOTO(esp_io_expander_get_level(handle, pin_mask, &level), err);
err:
    return level;
}

void ESP_IOExpander::printStatus(void)
{
    CHECK_ERROR_RETURN(esp_io_expander_print_state(handle));
}
