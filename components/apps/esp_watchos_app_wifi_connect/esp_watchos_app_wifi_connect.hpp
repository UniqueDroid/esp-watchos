#pragma once

#include <vector>

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "wifi_shared.h"

namespace esp_watchos::apps {

class WifiConnectApp: public esp_brookesia::systems::phone::App {
public:
    static WifiConnectApp *requestInstance();
    ~WifiConnectApp();

    using esp_brookesia::systems::phone::App::startRecordResource;
    using esp_brookesia::systems::phone::App::endRecordResource;

protected:
    WifiConnectApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    void buildUi(lv_obj_t *parent);
    void startScan(void);
    void refreshList(void);
    void showPasswordPopup(const char *ssid);
    void closePopup(void);
    void updateStatus(void);
    void buildSimpleKeyboard(lv_obj_t *parent);
    void handleKeyText(const char *text);

    static void onScanClicked(lv_event_t *e);
    static void onRowClicked(lv_event_t *e);
    static void onConnectClicked(lv_event_t *e);
    static void onCancelClicked(lv_event_t *e);
    static void onStatusTimer(lv_timer_t *t);
    static void onKeyClicked(lv_event_t *e);

    static WifiConnectApp *_instance;

    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_list = nullptr;
    lv_obj_t *_popup = nullptr;
    lv_obj_t *_password_ta = nullptr;
    lv_obj_t *_keyboard_keys = nullptr;
    bool _keyboard_numeric = false;
    bool _keyboard_shift = false;
    lv_timer_t *_status_timer = nullptr;

    char _pending_ssid[33] = {0};
    char _connecting_ssid[33] = {0};
    bool _connecting = false;
    int _connect_wait_ticks = 0;
    std::vector<wifi_shared_ap_t> _aps;
};

} // namespace esp_watchos::apps
