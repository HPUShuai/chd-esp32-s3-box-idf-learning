#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef struct {
    char location[64];
    char text[64];
    char code[16];
    char temperature[16];
    char last_update[40];
} weather_now_t;

/** Query Seniverse weather/now.json using the credentials in private_config.h. */
esp_err_t weather_fetch_now(weather_now_t *weather);
