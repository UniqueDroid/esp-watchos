#pragma once

#include <vector>

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "wifi_shared.h"

namespace esp_watchos::apps {

class SignalTrackerApp: public esp_brookesia::systems::phone::App {
public:
    static SignalTrackerApp *requestInstance();
    ~SignalTrackerApp();

protected:
    SignalTrackerApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static constexpr int HISTORY_LEN = 30;

    void buildUi(lv_obj_t *parent);
    void showPicker(void);
    void closePicker(void);
    void selectTarget(const wifi_shared_ap_t &ap);
    void appendSample(bool seen, int rssi);
    void onScanResults(const wifi_shared_ap_t *aps, int count);

    static void onPickClicked(lv_event_t *e);
    static void onPickerRowClicked(lv_event_t *e);
    static void onRefreshTimer(lv_timer_t *t);
    static void onPollTimer(lv_timer_t *t);

    static SignalTrackerApp *_instance;

    lv_obj_t *_status_label = nullptr;
    lv_obj_t *_target_label = nullptr;
    lv_obj_t *_chart = nullptr;
    lv_chart_series_t *_series = nullptr;
    lv_obj_t *_picker = nullptr;
    lv_timer_t *_refresh_timer = nullptr;
    lv_timer_t *_poll_timer = nullptr;

    bool _has_target = false;
    bool _picker_loading = false;
    char _target_ssid[33] = {0};
    uint8_t _target_bssid[6] = {0};

    std::vector<wifi_shared_ap_t> _picker_aps;
};

} // namespace esp_watchos::apps
