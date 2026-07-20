#pragma once

#include "weather/weather.h"

/** Create the LVGL dashboard, clock timer, and automatic page rotation. */
void dashboard_create(void);

/** Update all dashboard pages. Call while the LVGL port lock is held. */
void dashboard_update(const weather_dashboard_t *weather);

/** Show a short application state message in the footer. Call while locked. */
void dashboard_set_status(const char *status);
