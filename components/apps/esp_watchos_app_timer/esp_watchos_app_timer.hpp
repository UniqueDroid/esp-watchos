#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "alarm_shared.h"

namespace esp_watchos::apps {

class TimerApp: public esp_brookesia::systems::phone::App {
public:
    static TimerApp *requestInstance();
    ~TimerApp();

protected:
    TimerApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    void buildUi(lv_obj_t *parent);
    void updateCountdownLabel(void);
    void updateAlarmLabel(void);
    void setCountdownRunning(bool running);

    static void onPresetClicked(lv_event_t *e);
    static void onStartPauseClicked(lv_event_t *e);
    static void onResetClicked(lv_event_t *e);
    static void onCountdownTick(lv_timer_t *t);

    static void onAlarmHourChanged(lv_event_t *e);
    static void onAlarmMinuteChanged(lv_event_t *e);
    static void onAlarmToggled(lv_event_t *e);

    static TimerApp *_instance;

    lv_obj_t *_countdown_label = nullptr;
    lv_obj_t *_start_pause_label = nullptr;
    lv_timer_t *_countdown_timer = nullptr;
    int _pending_seconds = 0;

    lv_obj_t *_alarm_label = nullptr;
    alarm_shared_config_t _alarm_cfg = {};
};

} // namespace esp_watchos::apps
