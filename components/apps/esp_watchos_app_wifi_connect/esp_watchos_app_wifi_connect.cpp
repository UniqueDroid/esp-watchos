#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:WifiConnect"
#include "esp_lib_utils.h"
#include "esp_watchos_app_wifi_connect.hpp"

#include "esp_log.h"

#include <cstring>

#define APP_NAME "WiFi Connect"
#define MAX_APS 30
#define DISPLAY_HEIGHT 502
#define STATUS_BAR_HEIGHT 52
#define CONTENT_HEIGHT (DISPLAY_HEIGHT - STATUS_BAR_HEIGHT)

static const char *TAG = "WifiConnectApp";

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_wifi_connect_120_120);

namespace esp_watchos::apps {

WifiConnectApp *WifiConnectApp::_instance = nullptr;

WifiConnectApp *WifiConnectApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new WifiConnectApp();
    }
    return _instance;
}

WifiConnectApp::WifiConnectApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_wifi_connect_120_120, true, true, false)
{
}

WifiConnectApp::~WifiConnectApp()
{
}

bool WifiConnectApp::run(void)
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

    _status_timer = lv_timer_create(onStatusTimer, 300, this);

    return true;
}

bool WifiConnectApp::back(void)
{
    if (_popup != nullptr) {
        closePopup();
        return true;
    }

    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool WifiConnectApp::pause(void)
{
    if (_status_timer != nullptr) {
        lv_timer_pause(_status_timer);
    }
    return true;
}

bool WifiConnectApp::resume(void)
{
    if (_status_timer != nullptr) {
        lv_timer_resume(_status_timer);
    }
    return true;
}

void WifiConnectApp::buildUi(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "WiFi Connect");
    lv_obj_set_style_text_color(title, lv_color_hex(0x6cf0c2), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *scan_btn = lv_button_create(parent);
    lv_obj_set_size(scan_btn, 110, 42);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_add_event_cb(scan_btn, onScanClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);

    _status_label = lv_label_create(parent);
    lv_label_set_text(_status_label, "Not connected");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(_status_label, LV_ALIGN_TOP_LEFT, 8, 52);
    lv_obj_set_width(_status_label, LV_PCT(96));

    _list = lv_obj_create(parent);
    lv_obj_set_size(_list, LV_PCT(100), CONTENT_HEIGHT - 80);
    lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_color(_list, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_list, 6, 0);
    lv_obj_set_style_pad_all(_list, 6, 0);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);

    updateStatus();
}

void WifiConnectApp::startScan(void)
{
    if (wifi_shared_scan_is_running()) {
        return;
    }
    lv_label_set_text(_status_label, "Scanning...");
    wifi_shared_scan_start_async();
}

void WifiConnectApp::refreshList(void)
{
    lv_obj_clean(_list);

    for (size_t i = 0; i < _aps.size(); i++) {
        const wifi_shared_ap_t &ap = _aps[i];

        lv_obj_t *row = lv_obj_create(_list);
        lv_obj_set_size(row, LV_PCT(100), 58);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x141c22), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x355563), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(row, onRowClicked, LV_EVENT_CLICKED, this);

        lv_obj_t *ssid_lbl = lv_label_create(row);
        lv_label_set_text(ssid_lbl, ap.ssid[0] != '\0' ? ap.ssid : "<hidden>");
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(0xffffff), 0);
        lv_obj_align(ssid_lbl, LV_ALIGN_LEFT_MID, 4, -8);

        char info[48];
        bool open = (ap.authmode == WIFI_AUTH_OPEN);
        snprintf(info, sizeof(info), "Ch%u  %ddBm  %s", ap.primary, ap.rssi, open ? "OPEN" : "secured");
        lv_obj_t *info_lbl = lv_label_create(row);
        lv_label_set_text(info_lbl, info);
        lv_obj_set_style_text_color(info_lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(info_lbl, LV_ALIGN_LEFT_MID, 4, 12);
    }
}

void WifiConnectApp::showPasswordPopup(const char *ssid)
{
    closePopup();

    /* Free the (possibly long) device list's widgets while the popup - with
     * its own title/textarea/buttons/keyboard - is up; it's hidden behind
     * the popup anyway and this device has very little heap headroom. */
    lv_obj_clean(_list);

    memset(_pending_ssid, 0, sizeof(_pending_ssid));
    memcpy(_pending_ssid, ssid, sizeof(_pending_ssid) - 1);

    int popup_height = CONTENT_HEIGHT - 8;

    _popup = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(_popup);
    lv_obj_set_size(_popup, LV_PCT(96), popup_height);
    lv_obj_align(_popup, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 4);
    lv_obj_set_style_bg_color(_popup, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_bg_opa(_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_popup, lv_color_hex(0x6cf0c2), 0);
    lv_obj_set_style_border_width(_popup, 2, 0);
    lv_obj_set_style_radius(_popup, 10, 0);
    lv_obj_set_style_pad_all(_popup, 8, 0);
    lv_obj_clear_flag(_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(_popup);
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "Password for %s", ssid);
    lv_label_set_text(title, title_buf);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    _password_ta = lv_textarea_create(_popup);
    lv_textarea_set_password_mode(_password_ta, true);
    lv_textarea_set_one_line(_password_ta, true);
    lv_obj_set_size(_password_ta, LV_PCT(100), 52);
    lv_obj_set_style_text_font(_password_ta, &lv_font_montserrat_20, 0);
    lv_obj_align_to(_password_ta, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);

    lv_obj_t *connect_btn = lv_button_create(_popup);
    lv_obj_set_size(connect_btn, 110, 44);
    lv_obj_align_to(connect_btn, _password_ta, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_obj_add_event_cb(connect_btn, onConnectClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *connect_lbl = lv_label_create(connect_btn);
    lv_label_set_text(connect_lbl, "Connect");
    lv_obj_center(connect_lbl);

    lv_obj_t *cancel_btn = lv_button_create(_popup);
    lv_obj_set_size(cancel_btn, 100, 44);
    lv_obj_align_to(cancel_btn, connect_btn, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_add_event_cb(cancel_btn, onCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    buildSimpleKeyboard(_popup);

    lv_obj_add_state(_password_ta, LV_STATE_FOCUSED);
}

/* A plain lv_buttonmatrix (not lv_keyboard) - one widget for the whole grid,
 * far cheaper than one lv_obj per key. lv_keyboard's own draw path has
 * repeatedly hung on this display/LVGL combo under the tight heap this
 * device has (no PSRAM on this chip); a from-scratch one-button-per-key grid
 * also turned out to allocate too much to be safe here. */
static const char *KB_LOWER_MAP[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "123", "space", ""
};
static const char *KB_UPPER_MAP[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "123", "space", ""
};
static const char *KB_NUMERIC_MAP[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "-", "_", "=", "+", "!", "@", "#", "\n",
    "ABC", "$", "%", "^", "&", "*", "(", ")", LV_SYMBOL_BACKSPACE, "\n",
    "space", ""
};

void WifiConnectApp::buildSimpleKeyboard(lv_obj_t *parent)
{
    int keyboard_height = (CONTENT_HEIGHT - 8) * 50 / 100;

    lv_obj_t *kb = lv_buttonmatrix_create(parent);
    lv_obj_set_size(kb, LV_PCT(100), keyboard_height);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_14, 0);
    lv_buttonmatrix_set_map(kb, KB_LOWER_MAP);
    lv_obj_add_event_cb(kb, onKeyClicked, LV_EVENT_VALUE_CHANGED, this);

    _keyboard_keys = kb;
    _keyboard_numeric = false;
    _keyboard_shift = false;
}

void WifiConnectApp::handleKeyText(const char *text)
{
    if (_password_ta == nullptr || text == nullptr) {
        return;
    }
    if (strcmp(text, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_delete_char(_password_ta);
    } else if (strcmp(text, "123") == 0 || strcmp(text, "ABC") == 0) {
        _keyboard_numeric = !_keyboard_numeric;
        lv_buttonmatrix_set_map(_keyboard_keys, _keyboard_numeric ? KB_NUMERIC_MAP :
                                 (_keyboard_shift ? KB_UPPER_MAP : KB_LOWER_MAP));
    } else if (strcmp(text, LV_SYMBOL_UP) == 0) {
        _keyboard_shift = !_keyboard_shift;
        lv_buttonmatrix_set_map(_keyboard_keys, _keyboard_shift ? KB_UPPER_MAP : KB_LOWER_MAP);
    } else if (strcmp(text, "space") == 0) {
        lv_textarea_add_char(_password_ta, ' ');
    } else {
        lv_textarea_add_text(_password_ta, text);
    }
}

void WifiConnectApp::closePopup(void)
{
    if (_popup != nullptr) {
        lv_obj_del(_popup);
        _popup = nullptr;
        _password_ta = nullptr;
        _keyboard_keys = nullptr;
        refreshList();
    }
}

void WifiConnectApp::updateStatus(void)
{
    if (wifi_shared_scan_is_running()) {
        return;
    }

    char ssid[33] = {0};
    char ip[16] = {0};

    if (wifi_shared_is_connected() && wifi_shared_get_ssid(ssid, sizeof(ssid)) && wifi_shared_get_ip(ip, sizeof(ip))) {
        _connecting = false;
        char buf[80];
        snprintf(buf, sizeof(buf), "Connected to %s (%s)", ssid, ip);
        lv_label_set_text(_status_label, buf);
        lv_obj_set_style_text_color(_status_label, lv_color_hex(0x6cf0c2), 0);
        return;
    }

    if (_connecting) {
        _connect_wait_ticks++;
        if (_connect_wait_ticks > 33) { /* ~10s at the 300ms timer rate */
            _connecting = false;
            char buf[64];
            snprintf(buf, sizeof(buf), "Could not connect to %s", _connecting_ssid);
            lv_label_set_text(_status_label, buf);
            lv_obj_set_style_text_color(_status_label, lv_color_hex(0xff4444), 0);
        }
        return;
    }

    lv_label_set_text(_status_label, "Not connected");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xb8d2dd), 0);
}

void WifiConnectApp::onScanClicked(lv_event_t *e)
{
    WifiConnectApp *app = (WifiConnectApp *)lv_event_get_user_data(e);
    app->startScan();
}

void WifiConnectApp::onRowClicked(lv_event_t *e)
{
    WifiConnectApp *app = (WifiConnectApp *)lv_event_get_user_data(e);
    lv_obj_t *row = (lv_obj_t *)lv_event_get_target(e);
    size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(row);
    if (idx >= app->_aps.size()) {
        return;
    }

    const wifi_shared_ap_t &ap = app->_aps[idx];
    if (ap.authmode == WIFI_AUTH_OPEN) {
        wifi_shared_connect(ap.ssid, nullptr);
        app->updateStatus();
    } else {
        app->showPasswordPopup(ap.ssid);
    }
}

void WifiConnectApp::onConnectClicked(lv_event_t *e)
{
    WifiConnectApp *app = (WifiConnectApp *)lv_event_get_user_data(e);
    const char *password = lv_textarea_get_text(app->_password_ta);
    ESP_LOGI(TAG, "Connecting to %s", app->_pending_ssid);
    wifi_shared_connect(app->_pending_ssid, password);

    app->_connecting = true;
    app->_connect_wait_ticks = 0;
    memset(app->_connecting_ssid, 0, sizeof(app->_connecting_ssid));
    memcpy(app->_connecting_ssid, app->_pending_ssid, sizeof(app->_connecting_ssid) - 1);

    app->closePopup();

    char buf[48];
    snprintf(buf, sizeof(buf), "Connecting to %s...", app->_connecting_ssid);
    lv_label_set_text(app->_status_label, buf);
    lv_obj_set_style_text_color(app->_status_label, lv_color_hex(0xffaa00), 0);
}

void WifiConnectApp::onCancelClicked(lv_event_t *e)
{
    WifiConnectApp *app = (WifiConnectApp *)lv_event_get_user_data(e);
    app->closePopup();
}

void WifiConnectApp::onKeyClicked(lv_event_t *e)
{
    WifiConnectApp *app = (WifiConnectApp *)lv_event_get_user_data(e);
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(kb);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    const char *text = lv_buttonmatrix_get_button_text(kb, id);
    app->handleKeyText(text);
}

void WifiConnectApp::onStatusTimer(lv_timer_t *t)
{
    WifiConnectApp *app = (WifiConnectApp *)lv_timer_get_user_data(t);

    /* Skip background list/status churn while the password popup (with its
     * heavy full-QWERTY keyboard) is up - rebuilding the device list at the
     * same time the keyboard is being typed on can overwhelm rendering on
     * this display. */
    if (app->_popup != nullptr) {
        return;
    }

    wifi_shared_ap_t aps[MAX_APS];
    int count = 0;
    if (wifi_shared_scan_poll(aps, MAX_APS, &count)) {
        app->_aps.assign(aps, aps + count);
        app->refreshList();
    }

    app->updateStatus();
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, WifiConnectApp, APP_NAME, []()
{
    return std::shared_ptr<WifiConnectApp>(WifiConnectApp::requestInstance(), [](WifiConnectApp *) {});
})

} // namespace esp_watchos::apps
