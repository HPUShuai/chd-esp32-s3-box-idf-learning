#include "weather.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "private_config.h"

#define HTTP_RESPONSE_MAX_SIZE 4096
#define XIAOGAN_LATITUDE  "30.93"
#define XIAOGAN_LONGITUDE "113.91"

static const char *TAG = "weather";

typedef struct {
    char buffer[HTTP_RESPONSE_MAX_SIZE];
    size_t length;
} http_response_t;

// These buffers are static to keep the network task stack small.
static http_response_t response;
static char request_url[384];

static esp_err_t http_event_handler(esp_http_client_event_t *event)
{
    http_response_t *http_response = event->user_data;
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t remaining = sizeof(http_response->buffer) - http_response->length - 1;
        size_t copied = event->data_len < remaining ? event->data_len : remaining;
        memcpy(http_response->buffer + http_response->length, event->data, copied);
        http_response->length += copied;
        http_response->buffer[http_response->length] = '\0';
    }
    return ESP_OK;
}

static esp_err_t http_get(const char *service_name, const char *url)
{
    memset(&response, 0, sizeof(response));

    const esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &response,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t result = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (result != ESP_OK) {
        ESP_LOGE(TAG, "%s request failed: %s", service_name, esp_err_to_name(result));
        return result;
    }
    if (status_code != 200) {
        ESP_LOGW(TAG, "%s unavailable (HTTP %d): %s", service_name, status_code, response.buffer);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t weather_http_get(const char *endpoint, const char *extra_query)
{
    snprintf(request_url, sizeof(request_url),
             "https://api.seniverse.com/v3/%s?key=%s&location=%s&language=zh-Hans&unit=c%s",
             endpoint, SENIVERSE_API_KEY, WEATHER_LOCATION, extra_query);
    return http_get(endpoint, request_url);
}

static esp_err_t open_meteo_air_http_get(void)
{
    snprintf(request_url, sizeof(request_url),
             "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%s&longitude=%s"
             "&current=us_aqi,pm2_5,pm10,carbon_monoxide,nitrogen_dioxide,sulphur_dioxide,ozone"
             "&timezone=Asia%%2FShanghai&forecast_days=1",
             XIAOGAN_LATITUDE, XIAOGAN_LONGITUDE);
    return http_get("Open-Meteo air quality", request_url);
}

static void json_copy(const cJSON *object, const char *name, char *destination,
                      size_t destination_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        strlcpy(destination, item->valuestring, destination_size);
    }
}

static bool json_number_copy(const cJSON *object, const char *name, char *destination,
                             size_t destination_size, unsigned int decimals)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    snprintf(destination, destination_size, decimals == 0 ? "%.0f" : "%.1f", item->valuedouble);
    return true;
}

static cJSON *get_first_result(cJSON *root)
{
    if (root == NULL) {
        return NULL;
    }
    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    return cJSON_GetArrayItem(results, 0);
}

static esp_err_t parse_now(weather_dashboard_t *dashboard)
{
    cJSON *root = cJSON_Parse(response.buffer);
    cJSON *first = get_first_result(root);
    cJSON *location = first == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(first, "location");
    cJSON *now = first == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(first, "now");
    if (root == NULL || !cJSON_IsObject(location) || !cJSON_IsObject(now)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    json_copy(location, "name", dashboard->location, sizeof(dashboard->location));
    json_copy(now, "text", dashboard->text, sizeof(dashboard->text));
    json_copy(now, "code", dashboard->code, sizeof(dashboard->code));
    json_copy(now, "temperature", dashboard->temperature, sizeof(dashboard->temperature));
    json_copy(now, "feels_like", dashboard->feels_like, sizeof(dashboard->feels_like));
    json_copy(now, "pressure", dashboard->pressure, sizeof(dashboard->pressure));
    json_copy(now, "humidity", dashboard->humidity, sizeof(dashboard->humidity));
    json_copy(now, "visibility", dashboard->visibility, sizeof(dashboard->visibility));
    json_copy(now, "wind_direction", dashboard->wind_direction, sizeof(dashboard->wind_direction));
    json_copy(now, "wind_direction_degree", dashboard->wind_direction_degree,
              sizeof(dashboard->wind_direction_degree));
    json_copy(now, "wind_speed", dashboard->wind_speed, sizeof(dashboard->wind_speed));
    json_copy(now, "wind_scale", dashboard->wind_scale, sizeof(dashboard->wind_scale));
    json_copy(now, "clouds", dashboard->clouds, sizeof(dashboard->clouds));
    json_copy(now, "dew_point", dashboard->dew_point, sizeof(dashboard->dew_point));
    json_copy(first, "last_update", dashboard->last_update, sizeof(dashboard->last_update));
    dashboard->now_available = true;
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t parse_daily(weather_dashboard_t *dashboard)
{
    cJSON *root = cJSON_Parse(response.buffer);
    cJSON *first = get_first_result(root);
    cJSON *daily = first == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(first, "daily");
    if (root == NULL || !cJSON_IsArray(daily)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int count = cJSON_GetArraySize(daily);
    if (count > WEATHER_FORECAST_DAYS) {
        count = WEATHER_FORECAST_DAYS;
    }
    for (int index = 0; index < count; index++) {
        cJSON *item = cJSON_GetArrayItem(daily, index);
        weather_daily_t *day = &dashboard->daily[index];
        json_copy(item, "date", day->date, sizeof(day->date));
        json_copy(item, "text_day", day->text_day, sizeof(day->text_day));
        json_copy(item, "code_day", day->code_day, sizeof(day->code_day));
        json_copy(item, "high", day->high, sizeof(day->high));
        json_copy(item, "low", day->low, sizeof(day->low));
        json_copy(item, "precip", day->precip, sizeof(day->precip));
        json_copy(item, "rainfall", day->rainfall, sizeof(day->rainfall));
        json_copy(item, "humidity", day->humidity, sizeof(day->humidity));
        json_copy(item, "wind_direction", day->wind_direction, sizeof(day->wind_direction));
        json_copy(item, "wind_direction_degree", day->wind_direction_degree,
                  sizeof(day->wind_direction_degree));
        json_copy(item, "wind_speed", day->wind_speed, sizeof(day->wind_speed));
        json_copy(item, "wind_scale", day->wind_scale, sizeof(day->wind_scale));
    }
    dashboard->daily_count = count;
    dashboard->daily_available = count > 0;
    cJSON_Delete(root);
    return dashboard->daily_available ? ESP_OK : ESP_FAIL;
}

static const char *open_meteo_aqi_quality(int aqi)
{
    if (aqi <= 50) return "优";
    if (aqi <= 100) return "良";
    if (aqi <= 150) return "轻度污染";
    if (aqi <= 200) return "中度污染";
    if (aqi <= 300) return "重度污染";
    return "严重污染";
}

static esp_err_t parse_open_meteo_air(weather_dashboard_t *dashboard)
{
    cJSON *root = cJSON_Parse(response.buffer);
    cJSON *current = root == NULL ? NULL : cJSON_GetObjectItemCaseSensitive(root, "current");
    if (root == NULL || !cJSON_IsObject(current)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    bool has_aqi = json_number_copy(current, "us_aqi", dashboard->aqi,
                                    sizeof(dashboard->aqi), 0);
    json_number_copy(current, "pm2_5", dashboard->pm25, sizeof(dashboard->pm25), 1);
    json_number_copy(current, "pm10", dashboard->pm10, sizeof(dashboard->pm10), 1);
    json_number_copy(current, "sulphur_dioxide", dashboard->so2, sizeof(dashboard->so2), 1);
    json_number_copy(current, "nitrogen_dioxide", dashboard->no2, sizeof(dashboard->no2), 1);
    json_number_copy(current, "carbon_monoxide", dashboard->co, sizeof(dashboard->co), 1);
    json_number_copy(current, "ozone", dashboard->o3, sizeof(dashboard->o3), 1);
    if (has_aqi) {
        int aqi = (int)strtol(dashboard->aqi, NULL, 10);
        strlcpy(dashboard->quality, open_meteo_aqi_quality(aqi), sizeof(dashboard->quality));
        strlcpy(dashboard->primary_pollutant, "OPEN-METEO / CAMS",
                sizeof(dashboard->primary_pollutant));
        dashboard->air_available = true;
    }
    cJSON_Delete(root);
    return dashboard->air_available ? ESP_OK : ESP_FAIL;
}

esp_err_t weather_fetch_dashboard(weather_dashboard_t *dashboard)
{
    if (dashboard == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(dashboard, 0, sizeof(*dashboard));

    esp_err_t now_result = weather_http_get("weather/now.json", "");
    if (now_result == ESP_OK) {
        now_result = parse_now(dashboard);
    }

    if (weather_http_get("weather/daily.json", "&start=0&days=3") == ESP_OK) {
        parse_daily(dashboard);
    }
    /* Open-Meteo needs no API key. Its global CAMS model is used here for Xiaogan. */
    if (open_meteo_air_http_get() == ESP_OK) {
        parse_open_meteo_air(dashboard);
    }

    return now_result;
}

const char *weather_condition_name(const char *code)
{
    if (code == NULL || code[0] == '\0') {
        return "N/A";
    }
    int value = atoi(code);
    if (value <= 3) return "SUNNY";
    if (value <= 8) return "CLOUDY";
    if (value == 9) return "OVERCAST";
    if (value >= 10 && value <= 20) return "RAIN";
    if (value >= 21 && value <= 25) return "SNOW";
    if (value >= 26 && value <= 29) return "DUST";
    if (value == 30) return "FOG";
    if (value == 31) return "HAZE";
    if (value >= 32 && value <= 36) return "WIND";
    if (value == 37) return "COLD";
    if (value == 38) return "HOT";
    return "UNKNOWN";
}

const char *weather_air_quality_name(const char *quality)
{
    if (quality == NULL || quality[0] == '\0') return "N/A";
    if (strcmp(quality, "优") == 0) return "EXCELLENT";
    if (strcmp(quality, "良") == 0) return "GOOD";
    if (strcmp(quality, "轻度污染") == 0) return "LIGHT POLLUTION";
    if (strcmp(quality, "中度污染") == 0) return "MODERATE";
    if (strcmp(quality, "重度污染") == 0) return "HEAVY";
    if (strcmp(quality, "严重污染") == 0) return "SEVERE";
    return "UNKNOWN";
}
