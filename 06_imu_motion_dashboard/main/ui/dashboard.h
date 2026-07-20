#pragma once

#include <stdint.h>

#include "imu/motion.h"
#include "weather/weather.h"

/** Create the LVGL dashboard, clock timer, and automatic page rotation. */
void dashboard_create(void);

/** Update all dashboard pages. Call while the LVGL port lock is held. */
void dashboard_update(const weather_dashboard_t *weather);

/** Show a short application state message in the footer. Call while locked. */
void dashboard_set_status(const char *status);

/** Select a dashboard page (0 to 3). Call while the LVGL port lock is held. */
void dashboard_show_page(uint8_t page);

/** Advance to the next dashboard page. Call while the LVGL port lock is held. */
void dashboard_show_next_page(void);

/** Return the currently visible page index. Call while the LVGL port lock is held. */
uint8_t dashboard_get_page(void);

/** Enable or disable the eight-second automatic rotation. Call while locked. */
void dashboard_set_auto_rotate(bool enabled);

/** Update the IMU page and append one point to its roll/pitch chart. Call while locked. */
void dashboard_update_imu(const imu_motion_data_t *motion);
