#include "lcd_lvgl.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"

// CHD-ESP32-S3-BOX V2.0 LCD wiring, verified against the vendor board source.
#define LCD_HOST             SPI3_HOST
#define LCD_CS_GPIO          GPIO_NUM_5
#define LCD_DC_GPIO          GPIO_NUM_4
#define LCD_MOSI_GPIO        GPIO_NUM_6
#define LCD_SCLK_GPIO        GPIO_NUM_7
#define LCD_BACKLIGHT_GPIO   GPIO_NUM_47
#define LCD_LOGICAL_H_RES    320
#define LCD_LOGICAL_V_RES    240
#define LCD_PIXEL_CLK_HZ     (40 * 1000 * 1000)
#define LVGL_BUFFER_LINES    40

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

esp_err_t lcd_lvgl_init(lv_disp_t **display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_config_t backlight_config = {
        .pin_bit_mask = 1ULL << LCD_BACKLIGHT_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight_config), "display", "backlight GPIO failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(LCD_BACKLIGHT_GPIO, 1), "display", "backlight enable failed");

    const spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_SCLK_GPIO,
        .mosi_io_num = LCD_MOSI_GPIO,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_LOGICAL_H_RES * LVGL_BUFFER_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_config, SPI_DMA_CH_AUTO),
                        "display", "SPI bus init failed");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC_GPIO,
        .cs_gpio_num = LCD_CS_GPIO,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                                  &io_config, &io_handle),
                        "display", "LCD IO init failed");

    const ili9341_vendor_config_t vendor_config = {
        .init_cmds = ili9341_vendor_init,
        .init_cmds_size = sizeof(ili9341_vendor_init) / sizeof(ili9341_vendor_init[0]),
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle),
                        "display", "ILI9341 driver init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel_handle), "display", "LCD reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel_handle), "display", "LCD init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel_handle, false), "display", "color config failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel_handle, true), "display", "display enable failed");

    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_config), "display", "LVGL port init failed");

    const lvgl_port_display_cfg_t display_config = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_LOGICAL_H_RES * LVGL_BUFFER_LINES,
        .double_buffer = true,
        .hres = LCD_LOGICAL_H_RES,
        .vres = LCD_LOGICAL_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
        },
    };
    *display = lvgl_port_add_disp(&display_config);
    return *display == NULL ? ESP_FAIL : ESP_OK;
}
