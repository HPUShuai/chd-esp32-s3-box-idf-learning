#include "lcd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// CHD-ESP32-S3-BOX V2.0 LCD interface, verified against the supplied CHD source.
#define LCD_HOST             SPI3_HOST
#define LCD_CS_GPIO          GPIO_NUM_5
#define LCD_DC_GPIO          GPIO_NUM_4
#define LCD_MOSI_GPIO        GPIO_NUM_6
#define LCD_SCLK_GPIO        GPIO_NUM_7
#define LCD_BACKLIGHT_GPIO   GPIO_NUM_47
#define LCD_H_RES            240
#define LCD_V_RES            320
#define LCD_PIXEL_CLK_HZ     (40 * 1000 * 1000)

#define FONT_WIDTH           5
#define FONT_HEIGHT          7
#define FONT_SCALE           4
#define LCD_BACKGROUND_COLOR RGB565(0x08, 0x1B, 0x3A)
#define TIME_TEXT_X          24
#define TIME_TEXT_Y          84
#define TIME_TEXT_WIDTH      (8 * (FONT_WIDTH + 1) * FONT_SCALE)
#define TIME_TEXT_HEIGHT     (FONT_HEIGHT * FONT_SCALE)

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

static SemaphoreHandle_t color_transfer_done;
static esp_lcd_panel_handle_t panel_handle;
static uint16_t line_buffer[LCD_H_RES];
static uint16_t block_buffer[FONT_SCALE * FONT_SCALE];

static const ili9341_lcd_init_cmd_t ili9341_vendor_init[] = {
    {0xC8, (uint8_t[]){0xFF, 0x93, 0x42}, 3, 0},
    {0xC0, (uint8_t[]){0x0E, 0x0E}, 2, 0},
    {0xC5, (uint8_t[]){0xD0}, 1, 0},
    {0xC1, (uint8_t[]){0x02}, 1, 0},
    {0xB4, (uint8_t[]){0x02}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x03, 0x08, 0x06, 0x13, 0x09, 0x39, 0x39,
                        0x48, 0x02, 0x0A, 0x08, 0x17, 0x17, 0x0F}, 15, 0},
    {0xE1, (uint8_t[]){0x00, 0x28, 0x29, 0x01, 0x0D, 0x03, 0x3F, 0x33,
                        0x52, 0x04, 0x0F, 0x0E, 0x37, 0x38, 0x0F}, 15, 0},
    {0xB1, (uint8_t[]){0x00, 0x1B}, 2, 0},
    {0x36, (uint8_t[]){0x08}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0xB7, (uint8_t[]){0x06}, 1, 0},
    {0x11, (uint8_t[]){0}, 0x80, 0},
    {0x29, (uint8_t[]){0}, 0x80, 0},
};

static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;
    (void)panel_io;
    (void)edata;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static esp_err_t draw_bitmap_sync(int x_start, int y_start, int x_end, int y_end,
                                  const uint16_t *pixels)
{
    ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start,
                                                   x_end, y_end, pixels),
                        "lcd", "Failed to transfer LCD pixels");
    return xSemaphoreTake(color_transfer_done, portMAX_DELAY) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static esp_err_t fill_screen(uint16_t color)
{
    for (int x = 0; x < LCD_H_RES; x++) {
        line_buffer[x] = color;
    }
    for (int y = 0; y < LCD_V_RES; y++) {
        ESP_RETURN_ON_ERROR(draw_bitmap_sync(0, y, LCD_H_RES, y + 1, line_buffer),
                            "lcd", "Failed to clear LCD");
    }
    return ESP_OK;
}

static esp_err_t fill_rectangle(int x_start, int y_start, int width, int height,
                                uint16_t color)
{
    for (int x = 0; x < width; x++) {
        line_buffer[x] = color;
    }
    for (int y = y_start; y < y_start + height; y++) {
        ESP_RETURN_ON_ERROR(draw_bitmap_sync(x_start, y, x_start + width, y + 1,
                                              line_buffer),
                            "lcd", "Failed to clear LCD region");
    }
    return ESP_OK;
}

static esp_err_t draw_block(int x, int y, uint16_t color)
{
    for (int i = 0; i < FONT_SCALE * FONT_SCALE; i++) {
        block_buffer[i] = color;
    }
    return draw_bitmap_sync(x, y, x + FONT_SCALE, y + FONT_SCALE, block_buffer);
}

static const uint8_t *glyph_for(char character)
{
    static const uint8_t glyph_c[FONT_HEIGHT] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t glyph_d[FONT_HEIGHT] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t glyph_e[FONT_HEIGHT] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t glyph_i[FONT_HEIGHT] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t glyph_m[FONT_HEIGHT] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t glyph_o[FONT_HEIGHT] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_p[FONT_HEIGHT] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t glyph_t[FONT_HEIGHT] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t glyph_0[FONT_HEIGHT] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t glyph_1[FONT_HEIGHT] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t glyph_2[FONT_HEIGHT] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t glyph_3[FONT_HEIGHT] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t glyph_4[FONT_HEIGHT] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t glyph_5[FONT_HEIGHT] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    static const uint8_t glyph_6[FONT_HEIGHT] = {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_7[FONT_HEIGHT] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t glyph_8[FONT_HEIGHT] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t glyph_9[FONT_HEIGHT] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
    static const uint8_t glyph_colon[FONT_HEIGHT] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};

    switch (character) {
    case 'C': return glyph_c;
    case 'D': return glyph_d;
    case 'E': return glyph_e;
    case 'I': return glyph_i;
    case 'M': return glyph_m;
    case 'O': return glyph_o;
    case 'P': return glyph_p;
    case 'T': return glyph_t;
    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;
    case ':': return glyph_colon;
    default: return NULL;
    }
}

static esp_err_t draw_text(int x, int y, const char *text, uint16_t color)
{
    const int character_width = (FONT_WIDTH + 1) * FONT_SCALE;
    for (int index = 0; text[index] != '\0'; index++) {
        const uint8_t *glyph = glyph_for(text[index]);
        if (glyph == NULL) {
            continue;
        }
        for (int row = 0; row < FONT_HEIGHT; row++) {
            for (int column = 0; column < FONT_WIDTH; column++) {
                if (glyph[row] & (1U << (FONT_WIDTH - 1 - column))) {
                    ESP_RETURN_ON_ERROR(draw_block(x + index * character_width + column * FONT_SCALE,
                                                   y + row * FONT_SCALE, color),
                                        "lcd", "Failed to draw text");
                }
            }
        }
    }
    return ESP_OK;
}

esp_err_t lcd_init(void)
{
    gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << LCD_BACKLIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_config), "lcd", "Failed to configure backlight");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_BACKLIGHT_GPIO, 1), "lcd", "Failed to enable backlight");

    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_SCLK_GPIO,
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        "lcd", "Failed to initialize LCD SPI bus");

    color_transfer_done = xSemaphoreCreateBinary();
    if (color_transfer_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC_GPIO,
        .cs_gpio_num = LCD_CS_GPIO,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 1,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                                  &io_config, &io_handle),
                        "lcd", "Failed to create LCD panel IO");

    esp_lcd_panel_io_callbacks_t callbacks = {.on_color_trans_done = on_color_trans_done};
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io_handle, &callbacks,
                                                                    color_transfer_done),
                        "lcd", "Failed to register LCD callback");

    const ili9341_vendor_config_t vendor_config = {
        .init_cmds = ili9341_vendor_init,
        .init_cmds_size = sizeof(ili9341_vendor_init) / sizeof(ili9341_vendor_init[0]),
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle),
                        "lcd", "Failed to create ILI9341 panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), "lcd", "Failed to reset LCD");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), "lcd", "Failed to initialize LCD");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, false), "lcd", "Failed to configure colors");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_handle, true), "lcd", "Failed to set orientation");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, false, false), "lcd", "Failed to set mirroring");
    return esp_lcd_panel_disp_on_off(panel_handle, true);
}

esp_err_t lcd_show_network_info(const char *time_text,
                                const char *temperature_text,
                                const char *weather_code)
{
    ESP_RETURN_ON_ERROR(fill_screen(LCD_BACKGROUND_COLOR), "lcd", "Failed to clear LCD");
    ESP_RETURN_ON_ERROR(draw_text(24, 48, "TIME", RGB565(0xFF, 0xD6, 0x00)), "lcd", "Failed to draw label");
    ESP_RETURN_ON_ERROR(draw_text(TIME_TEXT_X, TIME_TEXT_Y, time_text, RGB565(0xFF, 0xFF, 0xFF)), "lcd", "Failed to draw time");
    ESP_RETURN_ON_ERROR(draw_text(24, 142, "TEMP", RGB565(0xFF, 0xD6, 0x00)), "lcd", "Failed to draw label");
    ESP_RETURN_ON_ERROR(draw_text(24, 178, temperature_text, RGB565(0xFF, 0xFF, 0xFF)), "lcd", "Failed to draw temperature");
    ESP_RETURN_ON_ERROR(draw_text(24, 232, "CODE", RGB565(0xFF, 0xD6, 0x00)), "lcd", "Failed to draw label");
    return draw_text(24, 268, weather_code, RGB565(0xFF, 0xFF, 0xFF));
}

esp_err_t lcd_update_time(const char *time_text)
{
    ESP_RETURN_ON_ERROR(fill_rectangle(TIME_TEXT_X, TIME_TEXT_Y,
                                       TIME_TEXT_WIDTH, TIME_TEXT_HEIGHT,
                                       LCD_BACKGROUND_COLOR),
                        "lcd", "Failed to clear previous time");
    return draw_text(TIME_TEXT_X, TIME_TEXT_Y, time_text, RGB565(0xFF, 0xFF, 0xFF));
}
