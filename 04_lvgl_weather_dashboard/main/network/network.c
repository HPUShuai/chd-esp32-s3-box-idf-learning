#include "network.h"

#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "private_config.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1
#define WIFI_MAX_RETRY     5

static const char *TAG = "network";
static EventGroupHandle_t wifi_event_group;
static int wifi_retry_count;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count++ < WIFI_MAX_RETRY) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, retrying");
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAILED_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t network_connect_wifi(void)
{
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            wifi_event_handler, NULL, NULL),
                        TAG, "Wi-Fi event registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            wifi_event_handler, NULL, NULL),
                        TAG, "IP event registration failed");

    wifi_config_t wifi_config = {.sta = {.threshold.authmode = WIFI_AUTH_WPA2_PSK}};
    strlcpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Wi-Fi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "Wi-Fi config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                                            pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        ESP_LOGI(TAG, "Connected to Wi-Fi: %s", WIFI_SSID);
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t network_sync_beijing_time(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_init();

    for (int attempt = 0; attempt < 20; attempt++) {
        time_t now = 0;
        struct tm time_info = {0};
        time(&now);
        localtime_r(&now, &time_info);
        if (time_info.tm_year >= (2016 - 1900)) {
            ESP_LOGI(TAG, "SNTP time synchronized");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return ESP_ERR_TIMEOUT;
}
