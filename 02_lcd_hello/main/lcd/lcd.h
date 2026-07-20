#pragma once

#include "esp_err.h"

/** Initialize the 2.8-inch ILI9341-compatible LCD used by CHD-ESP32-S3-BOX V2.0. */
esp_err_t lcd_init(void);

/** Clear the LCD and draw the demo greeting. Call after lcd_init(). */
esp_err_t lcd_show_greeting(void);
