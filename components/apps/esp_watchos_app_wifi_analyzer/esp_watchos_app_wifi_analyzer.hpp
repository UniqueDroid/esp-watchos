#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "wifi_shared.h"

namespace esp_watchos::apps {

class WifiAnalyzerApp: public esp_brookesia::systems::phone::App {
public:
    static WifiAnalyzerApp *requestInstance();
    ~WifiAnalyzerApp();

protected:
    WifiAnalyzerApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static constexpr int NUM_CHANNELS = 13;

    void buildUi(lv_obj_t *parent);
    void startScan(void);
    void onScanResults(const wifi_shared_ap_t *aps, int count);

    static void onScanClicked(lv_event_t *e);
    static void onPollTimer(lv_timer_t *t);

    static WifiAnalyzerApp *_instance;

    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_chart = nullptr;
    lv_chart_series_t *_series = nullptr;
    lv_timer_t *_poll_timer = nullptr;
};

} // namespace esp_watchos::apps
