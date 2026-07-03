#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:Timer"
#include "esp_lib_utils.h"
#include "esp_watchos_app_timer.hpp"

#include "timer_shared.h"

#include <cstdio>

#define APP_NAME "Timer"
#define DISPLAY_HEIGHT 502
#define STATUS_BAR_HEIGHT 52
#define CONTENT_HEIGHT (DISPLAY_HEIGHT - STATUS_BAR_HEIGHT)

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_timer_120_120);

namespace esp_watchos::apps {

static lv_obj_t *create_card(lv_obj_t *parent, const char *title_text)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x355563), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, lv_color_hex(0x6cf0c2), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    return card;
}

TimerApp *TimerApp::_instance = nullptr;

TimerApp *TimerApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new TimerApp();
    }
    return _instance;
}

TimerApp::TimerApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_timer_120_120, true, true, false)
{
}

TimerApp::~TimerApp()
{
}

bool TimerApp::run(void)
{
    alarm_shared_load(&_alarm_cfg);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), CONTENT_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

    buildUi(content);
    _countdown_timer = lv_timer_create(onCountdownTick, 1000, this);

    return true;
}

bool TimerApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool TimerApp::pause(void)
{
    if (_countdown_timer != nullptr) {
        lv_timer_pause(_countdown_timer);
    }
    return true;
}

bool TimerApp::resume(void)
{
    if (_countdown_timer != nullptr) {
        lv_timer_resume(_countdown_timer);
    }
    updateCountdownLabel();
    return true;
}

void TimerApp::buildUi(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_BELL " Timer");
    lv_obj_set_style_text_color(title, lv_color_hex(0x6cf0c2), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_HEIGHT - 50);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    // -- Countdown card --
    lv_obj_t *cd_card = create_card(list, "Countdown");

    _countdown_label = lv_label_create(cd_card);
    lv_label_set_text(_countdown_label, "00:00");
    lv_obj_set_style_text_color(_countdown_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_countdown_label, &lv_font_montserrat_44, 0);

    lv_obj_t *preset_row = lv_obj_create(cd_card);
    lv_obj_remove_style_all(preset_row);
    lv_obj_set_size(preset_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(preset_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(preset_row, 8, 0);
    lv_obj_set_style_pad_row(preset_row, 8, 0);
    lv_obj_clear_flag(preset_row, LV_OBJ_FLAG_SCROLLABLE);

    static const struct { const char *label; int seconds; } PRESETS[] = {
        {"1 min", 60}, {"5 min", 300}, {"10 min", 600}, {"30 min", 1800},
    };
    for (const auto &preset : PRESETS) {
        lv_obj_t *btn = lv_button_create(preset_row);
        lv_obj_set_size(btn, 84, 42);
        lv_obj_set_user_data(btn, (void *)(intptr_t)preset.seconds);
        lv_obj_add_event_cb(btn, onPresetClicked, LV_EVENT_CLICKED, this);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, preset.label);
        lv_obj_center(lbl);
    }

    lv_obj_t *control_row = lv_obj_create(cd_card);
    lv_obj_remove_style_all(control_row);
    lv_obj_set_size(control_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(control_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(control_row, 10, 0);
    lv_obj_clear_flag(control_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *start_btn = lv_button_create(control_row);
    lv_obj_set_size(start_btn, 130, 44);
    lv_obj_add_event_cb(start_btn, onStartPauseClicked, LV_EVENT_CLICKED, this);
    _start_pause_label = lv_label_create(start_btn);
    lv_label_set_text(_start_pause_label, "Start");
    lv_obj_center(_start_pause_label);

    lv_obj_t *reset_btn = lv_button_create(control_row);
    lv_obj_set_size(reset_btn, 100, 44);
    lv_obj_add_event_cb(reset_btn, onResetClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *reset_label = lv_label_create(reset_btn);
    lv_label_set_text(reset_label, "Reset");
    lv_obj_center(reset_label);

    updateCountdownLabel();

    // -- Alarm card --
    lv_obj_t *alarm_card = create_card(list, "Daily Alarm");

    _alarm_label = lv_label_create(alarm_card);
    lv_obj_set_style_text_color(_alarm_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_alarm_label, &lv_font_montserrat_36, 0);
    updateAlarmLabel();

    lv_obj_t *stepper_row = lv_obj_create(alarm_card);
    lv_obj_remove_style_all(stepper_row);
    lv_obj_set_size(stepper_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stepper_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(stepper_row, 8, 0);
    lv_obj_set_style_pad_row(stepper_row, 8, 0);
    lv_obj_clear_flag(stepper_row, LV_OBJ_FLAG_SCROLLABLE);

    static const struct { const char *label; int delta; bool is_hour; } STEPS[] = {
        {"H -1", -1, true}, {"H +1", 1, true}, {"M -5", -5, false}, {"M +5", 5, false},
    };
    for (const auto &step : STEPS) {
        lv_obj_t *btn = lv_button_create(stepper_row);
        lv_obj_set_size(btn, 84, 42);
        lv_obj_set_user_data(btn, (void *)(intptr_t)step.delta);
        lv_obj_add_event_cb(btn, step.is_hour ? onAlarmHourChanged : onAlarmMinuteChanged, LV_EVENT_CLICKED, this);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, step.label);
        lv_obj_center(lbl);
    }

    lv_obj_t *enable_row = lv_obj_create(alarm_card);
    lv_obj_remove_style_all(enable_row);
    lv_obj_set_size(enable_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(enable_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enable_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(enable_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *enable_label = lv_label_create(enable_row);
    lv_label_set_text(enable_label, "Enabled");
    lv_obj_set_style_text_color(enable_label, lv_color_hex(0xffffff), 0);

    lv_obj_t *enable_sw = lv_switch_create(enable_row);
    lv_obj_add_event_cb(enable_sw, onAlarmToggled, LV_EVENT_VALUE_CHANGED, this);
    if (_alarm_cfg.enabled) {
        lv_obj_add_state(enable_sw, LV_STATE_CHECKED);
    }
}

void TimerApp::updateCountdownLabel(void)
{
    if (_countdown_label == nullptr) {
        return;
    }
    int remaining = timer_shared_remaining_seconds();
    int display_seconds = remaining >= 0 ? remaining : _pending_seconds;

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", display_seconds / 60, display_seconds % 60);
    lv_label_set_text(_countdown_label, buf);

    if (_start_pause_label != nullptr) {
        lv_label_set_text(_start_pause_label, timer_shared_is_running() ? "Pause" : "Start");
    }
}

void TimerApp::updateAlarmLabel(void)
{
    if (_alarm_label == nullptr) {
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%02u:%02u", _alarm_cfg.hour, _alarm_cfg.minute);
    lv_label_set_text(_alarm_label, buf);
}

void TimerApp::setCountdownRunning(bool running)
{
    if (running) {
        int seconds = timer_shared_remaining_seconds();
        timer_shared_start(seconds > 0 ? seconds : (_pending_seconds > 0 ? _pending_seconds : 60));
    } else {
        _pending_seconds = timer_shared_remaining_seconds();
        if (_pending_seconds < 0) {
            _pending_seconds = 0;
        }
        timer_shared_cancel();
    }
    updateCountdownLabel();
}

void TimerApp::onPresetClicked(lv_event_t *e)
{
    TimerApp *app = (TimerApp *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    int seconds = (int)(intptr_t)lv_obj_get_user_data(btn);

    timer_shared_cancel();
    app->_pending_seconds = seconds;
    app->updateCountdownLabel();
}

void TimerApp::onStartPauseClicked(lv_event_t *e)
{
    TimerApp *app = (TimerApp *)lv_event_get_user_data(e);
    app->setCountdownRunning(!timer_shared_is_running());
}

void TimerApp::onResetClicked(lv_event_t *e)
{
    TimerApp *app = (TimerApp *)lv_event_get_user_data(e);
    timer_shared_cancel();
    app->_pending_seconds = 0;
    app->updateCountdownLabel();
}

void TimerApp::onCountdownTick(lv_timer_t *t)
{
    TimerApp *app = (TimerApp *)lv_timer_get_user_data(t);
    app->updateCountdownLabel();
}

void TimerApp::onAlarmHourChanged(lv_event_t *e)
{
    TimerApp *app = (TimerApp *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    int delta = (int)(intptr_t)lv_obj_get_user_data(btn);

    int hour = ((int)app->_alarm_cfg.hour + delta + 24) % 24;
    app->_alarm_cfg.hour = (uint8_t)hour;
    alarm_shared_save(&app->_alarm_cfg);
    app->updateAlarmLabel();
}

void TimerApp::onAlarmMinuteChanged(lv_event_t *e)
{
    TimerApp *app = (TimerApp *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    int delta = (int)(intptr_t)lv_obj_get_user_data(btn);

    int minute = ((int)app->_alarm_cfg.minute + delta + 60) % 60;
    app->_alarm_cfg.minute = (uint8_t)minute;
    alarm_shared_save(&app->_alarm_cfg);
    app->updateAlarmLabel();
}

void TimerApp::onAlarmToggled(lv_event_t *e)
{
    TimerApp *app = (TimerApp *)lv_event_get_user_data(e);
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);

    app->_alarm_cfg.enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    alarm_shared_save(&app->_alarm_cfg);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, TimerApp, APP_NAME, []()
{
    return std::shared_ptr<TimerApp>(TimerApp::requestInstance(), [](TimerApp *) {});
})

} // namespace esp_watchos::apps
