#include "lcd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// CHD-ESP32-S3-BOX V2.0 LCD interface, from the schematic.
#define LCD_HOST       SPI3_HOST
#define LCD_CS_GPIO    GPIO_NUM_5
#define LCD_DC_GPIO    GPIO_NUM_4
#define LCD_MOSI_GPIO  GPIO_NUM_6
#define LCD_SCLK_GPIO  GPIO_NUM_7
#define LCD_BACKLIGHT_GPIO GPIO_NUM_47

// CHD's 2.8-inch panel is driven as ILI9341-compatible in the supplied source.
#define LCD_H_RES      240
#define LCD_V_RES      320
#define LCD_PIXEL_CLK_HZ (40 * 1000 * 1000)

#define FONT_WIDTH     5
#define FONT_HEIGHT    7
#define FONT_SCALE     4

#define RGB565(r, g, b) \
    (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

static SemaphoreHandle_t color_transfer_done;
static esp_lcd_panel_handle_t panel_handle;
static uint16_t line_buffer[LCD_H_RES];
static uint16_t block_buffer[FONT_SCALE * FONT_SCALE];

// Vendor command sequence copied from the CHD ESP32-S3-BOX 2.8-inch example.
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
    SemaphoreHandle_t done = user_ctx;

    (void)panel_io;
    (void)edata;
    xSemaphoreGiveFromISR(done, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static esp_err_t draw_bitmap_sync(int x_start, int y_start,
                                  int x_end, int y_end,
                                  const uint16_t *pixels)
{
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, x_start, y_start,
                                              x_end, y_end, pixels);
    if (ret != ESP_OK) {
        return ret;
    }

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
        esp_err_t ret = draw_bitmap_sync(0, y, LCD_H_RES, y + 1, line_buffer);
        if (ret != ESP_OK) {
            return ret;
        }
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

// 5 x 7 glyphs used by the two greeting lines below.
static const uint8_t *glyph_for(char character)
{
    static const uint8_t glyph_c[FONT_HEIGHT] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t glyph_d[FONT_HEIGHT] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t glyph_e[FONT_HEIGHT] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t glyph_h[FONT_HEIGHT] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t glyph_l[FONT_HEIGHT] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t glyph_o[FONT_HEIGHT] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};

    switch (character) {
    case 'C': return glyph_c;
    case 'D': return glyph_d;
    case 'E': return glyph_e;
    case 'H': return glyph_h;
    case 'L': return glyph_l;
    case 'O': return glyph_o;
    default:  return NULL;
    }
}

static esp_err_t draw_text(int x, int y, const char *text, uint16_t color)
{
    const int character_width = (FONT_WIDTH + 1) * FONT_SCALE;

    for (int text_index = 0; text[text_index] != '\0'; text_index++) {
        const uint8_t *glyph = glyph_for(text[text_index]);
        if (glyph == NULL) {
            continue;
        }

        for (int row = 0; row < FONT_HEIGHT; row++) {
            for (int column = 0; column < FONT_WIDTH; column++) {
                if ((glyph[row] & (1U << (FONT_WIDTH - 1 - column))) != 0) {
                    esp_err_t ret = draw_block(x + text_index * character_width + column * FONT_SCALE,
                                               y + row * FONT_SCALE, color);
                    if (ret != ESP_OK) {
                        return ret;
                    }
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
                        "lcd", "Failed to create LCD SPI panel IO");

    esp_lcd_panel_io_callbacks_t callbacks = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(io_handle,
                                                                    &callbacks,
                                                                    color_transfer_done),
                        "lcd", "Failed to register LCD transfer callback");

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
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, false),
                        "lcd", "Failed to configure LCD color inversion");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(panel_handle, true),
                        "lcd", "Failed to set LCD orientation");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(panel_handle, false, false),
                        "lcd", "Failed to set LCD mirroring");
    return esp_lcd_panel_disp_on_off(panel_handle, true);
}

esp_err_t lcd_show_greeting(void)
{
    ESP_RETURN_ON_ERROR(fill_screen(RGB565(0x08, 0x1B, 0x3A)),
                        "lcd", "Failed to clear LCD");
    ESP_RETURN_ON_ERROR(draw_text(56, 110, "HELLO", RGB565(0xFF, 0xD6, 0x00)),
                        "lcd", "Failed to draw first greeting line");
    return draw_text(72, 160, "LCD", RGB565(0xFF, 0xFF, 0xFF));
}
