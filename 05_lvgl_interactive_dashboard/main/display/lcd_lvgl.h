#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

/** Initialize the CHD 2.8-inch ILI9341 display and attach it to LVGL. */
esp_err_t lcd_lvgl_init(lv_disp_t **display);

/** Set LCD backlight duty in the range 10 to 100 percent. */
esp_err_t lcd_backlight_set_brightness(uint8_t percent);
