#include "motion.h"

#include <math.h>
#include <string.h>

#define RAD_TO_DEG                 57.2957795f
#define COMPLEMENTARY_ALPHA        0.96f

static float clampf(float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

void motion_filter_init(motion_filter_t *filter)
{
    if (filter != NULL) {
        memset(filter, 0, sizeof(*filter));
    }
}

void motion_filter_process(motion_filter_t *filter, const icm42607_sample_t *sample,
                           uint8_t sensor_address, uint8_t who_am_i, bool calibrated,
                           imu_motion_data_t *output)
{
    if (filter == NULL || sample == NULL || output == NULL) {
        return;
    }

    float ax = sample->accel_g[0];
    float ay = sample->accel_g[1];
    float az = sample->accel_g[2];
    float gx = sample->gyro_dps[0];
    float gy = sample->gyro_dps[1];
    float gz = sample->gyro_dps[2];

    /* The sensor can be mounted with +Z or -Z facing the LCD. Establish the
     * screen-up sign from the first post-calibration sample so level is 0°. */
    if (!filter->initialized) {
        filter->mounting_sign = az >= 0.0f ? 1.0f : -1.0f;
    }
    float attitude_ax = ax * filter->mounting_sign;
    float attitude_ay = ay * filter->mounting_sign;
    float attitude_az = az * filter->mounting_sign;
    float attitude_gx = gx * filter->mounting_sign;
    float attitude_gy = gy * filter->mounting_sign;

    float accel_roll = atan2f(attitude_ay, attitude_az) * RAD_TO_DEG;
    float accel_pitch = atan2f(-attitude_ax,
                               sqrtf(attitude_ay * attitude_ay +
                                     attitude_az * attitude_az)) * RAD_TO_DEG;

    float delta_seconds = 0.02f;
    if (filter->previous_timestamp_us != 0) {
        delta_seconds = (float)(sample->timestamp_us - filter->previous_timestamp_us) /
                        1000000.0f;
        delta_seconds = clampf(delta_seconds, 0.005f, 0.10f);
    }
    filter->previous_timestamp_us = sample->timestamp_us;

    if (!filter->initialized) {
        filter->roll_deg = accel_roll;
        filter->pitch_deg = accel_pitch;
        filter->initialized = true;
    } else {
        filter->roll_deg = COMPLEMENTARY_ALPHA *
                           (filter->roll_deg + attitude_gx * delta_seconds) +
                           (1.0f - COMPLEMENTARY_ALPHA) * accel_roll;
        filter->pitch_deg = COMPLEMENTARY_ALPHA *
                            (filter->pitch_deg + attitude_gy * delta_seconds) +
                            (1.0f - COMPLEMENTARY_ALPHA) * accel_pitch;
    }

    float accel_norm = sqrtf(ax * ax + ay * ay + az * az);
    float linear_accel = fabsf(accel_norm - 1.0f);
    float gyro_norm = sqrtf(gx * gx + gy * gy + gz * gz);
    float instant_intensity = clampf(linear_accel * 130.0f + gyro_norm * 0.22f,
                                     0.0f, 100.0f);
    filter->filtered_intensity = filter->filtered_intensity * 0.72f +
                                 instant_intensity * 0.28f;

    memset(output, 0, sizeof(*output));
    for (size_t axis = 0; axis < 3; ++axis) {
        output->accel_g[axis] = sample->accel_g[axis];
        output->gyro_dps[axis] = sample->gyro_dps[axis];
    }
    output->temperature_c = sample->temperature_c;
    output->roll_deg = filter->roll_deg;
    output->pitch_deg = filter->pitch_deg;
    output->motion_intensity = filter->filtered_intensity;
    output->sequence = ++filter->sequence;
    output->sensor_address = sensor_address;
    output->who_am_i = who_am_i;
    output->calibrated = calibrated;
}
