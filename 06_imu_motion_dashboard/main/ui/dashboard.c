#include "dashboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

extern const lv_font_t weather_cn_font;

/* Compact landscape dashboard for the 320 x 240 ILI9341 panel. */
#define FRAME_X  3
#define FRAME_Y  3
#define FRAME_W  314
#define FRAME_H  234
#define PAGE_Y   31
#define PAGE_H   171
#define PAGE_COUNT 4

#define COLOR_BG          0x020617
#define COLOR_CARD        0x0F172A
#define COLOR_CARD_ALT    0x172554
#define COLOR_CARD_CYAN   0x082F49
#define COLOR_CARD_GREEN  0x052E2B
#define COLOR_CARD_ORANGE 0x3B2206
#define COLOR_CARD_PURPLE 0x2E1065
#define COLOR_BORDER      0x334155
#define COLOR_TEXT        0xF8FAFC
#define COLOR_MUTED       0x94A3B8
#define COLOR_CYAN        0x00E5FF
#define COLOR_BLUE        0x2979FF
#define COLOR_GREEN       0x00E676
#define COLOR_YELLOW      0xFFD600
#define COLOR_ORANGE      0xFF9100
#define COLOR_PURPLE      0xB388FF
#define COLOR_PINK        0xFF4081
#define COLOR_RED         0xFF1744

static lv_obj_t *pages[PAGE_COUNT];
static lv_obj_t *clock_label;
static lv_obj_t *footer_status_label;
static lv_obj_t *page_label;
static lv_obj_t *temperature_label;
static lv_obj_t *weather_label;
static lv_obj_t *update_label;
static lv_obj_t *overview_values[4];
static lv_obj_t *forecast_day_labels[WEATHER_FORECAST_DAYS];
static lv_obj_t *forecast_weather_labels[WEATHER_FORECAST_DAYS];
static lv_obj_t *forecast_temperature_labels[WEATHER_FORECAST_DAYS];
static lv_obj_t *forecast_detail_labels[WEATHER_FORECAST_DAYS];
static lv_obj_t *air_quality_label;
static lv_obj_t *aqi_label;
static lv_obj_t *air_values[7];
static lv_obj_t *detail_values[2];
static lv_obj_t *imu_header_label;
static lv_obj_t *imu_attitude_values[3];
static lv_obj_t *imu_accel_label;
static lv_obj_t *imu_gyro_label;
static lv_obj_t *imu_chart;
static lv_chart_series_t *imu_roll_series;
static lv_chart_series_t *imu_pitch_series;
static uint8_t current_page;
static bool auto_rotate = false;

static const char *value_or_empty(const char *value)
{
    return value != NULL && value[0] != '\0' ? value : "--";
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                            lv_text_align_t align, int x, int y, int width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

static lv_obj_t *make_chinese_label(lv_obj_t *parent, lv_color_t color,
                                    lv_text_align_t align, int x, int y, int width)
{
    return make_label(parent, &weather_cn_font, color, align, x, y, width);
}

static void make_solid(lv_obj_t *obj, uint32_t color)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_NONE, 0);
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *card = lv_obj_create(parent);
    make_solid(card, COLOR_CARD);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    return card;
}

static lv_obj_t *make_accent(lv_obj_t *parent, int x, int y, int width, int height,
                             uint32_t color)
{
    lv_obj_t *accent = lv_obj_create(parent);
    make_solid(accent, color);
    lv_obj_set_pos(accent, x, y);
    lv_obj_set_size(accent, width, height);
    lv_obj_set_style_radius(accent, 2, 0);
    lv_obj_clear_flag(accent, LV_OBJ_FLAG_SCROLLABLE);
    return accent;
}

static void set_aqi_badge_color(const char *aqi_text)
{
    long aqi = aqi_text == NULL ? 0 : strtol(aqi_text, NULL, 10);
    uint32_t color = COLOR_GREEN;
    if (aqi > 200) {
        color = COLOR_RED;
    } else if (aqi > 150) {
        color = COLOR_PINK;
    } else if (aqi > 100) {
        color = COLOR_ORANGE;
    } else if (aqi > 50) {
        color = COLOR_YELLOW;
    }
    lv_obj_set_style_text_color(aqi_label, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(aqi_label, lv_color_hex(color), 0);
}

static void show_page(uint8_t index)
{
    current_page = index % PAGE_COUNT;
    for (size_t page = 0; page < PAGE_COUNT; page++) {
        if (page == current_page) {
            lv_obj_clear_flag(pages[page], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pages[page], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_label_set_text_fmt(page_label, "%c %d/%d", auto_rotate ? 'A' : 'M',
                          current_page + 1, PAGE_COUNT);
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    time_t now = 0;
    struct tm time_info = {0};
    char time_text[16];
    time(&now);
    localtime_r(&now, &time_info);
    strftime(time_text, sizeof(time_text), "%H:%M:%S", &time_info);
    lv_label_set_text(clock_label, time_text);
}

static void page_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (auto_rotate) {
        show_page(current_page + 1);
    }
}

static void create_overview_page(lv_obj_t *page)
{
    lv_obj_t *hero = make_card(page, 8, 5, 112, 160);
    lv_obj_set_style_bg_color(hero, lv_color_hex(COLOR_CARD_ALT), 0);
    lv_obj_set_style_bg_grad_color(hero, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_grad_dir(hero, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_color(hero, lv_color_hex(COLOR_BLUE), 0);
    make_accent(hero, 0, 20, 3, 46, COLOR_CYAN);

    lv_obj_t *city = make_chinese_label(hero, lv_color_hex(COLOR_MUTED),
                                        LV_TEXT_ALIGN_LEFT, 10, 9, 88);
    lv_label_set_text(city, "孝感 · 当前");
    temperature_label = make_label(hero, &lv_font_montserrat_28, lv_color_hex(COLOR_TEXT),
                                   LV_TEXT_ALIGN_LEFT, 10, 39, 62);
    lv_obj_t *unit = make_chinese_label(hero, lv_color_hex(COLOR_ORANGE),
                                        LV_TEXT_ALIGN_LEFT, 73, 51, 28);
    lv_label_set_text(unit, "℃");
    weather_label = make_chinese_label(hero, lv_color_hex(COLOR_CYAN),
                                       LV_TEXT_ALIGN_LEFT, 10, 84, 92);
    update_label = make_chinese_label(hero, lv_color_hex(COLOR_MUTED),
                                      LV_TEXT_ALIGN_LEFT, 10, 126, 92);

    /* The configured Seniverse product provides current temperature and a
     * three-day forecast.  The latter supplies humidity, wind and rainfall. */
    static const char *titles[] = {"湿度", "风速", "降雨", "风力"};
    static const uint32_t accents[] = {COLOR_CYAN, COLOR_GREEN, COLOR_ORANGE, COLOR_PURPLE};
    static const uint32_t card_colors[] = {COLOR_CARD_CYAN, COLOR_CARD_GREEN,
                                           COLOR_CARD_ORANGE, COLOR_CARD_PURPLE};
    for (int index = 0; index < 4; index++) {
        int x = 126 + (index % 2) * 91;
        int y = 5 + (index / 2) * 81;
        lv_obj_t *card = make_card(page, x, y, 86, 79);
        lv_obj_set_style_bg_color(card, lv_color_hex(card_colors[index]), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(accents[index]), 0);
        make_accent(card, 8, 31, 26, 2, accents[index]);
        lv_obj_t *title = make_chinese_label(card, lv_color_hex(COLOR_MUTED),
                                             LV_TEXT_ALIGN_LEFT, 8, 7, 62);
        lv_label_set_text(title, titles[index]);
        overview_values[index] = make_label(card, &lv_font_montserrat_12,
                                            lv_color_hex(accents[index]), LV_TEXT_ALIGN_LEFT,
                                            8, 48, 74);
    }
}

static void create_forecast_page(lv_obj_t *page)
{
    make_accent(page, 8, 5, 3, 18, COLOR_CYAN);
    lv_obj_t *title = make_chinese_label(page, lv_color_hex(COLOR_TEXT),
                                         LV_TEXT_ALIGN_LEFT, 17, 3, 120);
    lv_label_set_text(title, "未来三日");
    lv_obj_t *legend = make_label(page, &lv_font_montserrat_12, lv_color_hex(COLOR_MUTED),
                                  LV_TEXT_ALIGN_RIGHT, 172, 6, 132);
    lv_label_set_text(legend, "TEMP     HUM / RAIN");

    static const uint32_t row_accents[] = {COLOR_CYAN, COLOR_PURPLE, COLOR_ORANGE};
    for (int index = 0; index < WEATHER_FORECAST_DAYS; index++) {
        lv_obj_t *card = make_card(page, 8, 30 + index * 46, 296, 41);
        lv_obj_set_style_border_color(card, lv_color_hex(row_accents[index]), 0);
        make_accent(card, 0, 7, 3, 27, row_accents[index]);
        forecast_day_labels[index] = make_chinese_label(card, lv_color_hex(row_accents[index]),
                                                        LV_TEXT_ALIGN_LEFT, 10, 9, 40);
        forecast_weather_labels[index] = make_chinese_label(card, lv_color_hex(COLOR_TEXT),
                                                            LV_TEXT_ALIGN_LEFT, 54, 9, 56);
        /* The custom font is intentionally used for ℃ and all punctuation. */
        forecast_temperature_labels[index] = make_chinese_label(card, lv_color_hex(COLOR_TEXT),
                                                                 LV_TEXT_ALIGN_LEFT, 116, 9, 70);
        forecast_detail_labels[index] = make_chinese_label(card, lv_color_hex(COLOR_MUTED),
                                                            LV_TEXT_ALIGN_RIGHT, 188, 9, 98);
    }
}

static void create_detail_page(lv_obj_t *page)
{
    make_accent(page, 8, 5, 3, 18, COLOR_CYAN);
    air_quality_label = make_chinese_label(page, lv_color_hex(COLOR_TEXT),
                                           LV_TEXT_ALIGN_LEFT, 17, 3, 205);
    aqi_label = make_label(page, &lv_font_montserrat_12, lv_color_hex(COLOR_GREEN),
                           LV_TEXT_ALIGN_CENTER, 230, 2, 74);
    lv_obj_set_style_bg_color(aqi_label, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(aqi_label, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(aqi_label, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_border_width(aqi_label, 1, 0);
    lv_obj_set_style_radius(aqi_label, 7, 0);
    lv_obj_set_style_pad_top(aqi_label, 2, 0);

    lv_obj_t *air_card = make_card(page, 8, 30, 296, 82);
    lv_obj_set_style_bg_color(air_card, lv_color_hex(COLOR_CARD_CYAN), 0);
    lv_obj_set_style_border_color(air_card, lv_color_hex(COLOR_CYAN), 0);
    for (int index = 0; index < 6; index++) {
        int x = 10 + (index % 3) * 94;
        int y = 8 + (index / 3) * 28;
        air_values[index] = make_label(air_card, &lv_font_montserrat_12,
                                       index < 2 ? lv_color_hex(COLOR_TEXT) : lv_color_hex(COLOR_MUTED),
                                       LV_TEXT_ALIGN_LEFT, x, y, 88);
    }
    air_values[6] = make_label(air_card, &lv_font_montserrat_12, lv_color_hex(COLOR_BLUE),
                               LV_TEXT_ALIGN_LEFT, 10, 61, 276);

    lv_obj_t *weather_card = make_card(page, 8, 118, 296, 48);
    lv_obj_set_style_bg_color(weather_card, lv_color_hex(COLOR_CARD_PURPLE), 0);
    lv_obj_set_style_border_color(weather_card, lv_color_hex(COLOR_PURPLE), 0);
    detail_values[0] = make_chinese_label(weather_card, lv_color_hex(COLOR_TEXT),
                                          LV_TEXT_ALIGN_LEFT, 8, 3, 280);
    detail_values[1] = make_chinese_label(weather_card, lv_color_hex(COLOR_MUTED),
                                          LV_TEXT_ALIGN_LEFT, 8, 26, 280);
}

static void create_imu_page(lv_obj_t *page)
{
    make_accent(page, 8, 5, 3, 18, COLOR_GREEN);
    lv_obj_t *title = make_chinese_label(page, lv_color_hex(COLOR_TEXT),
                                         LV_TEXT_ALIGN_LEFT, 17, 3, 78);
    lv_label_set_text(title, "实时姿态");
    imu_header_label = make_chinese_label(page, lv_color_hex(COLOR_MUTED),
                                          LV_TEXT_ALIGN_RIGHT, 92, 3, 212);
    lv_label_set_text(imu_header_label, "ICM42607 · 等待校准");

    lv_obj_t *attitude_card = make_card(page, 8, 30, 96, 136);
    lv_obj_set_style_bg_color(attitude_card, lv_color_hex(COLOR_CARD_GREEN), 0);
    lv_obj_set_style_border_color(attitude_card, lv_color_hex(COLOR_GREEN), 0);
    static const char *titles[] = {"横滚角", "俯仰角", "运动强度"};
    static const uint32_t colors[] = {COLOR_CYAN, COLOR_PURPLE, COLOR_ORANGE};
    static const int title_y[] = {4, 48, 92};
    for (size_t index = 0; index < 3; ++index) {
        lv_obj_t *metric_title = make_chinese_label(attitude_card,
                                                     lv_color_hex(COLOR_MUTED),
                                                     LV_TEXT_ALIGN_LEFT, 8,
                                                     title_y[index], 80);
        lv_label_set_text(metric_title, titles[index]);
        imu_attitude_values[index] = make_chinese_label(attitude_card,
                                                         lv_color_hex(colors[index]),
                                                         LV_TEXT_ALIGN_LEFT, 8,
                                                         title_y[index] + 21, 80);
        lv_label_set_text(imu_attitude_values[index], "--");
    }

    imu_chart = lv_chart_create(page);
    lv_obj_set_pos(imu_chart, 110, 30);
    lv_obj_set_size(imu_chart, 194, 82);
    lv_obj_clear_flag(imu_chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(imu_chart, lv_color_hex(COLOR_CARD_ALT), 0);
    lv_obj_set_style_bg_opa(imu_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(imu_chart, lv_color_hex(COLOR_BLUE), 0);
    lv_obj_set_style_border_width(imu_chart, 1, 0);
    lv_obj_set_style_radius(imu_chart, 6, 0);
    lv_obj_set_style_pad_all(imu_chart, 5, 0);
    lv_obj_set_style_line_color(imu_chart, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_line_opa(imu_chart, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_line_width(imu_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(imu_chart, 0, LV_PART_INDICATOR);
    lv_chart_set_type(imu_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(imu_chart, 48);
    lv_chart_set_range(imu_chart, LV_CHART_AXIS_PRIMARY_Y, -90, 90);
    lv_chart_set_div_line_count(imu_chart, 5, 7);
    imu_roll_series = lv_chart_add_series(imu_chart, lv_color_hex(COLOR_CYAN),
                                           LV_CHART_AXIS_PRIMARY_Y);
    imu_pitch_series = lv_chart_add_series(imu_chart, lv_color_hex(COLOR_PURPLE),
                                            LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(imu_chart, imu_roll_series, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(imu_chart, imu_pitch_series, LV_CHART_POINT_NONE);

    lv_obj_t *roll_legend = make_label(imu_chart, &lv_font_montserrat_12,
                                       lv_color_hex(COLOR_CYAN), LV_TEXT_ALIGN_LEFT,
                                       5, 2, 42);
    lv_label_set_text(roll_legend, "ROLL");
    lv_obj_t *pitch_legend = make_label(imu_chart, &lv_font_montserrat_12,
                                        lv_color_hex(COLOR_PURPLE), LV_TEXT_ALIGN_LEFT,
                                        48, 2, 45);
    lv_label_set_text(pitch_legend, "PITCH");

    lv_obj_t *vector_card = make_card(page, 110, 118, 194, 48);
    lv_obj_set_style_bg_color(vector_card, lv_color_hex(COLOR_CARD_CYAN), 0);
    lv_obj_set_style_border_color(vector_card, lv_color_hex(COLOR_CYAN), 0);
    imu_accel_label = make_label(vector_card, &lv_font_montserrat_12,
                                 lv_color_hex(COLOR_TEXT), LV_TEXT_ALIGN_LEFT,
                                 7, 4, 180);
    imu_gyro_label = make_label(vector_card, &lv_font_montserrat_12,
                                lv_color_hex(COLOR_MUTED), LV_TEXT_ALIGN_LEFT,
                                7, 26, 180);
    lv_label_set_text(imu_accel_label, "ACC  X --  Y --  Z --");
    lv_label_set_text(imu_gyro_label, "GYR  X --  Y --  Z --");
}

void dashboard_create(void)
{
    lv_obj_t *screen = lv_scr_act();
    make_solid(screen, COLOR_BG);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *frame = lv_obj_create(screen);
    make_solid(frame, COLOR_BG);
    lv_obj_set_pos(frame, FRAME_X, FRAME_Y);
    lv_obj_set_size(frame, FRAME_W, FRAME_H);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(frame, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_border_width(frame, 1, 0);
    lv_obj_set_style_radius(frame, 7, 0);
    lv_obj_set_style_pad_all(frame, 0, 0);

    make_accent(frame, 8, 7, 3, 17, COLOR_CYAN);
    lv_obj_t *brand = make_chinese_label(frame, lv_color_hex(COLOR_TEXT),
                                         LV_TEXT_ALIGN_LEFT, 17, 5, 180);
    lv_label_set_text(brand, "孝感 · 天气仪表");
    clock_label = make_label(frame, &lv_font_montserrat_12, lv_color_hex(COLOR_TEXT),
                             LV_TEXT_ALIGN_RIGHT, 230, 7, 70);
    lv_label_set_text(clock_label, "--:--:--");

    for (int index = 0; index < PAGE_COUNT; index++) {
        pages[index] = lv_obj_create(frame);
        lv_obj_remove_style_all(pages[index]);
        lv_obj_set_pos(pages[index], 0, PAGE_Y);
        lv_obj_set_size(pages[index], FRAME_W, PAGE_H);
        lv_obj_clear_flag(pages[index], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(pages[index], LV_OPA_TRANSP, 0);
    }
    create_overview_page(pages[0]);
    create_forecast_page(pages[1]);
    create_detail_page(pages[2]);
    create_imu_page(pages[3]);

    lv_obj_t *footer_line = lv_obj_create(frame);
    make_solid(footer_line, COLOR_BORDER);
    lv_obj_set_pos(footer_line, 8, 203);
    lv_obj_set_size(footer_line, 296, 1);
    lv_obj_set_style_border_width(footer_line, 0, 0);
    footer_status_label = make_chinese_label(frame, lv_color_hex(COLOR_MUTED),
                                             LV_TEXT_ALIGN_LEFT, 12, 208, 224);
    page_label = make_label(frame, &lv_font_montserrat_12, lv_color_hex(COLOR_CYAN),
                            LV_TEXT_ALIGN_RIGHT, 252, 211, 48);

    dashboard_set_status("系统启动中");
    show_page(0);
    lv_timer_create(clock_timer_cb, 1000, NULL);
    lv_timer_create(page_timer_cb, 8000, NULL);
    clock_timer_cb(NULL);
}

void dashboard_set_status(const char *status)
{
    lv_label_set_text(footer_status_label, status == NULL ? "无数据" : status);
}

void dashboard_show_page(uint8_t page)
{
    show_page(page);
}

void dashboard_show_next_page(void)
{
    show_page(current_page + 1);
}

uint8_t dashboard_get_page(void)
{
    return current_page;
}

void dashboard_set_auto_rotate(bool enabled)
{
    auto_rotate = enabled;
    show_page(current_page);
}

void dashboard_update_imu(const imu_motion_data_t *motion)
{
    if (motion == NULL || imu_chart == NULL) {
        return;
    }

    const char *model = motion->who_am_i == ICM42607P_WHO_AM_I_VALUE ?
                        "ICM42607-P" : "ICM42607-C";
    char header_text[64];
    char roll_text[16];
    char pitch_text[16];
    char intensity_text[16];
    char accel_text[64];
    char gyro_text[64];

    /* LVGL's compact printf implementation has floating-point formatting
     * disabled in this build. Use newlib snprintf so later %s arguments are
     * not misread after a float and the label always receives plain text. */
    snprintf(header_text, sizeof(header_text), "%s · %.1f℃ · %s",
             model, motion->temperature_c,
             motion->calibrated ? "已校准" : "未校准");
    snprintf(roll_text, sizeof(roll_text), "%+.1f°", motion->roll_deg);
    snprintf(pitch_text, sizeof(pitch_text), "%+.1f°", motion->pitch_deg);
    snprintf(intensity_text, sizeof(intensity_text), "%.0f%%", motion->motion_intensity);
    snprintf(accel_text, sizeof(accel_text), "ACC X%+.1f Y%+.1f Z%+.1f",
             motion->accel_g[0], motion->accel_g[1], motion->accel_g[2]);
    snprintf(gyro_text, sizeof(gyro_text), "GYR X%+.0f Y%+.0f Z%+.0f",
             motion->gyro_dps[0], motion->gyro_dps[1], motion->gyro_dps[2]);

    lv_label_set_text(imu_header_label, header_text);
    lv_label_set_text(imu_attitude_values[0], roll_text);
    lv_label_set_text(imu_attitude_values[1], pitch_text);
    lv_label_set_text(imu_attitude_values[2], intensity_text);
    lv_label_set_text(imu_accel_label, accel_text);
    lv_label_set_text(imu_gyro_label, gyro_text);

    int32_t roll_point = (int32_t)motion->roll_deg;
    int32_t pitch_point = (int32_t)motion->pitch_deg;
    if (roll_point < -90) {
        roll_point = -90;
    } else if (roll_point > 90) {
        roll_point = 90;
    }
    if (pitch_point < -90) {
        pitch_point = -90;
    } else if (pitch_point > 90) {
        pitch_point = 90;
    }
    lv_chart_set_next_value(imu_chart, imu_roll_series, roll_point);
    lv_chart_set_next_value(imu_chart, imu_pitch_series, pitch_point);
}

void dashboard_update(const weather_dashboard_t *weather)
{
    if (weather == NULL || !weather->now_available) {
        dashboard_set_status("天气数据不可用");
        return;
    }

    lv_label_set_text(temperature_label, value_or_empty(weather->temperature));
    lv_label_set_text(weather_label, value_or_empty(weather->text));
    if (strlen(weather->last_update) >= 16) {
        lv_label_set_text_fmt(update_label, "更新于 %.5s", weather->last_update + 11);
    } else {
        lv_label_set_text(update_label, "更新于 --:--");
    }
    const weather_daily_t *today = weather->daily_count > 0 ? &weather->daily[0] : NULL;
    const char *humidity = weather->humidity[0] != '\0' ? weather->humidity :
                           (today != NULL ? today->humidity : "--");
    const char *wind_speed = weather->wind_speed[0] != '\0' ? weather->wind_speed :
                             (today != NULL ? today->wind_speed : "--");
    const char *rainfall = today != NULL ? value_or_empty(today->rainfall) : "--";
    const char *wind_scale = weather->wind_scale[0] != '\0' ? weather->wind_scale :
                             (today != NULL ? today->wind_scale : "--");
    lv_label_set_text_fmt(overview_values[0], "%s%%", humidity);
    lv_label_set_text_fmt(overview_values[1], "%s km/h", wind_speed);
    lv_label_set_text_fmt(overview_values[2], "%s mm", rainfall);
    lv_label_set_text_fmt(overview_values[3], "Lv.%s", wind_scale);

    static const char *days[] = {"今天", "明天", "后天"};
    for (int index = 0; index < WEATHER_FORECAST_DAYS; index++) {
        lv_label_set_text(forecast_day_labels[index], days[index]);
        if (index < (int)weather->daily_count) {
            const weather_daily_t *day = &weather->daily[index];
            lv_label_set_text(forecast_weather_labels[index], value_or_empty(day->text_day));
            lv_label_set_text_fmt(forecast_temperature_labels[index], "%s/%s℃",
                                  value_or_empty(day->high), value_or_empty(day->low));
            lv_label_set_text_fmt(forecast_detail_labels[index], "%s%%  %smm",
                                  value_or_empty(day->humidity), value_or_empty(day->rainfall));
        } else {
            lv_label_set_text(forecast_weather_labels[index], "无数据");
            lv_label_set_text(forecast_temperature_labels[index], "--/--℃");
            lv_label_set_text(forecast_detail_labels[index], "--");
        }
    }

    if (weather->air_available) {
        lv_label_set_text_fmt(air_quality_label, "空气质量 · %s", value_or_empty(weather->quality));
        lv_label_set_text_fmt(aqi_label, "AQI %s", value_or_empty(weather->aqi));
        set_aqi_badge_color(weather->aqi);
        lv_label_set_text_fmt(air_values[0], "PM2.5  %s", value_or_empty(weather->pm25));
        lv_label_set_text_fmt(air_values[1], "PM10   %s", value_or_empty(weather->pm10));
        lv_label_set_text_fmt(air_values[2], "O3     %s", value_or_empty(weather->o3));
        lv_label_set_text_fmt(air_values[3], "NO2    %s", value_or_empty(weather->no2));
        lv_label_set_text_fmt(air_values[4], "SO2    %s", value_or_empty(weather->so2));
        lv_label_set_text_fmt(air_values[5], "CO     %s", value_or_empty(weather->co));
        lv_label_set_text_fmt(air_values[6], "DATA   %s", value_or_empty(weather->primary_pollutant));
    } else {
        lv_label_set_text(air_quality_label, "空气质量 · 暂不可用");
        lv_label_set_text(aqi_label, "N/A");
        set_aqi_badge_color(NULL);
        static const char *empty_air[] = {"PM2.5  --", "PM10   --", "O3     --",
                                          "NO2    --", "SO2    --", "CO     --"};
        for (int index = 0; index < 6; index++) {
            lv_label_set_text(air_values[index], empty_air[index]);
        }
        lv_label_set_text(air_values[6], "DATA   RETRYING");
    }

    const char *precip = today != NULL ? value_or_empty(today->precip) : "--";
    const char *wind_direction = weather->wind_direction[0] != '\0' ? weather->wind_direction :
                                 (today != NULL ? value_or_empty(today->wind_direction) : "--");
    lv_label_set_text_fmt(detail_values[0], "湿度 %s%%  降雨 %s%%  雨量 %smm",
                          humidity, precip, rainfall);
    lv_label_set_text_fmt(detail_values[1], "风向 %s  风速 %skm/h  风力 %s级",
                          wind_direction, wind_speed, wind_scale);
    dashboard_set_status(weather->air_available ? "在线 · 10分钟刷新" :
                         "天气已更新 · 空气数据重试中");
}
