#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define BOARD_I2C_SDA_GPIO  GPIO_NUM_8
#define BOARD_I2C_SCL_GPIO  GPIO_NUM_18
#define BOARD_I2C_FREQUENCY_HZ  400000

/** Initialize the board I2C master bus on GPIO8/SDA and GPIO18/SCL. */
esp_err_t board_i2c_init(i2c_master_bus_handle_t *bus_handle);

/**
 * Scan the normal 7-bit address range and log every responding device.
 * The returned count is the total number found, even if addresses is smaller.
 */
size_t board_i2c_scan(i2c_master_bus_handle_t bus_handle, uint8_t *addresses,
                      size_t address_capacity);
