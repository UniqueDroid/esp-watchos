#pragma once

#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "wifi_shared.h"

namespace esp_watchos::apps {

struct NetworkDevice {
    std::string ip;
    std::string hostname;
};

class NetworkScannerApp: public esp_brookesia::systems::phone::App {
public:
    static NetworkScannerApp *requestInstance();
    ~NetworkScannerApp();

protected:
    NetworkScannerApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;
    bool close(void) override;

private:
    void buildUi(lv_obj_t *parent);
    void startScan(void);
    void refreshList(void);

    static void onScanClicked(lv_event_t *e);
    static void onPollTimer(lv_timer_t *t);
    static void scanTask(void *arg);

    static NetworkScannerApp *_instance;

    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_list = nullptr;
    lv_timer_t *_poll_timer = nullptr;

    bool _mdns_started = false;
    bool _scan_running = false;
    QueueHandle_t _result_queue = nullptr;
    std::vector<NetworkDevice> _devices;
};

} // namespace esp_watchos::apps
