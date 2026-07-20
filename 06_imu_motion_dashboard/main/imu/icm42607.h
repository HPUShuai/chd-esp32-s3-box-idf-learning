#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#define ICM42607_WHO_AM_I_REGISTER  0x75
#define ICM42607P_WHO_AM_I_VALUE    0x60
#define ICM42607C_WHO_AM_I_VALUE    0x61

typedef struct {
    float accel_g[3];
    float gyro_dps[3];
    float temperature_c;
    int64_t timestamp_us;
} icm42607_sample_t;

typedef struct {
    i2c_master_dev_handle_t device;
    uint8_t address;
    uint8_t who_am_i;
    float accel_bias_g[3];
    float gyro_bias_dps[3];
    bool calibrated;
} icm42607_t;

/** Find an ICM42607-P/C at 0x68/0x69, verify WHO_AM_I, and enable 100 Hz LN mode. */
esp_err_t icm42607_init(icm42607_t *sensor, i2c_master_bus_handle_t bus_handle);

/** Read one converted and bias-corrected accelerometer/gyroscope sample. */
esp_err_t icm42607_read_sample(icm42607_t *sensor, icm42607_sample_t *sample);

/**
 * Estimate static accelerometer/gyroscope bias. Keep the display face upward
 * and the board motionless while this blocking routine collects samples.
 */
esp_err_t icm42607_calibrate_static(icm42607_t *sensor, size_t sample_count,
                                    uint32_t sample_period_ms);
