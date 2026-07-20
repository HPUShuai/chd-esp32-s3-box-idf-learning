#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define WEATHER_FORECAST_DAYS 3

typedef struct {
    char date[16];
    char text_day[32];
    char code_day[8];
    char high[12];
    char low[12];
    char precip[12];
    char rainfall[12];
    char humidity[12];
    char wind_direction[24];
    char wind_direction_degree[12];
    char wind_speed[12];
    char wind_scale[12];
} weather_daily_t;

typedef struct {
    char location[48];
    char text[32];
    char code[8];
    char temperature[12];
    char feels_like[12];
    char pressure[12];
    char humidity[12];
    char visibility[12];
    char wind_direction[24];
    char wind_direction_degree[12];
    char wind_speed[12];
    char wind_scale[12];
    char clouds[12];
    char dew_point[12];
    char last_update[40];
    bool now_available;

    weather_daily_t daily[WEATHER_FORECAST_DAYS];
    size_t daily_count;
    bool daily_available;

    char aqi[12];
    char pm25[12];
    char pm10[12];
    char so2[12];
    char no2[12];
    char co[12];
    char o3[12];
    char quality[24];
    char primary_pollutant[24];
    bool air_available;
} weather_dashboard_t;

/** Fetch Seniverse weather data and key-free Open-Meteo air-quality data. */
esp_err_t weather_fetch_dashboard(weather_dashboard_t *dashboard);

/** Convert a Seniverse weather code to a compact Latin label for the built-in LVGL font. */
const char *weather_condition_name(const char *code);

/** Convert an AQI class to a compact Latin label for screens without Chinese fonts. */
const char *weather_air_quality_name(const char *quality);
