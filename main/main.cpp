/*
 * SPDX-License-Identifier: CC0-1.0
 */

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_shared.h"
#include "os_fs.h"
#include "rtc_shared.h"
#include "homescreen_shared.h"
#include "alarm_shared.h"
#include "timer_shared.h"
#include "display_shared.h"
#include "battery_shared.h"
#include "weather_shared.h"

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"
#include "./dark/stylesheet.hpp"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;

LV_IMG_DECLARE(boot_logo);

/* Set once in app_main() after Phone is created, so the power-menu handlers
 * (which are plain static functions, not members) can reach the status bar
 * / navigation bar to hide them during the AOD clock. */
static Phone *s_phone = nullptr;

#define LVGL_PORT_INIT_CONFIG() \
    {                               \
        .task_priority = 4,       \
        .task_stack = 10 * 1024,       \
        .task_affinity = -1,      \
        .task_max_sleep_ms = 500, \
        .timer_period_ms = 5,     \
    }

/* Periodically logs heap stats so a slow fragmentation/leak (which can take
 * up to an hour of normal use to manifest as a hang) is visible in advance,
 * and runs independently of LVGL so it keeps reporting even if LVGL hangs. */
static void heap_monitor_task(void *arg)
{
    (void)arg;
    while (true) {
        size_t free_now = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        size_t min_free_ever = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
        ESP_LOGI("HeapMonitor", "free=%u largest_block=%u min_free_ever=%u",
                 (unsigned)free_now, (unsigned)largest_block, (unsigned)min_free_ever);
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* Physical side PWR button: GPIO18, confirmed active-high with pull-down on
 * hardware (an initial active-low/pull-up guess fired the menu ~3s after
 * every boot and only registered real presses on release). Polled from an
 * LVGL timer (not an ISR) so show_power_menu() can safely touch LVGL
 * objects directly. */
#define PWR_BUTTON_GPIO GPIO_NUM_18
#define PWR_BUTTON_PRESSED_LEVEL 1
#define PWR_BUTTON_HOLD_MS 2000

static bool s_pwr_button_was_pressed = false;
static uint32_t s_pwr_button_press_start = 0;
static bool s_pwr_menu_triggered_this_press = false;

static void init_pwr_button(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << PWR_BUTTON_GPIO;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

/* BOOT button: GPIO9, the chip's own flash-mode strapping pin - safe to read
 * as a plain input during normal runtime (strapping only matters at reset).
 * Active-low with internal pull-up, matching the hardware's own boot-mode
 * convention (must read LOW at reset to enter download mode). Short clean
 * press toggles between the AOD clock and the home screen. */
#define BOOT_BUTTON_GPIO GPIO_NUM_9
#define BOOT_BUTTON_PRESSED_LEVEL 0

static bool s_boot_button_was_pressed = false;

static void init_boot_button(void)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

/* Shown on top of whatever app is currently active, so an alarm or timer
 * firing is visible regardless of which screen the user is looking at. */
static lv_obj_t *s_notify_overlay = nullptr;
static lv_timer_t *s_notify_flash_timer = nullptr;

static void close_notification_overlay(void)
{
    if (s_notify_flash_timer != nullptr) {
        lv_timer_delete(s_notify_flash_timer);
        s_notify_flash_timer = nullptr;
    }
    if (s_notify_overlay != nullptr) {
        lv_obj_del(s_notify_overlay);
        s_notify_overlay = nullptr;
    }
}

static void on_notify_dismiss_clicked(lv_event_t *e)
{
    (void)e;
    close_notification_overlay();
}

static void on_notify_flash_timer(lv_timer_t *t)
{
    (void)t;
    if (s_notify_overlay == nullptr) {
        return;
    }
    static bool toggle = false;
    toggle = !toggle;
    lv_obj_set_style_bg_color(s_notify_overlay, toggle ? lv_color_hex(0xff2222) : lv_color_hex(0x1a0505), 0);
}

static void show_notification_overlay(const char *title)
{
    close_notification_overlay();

    s_notify_overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_notify_overlay);
    lv_obj_set_size(s_notify_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_notify_overlay, lv_color_hex(0x1a0505), 0);
    lv_obj_set_style_bg_opa(s_notify_overlay, LV_OPA_COVER, 0);
    lv_obj_center(s_notify_overlay);
    lv_obj_add_flag(s_notify_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(s_notify_overlay);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_36, 0);
    lv_obj_center(label);

    lv_obj_t *btn = lv_button_create(s_notify_overlay);
    lv_obj_set_size(btn, 150, 50);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_event_cb(btn, on_notify_dismiss_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Dismiss");
    lv_obj_center(btn_label);

    s_notify_flash_timer = lv_timer_create(on_notify_flash_timer, 500, nullptr);
}

/* Power menu - opened on lv_layer_top() (renders above every app screen)
 * by a long-press of the physical PWR button. This board has no
 * PMIC/power-cutoff hardware, so true deep sleep would only be wakeable by
 * a hardware reset/power-cycle - not useful as a one-click "off". Reboot
 * is the only action offered. */
static lv_obj_t *s_power_menu = nullptr;

static void close_power_menu(void)
{
    if (s_power_menu != nullptr) {
        lv_obj_del(s_power_menu);
        s_power_menu = nullptr;
    }
}

static void on_power_menu_backdrop_clicked(lv_event_t *e)
{
    (void)e;
    close_power_menu();
}

static void on_power_reboot_clicked(lv_event_t *e)
{
    (void)e;
    esp_restart();
}

/* Seven-segment digit rendering - plain rectangles switched on/off per the
 * standard a-g segment layout (universal calculator/clock geometry, not tied
 * to any particular font/typeface). Used for the always-on-display clock
 * shown during light sleep, full width, nothing else on screen. */
struct SevenSegDigit {
    lv_obj_t *segs[7]; // a,b,c,d,e,f,g
};

static uint32_t s_aod_color = 0x6cf0c2;

static const uint8_t SEVEN_SEG_DIGIT_MAP[10] = {
    0b0111111, // 0: a b c d e f
    0b0000110, // 1: b c
    0b1011011, // 2: a b g e d
    0b1001111, // 3: a b g c d
    0b1100110, // 4: f g b c
    0b1101101, // 5: a f g c d
    0b1111101, // 6: a f g e c d
    0b0000111, // 7: a b c
    0b1111111, // 8: all
    0b1101111, // 9: a b c d f g
};

static lv_obj_t *s_aod_screen = nullptr;
static lv_obj_t *s_aod_digit_row = nullptr;
static lv_obj_t *s_aod_date_label = nullptr;
static lv_obj_t *s_aod_status_label = nullptr;
static lv_obj_t *s_aod_weather_label = nullptr;
static SevenSegDigit s_aod_digits[4];
static lv_obj_t *s_aod_colon_dots[2] = {nullptr, nullptr};

static const char *const AOD_WEEKDAY_NAMES[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const AOD_MONTH_NAMES[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static void create_seven_seg_digit(lv_obj_t *parent, SevenSegDigit *digit)
{
    static const struct { int x, y, w, h; } SEG_RECTS[7] = {
        {14, 0, 52, 14},    // a
        {66, 8, 14, 55},    // b
        {66, 67, 14, 55},   // c
        {14, 116, 52, 14},  // d
        {0, 67, 14, 55},    // e
        {0, 8, 14, 55},     // f
        {14, 58, 52, 14},   // g
    };
    lv_obj_t *mod = lv_obj_create(parent);
    lv_obj_remove_style_all(mod);
    lv_obj_set_size(mod, 80, 130);
    lv_obj_clear_flag(mod, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(mod, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 7; i++) {
        lv_obj_t *seg = lv_obj_create(mod);
        lv_obj_remove_style_all(seg);
        lv_obj_set_pos(seg, SEG_RECTS[i].x, SEG_RECTS[i].y);
        lv_obj_set_size(seg, SEG_RECTS[i].w, SEG_RECTS[i].h);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(seg, 3, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE);
        digit->segs[i] = seg;
    }
}

static void set_seven_seg_digit(SevenSegDigit *digit, int value)
{
    uint8_t mask = SEVEN_SEG_DIGIT_MAP[value];
    for (int i = 0; i < 7; i++) {
        bool on = (mask >> i) & 1;
        lv_obj_set_style_bg_color(digit->segs[i],
                                   on ? lv_color_hex(s_aod_color) : lv_color_hex(0x141414), 0);
    }
}

static void build_aod_screen(void)
{
    s_aod_screen = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_aod_screen);
    lv_obj_set_size(s_aod_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_aod_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_aod_screen, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_aod_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_aod_screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_aod_screen, 14, 0);
    lv_obj_clear_flag(s_aod_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_aod_screen, LV_OBJ_FLAG_CLICKABLE);

    s_aod_digit_row = lv_obj_create(s_aod_screen);
    lv_obj_remove_style_all(s_aod_digit_row);
    lv_obj_set_size(s_aod_digit_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_aod_digit_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_aod_digit_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_aod_digit_row, 10, 0);
    lv_obj_clear_flag(s_aod_digit_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_aod_digit_row, LV_OBJ_FLAG_CLICKABLE);

    create_seven_seg_digit(s_aod_digit_row, &s_aod_digits[0]);
    create_seven_seg_digit(s_aod_digit_row, &s_aod_digits[1]);

    lv_obj_t *colon = lv_obj_create(s_aod_digit_row);
    lv_obj_remove_style_all(colon);
    lv_obj_set_size(colon, 20, 130);
    lv_obj_clear_flag(colon, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(colon, LV_OBJ_FLAG_CLICKABLE);
    int dot_idx = 0;
    for (int dot_y : {40, 76}) {
        lv_obj_t *dot = lv_obj_create(colon);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_pos(dot, 3, dot_y);
        lv_obj_set_style_bg_color(dot, lv_color_hex(s_aod_color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, 3, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        s_aod_colon_dots[dot_idx++] = dot;
    }

    create_seven_seg_digit(s_aod_digit_row, &s_aod_digits[2]);
    create_seven_seg_digit(s_aod_digit_row, &s_aod_digits[3]);

    s_aod_date_label = lv_label_create(s_aod_screen);
    lv_label_set_text(s_aod_date_label, "");
    lv_obj_set_style_text_color(s_aod_date_label, lv_color_hex(s_aod_color), 0);
    lv_obj_set_style_text_font(s_aod_date_label, &lv_font_montserrat_24, 0);
    lv_obj_clear_flag(s_aod_date_label, LV_OBJ_FLAG_CLICKABLE);

    s_aod_status_label = lv_label_create(s_aod_screen);
    lv_label_set_text(s_aod_status_label, "");
    lv_obj_set_style_text_color(s_aod_status_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_aod_status_label, &lv_font_montserrat_20, 0);
    lv_obj_clear_flag(s_aod_status_label, LV_OBJ_FLAG_CLICKABLE);

    s_aod_weather_label = lv_label_create(s_aod_screen);
    lv_label_set_text(s_aod_weather_label, "");
    lv_obj_set_style_text_color(s_aod_weather_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(s_aod_weather_label, &lv_font_montserrat_20, 0);
    lv_obj_clear_flag(s_aod_weather_label, LV_OBJ_FLAG_CLICKABLE);
}

static void update_aod_clock(void)
{
    rtc_shared_datetime_t dt = {};
    rtc_shared_get_datetime(&dt);
    set_seven_seg_digit(&s_aod_digits[0], dt.hour / 10);
    set_seven_seg_digit(&s_aod_digits[1], dt.hour % 10);
    set_seven_seg_digit(&s_aod_digits[2], dt.min / 10);
    set_seven_seg_digit(&s_aod_digits[3], dt.min % 10);

    if (s_aod_date_label != nullptr) {
        int weekday = rtc_shared_weekday(dt.year, dt.month, dt.day);
        char buf[48];
        snprintf(buf, sizeof(buf), "%s, %s %u", AOD_WEEKDAY_NAMES[weekday], AOD_MONTH_NAMES[dt.month - 1],
                 (unsigned)dt.day);
        lv_label_set_text(s_aod_date_label, buf);
    }
}

static void update_aod_status(void)
{
    if (s_aod_status_label == nullptr) {
        return;
    }

    char buf[64];
    const char *wifi_icon = wifi_shared_is_connected() ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;

    int batt_percent = battery_shared_get_percent();
    if (batt_percent < 0) {
        snprintf(buf, sizeof(buf), "%s", wifi_icon);
    } else {
        const char *batt_icon = LV_SYMBOL_BATTERY_EMPTY;
        if (battery_shared_get_state() == BATTERY_SHARED_STATE_CHARGING) {
            batt_icon = LV_SYMBOL_CHARGE;
        } else if (batt_percent >= 90) {
            batt_icon = LV_SYMBOL_BATTERY_FULL;
        } else if (batt_percent >= 60) {
            batt_icon = LV_SYMBOL_BATTERY_3;
        } else if (batt_percent >= 40) {
            batt_icon = LV_SYMBOL_BATTERY_2;
        } else if (batt_percent >= 15) {
            batt_icon = LV_SYMBOL_BATTERY_1;
        }
        snprintf(buf, sizeof(buf), "%s   %s %d%%", wifi_icon, batt_icon, batt_percent);
    }
    lv_label_set_text(s_aod_status_label, buf);
}

/* Kicks off a background refresh when the cached weather is missing/stale -
 * never blocks this (UI) task. weather_shared_refresh_async() itself
 * no-ops if Wi-Fi is down, no location is saved, or a fetch is already
 * running, so it's cheap to call speculatively on every AOD tick. */
static void update_aod_weather(void)
{
    if (s_aod_weather_label == nullptr) {
        return;
    }

    weather_shared_data_t data;
    weather_shared_poll(&data);  // drains the result queue; weather_shared_get_last() below has the value either way
    weather_shared_data_t last = weather_shared_get_last();
    if (last.valid) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%.0f°C  %s", last.temperature_c, weather_shared_describe(last.weather_code));
        lv_label_set_text(s_aod_weather_label, buf);
    } else {
        lv_label_set_text(s_aod_weather_label, "");
    }

    if (weather_shared_is_stale()) {
        weather_shared_refresh_async();
    }
}

static void set_aod_colon_visible(bool visible)
{
    lv_color_t color = visible ? lv_color_hex(s_aod_color) : lv_color_hex(0x141414);
    for (lv_obj_t *dot : s_aod_colon_dots) {
        if (dot != nullptr) {
            lv_obj_set_style_bg_color(dot, color, 0);
        }
    }
}

/* The AOD clock is the default boot screen (toggled via BOOT_BUTTON_GPIO,
 * see on_boot_button_poll). No CPU/light sleep involved - just a normal 1Hz
 * LVGL timer redrawing the digits and blinking the colon, same as any other
 * screen. Status bar / navigation bar / gesture indicator all live on
 * esp-brookesia's own system/gesture layers, which render above
 * lv_layer_top() - without hiding them they'd show through on top of the
 * clock. */
static bool s_aod_view_active = false;
static lv_timer_t *s_aod_update_timer = nullptr;

static void on_aod_update_timer(lv_timer_t *t)
{
    (void)t;
    static bool colon_visible = true;
    update_aod_clock();
    update_aod_status();
    update_aod_weather();
    colon_visible = !colon_visible;
    set_aod_colon_visible(colon_visible);
}

static void show_aod_view(void)
{
    if (s_aod_view_active) {
        return;
    }
    s_aod_view_active = true;

    uint32_t saved_color;
    if (display_shared_get_saved_aod_color(&saved_color)) {
        s_aod_color = saved_color;
    }

    build_aod_screen();
    update_aod_clock();
    set_aod_colon_visible(true);
    s_aod_update_timer = lv_timer_create(on_aod_update_timer, 1000, nullptr);

    if (s_phone != nullptr) {
        s_phone->getDisplay().getStatusBar()->setVisualMode(StatusBar::VisualMode::HIDE);
        s_phone->getDisplay().getNavigationBar()->setVisualMode(NavigationBar::VisualMode::HIDE);
        Gesture *gesture = s_phone->getManager().getGesture();
        if (gesture != nullptr) {
            gesture->setIndicatorBarVisible(Gesture::IndicatorBarType::BOTTOM, false);
        }
    }

    /* Hiding the status bar / gesture indicator only hides them visually -
     * a swipe-up still gets processed by the gesture system underneath and
     * re-shows them. Disabling touch input entirely while the clock is up
     * stops that at the source. */
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != nullptr) {
        lv_indev_enable(indev, false);
    }
}

static void hide_aod_view(void)
{
    if (!s_aod_view_active) {
        return;
    }
    s_aod_view_active = false;

    if (s_aod_update_timer != nullptr) {
        lv_timer_delete(s_aod_update_timer);
        s_aod_update_timer = nullptr;
    }
    if (s_aod_screen != nullptr) {
        lv_obj_del(s_aod_screen);  // deletes the whole subtree (digit row, colon, labels) too
        s_aod_screen = nullptr;
        s_aod_digit_row = nullptr;
        s_aod_date_label = nullptr;
        s_aod_status_label = nullptr;
        s_aod_weather_label = nullptr;
        s_aod_colon_dots[0] = nullptr;
        s_aod_colon_dots[1] = nullptr;
    }

    /* Restore the stylesheet's normal modes (status bar fixed-visible,
     * navigation bar hidden - see STYLESHEET_410_502_DARK_DISPLAY_DATA). */
    if (s_phone != nullptr) {
        s_phone->getDisplay().getStatusBar()->setVisualMode(StatusBar::VisualMode::SHOW_FIXED);
        s_phone->getDisplay().getNavigationBar()->setVisualMode(NavigationBar::VisualMode::HIDE);
        Gesture *gesture = s_phone->getManager().getGesture();
        if (gesture != nullptr) {
            gesture->setIndicatorBarVisible(Gesture::IndicatorBarType::BOTTOM, true);
        }
    }

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != nullptr) {
        lv_indev_enable(indev, true);
    }
}

static void toggle_aod_view(void)
{
    if (s_aod_view_active) {
        hide_aod_view();
    } else {
        show_aod_view();
    }
}

static void on_boot_button_poll(lv_timer_t *t)
{
    (void)t;
    bool pressed = (gpio_get_level(BOOT_BUTTON_GPIO) == BOOT_BUTTON_PRESSED_LEVEL);
    if (pressed && !s_boot_button_was_pressed) {
        toggle_aod_view();
    }
    s_boot_button_was_pressed = pressed;
}

static void show_power_menu(void)
{
    close_power_menu();

    s_power_menu = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_power_menu);
    lv_obj_set_size(s_power_menu, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_power_menu, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_power_menu, LV_OPA_70, 0);
    lv_obj_add_flag(s_power_menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_power_menu, on_power_menu_backdrop_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *panel = lv_obj_create(s_power_menu);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 260, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x355563), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 10, 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, [](lv_event_t *e) {
        lv_event_stop_bubbling(e);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, LV_SYMBOL_POWER " Power");
    lv_obj_set_style_text_color(title, lv_color_hex(0x6cf0c2), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t *btn = lv_button_create(panel);
    lv_obj_set_size(btn, LV_PCT(100), 90);
    lv_obj_add_event_cb(btn, on_power_reboot_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Reboot");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);
}

static void on_pwr_button_poll(lv_timer_t *t)
{
    (void)t;
    bool pressed = (gpio_get_level(PWR_BUTTON_GPIO) == PWR_BUTTON_PRESSED_LEVEL);

    if (pressed && !s_pwr_button_was_pressed) {
        s_pwr_button_press_start = lv_tick_get();
        s_pwr_menu_triggered_this_press = false;
    } else if (pressed && !s_pwr_menu_triggered_this_press) {
        if (lv_tick_elaps(s_pwr_button_press_start) >= PWR_BUTTON_HOLD_MS) {
            s_pwr_menu_triggered_this_press = true;
            show_power_menu();
        }
    }
    s_pwr_button_was_pressed = pressed;
}

extern "C" void app_main(void)
{
    ESP_UTILS_LOGI("Starting ESPWatchOS");

    xTaskCreate(heap_monitor_task, "heap_mon", 2560, NULL, 1, NULL);
    init_pwr_button();
    init_boot_button();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(wifi_shared_init());
    ESP_ERROR_CHECK(os_fs_mount());
    wifi_shared_try_autoconnect();

    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = LVGL_PORT_INIT_CONFIG(),
    };
    ESP_UTILS_CHECK_NULL_EXIT(bsp_display_start_with_config(&cfg), "Start display failed");
    ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Turn on display backlight failed");
    display_shared_apply_saved();

    /* Show the boot logo for a few seconds before anything else - the LVGL
     * port task (already running, started above) keeps refreshing the
     * display on its own, so blocking this task just holds the splash up. */
    {
        bsp_display_lock(0);
        lv_obj_t *logo_screen = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(logo_screen);
        lv_obj_set_size(logo_screen, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(logo_screen, lv_color_hex(0x0b1115), 0);
        lv_obj_set_style_bg_opa(logo_screen, LV_OPA_COVER, 0);
        lv_obj_clear_flag(logo_screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(logo_screen, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *logo_img = lv_image_create(logo_screen);
        lv_image_set_src(logo_img, &boot_logo);
        lv_obj_center(logo_img);
        bsp_display_unlock();

        vTaskDelay(pdMS_TO_TICKS(3000));

        bsp_display_lock(0);
        lv_obj_del(logo_screen);
        bsp_display_unlock();
    }

    /* Sync the system clock from the RTC chip now, so the status bar's clock
     * (which uses plain time()) is correct from the first frame, instead of
     * only once some app happens to open and call this lazily. */
    rtc_shared_init();
    battery_shared_init();
    weather_shared_init();

    /* Configure GUI lock */
    LvLock::registerCallbacks([](int timeout_ms) {
        if (timeout_ms < 0) {
            timeout_ms = 0;
        } else if (timeout_ms == 0) {
            timeout_ms = 1;
        }
        ESP_UTILS_CHECK_FALSE_RETURN(bsp_display_lock(timeout_ms), false, "Lock failed");

        return true;
    }, []() {
        bsp_display_unlock();

        return true;
    });

    Phone *phone = new (std::nothrow) Phone();
    ESP_UTILS_CHECK_NULL_EXIT(phone, "Create phone failed");
    s_phone = phone;

    if ((BSP_LCD_H_RES == 410) && (BSP_LCD_V_RES == 502)) {
        Stylesheet *stylesheet = new (std::nothrow) Stylesheet(STYLESHEET_410_502_DARK);
        ESP_UTILS_CHECK_NULL_EXIT(stylesheet, "Create stylesheet failed");

        ESP_UTILS_LOGI("Using stylesheet (%s)", stylesheet->core.name);
        ESP_UTILS_CHECK_FALSE_EXIT(phone->addStylesheet(stylesheet), "Add stylesheet failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->activateStylesheet(stylesheet), "Activate stylesheet failed");
        delete stylesheet;
    }

    {
        LvLockGuard gui_guard;

        ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");

        homescreen_shared_register_main_screen(phone->getDisplay().getMainScreenObject());
        homescreen_shared_apply_saved();

        std::vector<systems::base::Manager::RegistryAppInfo> inited_apps;
        ESP_UTILS_CHECK_FALSE_EXIT(phone->initAppFromRegistry(inited_apps), "Init app registry failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installAppFromRegistry(inited_apps), "Install app registry failed");

        lv_timer_create(on_pwr_button_poll, 100, nullptr);
        lv_timer_create(on_boot_button_poll, 100, nullptr);

        lv_timer_create([](lv_timer_t *t) {
            time_t now;
            struct tm timeinfo;
            Phone *phone = (Phone *)t->user_data;

            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

            time(&now);
            localtime_r(&now, &timeinfo);

            ESP_UTILS_CHECK_FALSE_EXIT(
                phone->getDisplay().getStatusBar()->setClock(timeinfo.tm_hour, timeinfo.tm_min),
                "Refresh status bar failed"
            );

            using WifiState = systems::phone::StatusBar::WifiState;
            WifiState wifi_state = wifi_shared_is_connected() ? WifiState::SIGNAL_3 : WifiState::DISCONNECTED;
            ESP_UTILS_CHECK_FALSE_EXIT(
                phone->getDisplay().getStatusBar()->setWifiIconState(wifi_state),
                "Refresh status bar wifi icon failed"
            );

            if (alarm_shared_check_and_consume(timeinfo.tm_hour, timeinfo.tm_min)) {
                show_notification_overlay("Alarm");
            }
            if (timer_shared_check_and_consume()) {
                show_notification_overlay("Timer done");
            }
        }, 1000, phone);

        /* The AOD clock is the default boot screen - press BOOT to switch
         * to the home screen, press again to come back. */
        show_aod_view();
    }
}
