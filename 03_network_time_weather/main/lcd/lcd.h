#pragma once

#include "esp_err.h"

/** Initialize the CHD 2.8-inch ILI9341-compatible LCD and enable its backlight. */
esp_err_t lcd_init(void);

/** Draw the synchronized time, temperature, and Seniverse weather code. */
esp_err_t lcd_show_network_info(const char *time_text,
                                const char *temperature_text,
                                const char *weather_code);

/** Refresh only the time digits, without clearing the rest of the screen. */
esp_err_t lcd_update_time(const char *time_text);
