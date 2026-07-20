#include "settings.h"

#include "esp_check.h"
#include "nvs.h"

#define SETTINGS_NAMESPACE          "dashboard"
#define SETTINGS_KEY_AUTO_ROTATE    "auto"
#define SETTINGS_KEY_BRIGHTNESS     "bright"
#define SETTINGS_KEY_PAGE           "page"

static const char *TAG = "settings";

static void set_defaults(app_settings_t *settings)
{
    settings->auto_rotate = false;
    settings->brightness = 100;
    settings->page = 0;
}

esp_err_t settings_load(app_settings_t *settings)
{
    ESP_RETURN_ON_FALSE(settings != NULL, ESP_ERR_INVALID_ARG, TAG, "settings is null");
    set_defaults(settings);

    nvs_handle_t handle;
    esp_err_t result = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(result, TAG, "NVS open failed");

    uint8_t value = 0;
    if (nvs_get_u8(handle, SETTINGS_KEY_AUTO_ROTATE, &value) == ESP_OK) {
        settings->auto_rotate = value != 0;
    }
    if (nvs_get_u8(handle, SETTINGS_KEY_BRIGHTNESS, &value) == ESP_OK &&
        value >= 10 && value <= 100) {
        settings->brightness = value;
    }
    if (nvs_get_u8(handle, SETTINGS_KEY_PAGE, &value) == ESP_OK && value < 3) {
        settings->page = value;
    }
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t settings_save(const app_settings_t *settings)
{
    ESP_RETURN_ON_FALSE(settings != NULL, ESP_ERR_INVALID_ARG, TAG, "settings is null");

    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle),
                        TAG, "NVS open failed");

    esp_err_t result = nvs_set_u8(handle, SETTINGS_KEY_AUTO_ROTATE,
                                  settings->auto_rotate ? 1 : 0);
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, SETTINGS_KEY_BRIGHTNESS, settings->brightness);
    }
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, SETTINGS_KEY_PAGE, settings->page);
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result;
}
