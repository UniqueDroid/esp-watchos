#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:PortChecker"
#include "esp_lib_utils.h"
#include "esp_watchos_app_port_checker.hpp"

#include "esp_netif.h"
#include "lwip/sockets.h"
#include <vector>
#include <cstring>
#include <cstdlib>

#define APP_NAME "Port Checker"
#define DISPLAY_HEIGHT 502
#define STATUS_BAR_HEIGHT 52
#define CONTENT_HEIGHT (DISPLAY_HEIGHT - STATUS_BAR_HEIGHT)

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_port_checker_120_120);

namespace esp_watchos::apps {

static const uint16_t COMMON_PORTS[] = {21, 22, 23, 25, 53, 80, 110, 143, 443, 445, 3389, 8080, 8443};

struct PortScanEntry {
    uint16_t port;
    bool open;
};

struct PortScanResult {
    std::vector<PortScanEntry> entries;
    std::string status_text;
    bool any_open;
};

PortCheckerApp *PortCheckerApp::_instance = nullptr;

PortCheckerApp *PortCheckerApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new PortCheckerApp();
    }
    return _instance;
}

PortCheckerApp::PortCheckerApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_port_checker_120_120, true, true, false)
{
}

PortCheckerApp::~PortCheckerApp()
{
}

static bool guess_gateway(char *buf, size_t len)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) {
        return false;
    }
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.gw.addr == 0) {
        return false;
    }
    esp_ip4addr_ntoa(&ip_info.gw, buf, len);
    return true;
}

static bool check_port_open(const char *ip, uint16_t port, int timeout_ms)
{
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        return false;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    bool open = false;
    int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret == 0) {
        open = true;
    } else if (errno == EINPROGRESS) {
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(sock, &write_set);
        struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
        if (select(sock + 1, nullptr, &write_set, nullptr, &tv) > 0) {
            int err = 0;
            socklen_t err_len = sizeof(err);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &err_len);
            open = (err == 0);
        }
    }

    close(sock);
    return open;
}

bool PortCheckerApp::run(void)
{
    wifi_shared_try_autoconnect();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    if (_target_ip[0] == '\0') {
        if (!guess_gateway(_target_ip, sizeof(_target_ip))) {
            strncpy(_target_ip, "192.168.1.1", sizeof(_target_ip) - 1);
        }
    }

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), CONTENT_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

    buildUi(content);
    if (_result_queue == nullptr) {
        _result_queue = xQueueCreate(1, sizeof(PortScanResult *));
    }
    _poll_timer = lv_timer_create(onPollTimer, 200, this);

    return true;
}

bool PortCheckerApp::back(void)
{
    if (_ip_popup != nullptr) {
        closeIpEditor();
        return true;
    }

    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool PortCheckerApp::pause(void)
{
    if (_poll_timer != nullptr) {
        lv_timer_pause(_poll_timer);
    }
    return true;
}

bool PortCheckerApp::resume(void)
{
    if (_poll_timer != nullptr) {
        lv_timer_resume(_poll_timer);
    }
    return true;
}

void PortCheckerApp::buildUi(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_SHUFFLE " Port Checker");
    lv_obj_set_style_text_color(title, lv_color_hex(0xff8844), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *scan_btn = lv_button_create(parent);
    lv_obj_set_size(scan_btn, 110, 42);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_add_event_cb(scan_btn, onScanClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, "Scan");
    lv_obj_center(scan_label);

    _target_label = lv_label_create(parent);
    char buf[40];
    snprintf(buf, sizeof(buf), "Target: %s", _target_ip);
    lv_label_set_text(_target_label, buf);
    lv_obj_set_style_text_color(_target_label, lv_color_hex(0xffffff), 0);
    lv_obj_align(_target_label, LV_ALIGN_TOP_LEFT, 8, 34);

    lv_obj_t *edit_btn = lv_button_create(parent);
    lv_obj_set_size(edit_btn, 76, 36);
    lv_obj_align_to(edit_btn, _target_label, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_add_event_cb(edit_btn, onEditClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *edit_label = lv_label_create(edit_btn);
    lv_label_set_text(edit_label, "Edit");
    lv_obj_center(edit_label);

    _status_label = lv_label_create(parent);
    lv_label_set_text(_status_label, !wifi_shared_is_connected() ? "Not connected - use WiFi Connect app first" : "Ready");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_16, 0);
    lv_obj_align(_status_label, LV_ALIGN_TOP_LEFT, 8, 80);
    lv_obj_set_width(_status_label, LV_PCT(96));

    _list = lv_obj_create(parent);
    lv_obj_set_size(_list, LV_PCT(100), CONTENT_HEIGHT - 112);
    lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_style_bg_color(_list, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(_list, 6, 0);
    lv_obj_set_style_pad_column(_list, 6, 0);
    lv_obj_set_style_pad_all(_list, 6, 0);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
}

void PortCheckerApp::showIpEditor(void)
{
    closeIpEditor();

    _ip_popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_ip_popup, LV_PCT(96), CONTENT_HEIGHT - 8);
    lv_obj_align(_ip_popup, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 4);
    lv_obj_set_style_bg_color(_ip_popup, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_border_color(_ip_popup, lv_color_hex(0xff8844), 0);
    lv_obj_set_style_border_width(_ip_popup, 2, 0);
    lv_obj_set_style_radius(_ip_popup, 10, 0);
    lv_obj_set_style_pad_all(_ip_popup, 10, 0);

    lv_obj_t *title = lv_label_create(_ip_popup);
    lv_label_set_text(title, "Target IP address");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

    _ip_ta = lv_textarea_create(_ip_popup);
    lv_textarea_set_one_line(_ip_ta, true);
    lv_textarea_set_text(_ip_ta, _target_ip);
    lv_obj_set_width(_ip_ta, LV_PCT(60));
    lv_obj_align(_ip_ta, LV_ALIGN_TOP_LEFT, 4, 30);

    lv_obj_t *save_btn = lv_button_create(_ip_popup);
    lv_obj_set_size(save_btn, 90, 44);
    lv_obj_align(save_btn, LV_ALIGN_TOP_RIGHT, -4, 26);
    lv_obj_add_event_cb(save_btn, onIpSaveClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);

    lv_obj_t *cancel_btn = lv_button_create(_ip_popup);
    lv_obj_set_size(cancel_btn, 90, 40);
    lv_obj_align_to(cancel_btn, _ip_ta, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_add_event_cb(cancel_btn, onIpCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    buildSimpleKeypad(_ip_popup);
}

/* A plain lv_buttonmatrix (not lv_keyboard) for a numeric-only IP address
 * entry - far fewer keys/draw work than even lv_keyboard's NUMBER mode, and
 * avoids lv_keyboard's draw path, which has repeatedly hung in
 * lv_draw_add_task on this memory-constrained display/LVGL combo. */
static const char *KEYPAD_MAP[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    ".", "0", LV_SYMBOL_BACKSPACE, ""
};

void PortCheckerApp::buildSimpleKeypad(lv_obj_t *parent)
{
    _keypad = lv_buttonmatrix_create(parent);
    lv_obj_set_size(_keypad, LV_PCT(60), LV_PCT(55));
    lv_obj_align(_keypad, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(_keypad, &lv_font_montserrat_16, 0);
    lv_buttonmatrix_set_map(_keypad, KEYPAD_MAP);
    lv_obj_add_event_cb(_keypad, onKeyClicked, LV_EVENT_VALUE_CHANGED, this);
}

void PortCheckerApp::handleKeyText(const char *text)
{
    if (_ip_ta == nullptr || text == nullptr) {
        return;
    }
    if (strcmp(text, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_delete_char(_ip_ta);
    } else {
        lv_textarea_add_text(_ip_ta, text);
    }
}

void PortCheckerApp::closeIpEditor(void)
{
    if (_ip_popup != nullptr) {
        lv_obj_del(_ip_popup);
        _ip_popup = nullptr;
        _ip_ta = nullptr;
        _keypad = nullptr;
    }
}

void PortCheckerApp::startScan(void)
{
    if (_scan_running) {
        return;
    }

    if (!wifi_shared_is_connected()) {
        lv_label_set_text(_status_label, "Not connected - use WiFi Connect app first");
        lv_obj_set_style_text_color(_status_label, lv_color_hex(0xff8888), 0);
        return;
    }

    lv_label_set_text(_status_label, "Scanning ports...");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xb8d2dd), 0);

    _scan_running = true;

    char *target = strdup(_target_ip);
    xTaskCreate(scanTask, "port_scan", 6144, target, 5, nullptr);
}

void PortCheckerApp::scanTask(void *arg)
{
    char *target = (char *)arg;
    PortCheckerApp *app = requestInstance();
    PortScanResult *result = new PortScanResult();

    int open_count = 0;
    for (uint16_t port : COMMON_PORTS) {
        bool open = check_port_open(target, port, 400);
        if (open) {
            open_count++;
        }
        result->entries.push_back({port, open});
    }

    char status[64];
    snprintf(status, sizeof(status), "%d of %d ports open on %s", open_count,
             (int)(sizeof(COMMON_PORTS) / sizeof(COMMON_PORTS[0])), target);
    result->status_text = status;
    result->any_open = open_count > 0;

    // If a previous result is still sitting undrained (poll timer was
    // paused, or the user hit "rescan" before it was picked up),
    // xQueueOverwrite would silently orphan that pointer - drain and free
    // it first.
    PortScanResult *stale = nullptr;
    if (xQueueReceive(app->_result_queue, &stale, 0) == pdTRUE) {
        delete stale;
    }
    xQueueOverwrite(app->_result_queue, &result);
    app->_scan_running = false;
    free(target);
    vTaskDelete(NULL);
}

void PortCheckerApp::onEditClicked(lv_event_t *e)
{
    PortCheckerApp *app = (PortCheckerApp *)lv_event_get_user_data(e);

    /* Clear the previous scan's results - they're for the old target and
     * would be misleading once a different IP is being typed in. */
    lv_obj_clean(app->_list);
    lv_label_set_text(app->_status_label, "Ready");
    lv_obj_set_style_text_color(app->_status_label, lv_color_hex(0xb8d2dd), 0);

    app->showIpEditor();
}

void PortCheckerApp::onKeyClicked(lv_event_t *e)
{
    PortCheckerApp *app = (PortCheckerApp *)lv_event_get_user_data(e);
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(kb);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    app->handleKeyText(lv_buttonmatrix_get_button_text(kb, id));
}

void PortCheckerApp::onIpSaveClicked(lv_event_t *e)
{
    PortCheckerApp *app = (PortCheckerApp *)lv_event_get_user_data(e);
    const char *text = lv_textarea_get_text(app->_ip_ta);
    memset(app->_target_ip, 0, sizeof(app->_target_ip));
    strncpy(app->_target_ip, text, sizeof(app->_target_ip) - 1);

    char buf[40];
    snprintf(buf, sizeof(buf), "Target: %s", app->_target_ip);
    lv_label_set_text(app->_target_label, buf);

    app->closeIpEditor();
}

void PortCheckerApp::onIpCancelClicked(lv_event_t *e)
{
    PortCheckerApp *app = (PortCheckerApp *)lv_event_get_user_data(e);
    app->closeIpEditor();
}

void PortCheckerApp::onScanClicked(lv_event_t *e)
{
    PortCheckerApp *app = (PortCheckerApp *)lv_event_get_user_data(e);
    app->startScan();
}

void PortCheckerApp::onPollTimer(lv_timer_t *t)
{
    PortCheckerApp *app = (PortCheckerApp *)lv_timer_get_user_data(t);
    if (app->_result_queue == nullptr) {
        return;
    }

    PortScanResult *result = nullptr;
    if (xQueueReceive(app->_result_queue, &result, 0) != pdTRUE) {
        return;
    }

    lv_obj_clean(app->_list);
    for (const auto &entry : result->entries) {
        lv_obj_t *chip = lv_obj_create(app->_list);
        lv_obj_set_size(chip, 96, 54);
        lv_obj_set_style_bg_color(chip, entry.open ? lv_color_hex(0x123322) : lv_color_hex(0x141c22), 0);
        lv_obj_set_style_border_color(chip, entry.open ? lv_color_hex(0x44ff88) : lv_color_hex(0x355563), 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_radius(chip, 6, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(chip);
        char buf[16];
        snprintf(buf, sizeof(buf), "%u\n%s", entry.port, entry.open ? "OPEN" : "closed");
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(lbl, entry.open ? lv_color_hex(0x44ff88) : lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_center(lbl);
    }

    lv_label_set_text(app->_status_label, result->status_text.c_str());
    lv_obj_set_style_text_color(app->_status_label, result->any_open ? lv_color_hex(0xffaa00) : lv_color_hex(0x6cf0c2), 0);

    delete result;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, PortCheckerApp, APP_NAME, []()
{
    return std::shared_ptr<PortCheckerApp>(PortCheckerApp::requestInstance(), [](PortCheckerApp *) {});
})

} // namespace esp_watchos::apps
