#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "rtc_shared.h"
#include "homescreen_shared.h"
#include "webserver_shared.h"
#include "bt_shared.h"
#include "weather_shared.h"
#include <vector>
#include <string>

namespace esp_watchos::apps {

struct WatchfaceEntry {
    std::string filename;
    std::string name;
    lv_obj_t *button = nullptr;
};

struct HomeColorSwatch {
    uint32_t color;
    lv_obj_t *obj = nullptr;
};

enum class SettingsCategory {
    TIME,
    DISPLAY,
    FACES,
    SERVER,
    BLUETOOTH,
    COUNT,
};

class SettingsApp: public esp_brookesia::systems::phone::App {
public:
    static SettingsApp *requestInstance();
    ~SettingsApp();

protected:
    SettingsApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    void buildUi(lv_obj_t *parent);
    void buildCategoryBar(lv_obj_t *parent);
    void selectCategory(SettingsCategory category);
    void refreshCategoryButtons(void);
    void buildTimeSection(lv_obj_t *list);
    void buildDisplaySection(lv_obj_t *list);
    void buildFacesSection(lv_obj_t *list);
    void buildServerSection(lv_obj_t *list);
    void buildBluetoothSection(lv_obj_t *list);
    void updateClockLabel(void);
    void loadWatchfaceEntries(void);
    void refreshFaceButtons(void);
    void showSetTimePopup(void);
    void closeSetTimePopup(void);
    void selectFace(int index);

    void buildLocationCard(lv_obj_t *parent);
    void updateLocationLabel(void);
    void showLocationPopup(void);
    void closeLocationPopup(void);
    void buildLocationKeyboard(lv_obj_t *parent);
    void handleLocationKeyText(const char *text);

    static void onCategoryButtonClicked(lv_event_t *e);
    static void onSetTimeClicked(lv_event_t *e);
    static void onSaveClicked(lv_event_t *e);
    static void onCancelClicked(lv_event_t *e);
    static void onClockTimer(lv_timer_t *t);
    static void onFaceButtonClicked(lv_event_t *e);
    static void onBrightnessChanged(lv_event_t *e);
    static void onHomeColorClicked(lv_event_t *e);
    static void onAodColorClicked(lv_event_t *e);
    static void onWebserverToggled(lv_event_t *e);
    static void onBluetoothToggled(lv_event_t *e);

    static void onLocationChangeClicked(lv_event_t *e);
    static void onLocationSaveClicked(lv_event_t *e);
    static void onLocationCancelClicked(lv_event_t *e);
    static void onLocationKeyClicked(lv_event_t *e);
    static void onLocationPollTimer(lv_timer_t *t);

    void refreshHomeColorSwatches(void);
    void refreshAodColorSwatches(void);
    void updateWebserverLabel(void);
    void updateBluetoothLabel(void);

    static SettingsApp *_instance;

    lv_obj_t *_clock_label = nullptr;
    lv_obj_t *_popup = nullptr;
    lv_obj_t *_year_roller = nullptr;
    lv_obj_t *_month_roller = nullptr;
    lv_obj_t *_day_roller = nullptr;
    lv_obj_t *_hour_roller = nullptr;
    lv_obj_t *_min_roller = nullptr;
    int _roller_base_year = 2024;
    lv_timer_t *_clock_timer = nullptr;

    std::vector<WatchfaceEntry> _faces;
    int _selected_face = 0;

    std::vector<HomeColorSwatch> _home_swatches;
    uint32_t _selected_home_color = 0x1a1a1a;

    std::vector<HomeColorSwatch> _aod_swatches;
    uint32_t _selected_aod_color = 0x6cf0c2;

    lv_obj_t *_list = nullptr;
    lv_obj_t *_webserver_label = nullptr;
    lv_obj_t *_webserver_switch = nullptr;
    lv_obj_t *_bt_label = nullptr;
    lv_obj_t *_bt_switch = nullptr;

    lv_obj_t *_location_label = nullptr;
    lv_obj_t *_location_popup = nullptr;
    lv_obj_t *_location_ta = nullptr;
    lv_obj_t *_location_status_label = nullptr;
    lv_obj_t *_location_keyboard = nullptr;
    lv_obj_t *_location_save_btn = nullptr;
    bool _location_keyboard_numeric = false;
    bool _location_keyboard_shift = false;
    lv_timer_t *_location_poll_timer = nullptr;

    SettingsCategory _current_category = SettingsCategory::TIME;
    lv_obj_t *_category_buttons[static_cast<int>(SettingsCategory::COUNT)] = {};
};

} // namespace esp_watchos::apps
