#include "i2c_bus.h"

#include "esp_check.h"
#include "esp_log.h"

#define I2C_SCAN_FIRST_ADDRESS  0x08
#define I2C_SCAN_LAST_ADDRESS   0x77
#define I2C_SCAN_TIMEOUT_MS     20

static const char *TAG = "i2c_bus";

esp_err_t board_i2c_init(i2c_master_bus_handle_t *bus_handle)
{
    ESP_RETURN_ON_FALSE(bus_handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "bus_handle is null");

    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
        },
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, bus_handle), TAG,
                        "unable to create I2C master bus");
    ESP_LOGI(TAG, "I2C ready: SDA=GPIO%d SCL=GPIO%d speed=%d Hz",
             BOARD_I2C_SDA_GPIO, BOARD_I2C_SCL_GPIO, BOARD_I2C_FREQUENCY_HZ);
    return ESP_OK;
}

size_t board_i2c_scan(i2c_master_bus_handle_t bus_handle, uint8_t *addresses,
                      size_t address_capacity)
{
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "cannot scan a null I2C bus");
        return 0;
    }

    ESP_LOGI(TAG, "scanning I2C addresses 0x%02X..0x%02X",
             I2C_SCAN_FIRST_ADDRESS, I2C_SCAN_LAST_ADDRESS);
    size_t found = 0;
    for (uint16_t address = I2C_SCAN_FIRST_ADDRESS;
         address <= I2C_SCAN_LAST_ADDRESS; ++address) {
        esp_err_t result = i2c_master_probe(bus_handle, address, I2C_SCAN_TIMEOUT_MS);
        if (result == ESP_OK) {
            if (addresses != NULL && found < address_capacity) {
                addresses[found] = (uint8_t)address;
            }
            ++found;
            ESP_LOGI(TAG, "I2C device found at 0x%02X", address);
        } else if (result == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "I2C timeout while probing 0x%02X; check pull-ups/bus state",
                     address);
        }
    }

    ESP_LOGI(TAG, "I2C scan complete: %u device(s)", (unsigned)found);
    return found;
}
