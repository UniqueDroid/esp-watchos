#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "wifi_shared.h"

namespace esp_watchos::apps {

class PortCheckerApp: public esp_brookesia::systems::phone::App {
public:
    static PortCheckerApp *requestInstance();
    ~PortCheckerApp();

protected:
    PortCheckerApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    void buildUi(lv_obj_t *parent);
    void showIpEditor(void);
    void closeIpEditor(void);
    void startScan(void);
    void buildSimpleKeypad(lv_obj_t *parent);
    void handleKeyText(const char *text);

    static void onEditClicked(lv_event_t *e);
    static void onIpSaveClicked(lv_event_t *e);
    static void onIpCancelClicked(lv_event_t *e);
    static void onScanClicked(lv_event_t *e);
    static void onPollTimer(lv_timer_t *t);
    static void onKeyClicked(lv_event_t *e);
    static void scanTask(void *arg);

    static PortCheckerApp *_instance;

    lv_obj_t *_target_label = nullptr;
    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_list = nullptr;
    lv_obj_t *_ip_popup = nullptr;
    lv_obj_t *_ip_ta = nullptr;
    lv_obj_t *_keypad = nullptr;
    lv_timer_t *_poll_timer = nullptr;

    bool _scan_running = false;
    QueueHandle_t _result_queue = nullptr;
    char _target_ip[16] = {0};
};

} // namespace esp_watchos::apps
