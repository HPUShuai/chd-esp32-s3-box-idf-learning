#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "icm42607.h"

typedef struct {
    float accel_g[3];
    float gyro_dps[3];
    float temperature_c;
    float roll_deg;
    float pitch_deg;
    float motion_intensity;
    uint32_t sequence;
    uint8_t sensor_address;
    uint8_t who_am_i;
    bool calibrated;
} imu_motion_data_t;

typedef struct {
    bool initialized;
    float roll_deg;
    float pitch_deg;
    float filtered_intensity;
    float mounting_sign;
    int64_t previous_timestamp_us;
    uint32_t sequence;
} motion_filter_t;

/** Reset the complementary attitude filter. */
void motion_filter_init(motion_filter_t *filter);

/** Convert one corrected sensor sample into attitude and motion intensity. */
void motion_filter_process(motion_filter_t *filter, const icm42607_sample_t *sample,
                           uint8_t sensor_address, uint8_t who_am_i, bool calibrated,
                           imu_motion_data_t *output);
