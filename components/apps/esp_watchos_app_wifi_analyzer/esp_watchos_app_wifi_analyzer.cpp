#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:WifiAnalyzer"
#include "esp_lib_utils.h"
#include "esp_watchos_app_wifi_analyzer.hpp"

#include <vector>

#define APP_NAME "WiFi Analyzer"
#define MAX_APS 40
#define DISPLAY_HEIGHT 502
#define STATUS_BAR_HEIGHT 52
#define CONTENT_HEIGHT (DISPLAY_HEIGHT - STATUS_BAR_HEIGHT)

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_wifi_analyzer_120_120);

namespace esp_watchos::apps {

WifiAnalyzerApp *WifiAnalyzerApp::_instance = nullptr;

WifiAnalyzerApp *WifiAnalyzerApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new WifiAnalyzerApp();
    }
    return _instance;
}

WifiAnalyzerApp::WifiAnalyzerApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_wifi_analyzer_120_120, true, true, false)
{
}

WifiAnalyzerApp::~WifiAnalyzerApp()
{
}

bool WifiAnalyzerApp::run(void)
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
    startScan();
    _poll_timer = lv_timer_create(onPollTimer, 200, this);

    return true;
}

bool WifiAnalyzerApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool WifiAnalyzerApp::pause(void)
{
    if (_poll_timer != nullptr) {
        lv_timer_pause(_poll_timer);
    }
    return true;
}

bool WifiAnalyzerApp::resume(void)
{
    if (_poll_timer != nullptr) {
        lv_timer_resume(_poll_timer);
    }
    return true;
}

void WifiAnalyzerApp::buildUi(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_WIFI " WiFi Analyzer");
    lv_obj_set_style_text_color(title, lv_color_hex(0x4488ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *scan_btn = lv_button_create(parent);
    lv_obj_set_size(scan_btn, 110, 42);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_add_event_cb(scan_btn, onScanClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);

    _status_label = lv_label_create(parent);
    lv_label_set_text(_status_label, "Ready");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(_status_label, LV_ALIGN_TOP_LEFT, 8, 52);
    lv_obj_set_width(_status_label, LV_PCT(96));

    _chart = lv_chart_create(parent);
    lv_obj_set_size(_chart, LV_PCT(94), CONTENT_HEIGHT - 130);
    lv_obj_align(_chart, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_chart_set_type(_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(_chart, NUM_CHANNELS);
    lv_chart_set_range(_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -20);
    lv_obj_set_style_bg_color(_chart, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_border_color(_chart, lv_color_hex(0x355563), 0);
    lv_obj_set_style_radius(_chart, 8, 0);
    lv_chart_set_div_line_count(_chart, 3, NUM_CHANNELS);
    _series = lv_chart_add_series(_chart, lv_color_hex(0x4488ff), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < NUM_CHANNELS; i++) {
        lv_chart_set_value_by_id(_chart, _series, i, -100);
    }

    lv_obj_t *x_lbl = lv_label_create(parent);
    lv_label_set_text(x_lbl, "Channel 1 - 13 (2.4 GHz)   |   bar height = strongest RSSI seen");
    lv_obj_set_style_text_color(x_lbl, lv_color_hex(0x666688), 0);
    lv_obj_set_style_text_font(x_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(x_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void WifiAnalyzerApp::startScan(void)
{
    if (wifi_shared_scan_is_running()) {
        return;
    }
    lv_label_set_text(_status_label, "Scanning...");
    wifi_shared_scan_start_async();
}

void WifiAnalyzerApp::onScanResults(const wifi_shared_ap_t *aps_ptr, int count)
{
    std::vector<wifi_shared_ap_t> aps(aps_ptr, aps_ptr + count);

    int8_t channel_rssi[NUM_CHANNELS + 1];
    for (int i = 0; i <= NUM_CHANNELS; i++) {
        channel_rssi[i] = -100;
    }

    int channels_used = 0;
    bool seen[NUM_CHANNELS + 1] = {false};
    for (const auto &ap : aps) {
        if (ap.primary >= 1 && ap.primary <= NUM_CHANNELS) {
            if (ap.rssi > channel_rssi[ap.primary]) {
                channel_rssi[ap.primary] = ap.rssi;
            }
            if (!seen[ap.primary]) {
                seen[ap.primary] = true;
                channels_used++;
            }
        }
    }

    for (int ch = 1; ch <= NUM_CHANNELS; ch++) {
        lv_chart_set_value_by_id(_chart, _series, ch - 1, channel_rssi[ch]);
    }
    lv_chart_refresh(_chart);

    char status[64];
    snprintf(status, sizeof(status), "%d APs found on %d of %d channels", count, channels_used, NUM_CHANNELS);
    lv_label_set_text(_status_label, status);
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0x6cf0c2), 0);
}

void WifiAnalyzerApp::onScanClicked(lv_event_t *e)
{
    WifiAnalyzerApp *app = (WifiAnalyzerApp *)lv_event_get_user_data(e);
    app->startScan();
}

void WifiAnalyzerApp::onPollTimer(lv_timer_t *t)
{
    WifiAnalyzerApp *app = (WifiAnalyzerApp *)lv_timer_get_user_data(t);
    wifi_shared_ap_t aps[MAX_APS];
    int count = 0;
    if (wifi_shared_scan_poll(aps, MAX_APS, &count)) {
        app->onScanResults(aps, count);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, WifiAnalyzerApp, APP_NAME, []()
{
    return std::shared_ptr<WifiAnalyzerApp>(WifiAnalyzerApp::requestInstance(), [](WifiAnalyzerApp *) {});
})

} // namespace esp_watchos::apps
