#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef enum {
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_DOUBLE_PRESS,
    BUTTON_EVENT_LONG_PRESS,
} button_event_t;

/** Initialize the active-low BOOT button on GPIO0. */
esp_err_t button_init(void);

/** Wait for a debounced short, double, or long press event. */
bool button_wait_event(button_event_t *event, TickType_t timeout_ticks);
