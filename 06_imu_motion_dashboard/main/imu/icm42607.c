#include "icm42607.h"

#include <math.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ICM42607_ADDRESS_LOW          0x68
#define ICM42607_ADDRESS_HIGH         0x69
#define ICM42607_TEMP_DATA1           0x09
#define ICM42607_PWR_MGMT0            0x1F
#define ICM42607_GYRO_CONFIG0         0x20
#define ICM42607_ACCEL_CONFIG0        0x21
#define ICM42607_INTF_CONFIG0         0x35

#define ICM42607_ACCEL_GYRO_LN_MODE   0x0F
#define ICM42607_500DPS_100HZ         0x49
#define ICM42607_4G_100HZ             0x49
#define ICM42607_SENSOR_DATA_BIG_ENDIAN 0x30
#define ICM42607_TRANSFER_TIMEOUT_MS  100
#define ICM42607_ACCEL_LSB_PER_G      8192.0f
#define ICM42607_GYRO_LSB_PER_DPS     65.5f

static const char *TAG = "icm42607";

static bool who_am_i_is_supported(uint8_t value)
{
    return value == ICM42607P_WHO_AM_I_VALUE ||
           value == ICM42607C_WHO_AM_I_VALUE;
}

static const char *model_name(uint8_t who_am_i)
{
    return who_am_i == ICM42607P_WHO_AM_I_VALUE ? "ICM42607-P" : "ICM42607-C";
}

static int16_t decode_be_i16(const uint8_t *bytes)
{
    return (int16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static esp_err_t read_registers(i2c_master_dev_handle_t device, uint8_t start_register,
                                uint8_t *data, size_t size)
{
    return i2c_master_transmit_receive(device, &start_register, sizeof(start_register),
                                       data, size, ICM42607_TRANSFER_TIMEOUT_MS);
}

static esp_err_t write_register(i2c_master_dev_handle_t device, uint8_t register_address,
                                uint8_t value)
{
    const uint8_t command[] = {register_address, value};
    return i2c_master_transmit(device, command, sizeof(command),
                               ICM42607_TRANSFER_TIMEOUT_MS);
}

static esp_err_t read_physical_sample(icm42607_t *sensor, icm42607_sample_t *sample,
                                      bool apply_bias)
{
    ESP_RETURN_ON_FALSE(sensor != NULL && sensor->device != NULL && sample != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid sample arguments");

    uint8_t raw[14];
    ESP_RETURN_ON_ERROR(read_registers(sensor->device, ICM42607_TEMP_DATA1,
                                       raw, sizeof(raw)), TAG, "sample read failed");

    int16_t raw_temperature = decode_be_i16(&raw[0]);
    sample->temperature_c = (float)raw_temperature / 128.0f + 25.0f;
    for (size_t axis = 0; axis < 3; ++axis) {
        int16_t raw_accel = decode_be_i16(&raw[2 + axis * 2]);
        int16_t raw_gyro = decode_be_i16(&raw[8 + axis * 2]);
        sample->accel_g[axis] = (float)raw_accel / ICM42607_ACCEL_LSB_PER_G;
        sample->gyro_dps[axis] = (float)raw_gyro / ICM42607_GYRO_LSB_PER_DPS;
        if (apply_bias) {
            sample->accel_g[axis] -= sensor->accel_bias_g[axis];
            sample->gyro_dps[axis] -= sensor->gyro_bias_dps[axis];
        }
    }
    sample->timestamp_us = esp_timer_get_time();
    return ESP_OK;
}

esp_err_t icm42607_init(icm42607_t *sensor, i2c_master_bus_handle_t bus_handle)
{
    ESP_RETURN_ON_FALSE(sensor != NULL && bus_handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "invalid init arguments");
    memset(sensor, 0, sizeof(*sensor));

    static const uint8_t candidates[] = {ICM42607_ADDRESS_LOW, ICM42607_ADDRESS_HIGH};
    for (size_t index = 0; index < sizeof(candidates); ++index) {
        const i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = candidates[index],
            .scl_speed_hz = 400000,
        };
        i2c_master_dev_handle_t candidate_device = NULL;
        esp_err_t result = i2c_master_bus_add_device(bus_handle, &device_config,
                                                      &candidate_device);
        if (result != ESP_OK) {
            continue;
        }

        uint8_t who_am_i = 0;
        result = read_registers(candidate_device, ICM42607_WHO_AM_I_REGISTER,
                                &who_am_i, sizeof(who_am_i));
        if (result == ESP_OK && who_am_i_is_supported(who_am_i)) {
            sensor->device = candidate_device;
            sensor->address = candidates[index];
            sensor->who_am_i = who_am_i;
            ESP_LOGI(TAG, "%s found at 0x%02X, WHO_AM_I=0x%02X",
                     model_name(who_am_i), sensor->address, who_am_i);
            break;
        }

        if (result == ESP_OK) {
            ESP_LOGW(TAG,
                     "device at 0x%02X returned WHO_AM_I=0x%02X (expected 0x60 or 0x61)",
                     candidates[index], who_am_i);
        }
        i2c_master_bus_rm_device(candidate_device);
    }

    ESP_RETURN_ON_FALSE(sensor->device != NULL, ESP_ERR_NOT_FOUND, TAG,
                        "ICM42607-P/C not found at 0x68 or 0x69");

    /* On this board the ICM42607-P can remain in OTP reload with WHO_AM_I=0x00
     * after a host-triggered soft reset.  A reset is not required here: stop
     * both data paths and explicitly program every setting used below. */
    ESP_RETURN_ON_ERROR(write_register(sensor->device, ICM42607_PWR_MGMT0, 0x00),
                        TAG, "sensor data path stop failed");
    vTaskDelay(pdMS_TO_TICKS(2));
    ESP_RETURN_ON_ERROR(write_register(sensor->device, ICM42607_INTF_CONFIG0,
                                       ICM42607_SENSOR_DATA_BIG_ENDIAN), TAG,
                        "sensor data endian configuration failed");
    ESP_RETURN_ON_ERROR(write_register(sensor->device, ICM42607_GYRO_CONFIG0,
                                       ICM42607_500DPS_100HZ), TAG,
                        "gyroscope configuration failed");
    ESP_RETURN_ON_ERROR(write_register(sensor->device, ICM42607_ACCEL_CONFIG0,
                                       ICM42607_4G_100HZ), TAG,
                        "accelerometer configuration failed");
    ESP_RETURN_ON_ERROR(write_register(sensor->device, ICM42607_PWR_MGMT0,
                                       ICM42607_ACCEL_GYRO_LN_MODE), TAG,
                        "low-noise mode enable failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "%s configured: accel=+/-4g gyro=+/-500dps ODR=100Hz",
             model_name(sensor->who_am_i));
    return ESP_OK;
}

esp_err_t icm42607_read_sample(icm42607_t *sensor, icm42607_sample_t *sample)
{
    return read_physical_sample(sensor, sample, true);
}

esp_err_t icm42607_calibrate_static(icm42607_t *sensor, size_t sample_count,
                                    uint32_t sample_period_ms)
{
    ESP_RETURN_ON_FALSE(sensor != NULL && sensor->device != NULL && sample_count >= 20,
                        ESP_ERR_INVALID_ARG, TAG, "invalid calibration arguments");

    float accel_sum[3] = {0};
    float gyro_sum[3] = {0};
    float accel_norm_mean = 0.0f;
    float accel_norm_m2 = 0.0f;
    float gyro_norm_sum = 0.0f;

    /* Discard the first few samples after enabling the low-noise data paths. */
    for (size_t index = 0; index < 10; ++index) {
        icm42607_sample_t discarded;
        ESP_RETURN_ON_ERROR(read_physical_sample(sensor, &discarded, false), TAG,
                            "calibration warm-up read failed");
        vTaskDelay(pdMS_TO_TICKS(sample_period_ms));
    }

    for (size_t index = 0; index < sample_count; ++index) {
        icm42607_sample_t sample;
        ESP_RETURN_ON_ERROR(read_physical_sample(sensor, &sample, false), TAG,
                            "calibration sample read failed");

        float accel_norm_squared = 0.0f;
        float gyro_norm_squared = 0.0f;
        for (size_t axis = 0; axis < 3; ++axis) {
            accel_sum[axis] += sample.accel_g[axis];
            gyro_sum[axis] += sample.gyro_dps[axis];
            accel_norm_squared += sample.accel_g[axis] * sample.accel_g[axis];
            gyro_norm_squared += sample.gyro_dps[axis] * sample.gyro_dps[axis];
        }

        float accel_norm = sqrtf(accel_norm_squared);
        float delta = accel_norm - accel_norm_mean;
        accel_norm_mean += delta / (float)(index + 1);
        accel_norm_m2 += delta * (accel_norm - accel_norm_mean);
        gyro_norm_sum += sqrtf(gyro_norm_squared);
        vTaskDelay(pdMS_TO_TICKS(sample_period_ms));
    }

    float mean_accel[3];
    float mean_gyro[3];
    for (size_t axis = 0; axis < 3; ++axis) {
        mean_accel[axis] = accel_sum[axis] / (float)sample_count;
        mean_gyro[axis] = gyro_sum[axis] / (float)sample_count;
    }
    float accel_sigma = sqrtf(accel_norm_m2 / (float)(sample_count - 1));
    float gyro_norm_mean = gyro_norm_sum / (float)sample_count;

    bool face_up_and_level = fabsf(mean_accel[0]) < 0.35f &&
                             fabsf(mean_accel[1]) < 0.35f &&
                             fabsf(mean_accel[2]) > 0.70f;
    bool motionless = accel_sigma < 0.05f && gyro_norm_mean < 6.0f;
    if (!face_up_and_level || !motionless) {
        ESP_LOGW(TAG,
                 "static calibration rejected: accel=(%.3f, %.3f, %.3f) sigma=%.3f gyro=%.2f",
                 mean_accel[0], mean_accel[1], mean_accel[2], accel_sigma,
                 gyro_norm_mean);
        return ESP_ERR_INVALID_STATE;
    }

    sensor->accel_bias_g[0] = mean_accel[0];
    sensor->accel_bias_g[1] = mean_accel[1];
    sensor->accel_bias_g[2] = mean_accel[2] - copysignf(1.0f, mean_accel[2]);
    for (size_t axis = 0; axis < 3; ++axis) {
        sensor->gyro_bias_dps[axis] = mean_gyro[axis];
    }
    sensor->calibrated = true;

    ESP_LOGI(TAG, "static calibration complete");
    ESP_LOGI(TAG, "accel bias [g]: %.4f %.4f %.4f",
             sensor->accel_bias_g[0], sensor->accel_bias_g[1], sensor->accel_bias_g[2]);
    ESP_LOGI(TAG, "gyro bias [dps]: %.3f %.3f %.3f",
             sensor->gyro_bias_dps[0], sensor->gyro_bias_dps[1],
             sensor->gyro_bias_dps[2]);
    return ESP_OK;
}
