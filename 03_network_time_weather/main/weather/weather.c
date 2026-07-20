#include "weather.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "private_config.h"

#define HTTP_RESPONSE_MAX_SIZE 2048

static const char *TAG = "weather";

typedef struct {
    char buffer[HTTP_RESPONSE_MAX_SIZE];
    size_t length;
} http_response_t;

// Keep these large buffers out of the calling task's stack.
static http_response_t weather_response;
static char weather_url[320];

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_t *response = event->user_data;

    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t remaining = sizeof(response->buffer) - response->length - 1;
        size_t copy_length = event->data_len < remaining ? event->data_len : remaining;
        memcpy(response->buffer + response->length, event->data, copy_length);
        response->length += copy_length;
        response->buffer[response->length] = '\0';
    }

    return ESP_OK;
}

static void copy_json_string(const cJSON *object, const char *name,
                             char *destination, size_t destination_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strlcpy(destination, item->valuestring, destination_size);
    }
}

esp_err_t weather_fetch_now(weather_now_t *weather)
{
    if (weather == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(weather, 0, sizeof(*weather));

    memset(&weather_response, 0, sizeof(weather_response));
    snprintf(weather_url, sizeof(weather_url),
             "https://api.seniverse.com/v3/weather/now.json?key=%s&location=%s&language=zh-Hans&unit=c",
             SENIVERSE_API_KEY, WEATHER_LOCATION);

    esp_http_client_config_t client_config = {
        .url = weather_url,
        .event_handler = http_event_handler,
        .user_data = &weather_response,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&client_config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Weather HTTP request failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "Weather server returned HTTP %d: %s", status_code, weather_response.buffer);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(weather_response.buffer);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse weather JSON: %s", weather_response.buffer);
        return ESP_FAIL;
    }

    esp_err_t parse_result = ESP_FAIL;
    const cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    const cJSON *first_result = cJSON_GetArrayItem(results, 0);
    const cJSON *location = cJSON_GetObjectItemCaseSensitive(first_result, "location");
    const cJSON *now = cJSON_GetObjectItemCaseSensitive(first_result, "now");
    if (cJSON_IsObject(location) && cJSON_IsObject(now)) {
        copy_json_string(location, "name", weather->location, sizeof(weather->location));
        copy_json_string(now, "text", weather->text, sizeof(weather->text));
        copy_json_string(now, "code", weather->code, sizeof(weather->code));
        copy_json_string(now, "temperature", weather->temperature, sizeof(weather->temperature));
        copy_json_string(first_result, "last_update", weather->last_update, sizeof(weather->last_update));
        parse_result = ESP_OK;
    }
    cJSON_Delete(root);

    if (parse_result != ESP_OK) {
        ESP_LOGE(TAG, "Weather JSON does not contain results[0].location/now");
        return parse_result;
    }

    ESP_LOGI(TAG, "Weather: %s, %s, %s C", weather->location,
             weather->text, weather->temperature);
    return ESP_OK;
}
