#pragma once

#include "esp_err.h"
#include "lvgl.h"

/** Initialize the CHD 2.8-inch ILI9341 display and attach it to LVGL. */
esp_err_t lcd_lvgl_init(lv_disp_t **display);
