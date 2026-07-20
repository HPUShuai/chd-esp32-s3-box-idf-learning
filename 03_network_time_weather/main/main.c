#include <time.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "lcd/lcd.h"
#include "network/network.h"
#include "weather/weather.h"

static const char *TAG = "network_time_weather";

#define WEATHER_UPDATE_PERIOD_MS (10 * 60 * 1000)

static void update_weather(weather_now_t *weather, char *temperature_text,
                           size_t temperature_text_size)
{
    esp_err_t weather_result = weather_fetch_now(weather);
    if (weather_result == ESP_OK) {
        ESP_LOGI(TAG, "Weather update time: %s", weather->last_update);
        snprintf(temperature_text, temperature_text_size, "%s C", weather->temperature);
        return;
    }

    // Keep the clock usable if the weather service rejects a request or is unavailable.
    ESP_LOGE(TAG, "Weather query failed (%s). Check WEATHER_LOCATION and API key.",
             esp_err_to_name(weather_result));
    snprintf(temperature_text, temperature_text_size, "0 C");
    snprintf(weather->code, sizeof(weather->code), "0");
}

static void network_time_weather_task(void *argument)
{
    (void)argument;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(network_connect_wifi());
    ESP_ERROR_CHECK(network_sync_beijing_time());

    char time_text[9];
    char temperature_text[20];
    weather_now_t weather = {0};
    update_weather(&weather, temperature_text, sizeof(temperature_text));

    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(lcd_show_network_info("00:00:00", temperature_text, weather.code));
    ESP_LOGI(TAG, "LCD updated with network time and weather");

    TickType_t last_weather_update = xTaskGetTickCount();
    while (true) {
        time_t now = 0;
        struct tm time_info = {0};
        time(&now);
        localtime_r(&now, &time_info);
        snprintf(time_text, sizeof(time_text), "%02d:%02d:%02d",
                 time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
        ESP_ERROR_CHECK(lcd_update_time(time_text));

        if (xTaskGetTickCount() - last_weather_update >=
            pdMS_TO_TICKS(WEATHER_UPDATE_PERIOD_MS)) {
            update_weather(&weather, temperature_text, sizeof(temperature_text));
            ESP_ERROR_CHECK(lcd_show_network_info(time_text, temperature_text, weather.code));
            last_weather_update = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    BaseType_t result = xTaskCreate(network_time_weather_task,
                                    "network_time_weather",
                                    8192,
                                    NULL,
                                    5,
                                    NULL);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create network task");
    }
}
