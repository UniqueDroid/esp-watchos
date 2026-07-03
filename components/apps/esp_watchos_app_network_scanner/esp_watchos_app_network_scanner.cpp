#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:NetworkScanner"
#include "esp_lib_utils.h"
#include "esp_watchos_app_network_scanner.hpp"

#include "mdns.h"
#include "esp_netif.h"

#define APP_NAME "Network Scanner"
#define DISPLAY_HEIGHT 502
#define STATUS_BAR_HEIGHT 52
#define CONTENT_HEIGHT (DISPLAY_HEIGHT - STATUS_BAR_HEIGHT)

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_network_scanner_120_120);

namespace esp_watchos::apps {

struct NetworkScanResult {
    std::vector<NetworkDevice> devices;
    std::string status_text;
};

NetworkScannerApp *NetworkScannerApp::_instance = nullptr;

NetworkScannerApp *NetworkScannerApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new NetworkScannerApp();
    }
    return _instance;
}

NetworkScannerApp::NetworkScannerApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_network_scanner_120_120, true, true, false)
{
}

NetworkScannerApp::~NetworkScannerApp()
{
}

bool NetworkScannerApp::run(void)
{
    wifi_shared_try_autoconnect();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), CONTENT_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

    buildUi(content);
    if (_result_queue == nullptr) {
        _result_queue = xQueueCreate(1, sizeof(NetworkScanResult *));
    }
    startScan();
    _poll_timer = lv_timer_create(onPollTimer, 200, this);

    return true;
}

bool NetworkScannerApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool NetworkScannerApp::pause(void)
{
    if (_poll_timer != nullptr) {
        lv_timer_pause(_poll_timer);
    }
    return true;
}

bool NetworkScannerApp::resume(void)
{
    if (_poll_timer != nullptr) {
        lv_timer_resume(_poll_timer);
    }
    return true;
}

bool NetworkScannerApp::close(void)
{
    if (_mdns_started) {
        mdns_free();
        _mdns_started = false;
    }
    return true;
}

void NetworkScannerApp::buildUi(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_LIST " Network Scanner");
    lv_obj_set_style_text_color(title, lv_color_hex(0x44ffaa), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 6);

    lv_obj_t *scan_btn = lv_button_create(parent);
    lv_obj_set_size(scan_btn, 120, 42);
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

    _list = lv_obj_create(parent);
    lv_obj_set_size(_list, LV_PCT(100), CONTENT_HEIGHT - 84);
    lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(_list, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_list, 6, 0);
    lv_obj_set_style_pad_all(_list, 6, 0);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
}

void NetworkScannerApp::startScan(void)
{
    if (_scan_running) {
        return;
    }

    if (!wifi_shared_is_connected()) {
        lv_label_set_text(_status_label, "Not connected - use the WiFi Connect app first");
        lv_obj_set_style_text_color(_status_label, lv_color_hex(0xff8888), 0);
        _devices.clear();
        refreshList();
        return;
    }

    lv_label_set_text(_status_label, "Scanning network...");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0xb8d2dd), 0);

    _scan_running = true;
    xTaskCreate(scanTask, "net_scan", 8192, this, 5, nullptr);
}

void NetworkScannerApp::scanTask(void *arg)
{
    NetworkScannerApp *app = (NetworkScannerApp *)arg;
    NetworkScanResult *result = new NetworkScanResult();

    char own_ip[16];
    if (wifi_shared_get_ip(own_ip, sizeof(own_ip))) {
        result->devices.push_back({own_ip, "This device"});
    }

    if (!app->_mdns_started) {
        if (mdns_init() == ESP_OK) {
            app->_mdns_started = true;
        }
    }

    if (app->_mdns_started) {
        static const char *services[][2] = {
            {"_http", "_tcp"},
            {"_ssh", "_tcp"},
            {"_smb", "_tcp"},
            {"_ftp", "_tcp"},
            {"_printer", "_tcp"},
            {"_workstation", "_tcp"},
        };

        for (auto &service : services) {
            mdns_result_t *results = nullptr;
            esp_err_t err = mdns_query_ptr(service[0], service[1], 2000, 10, &results);
            if (err != ESP_OK || results == nullptr) {
                continue;
            }

            for (mdns_result_t *r = results; r != nullptr; r = r->next) {
                std::string ip;
                mdns_ip_addr_t *addr = r->addr;
                while (addr != nullptr) {
                    if (addr->addr.type == ESP_IPADDR_TYPE_V4) {
                        char buf[16];
                        esp_ip4addr_ntoa(&addr->addr.u_addr.ip4, buf, sizeof(buf));
                        ip = buf;
                        break;
                    }
                    addr = addr->next;
                }

                if (ip.empty()) {
                    continue;
                }

                bool dup = false;
                for (auto &dev : result->devices) {
                    if (dev.ip == ip) {
                        dup = true;
                        break;
                    }
                }
                if (dup) {
                    continue;
                }

                std::string hostname = (r->hostname != nullptr) ? r->hostname : (std::string(service[0]) + " device");
                result->devices.push_back({ip, hostname});
            }

            mdns_query_results_free(results);
        }
    }

    char status[48];
    snprintf(status, sizeof(status), "%d device(s) found", (int)result->devices.size());
    result->status_text = status;

    // If a previous result is still sitting undrained (poll timer was
    // paused, or the user hit "rescan" before it was picked up),
    // xQueueOverwrite would silently orphan that pointer - drain and free
    // it first.
    NetworkScanResult *stale = nullptr;
    if (xQueueReceive(app->_result_queue, &stale, 0) == pdTRUE) {
        delete stale;
    }
    xQueueOverwrite(app->_result_queue, &result);
    app->_scan_running = false;
    vTaskDelete(NULL);
}

void NetworkScannerApp::refreshList(void)
{
    lv_obj_clean(_list);

    for (const auto &dev : _devices) {
        lv_obj_t *row = lv_obj_create(_list);
        lv_obj_set_size(row, LV_PCT(100), 58);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x141c22), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(0x355563), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *ip_lbl = lv_label_create(row);
        lv_label_set_text(ip_lbl, dev.ip.c_str());
        lv_obj_set_style_text_color(ip_lbl, lv_color_hex(0xffffff), 0);
        lv_obj_align(ip_lbl, LV_ALIGN_LEFT_MID, 6, -8);

        lv_obj_t *host_lbl = lv_label_create(row);
        lv_label_set_text(host_lbl, dev.hostname.c_str());
        lv_obj_set_style_text_color(host_lbl, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(host_lbl, &lv_font_montserrat_16, 0);
        lv_obj_align(host_lbl, LV_ALIGN_LEFT_MID, 6, 12);
    }
}

void NetworkScannerApp::onScanClicked(lv_event_t *e)
{
    NetworkScannerApp *app = (NetworkScannerApp *)lv_event_get_user_data(e);
    app->startScan();
}

void NetworkScannerApp::onPollTimer(lv_timer_t *t)
{
    NetworkScannerApp *app = (NetworkScannerApp *)lv_timer_get_user_data(t);
    if (app->_result_queue == nullptr) {
        return;
    }

    NetworkScanResult *result = nullptr;
    if (xQueueReceive(app->_result_queue, &result, 0) != pdTRUE) {
        return;
    }

    app->_devices = result->devices;
    lv_label_set_text(app->_status_label, result->status_text.c_str());
    lv_obj_set_style_text_color(app->_status_label, lv_color_hex(0x6cf0c2), 0);
    app->refreshList();

    delete result;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, NetworkScannerApp, APP_NAME, []()
{
    return std::shared_ptr<NetworkScannerApp>(NetworkScannerApp::requestInstance(), [](NetworkScannerApp *) {});
})

} // namespace esp_watchos::apps
