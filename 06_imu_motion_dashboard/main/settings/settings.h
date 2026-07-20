#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool auto_rotate;
    uint8_t brightness;
    uint8_t page;
} app_settings_t;

/** Load saved settings, falling back to safe defaults when no data exists. */
esp_err_t settings_load(app_settings_t *settings);

/** Persist interaction settings in the NVS namespace "dashboard". */
esp_err_t settings_save(const app_settings_t *settings);
