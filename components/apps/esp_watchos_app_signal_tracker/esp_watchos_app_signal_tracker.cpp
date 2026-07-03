#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:SignalTracker"
#include "esp_lib_utils.h"
#include "esp_watchos_app_signal_tracker.hpp"

#include <cstring>

#define APP_NAME "Signal Tracker"
#define MAX_APS 40
#define REFRESH_MS 3000
#define DISPLAY_HEIGHT 502
#define STATUS_BAR_HEIGHT 52
#define CONTENT_HEIGHT (DISPLAY_HEIGHT - STATUS_BAR_HEIGHT)

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_signal_tracker_120_120);

namespace esp_watchos::apps {

SignalTrackerApp *SignalTrackerApp::_instance = nullptr;

SignalTrackerApp *SignalTrackerApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new SignalTrackerApp();
    }
    return _instance;
}

SignalTrackerApp::SignalTrackerApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_signal_tracker_120_120, true, true, false)
{
}

SignalTrackerApp::~SignalTrackerApp()
{
}

bool SignalTrackerApp::run(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), CONTENT_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

    buildUi(content);
    _refresh_timer = lv_timer_create(onRefreshTimer, REFRESH_MS, this);
    _poll_timer = lv_timer_create(onPollTimer, 200, this);

    return true;
}

bool SignalTrackerApp::back(void)
{
    if (_picker != nullptr) {
        closePicker();
        return true;
    }

    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool SignalTrackerApp::pause(void)
{
    if (_refresh_timer != nullptr) {
        lv_timer_pause(_refresh_timer);
    }
    if (_poll_timer != nullptr) {
        lv_timer_pause(_poll_timer);
    }
    return true;
}

bool SignalTrackerApp::resume(void)
{
    if (_refresh_timer != nullptr) {
        lv_timer_resume(_refresh_timer);
    }
    if (_poll_timer != nullptr) {
        lv_timer_resume(_poll_timer);
    }
    return true;
}

void SignalTrackerApp::buildUi(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_CHARGE " Signal Tracker");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffaa00), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *pick_btn = lv_button_create(parent);
    lv_obj_set_size(pick_btn, 130, 42);
    lv_obj_align(pick_btn, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_add_event_cb(pick_btn, onPickClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *pick_label = lv_label_create(pick_btn);
    lv_label_set_text(pick_label, "Pick AP");
    lv_obj_center(pick_label);

    _target_label = lv_label_create(parent);
    lv_label_set_text(_target_label, "No target selected");
    lv_obj_set_style_text_color(_target_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_target_label, &lv_font_montserrat_16, 0);
    lv_obj_align(_target_label, LV_ALIGN_TOP_LEFT, 8, 34);
    lv_obj_set_width(_target_label, LV_PCT(96));

    _status_label = lv_label_create(parent);
    lv_label_set_text(_status_label, "");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align_to(_status_label, _target_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    _chart = lv_chart_create(parent);
    lv_obj_set_size(_chart, LV_PCT(94), CONTENT_HEIGHT - 130);
    lv_obj_align(_chart, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_chart_set_type(_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(_chart, HISTORY_LEN);
    lv_chart_set_range(_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -20);
    lv_obj_set_style_bg_color(_chart, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_border_color(_chart, lv_color_hex(0x355563), 0);
    lv_obj_set_style_radius(_chart, 8, 0);
    lv_obj_set_style_size(_chart, 0, 0, LV_PART_INDICATOR);
    _series = lv_chart_add_series(_chart, lv_color_hex(0xffaa00), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < HISTORY_LEN; i++) {
        lv_chart_set_next_value(_chart, _series, -100);
    }
}

void SignalTrackerApp::showPicker(void)
{
    closePicker();

    _picker = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_picker, LV_PCT(94), CONTENT_HEIGHT - 8);
    lv_obj_align(_picker, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 4);
    lv_obj_set_style_bg_color(_picker, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_border_color(_picker, lv_color_hex(0x355563), 0);
    lv_obj_set_style_border_width(_picker, 1, 0);
    lv_obj_set_style_radius(_picker, 10, 0);
    lv_obj_set_flex_flow(_picker, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_picker, 6, 0);
    lv_obj_set_style_pad_all(_picker, 8, 0);
    lv_obj_set_scroll_dir(_picker, LV_DIR_VER);

    lv_obj_t *loading = lv_label_create(_picker);
    lv_label_set_text(loading, "Scanning...");
    lv_obj_set_style_text_color(loading, lv_color_hex(0xffffff), 0);

    _picker_loading = true;
    if (!wifi_shared_scan_is_running()) {
        wifi_shared_scan_start_async();
    }
}

void SignalTrackerApp::closePicker(void)
{
    _picker_loading = false;
    if (_picker != nullptr) {
        lv_obj_del(_picker);
        _picker = nullptr;
    }
}

void SignalTrackerApp::selectTarget(const wifi_shared_ap_t &ap)
{
    _has_target = true;
    memset(_target_ssid, 0, sizeof(_target_ssid));
    memcpy(_target_ssid, ap.ssid, sizeof(_target_ssid) - 1);
    memcpy(_target_bssid, ap.bssid, sizeof(_target_bssid));

    char buf[64];
    snprintf(buf, sizeof(buf), "Tracking: %s (%02X:%02X:%02X:%02X:%02X:%02X)",
             ap.ssid[0] != '\0' ? ap.ssid : "<hidden>",
             ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    lv_label_set_text(_target_label, buf);

    for (int i = 0; i < HISTORY_LEN; i++) {
        lv_chart_set_next_value(_chart, _series, -100);
    }
    appendSample(true, ap.rssi);
}

void SignalTrackerApp::appendSample(bool seen, int rssi)
{
    lv_chart_set_next_value(_chart, _series, seen ? rssi : -100);

    if (seen) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Last seen: %d dBm", rssi);
        lv_label_set_text(_status_label, buf);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(0x6cf0c2), 0);
    } else {
        lv_label_set_text(_status_label, "Not seen in last scan");
        lv_obj_set_style_text_color(_status_label, lv_color_hex(0xff8888), 0);
    }
}

void SignalTrackerApp::onPickClicked(lv_event_t *e)
{
    SignalTrackerApp *app = (SignalTrackerApp *)lv_event_get_user_data(e);
    app->showPicker();
}

void SignalTrackerApp::onPickerRowClicked(lv_event_t *e)
{
    SignalTrackerApp *app = (SignalTrackerApp *)lv_event_get_user_data(e);
    lv_obj_t *row = (lv_obj_t *)lv_event_get_target(e);
    size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(row);
    if (idx < app->_picker_aps.size()) {
        app->selectTarget(app->_picker_aps[idx]);
    }
    app->closePicker();
}

void SignalTrackerApp::onScanResults(const wifi_shared_ap_t *aps, int count)
{
    if (_picker_loading) {
        _picker_loading = false;
        _picker_aps.assign(aps, aps + count);

        if (_picker != nullptr) {
            lv_obj_clean(_picker);

            for (size_t i = 0; i < _picker_aps.size(); i++) {
                const wifi_shared_ap_t &ap = _picker_aps[i];

                lv_obj_t *row = lv_obj_create(_picker);
                lv_obj_set_size(row, LV_PCT(100), 58);
                lv_obj_set_style_bg_color(row, lv_color_hex(0x141c22), 0);
                lv_obj_set_style_border_color(row, lv_color_hex(0x355563), 0);
                lv_obj_set_style_border_width(row, 1, 0);
                lv_obj_set_style_radius(row, 8, 0);
                lv_obj_set_style_pad_all(row, 6, 0);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_set_user_data(row, (void *)(uintptr_t)i);
                lv_obj_add_event_cb(row, onPickerRowClicked, LV_EVENT_CLICKED, this);

                lv_obj_t *ssid_lbl = lv_label_create(row);
                lv_label_set_text(ssid_lbl, ap.ssid[0] != '\0' ? ap.ssid : "<hidden>");
                lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xffffff), 0);
                lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 4, -8);

                char info[48];
                snprintf(info, sizeof(info), "Ch%u  %ddBm  %s", ap.primary, ap.rssi,
                         ap.authmode == WIFI_AUTH_OPEN ? "OPEN" : "secured");
                lv_obj_t *info_lbl = lv_label_create(row);
                lv_label_set_text(info_lbl, info);
                lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
                lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_16, 0);
                lv_obj_align(info_lbl, LV_ALIGN_LEFT_MID, 4, 12);
            }
        }
        return;
    }

    if (!_has_target) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (memcmp(aps[i].bssid, _target_bssid, sizeof(_target_bssid)) == 0) {
            appendSample(true, aps[i].rssi);
            return;
        }
    }
    appendSample(false, 0);
}

void SignalTrackerApp::onRefreshTimer(lv_timer_t *t)
{
    SignalTrackerApp *app = (SignalTrackerApp *)lv_timer_get_user_data(t);
    if (!app->_has_target || app->_picker != nullptr || wifi_shared_scan_is_running()) {
        return;
    }
    wifi_shared_scan_start_async();
}

void SignalTrackerApp::onPollTimer(lv_timer_t *t)
{
    SignalTrackerApp *app = (SignalTrackerApp *)lv_timer_get_user_data(t);
    wifi_shared_ap_t aps[MAX_APS];
    int count = 0;
    if (wifi_shared_scan_poll(aps, MAX_APS, &count)) {
        app->onScanResults(aps, count);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, SignalTrackerApp, APP_NAME, []()
{
    return std::shared_ptr<SignalTrackerApp>(SignalTrackerApp::requestInstance(), [](SignalTrackerApp *) {});
})

} // namespace esp_watchos::apps
