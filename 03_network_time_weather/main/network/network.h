#pragma once

#include "esp_err.h"

/** Connect to the Wi-Fi network configured in private_config.h. */
esp_err_t network_connect_wifi(void);

/** Synchronize local time through SNTP and set the timezone to China Standard Time. */
esp_err_t network_sync_beijing_time(void);
