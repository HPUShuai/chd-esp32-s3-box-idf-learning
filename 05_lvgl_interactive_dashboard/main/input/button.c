#include "button.h"

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BUTTON_GPIO             GPIO_NUM_0
#define BUTTON_ACTIVE_LEVEL     0
#define BUTTON_DEBOUNCE_MS      30
#define BUTTON_DOUBLE_GAP_MS    350
#define BUTTON_LONG_PRESS_MS    850
#define BUTTON_TASK_STACK_SIZE  3072

static const char *TAG = "button";
static QueueHandle_t event_queue;
static TaskHandle_t button_task_handle;

static bool tick_reached(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void emit_event(button_event_t event)
{
    if (xQueueSend(event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Button event queue full; event %d dropped", (int)event);
    }
}

static void IRAM_ATTR button_gpio_isr(void *argument)
{
    (void)argument;
    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(button_task_handle, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void button_task(void *argument)
{
    (void)argument;

    bool stable_pressed = gpio_get_level(BUTTON_GPIO) == BUTTON_ACTIVE_LEVEL;
    bool short_press_pending = false;
    TickType_t press_started = xTaskGetTickCount();
    TickType_t short_press_deadline = 0;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait_ticks = portMAX_DELAY;
        if (short_press_pending) {
            wait_ticks = tick_reached(now, short_press_deadline) ? 0 : short_press_deadline - now;
        }

        uint32_t notified = ulTaskNotifyTake(pdTRUE, wait_ticks);
        now = xTaskGetTickCount();

        if (notified == 0) {
            if (short_press_pending && tick_reached(now, short_press_deadline)) {
                short_press_pending = false;
                emit_event(BUTTON_EVENT_SHORT_PRESS);
            }
            continue;
        }

        /* Wait until contact bounce has settled, then accept only a stable edge. */
        vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
        bool pressed = gpio_get_level(BUTTON_GPIO) == BUTTON_ACTIVE_LEVEL;
        if (pressed == stable_pressed) {
            continue;
        }
        stable_pressed = pressed;
        now = xTaskGetTickCount();

        if (pressed) {
            press_started = now;
            continue;
        }

        TickType_t held_ticks = now - press_started;
        if (held_ticks >= pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS)) {
            if (short_press_pending) {
                emit_event(BUTTON_EVENT_SHORT_PRESS);
                short_press_pending = false;
            }
            emit_event(BUTTON_EVENT_LONG_PRESS);
        } else if (short_press_pending && !tick_reached(now, short_press_deadline)) {
            short_press_pending = false;
            emit_event(BUTTON_EVENT_DOUBLE_PRESS);
        } else {
            if (short_press_pending) {
                emit_event(BUTTON_EVENT_SHORT_PRESS);
            }
            short_press_pending = true;
            short_press_deadline = now + pdMS_TO_TICKS(BUTTON_DOUBLE_GAP_MS);
        }
    }
}

esp_err_t button_init(void)
{
    event_queue = xQueueCreate(8, sizeof(button_event_t));
    ESP_RETURN_ON_FALSE(event_queue != NULL, ESP_ERR_NO_MEM, TAG, "Unable to create event queue");

    const gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&button_config), TAG, "GPIO0 configuration failed");

    BaseType_t task_created = xTaskCreate(button_task, "button", BUTTON_TASK_STACK_SIZE,
                                          NULL, 6, &button_task_handle);
    ESP_RETURN_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, TAG,
                        "Unable to create button task");

    esp_err_t result = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(BUTTON_GPIO, button_gpio_isr, NULL),
                        TAG, "GPIO0 ISR registration failed");

    ESP_LOGI(TAG, "BOOT button ready on GPIO0 (active low)");
    return ESP_OK;
}

bool button_wait_event(button_event_t *event, TickType_t timeout_ticks)
{
    return event != NULL && event_queue != NULL &&
           xQueueReceive(event_queue, event, timeout_ticks) == pdTRUE;
}
