#include "esp_log.h"
#include "lcd/lcd.h"

static const char *TAG = "lcd_hello";

void app_main(void)
{
    ESP_ERROR_CHECK(lcd_init());
    ESP_ERROR_CHECK(lcd_show_greeting());
    ESP_LOGI(TAG, "LCD initialized: HELLO LCD is displayed");
}
