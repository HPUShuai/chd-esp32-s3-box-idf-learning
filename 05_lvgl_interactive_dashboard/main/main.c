#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "display/lcd_lvgl.h"
#include "esp_lvgl_port.h"
#include "input/button.h"
#include "network/network.h"
#include "settings/settings.h"
#include "ui/dashboard.h"
#include "weather/weather.h"

#define WEATHER_REFRESH_PERIOD_MS (10 * 60 * 1000)
#define NETWORK_TASK_STACK_SIZE   10240
#define CONTROL_TASK_STACK_SIZE   4096

static const char *TAG = "weather_dashboard";
static app_settings_t app_settings;

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

static void save_settings(void)
{
    esp_err_t result = settings_save(&app_settings);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Unable to save settings: %s", esp_err_to_name(result));
    }
}

static uint8_t next_brightness(uint8_t current)
{
    static const uint8_t levels[] = {25, 50, 75, 100};
    for (size_t index = 0; index < sizeof(levels); index++) {
        if (current <= levels[index]) {
            return levels[(index + 1) % sizeof(levels)];
        }
    }
    return levels[0];
}

static void button_control_task(void *argument)
{
    (void)argument;
    button_event_t event;

    while (true) {
        if (!button_wait_event(&event, portMAX_DELAY)) {
            continue;
        }

        if (event == BUTTON_EVENT_LONG_PRESS) {
            app_settings.brightness = next_brightness(app_settings.brightness);
            esp_err_t result = lcd_backlight_set_brightness(app_settings.brightness);
            if (result == ESP_OK) {
                char status[32];
                snprintf(status, sizeof(status), "屏幕亮度 %u%%", app_settings.brightness);
                show_status(status);
            } else {
                ESP_LOGW(TAG, "Brightness update failed: %s", esp_err_to_name(result));
            }
            save_settings();
            continue;
        }

        if (!lvgl_port_lock(0)) {
            ESP_LOGW(TAG, "Unable to acquire LVGL lock for button event");
            continue;
        }

        if (event == BUTTON_EVENT_SHORT_PRESS) {
            dashboard_show_next_page();
            app_settings.page = dashboard_get_page();
            dashboard_set_status("BOOT短按 · 页面已切换");
        } else if (event == BUTTON_EVENT_DOUBLE_PRESS) {
            app_settings.auto_rotate = !app_settings.auto_rotate;
            dashboard_set_auto_rotate(app_settings.auto_rotate);
            dashboard_set_status(app_settings.auto_rotate ? "自动轮播已开启" : "手动翻页已开启");
        }
        lvgl_port_unlock();
        save_settings();
    }
}

static void init_nvs(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
}

void app_main(void)
{
    init_nvs();
    ESP_ERROR_CHECK(settings_load(&app_settings));

    lv_disp_t *display = NULL;
    ESP_ERROR_CHECK(lcd_lvgl_init(&display));
    ESP_ERROR_CHECK(lcd_backlight_set_brightness(app_settings.brightness));
    ESP_LOGI(TAG, "LVGL display ready: %dx%d", lv_disp_get_hor_res(display),
             lv_disp_get_ver_res(display));

    if (lvgl_port_lock(0)) {
        dashboard_create();
        dashboard_set_auto_rotate(app_settings.auto_rotate);
        dashboard_show_page(app_settings.page);
        lvgl_port_unlock();
    } else {
        ESP_LOGE(TAG, "Unable to acquire LVGL lock during UI startup");
        return;
    }

    ESP_ERROR_CHECK(button_init());
    BaseType_t control_task_created = xTaskCreate(button_control_task, "button_control",
                                                  CONTROL_TASK_STACK_SIZE, NULL, 5, NULL);
    if (control_task_created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create button control task");
        show_status("按键控制任务创建失败");
    }

    BaseType_t task_created = xTaskCreate(network_weather_task, "network_weather",
                                          NETWORK_TASK_STACK_SIZE, NULL, 5, NULL);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Unable to create network task");
        show_status("网络任务创建失败");
    }
}
