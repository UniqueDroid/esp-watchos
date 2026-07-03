#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "rtc_shared.h"
#include <vector>
#include <string>

namespace esp_watchos::apps {

/* A watchface "face" is a small JSON file living on the on-device filesystem
 * at /data/apps/watchface/faces/ as JSON files. Adding a new watchface means dropping
 * a new file there - no firmware rebuild needed. */
struct WatchfaceDescriptor {
    std::string filename;
    std::string name;
    std::string type; // "analog" or "digital"
    std::string bg_color;
    std::string ring_color;
    std::string major_color;
    std::string minor_color;
    std::string hour_color;
    std::string minute_color;
    std::string second_color;
    std::string time_color;
    std::string date_color;

    /* Optional vector-styling extras - all backward compatible, faces that
     * omit them keep rendering exactly as before. */
    std::string bg_color2;    // if set, gradient from bg_color (top) to bg_color2 (bottom)
    std::string accent_color; // if set, adds a soft glow ring + tints the 12-o'clock mark
    std::string marks;        // "numbers" (default) | "ticks" | "dots"
};

class WatchfaceApp: public esp_brookesia::systems::phone::App {
public:
    static WatchfaceApp *requestInstance();
    ~WatchfaceApp();

protected:
    WatchfaceApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    void loadFaces(void);
    void buildFace(lv_obj_t *scr);
    void buildAnalogFace(lv_obj_t *parent, const WatchfaceDescriptor &desc);
    void buildDigitalFace(lv_obj_t *parent, const WatchfaceDescriptor &desc);
    void buildModularFace(lv_obj_t *parent, const WatchfaceDescriptor &desc);
    void updateFace(void);
    void cycleFace(void);

    static void onTick(lv_timer_t *t);
    static void onScreenClicked(lv_event_t *e);

    static WatchfaceApp *_instance;

    std::vector<WatchfaceDescriptor> _faces;
    size_t _face_index = 0;
    lv_timer_t *_timer = nullptr;

    lv_obj_t *_face_root = nullptr;
    lv_obj_t *_hour_hand = nullptr;
    lv_obj_t *_minute_hand = nullptr;
    lv_obj_t *_second_hand = nullptr;
    /* lv_line keeps a pointer (not a copy) to these, so they must outlive the line objects. */
    lv_point_precise_t _hour_points[2] = {};
    lv_point_precise_t _minute_points[2] = {};
    lv_point_precise_t _second_points[2] = {};
    lv_obj_t *_analog_date_label = nullptr;

    lv_obj_t *_digital_time_label = nullptr;
    lv_obj_t *_digital_date_label = nullptr;

    /* "modular" face complications - small LVGL containers/labels, no
     * arcs/gauges (those need the same costly draw-layer machinery as the
     * shadow glow that turned out to be too heavy). The RSSI chart reuses
     * lv_chart exactly as Signal Tracker does - already proven safe. */
    static constexpr int WIFI_HISTORY_LEN = 30;
    lv_obj_t *_mod_icon = nullptr;
    lv_obj_t *_mod_time_label = nullptr;
    lv_obj_t *_mod_status_label = nullptr;
    lv_obj_t *_mod_date_label = nullptr;
    lv_obj_t *_mod_alarm_label = nullptr;
    lv_obj_t *_mod_chart = nullptr;
    lv_chart_series_t *_mod_chart_series = nullptr;
};

} // namespace esp_watchos::apps
