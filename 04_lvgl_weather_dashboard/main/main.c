#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "display/lcd_lvgl.h"
#include "esp_lvgl_port.h"
#include "network/network.h"
#include "ui/dashboard.h"
#include "weather/weather.h"

#define WEATHER_REFRESH_PERIOD_MS (10 * 60 * 1000)
#define NETWORK_TASK_STACK_SIZE   10240

static const char *TAG = "weather_dashboard";

static void show_status(const char *text)
{
    if (lvgl_port_lock(0)) {
        dashboard_set_status(text);
        lvgl_port_unlock();
    }
}

static void network_weather_task(void *argument)
{
    (void)argument;

    show_status("正在连接无线网络");
    if (network_connect_wifi() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi connection failed");
        show_status("无线网络连接失败");
        vTaskDelete(NULL);
        return;
    }

    show_status("正在同步网络时间");
    if (network_sync_beijing_time() != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync failed; the dashboard clock may be inaccurate");
    }

    while (true) {
        weather_dashboard_t weather = {0};
        show_status("正在获取天气数据");
        esp_err_t result = weather_fetch_dashboard(&weather);

        if (lvgl_port_lock(0)) {
            dashboard_update(&weather);
            if (result != ESP_OK) {
                dashboard_set_status("天气请求失败");
            }
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(WEATHER_REFRESH_PERIOD_MS));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    lv_disp_t *display = NULL;
    ESP_ERROR_CHECK(lcd_lvgl_init(&display));
    ESP_LOGI(TAG, "LVGL display ready: %dx%d", lv_disp_get_hor_res(display),
             lv_disp_get_ver_res(display));

    if (lvgl_port_lock(0)) {
        dashboard_create();
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Unable to acquire LVGL lock during UI startup");
        return;
    }

    BaseType_t task_created = xTaskCreate(network_weather_task, "network_weather",
                                          NETWORK_TASK_STACK_SIZE, NULL, 5, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create network task");
        show_status("网络任务创建失败");
    }
}
