#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:Settings"
#include "esp_lib_utils.h"
#include "esp_watchos_app_settings.hpp"

#include "bsp/esp-bsp.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "os_fs.h"
#include "wifi_shared.h"
#include "display_shared.h"

#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

#define APP_NAME "Settings"
#define DISPLAY_HEIGHT 502
#define STATUS_BAR_HEIGHT 52
#define CONTENT_HEIGHT (DISPLAY_HEIGHT - STATUS_BAR_HEIGHT)

#define FACE_NVS_NAMESPACE "appcfg"
#define FACE_NVS_KEY "facefile"

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_settings_120_120);

namespace esp_watchos::apps {

static std::string load_selected_face(void)
{
    nvs_handle_t handle;
    char buf[40] = {};
    if (nvs_open(FACE_NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(buf);
        nvs_get_str(handle, FACE_NVS_KEY, buf, &len);
        nvs_close(handle);
    }
    return std::string(buf);
}

static void save_selected_face(const std::string &filename)
{
    nvs_handle_t handle;
    if (nvs_open(FACE_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, FACE_NVS_KEY, filename.c_str());
        nvs_commit(handle);
        nvs_close(handle);
    }
}

/* Same /data/apps/watchface/faces/ JSON files the Watchface app reads -
 * Settings only needs each file's "name" field to label a picker button. */
static std::string read_file(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "r");
    if (!f) {
        return "";
    }
    std::string data;
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        data.append(buf, n);
    }
    fclose(f);
    return data;
}

static bool json_get(const std::string &json, const char *key, std::string &out)
{
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return false;
    }
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) {
        return false;
    }
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return false;
    }
    pos++;
    size_t end = json.find('"', pos);
    if (end == std::string::npos) {
        return false;
    }
    out = json.substr(pos, end - pos);
    return true;
}

SettingsApp *SettingsApp::_instance = nullptr;

SettingsApp *SettingsApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new SettingsApp();
    }
    return _instance;
}

SettingsApp::SettingsApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_settings_120_120, true, true, false)
{
}

SettingsApp::~SettingsApp()
{
}

bool SettingsApp::run(void)
{
    rtc_shared_init();
    loadWatchfaceEntries();

    std::string selected = load_selected_face();
    _selected_face = 0;
    for (size_t i = 0; i < _faces.size(); i++) {
        if (_faces[i].filename == selected) {
            _selected_face = (int)i;
            break;
        }
    }

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), CONTENT_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

    buildUi(content);
    updateClockLabel();
    _clock_timer = lv_timer_create(onClockTimer, 1000, this);

    return true;
}

bool SettingsApp::back(void)
{
    if (_popup != nullptr) {
        closeSetTimePopup();
        return true;
    }
    if (_location_popup != nullptr) {
        closeLocationPopup();
        return true;
    }

    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool SettingsApp::pause(void)
{
    if (_clock_timer != nullptr) {
        lv_timer_pause(_clock_timer);
    }
    return true;
}

bool SettingsApp::resume(void)
{
    if (_clock_timer != nullptr) {
        lv_timer_resume(_clock_timer);
    }
    return true;
}

static void write_file(const std::string &path, const char *content)
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f) {
        return;
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
}

/* Mirrors the Watchface app's own seeding logic - both apps are independent
 * and may be opened in either order, so each ensures the shared on-disk
 * watchface directory exists rather than depending on the other having run. */
static void seed_default_faces_if_missing(const std::string &dir)
{
    bool has_any = false;
    DIR *d = opendir(dir.c_str());
    if (d != nullptr) {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string fname = entry->d_name;
            if (fname.size() > 5 && fname.compare(fname.size() - 5, 5, ".json") == 0) {
                has_any = true;
                break;
            }
        }
        closedir(d);
    }
    if (has_any) {
        return;
    }

    write_file(dir + "/classic.json",
               "{\"name\":\"Classic\",\"type\":\"analog\",\"bg_color\":\"0x10181d\",\"bg_color2\":\"0x05080a\","
               "\"ring_color\":\"0x355563\",\"major_color\":\"0xd9f2ea\",\"minor_color\":\"0x7d96a0\","
               "\"hour_color\":\"0xffffff\",\"minute_color\":\"0x6cf0c2\",\"second_color\":\"0xff8844\","
               "\"accent_color\":\"0x6cf0c2\",\"marks\":\"ticks\"}");
    write_file(dir + "/ocean.json",
               "{\"name\":\"Ocean\",\"type\":\"analog\",\"bg_color\":\"0x031a26\",\"bg_color2\":\"0x00060d\","
               "\"ring_color\":\"0x0d4f66\",\"major_color\":\"0x9fe7ff\",\"minor_color\":\"0x4c7a8c\","
               "\"hour_color\":\"0xeaffff\",\"minute_color\":\"0x32c5e8\",\"second_color\":\"0xffd166\","
               "\"accent_color\":\"0x32c5e8\",\"marks\":\"dots\"}");
    write_file(dir + "/ember.json",
               "{\"name\":\"Ember\",\"type\":\"analog\",\"bg_color\":\"0x1a0d05\",\"bg_color2\":\"0x06010a\","
               "\"ring_color\":\"0x6b3216\",\"major_color\":\"0xffe0c2\",\"minor_color\":\"0xa6713f\","
               "\"hour_color\":\"0xffffff\",\"minute_color\":\"0xff9a3c\",\"second_color\":\"0xff3c3c\","
               "\"accent_color\":\"0xff9a3c\",\"marks\":\"numbers\"}");
    write_file(dir + "/mono.json",
               "{\"name\":\"Mono\",\"type\":\"digital\",\"bg_color\":\"0x000000\",\"bg_color2\":\"0x101820\","
               "\"time_color\":\"0xffffff\",\"date_color\":\"0x888888\",\"accent_color\":\"0x6cf0c2\"}");
    write_file(dir + "/modular.json",
               "{\"name\":\"Modular\",\"type\":\"modular\",\"bg_color\":\"0x05080a\",\"bg_color2\":\"0x10181d\","
               "\"time_color\":\"0xffffff\",\"date_color\":\"0x6cf0c2\",\"accent_color\":\"0x6cf0c2\"}");
}

void SettingsApp::loadWatchfaceEntries(void)
{
    _faces.clear();

    std::string watchface_dir = std::string(OS_FS_APPS_DIR) + "/watchface";
    std::string faces_dir = watchface_dir + "/faces";
    mkdir(watchface_dir.c_str(), 0755);
    mkdir(faces_dir.c_str(), 0755);
    seed_default_faces_if_missing(faces_dir);

    DIR *d = opendir(faces_dir.c_str());
    if (d == nullptr) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string fname = entry->d_name;
        if (fname.size() <= 5 || fname.compare(fname.size() - 5, 5, ".json") != 0) {
            continue;
        }
        std::string json = read_file(faces_dir + "/" + fname);
        if (json.empty()) {
            continue;
        }
        WatchfaceEntry face;
        face.filename = fname;
        if (!json_get(json, "name", face.name)) {
            face.name = fname;
        }
        _faces.push_back(face);
    }
    closedir(d);

    std::sort(_faces.begin(), _faces.end(), [](const WatchfaceEntry & a, const WatchfaceEntry & b) {
        return a.filename < b.filename;
    });
}

static lv_obj_t *create_card(lv_obj_t *parent, const char *title_text)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x355563), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, lv_color_hex(0x6cf0c2), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

    return card;
}

#define CATEGORY_BAR_HEIGHT 148

/* Category tabs wrap onto two rows instead of scrolling off-screen in one -
 * on a 410px-wide round display, 5 text buttons in a single scrollable row
 * hid "Bluetooth" off the edge with no hint that more tabs existed. Wrapping
 * shows every category at once, no swipe required. */
void SettingsApp::buildUi(lv_obj_t *parent)
{
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0x6cf0c2), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 6);

    buildCategoryBar(parent);

    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), CONTENT_HEIGHT - 46 - CATEGORY_BAR_HEIGHT);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 46 + CATEGORY_BAR_HEIGHT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_set_style_pad_all(list, 8, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    _list = list;

    selectCategory(SettingsCategory::TIME);
}

void SettingsApp::buildCategoryBar(lv_obj_t *parent)
{
    static const struct { const char *label; SettingsCategory category; } CATEGORIES[] = {
        {"Time", SettingsCategory::TIME},
        {"Display", SettingsCategory::DISPLAY},
        {"Faces", SettingsCategory::FACES},
        {"Server", SettingsCategory::SERVER},
        {"Bluetooth", SettingsCategory::BLUETOOTH},
    };

    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), CATEGORY_BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_style_pad_row(bar, 8, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < sizeof(CATEGORIES) / sizeof(CATEGORIES[0]); i++) {
        lv_obj_t *btn = lv_button_create(bar);
        lv_obj_set_size(btn, 120, 66);
        lv_obj_set_user_data(btn, (void *)(intptr_t)CATEGORIES[i].category);
        lv_obj_add_event_cb(btn, onCategoryButtonClicked, LV_EVENT_CLICKED, this);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, CATEGORIES[i].label);
        lv_obj_center(lbl);
        _category_buttons[(int)CATEGORIES[i].category] = btn;
    }
    refreshCategoryButtons();
}

void SettingsApp::refreshCategoryButtons(void)
{
    for (int i = 0; i < (int)SettingsCategory::COUNT; i++) {
        lv_obj_t *btn = _category_buttons[i];
        if (btn == nullptr) {
            continue;
        }
        bool selected = (i == (int)_current_category);
        lv_obj_set_style_bg_color(btn, selected ? lv_color_hex(0x123322) : lv_color_hex(0x141c22), 0);
        lv_obj_set_style_border_color(btn, selected ? lv_color_hex(0x6cf0c2) : lv_color_hex(0x355563), 0);
        lv_obj_set_style_border_width(btn, selected ? 2 : 1, 0);
    }
}

void SettingsApp::selectCategory(SettingsCategory category)
{
    if (_list == nullptr) {
        return;
    }

    closeSetTimePopup();
    lv_obj_clean(_list);
    _clock_label = nullptr;
    _webserver_label = nullptr;
    _webserver_switch = nullptr;
    _bt_label = nullptr;
    _bt_switch = nullptr;
    _location_label = nullptr;
    _home_swatches.clear();
    _aod_swatches.clear();
    for (auto &face : _faces) {
        face.button = nullptr;
    }

    _current_category = category;
    refreshCategoryButtons();

    switch (category) {
    case SettingsCategory::TIME:
        buildTimeSection(_list);
        break;
    case SettingsCategory::DISPLAY:
        buildDisplaySection(_list);
        break;
    case SettingsCategory::FACES:
        buildFacesSection(_list);
        break;
    case SettingsCategory::SERVER:
        buildServerSection(_list);
        break;
    case SettingsCategory::BLUETOOTH:
        buildBluetoothSection(_list);
        break;
    default:
        break;
    }
}

void SettingsApp::buildTimeSection(lv_obj_t *list)
{
    lv_obj_t *time_card = create_card(list, "Date & Time");

    _clock_label = lv_label_create(time_card);
    lv_label_set_text(_clock_label, "----");
    lv_obj_set_style_text_color(_clock_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_clock_label, &lv_font_montserrat_20, 0);

    lv_obj_t *set_btn = lv_button_create(time_card);
    lv_obj_set_size(set_btn, 130, 42);
    lv_obj_add_event_cb(set_btn, onSetTimeClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *set_label = lv_label_create(set_btn);
    lv_label_set_text(set_label, "Set Time");
    lv_obj_center(set_label);

    updateClockLabel();
}

void SettingsApp::buildDisplaySection(lv_obj_t *list)
{
    // -- Brightness card --
    lv_obj_t *bright_card = create_card(list, "Display Brightness");

    lv_obj_t *slider = lv_slider_create(bright_card);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_slider_set_range(slider, 10, 100);
    int saved_brightness = display_shared_get_saved_brightness();
    lv_slider_set_value(slider, saved_brightness > 0 ? saved_brightness : 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, onBrightnessChanged, LV_EVENT_VALUE_CHANGED, this);

    // -- Home Screen Background card --
    lv_obj_t *home_card = create_card(list, "Home Screen Background");

    lv_obj_t *color_row = lv_obj_create(home_card);
    lv_obj_remove_style_all(color_row);
    lv_obj_set_size(color_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(color_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(color_row, 10, 0);
    lv_obj_set_style_pad_row(color_row, 10, 0);
    lv_obj_clear_flag(color_row, LV_OBJ_FLAG_SCROLLABLE);

    static const uint32_t HOME_COLORS[] = {
        0x1a1a1a, 0x10181d, 0x031a26, 0x1a0d05, 0x0d1a0d, 0x1a0d1a, 0x0d0d1a, 0x000000,
    };
    uint32_t saved_color = 0x1a1a1a;
    homescreen_shared_get_saved_color(&saved_color);
    _selected_home_color = saved_color;

    for (uint32_t color : HOME_COLORS) {
        lv_obj_t *swatch = lv_obj_create(color_row);
        lv_obj_remove_style_all(swatch);
        lv_obj_set_size(swatch, 58, 58);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(swatch, 8, 0);
        lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(swatch, (void *)(uintptr_t)color);
        lv_obj_add_event_cb(swatch, onHomeColorClicked, LV_EVENT_CLICKED, this);
        _home_swatches.push_back({color, swatch});
    }
    refreshHomeColorSwatches();

    // -- AOD Clock Color card --
    lv_obj_t *aod_card = create_card(list, "AOD Clock Color");

    lv_obj_t *aod_row = lv_obj_create(aod_card);
    lv_obj_remove_style_all(aod_row);
    lv_obj_set_size(aod_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(aod_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(aod_row, 10, 0);
    lv_obj_set_style_pad_row(aod_row, 10, 0);
    lv_obj_clear_flag(aod_row, LV_OBJ_FLAG_SCROLLABLE);

    static const uint32_t AOD_COLORS[] = {
        0x6cf0c2, 0xffffff, 0xff8844, 0xff4444, 0xffd166, 0x6cc8f0, 0xc46cf0, 0x6cf06c,
    };
    uint32_t saved_aod_color = 0x6cf0c2;
    display_shared_get_saved_aod_color(&saved_aod_color);
    _selected_aod_color = saved_aod_color;

    for (uint32_t color : AOD_COLORS) {
        lv_obj_t *swatch = lv_obj_create(aod_row);
        lv_obj_remove_style_all(swatch);
        lv_obj_set_size(swatch, 58, 58);
        lv_obj_set_style_bg_color(swatch, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(swatch, 8, 0);
        lv_obj_clear_flag(swatch, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(swatch, (void *)(uintptr_t)color);
        lv_obj_add_event_cb(swatch, onAodColorClicked, LV_EVENT_CLICKED, this);
        _aod_swatches.push_back({color, swatch});
    }
    refreshAodColorSwatches();

    buildLocationCard(list);
}

void SettingsApp::buildLocationCard(lv_obj_t *parent)
{
    lv_obj_t *loc_card = create_card(parent, "Location (for Weather)");

    _location_label = lv_label_create(loc_card);
    lv_obj_set_width(_location_label, LV_PCT(100));
    lv_label_set_long_mode(_location_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_location_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_location_label, &lv_font_montserrat_20, 0);

    lv_obj_t *change_btn = lv_button_create(loc_card);
    lv_obj_set_size(change_btn, 130, 42);
    lv_obj_add_event_cb(change_btn, onLocationChangeClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *change_lbl = lv_label_create(change_btn);
    lv_label_set_text(change_lbl, "Change");
    lv_obj_center(change_lbl);

    updateLocationLabel();
}

void SettingsApp::updateLocationLabel(void)
{
    if (_location_label == nullptr) {
        return;
    }
    weather_shared_location_t loc = {};
    if (weather_shared_get_location(&loc)) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s (%.2f, %.2f)", loc.city, loc.latitude, loc.longitude);
        lv_label_set_text(_location_label, buf);
    } else {
        lv_label_set_text(_location_label, "Not set - weather won't show on the clock");
    }
}

void SettingsApp::onLocationChangeClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    app->showLocationPopup();
}

void SettingsApp::showLocationPopup(void)
{
    closeLocationPopup();

    /* Same reasoning as the Wi-Fi password popup: free the settings list's
     * widgets while the popup (with its own textarea + keyboard) is up -
     * this device has very little heap headroom without PSRAM. */
    lv_obj_clean(_list);

    int popup_height = CONTENT_HEIGHT - 8;

    _location_popup = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(_location_popup);
    lv_obj_set_size(_location_popup, LV_PCT(96), popup_height);
    lv_obj_align(_location_popup, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 4);
    lv_obj_set_style_bg_color(_location_popup, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_bg_opa(_location_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_location_popup, lv_color_hex(0x6cf0c2), 0);
    lv_obj_set_style_border_width(_location_popup, 2, 0);
    lv_obj_set_style_radius(_location_popup, 10, 0);
    lv_obj_set_style_pad_all(_location_popup, 8, 0);
    lv_obj_clear_flag(_location_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(_location_popup);
    lv_label_set_text(title, "City name");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(title, LV_PCT(100));
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    _location_ta = lv_textarea_create(_location_popup);
    lv_textarea_set_one_line(_location_ta, true);
    lv_obj_set_size(_location_ta, LV_PCT(100), 52);
    lv_obj_set_style_text_font(_location_ta, &lv_font_montserrat_20, 0);
    lv_obj_align_to(_location_ta, title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    weather_shared_location_t loc = {};
    if (weather_shared_get_location(&loc)) {
        lv_textarea_set_text(_location_ta, loc.city);
    }

    _location_status_label = lv_label_create(_location_popup);
    lv_label_set_text(_location_status_label, "");
    lv_obj_set_style_text_color(_location_status_label, lv_color_hex(0xff8844), 0);
    lv_obj_set_style_text_font(_location_status_label, &lv_font_montserrat_18, 0);
    lv_obj_align_to(_location_status_label, _location_ta, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

    _location_save_btn = lv_button_create(_location_popup);
    lv_obj_set_size(_location_save_btn, 110, 44);
    lv_obj_align_to(_location_save_btn, _location_status_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_add_event_cb(_location_save_btn, onLocationSaveClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *save_lbl = lv_label_create(_location_save_btn);
    lv_label_set_text(save_lbl, "Save");
    lv_obj_center(save_lbl);

    lv_obj_t *cancel_btn = lv_button_create(_location_popup);
    lv_obj_set_size(cancel_btn, 100, 44);
    lv_obj_align_to(cancel_btn, _location_save_btn, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
    lv_obj_add_event_cb(cancel_btn, onLocationCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "Cancel");
    lv_obj_center(cancel_lbl);

    buildLocationKeyboard(_location_popup);

    lv_obj_add_state(_location_ta, LV_STATE_FOCUSED);
}

/* Same lv_buttonmatrix approach as the Wi-Fi password keyboard (see that
 * file for why: lv_keyboard has repeatedly hung on this display/LVGL combo
 * on this no-PSRAM chip). Duplicated rather than shared since apps in this
 * project are self-contained. */
static const char *LOCATION_KB_LOWER_MAP[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "123", "space", ""
};
static const char *LOCATION_KB_UPPER_MAP[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "123", "space", ""
};
static const char *LOCATION_KB_NUMERIC_MAP[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "-", "_", "=", "+", "!", "@", "#", "\n",
    "ABC", "$", "%", "^", "&", "*", "(", ")", LV_SYMBOL_BACKSPACE, "\n",
    "space", ""
};

void SettingsApp::buildLocationKeyboard(lv_obj_t *parent)
{
    int keyboard_height = (CONTENT_HEIGHT - 8) * 50 / 100;

    lv_obj_t *kb = lv_buttonmatrix_create(parent);
    lv_obj_set_size(kb, LV_PCT(100), keyboard_height);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_18, 0);
    lv_buttonmatrix_set_map(kb, LOCATION_KB_LOWER_MAP);
    lv_obj_add_event_cb(kb, onLocationKeyClicked, LV_EVENT_VALUE_CHANGED, this);

    _location_keyboard = kb;
    _location_keyboard_numeric = false;
    _location_keyboard_shift = false;
}

void SettingsApp::handleLocationKeyText(const char *text)
{
    if (_location_ta == nullptr || text == nullptr) {
        return;
    }
    if (strcmp(text, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_delete_char(_location_ta);
    } else if (strcmp(text, "123") == 0 || strcmp(text, "ABC") == 0) {
        _location_keyboard_numeric = !_location_keyboard_numeric;
        lv_buttonmatrix_set_map(_location_keyboard, _location_keyboard_numeric ? LOCATION_KB_NUMERIC_MAP :
                                 (_location_keyboard_shift ? LOCATION_KB_UPPER_MAP : LOCATION_KB_LOWER_MAP));
    } else if (strcmp(text, LV_SYMBOL_UP) == 0) {
        _location_keyboard_shift = !_location_keyboard_shift;
        lv_buttonmatrix_set_map(_location_keyboard, _location_keyboard_shift ? LOCATION_KB_UPPER_MAP : LOCATION_KB_LOWER_MAP);
    } else if (strcmp(text, "space") == 0) {
        lv_textarea_add_char(_location_ta, ' ');
    } else {
        lv_textarea_add_text(_location_ta, text);
    }
}

void SettingsApp::onLocationKeyClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(kb);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) {
        return;
    }
    const char *text = lv_buttonmatrix_get_button_text(kb, id);
    app->handleLocationKeyText(text);
}

void SettingsApp::onLocationSaveClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    const char *city = lv_textarea_get_text(app->_location_ta);
    if (city == nullptr || city[0] == '\0') {
        return;
    }

    if (!weather_shared_geocode_city_async(city)) {
        lv_label_set_text(app->_location_status_label,
                           wifi_shared_is_connected() ? "Lookup already running" : "Wi-Fi not connected");
        return;
    }

    lv_label_set_text(app->_location_status_label, "Looking up...");
    lv_obj_add_state(app->_location_save_btn, LV_STATE_DISABLED);
    if (app->_location_poll_timer == nullptr) {
        app->_location_poll_timer = lv_timer_create(onLocationPollTimer, 500, app);
    }
}

void SettingsApp::onLocationPollTimer(lv_timer_t *t)
{
    SettingsApp *app = (SettingsApp *)lv_timer_get_user_data(t);
    bool ok = false;
    if (!weather_shared_geocode_poll(&ok)) {
        return;  // still running
    }

    lv_timer_del(app->_location_poll_timer);
    app->_location_poll_timer = nullptr;

    if (!ok) {
        if (app->_location_status_label != nullptr) {
            lv_label_set_text(app->_location_status_label, "City not found - try a different spelling");
        }
        if (app->_location_save_btn != nullptr) {
            lv_obj_remove_state(app->_location_save_btn, LV_STATE_DISABLED);
        }
        return;
    }

    weather_shared_refresh_async();
    app->closeLocationPopup();
}

void SettingsApp::onLocationCancelClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    app->closeLocationPopup();
}

void SettingsApp::closeLocationPopup(void)
{
    if (_location_poll_timer != nullptr) {
        lv_timer_del(_location_poll_timer);
        _location_poll_timer = nullptr;
    }
    if (_location_popup != nullptr) {
        lv_obj_del(_location_popup);
        _location_popup = nullptr;
        _location_ta = nullptr;
        _location_status_label = nullptr;
        _location_keyboard = nullptr;
        _location_save_btn = nullptr;
        selectCategory(_current_category);  // rebuild the list the popup blanked
    }
}

void SettingsApp::buildFacesSection(lv_obj_t *list)
{
    lv_obj_t *face_card = create_card(list, "Watchface");

    lv_obj_t *face_row = lv_obj_create(face_card);
    lv_obj_remove_style_all(face_row);
    lv_obj_set_size(face_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(face_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(face_row, 10, 0);
    lv_obj_set_style_pad_row(face_row, 10, 0);
    lv_obj_clear_flag(face_row, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < _faces.size(); i++) {
        lv_obj_t *btn = lv_button_create(face_row);
        lv_obj_set_size(btn, 150, 58);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, onFaceButtonClicked, LV_EVENT_CLICKED, this);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, _faces[i].name.c_str());
        lv_obj_center(lbl);
        _faces[i].button = btn;
    }
    refreshFaceButtons();
}

void SettingsApp::buildServerSection(lv_obj_t *list)
{
    lv_obj_t *web_card = create_card(list, "Watchface Server");

    lv_obj_t *web_row = lv_obj_create(web_card);
    lv_obj_remove_style_all(web_row);
    lv_obj_set_size(web_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(web_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(web_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(web_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *web_label = lv_label_create(web_row);
    lv_label_set_text(web_label, "Enable upload server");
    lv_obj_set_style_text_color(web_label, lv_color_hex(0xffffff), 0);

    _webserver_switch = lv_switch_create(web_row);
    lv_obj_add_event_cb(_webserver_switch, onWebserverToggled, LV_EVENT_VALUE_CHANGED, this);
    if (webserver_shared_is_running()) {
        lv_obj_add_state(_webserver_switch, LV_STATE_CHECKED);
    }

    _webserver_label = lv_label_create(web_card);
    lv_obj_set_width(_webserver_label, LV_PCT(100));
    lv_label_set_long_mode(_webserver_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_webserver_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_webserver_label, &lv_font_montserrat_20, 0);
    updateWebserverLabel();
}

void SettingsApp::buildBluetoothSection(lv_obj_t *list)
{
    lv_obj_t *bt_card = create_card(list, "Bluetooth");

    lv_obj_t *bt_row = lv_obj_create(bt_card);
    lv_obj_remove_style_all(bt_row);
    lv_obj_set_size(bt_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bt_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bt_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bt_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bt_label = lv_label_create(bt_row);
    lv_label_set_text(bt_label, "Enable BLE advertising");
    lv_obj_set_style_text_color(bt_label, lv_color_hex(0xffffff), 0);

    _bt_switch = lv_switch_create(bt_row);
    lv_obj_add_event_cb(_bt_switch, onBluetoothToggled, LV_EVENT_VALUE_CHANGED, this);
    if (bt_shared_is_running()) {
        lv_obj_add_state(_bt_switch, LV_STATE_CHECKED);
    }

    _bt_label = lv_label_create(bt_card);
    lv_obj_set_width(_bt_label, LV_PCT(100));
    lv_label_set_long_mode(_bt_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(_bt_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_bt_label, &lv_font_montserrat_20, 0);
    updateBluetoothLabel();
}

void SettingsApp::updateClockLabel(void)
{
    if (_clock_label == nullptr) {
        return;
    }

    rtc_shared_datetime_t dt = {};
    rtc_shared_get_datetime(&dt);

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
    lv_label_set_text(_clock_label, buf);
}

void SettingsApp::refreshFaceButtons(void)
{
    for (size_t i = 0; i < _faces.size(); i++) {
        bool selected = ((int)i == _selected_face);
        lv_obj_t *btn = _faces[i].button;
        if (btn == nullptr) {
            continue;
        }
        lv_obj_set_style_border_color(btn, selected ? lv_color_hex(0x6cf0c2) : lv_color_hex(0x355563), 0);
        lv_obj_set_style_border_width(btn, selected ? 3 : 1, 0);
        lv_obj_set_style_bg_color(btn, selected ? lv_color_hex(0x123322) : lv_color_hex(0x222a30), 0);
    }
}

void SettingsApp::selectFace(int index)
{
    if (index < 0 || index >= (int)_faces.size()) {
        return;
    }
    _selected_face = index;
    save_selected_face(_faces[index].filename);
    refreshFaceButtons();
}

/* Builds "min..max" (zero-padded to `digits`) as newline-separated roller
 * options, e.g. makeRange(0, 23, 2) -> "00\n01\n...\n23". */
static std::string makeRange(int min, int max, int digits)
{
    std::string out;
    char buf[8];
    for (int v = min; v <= max; v++) {
        snprintf(buf, sizeof(buf), "%0*d", digits, v);
        out += buf;
        if (v != max) {
            out += '\n';
        }
    }
    return out;
}

static lv_obj_t *create_time_roller(lv_obj_t *parent, const char *header, const std::string &options,
                                     int selected_index)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, 68, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(col);
    lv_label_set_text(label, header);
    lv_obj_set_style_text_color(label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);

    lv_obj_t *roller = lv_roller_create(col);
    lv_obj_set_width(roller, 68);
    lv_roller_set_options(roller, options.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_roller_set_selected(roller, selected_index, LV_ANIM_OFF);

    return roller;
}

/* Rollers replace the old free-text "YYYY-MM-DD HH:MM" entry - scrolling to
 * a value beats typing an exact string on a small touchscreen, and it can't
 * produce an unparsable date. */
void SettingsApp::showSetTimePopup(void)
{
    closeSetTimePopup();

    rtc_shared_datetime_t dt = {};
    rtc_shared_get_datetime(&dt);
    _roller_base_year = dt.year - 5;

    _popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_popup, LV_PCT(96), CONTENT_HEIGHT - 8);
    lv_obj_align(_popup, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT + 4);
    lv_obj_set_style_bg_color(_popup, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_border_color(_popup, lv_color_hex(0x6cf0c2), 0);
    lv_obj_set_style_border_width(_popup, 2, 0);
    lv_obj_set_style_radius(_popup, 10, 0);
    lv_obj_set_style_pad_all(_popup, 10, 0);

    lv_obj_t *title = lv_label_create(_popup);
    lv_label_set_text(title, "Set Date & Time");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

    lv_obj_t *roller_row = lv_obj_create(_popup);
    lv_obj_remove_style_all(roller_row);
    lv_obj_set_size(roller_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(roller_row, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_flex_flow(roller_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(roller_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(roller_row, LV_OBJ_FLAG_SCROLLABLE);

    _year_roller = create_time_roller(roller_row, "Year", makeRange(_roller_base_year, _roller_base_year + 15, 4),
                                       dt.year - _roller_base_year);
    _month_roller = create_time_roller(roller_row, "Mon", makeRange(1, 12, 2), dt.month - 1);
    _day_roller = create_time_roller(roller_row, "Day", makeRange(1, 31, 2), dt.day - 1);
    _hour_roller = create_time_roller(roller_row, "Hr", makeRange(0, 23, 2), dt.hour);
    _min_roller = create_time_roller(roller_row, "Min", makeRange(0, 59, 2), dt.min);

    lv_obj_t *save_btn = lv_button_create(_popup);
    lv_obj_set_size(save_btn, 110, 48);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_MID, -60, -4);
    lv_obj_add_event_cb(save_btn, onSaveClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);

    lv_obj_t *cancel_btn = lv_button_create(_popup);
    lv_obj_set_size(cancel_btn, 110, 48);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_MID, 60, -4);
    lv_obj_add_event_cb(cancel_btn, onCancelClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
}

void SettingsApp::closeSetTimePopup(void)
{
    if (_popup != nullptr) {
        lv_obj_del(_popup);
        _popup = nullptr;
        _year_roller = nullptr;
        _month_roller = nullptr;
        _day_roller = nullptr;
        _hour_roller = nullptr;
        _min_roller = nullptr;
    }
}

void SettingsApp::onSetTimeClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    app->showSetTimePopup();
}

void SettingsApp::onSaveClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);

    rtc_shared_datetime_t dt = {};
    dt.year = app->_roller_base_year + (int)lv_roller_get_selected(app->_year_roller);
    dt.month = 1 + (int)lv_roller_get_selected(app->_month_roller);
    dt.day = 1 + (int)lv_roller_get_selected(app->_day_roller);
    dt.hour = (int)lv_roller_get_selected(app->_hour_roller);
    dt.min = (int)lv_roller_get_selected(app->_min_roller);
    dt.sec = 0;
    rtc_shared_set_datetime(&dt);
    app->updateClockLabel();

    app->closeSetTimePopup();
}

void SettingsApp::onCancelClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    app->closeSetTimePopup();
}

void SettingsApp::onClockTimer(lv_timer_t *t)
{
    SettingsApp *app = (SettingsApp *)lv_timer_get_user_data(t);
    if (app->_popup == nullptr) {
        app->updateClockLabel();
    }
}

void SettingsApp::onFaceButtonClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);
    app->selectFace(index);
}

void SettingsApp::onBrightnessChanged(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int value = lv_slider_get_value(slider);
    display_shared_set_brightness(value);
}

void SettingsApp::onHomeColorClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *swatch = (lv_obj_t *)lv_event_get_target(e);
    uint32_t color = (uint32_t)(uintptr_t)lv_obj_get_user_data(swatch);

    homescreen_shared_set_bg_color(color);
    app->_selected_home_color = color;
    app->refreshHomeColorSwatches();
}

void SettingsApp::refreshHomeColorSwatches(void)
{
    for (const auto &swatch : _home_swatches) {
        bool selected = (swatch.color == _selected_home_color);
        lv_obj_set_style_border_color(swatch.obj, lv_color_hex(0x6cf0c2), 0);
        lv_obj_set_style_border_width(swatch.obj, selected ? 3 : 1, 0);
        lv_obj_set_style_border_opa(swatch.obj, selected ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

void SettingsApp::onAodColorClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *swatch = (lv_obj_t *)lv_event_get_target(e);
    uint32_t color = (uint32_t)(uintptr_t)lv_obj_get_user_data(swatch);

    display_shared_set_aod_color(color);
    app->_selected_aod_color = color;
    app->refreshAodColorSwatches();
}

void SettingsApp::refreshAodColorSwatches(void)
{
    for (const auto &swatch : _aod_swatches) {
        bool selected = (swatch.color == _selected_aod_color);
        lv_obj_set_style_border_color(swatch.obj, lv_color_hex(0x6cf0c2), 0);
        lv_obj_set_style_border_width(swatch.obj, selected ? 3 : 1, 0);
        lv_obj_set_style_border_opa(swatch.obj, selected ? LV_OPA_COVER : LV_OPA_40, 0);
    }
}

void SettingsApp::onCategoryButtonClicked(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    SettingsCategory category = (SettingsCategory)(intptr_t)lv_obj_get_user_data(btn);
    app->selectCategory(category);
}

/* Now that each category only ever shows its own widgets, the server toggle
 * no longer needs to manually tear down the rest of the screen first - it
 * was already torn down the moment the "Server" category was selected. */
void SettingsApp::onWebserverToggled(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool turning_on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (!turning_on) {
        webserver_shared_stop();
        app->updateWebserverLabel();
        return;
    }

    if (!wifi_shared_is_connected()) {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
        lv_label_set_text(app->_webserver_label, "Connect to WiFi first (WiFi Connect app)");
        lv_obj_set_style_text_color(app->_webserver_label, lv_color_hex(0xff8888), 0);
        return;
    }

    if (!webserver_shared_start()) {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
        lv_label_set_text(app->_webserver_label,
                           "Not enough free memory right now - try closing other apps");
        lv_obj_set_style_text_color(app->_webserver_label, lv_color_hex(0xff8888), 0);
        return;
    }

    app->updateWebserverLabel();
}

void SettingsApp::updateWebserverLabel(void)
{
    if (_webserver_label == nullptr) {
        return;
    }
    if (!webserver_shared_is_running()) {
        lv_label_set_text(_webserver_label, "Off");
        lv_obj_set_style_text_color(_webserver_label, lv_color_hex(0xb8d2dd), 0);
        return;
    }

    char ip[16] = {};
    char password[9] = {};
    bool have_password = webserver_shared_get_password(password, sizeof(password));
    char buf[96];
    if (wifi_shared_get_ip(ip, sizeof(ip))) {
        if (have_password) {
            snprintf(buf, sizeof(buf), "Open http://%s - user admin, password %s", ip, password);
        } else {
            snprintf(buf, sizeof(buf), "Open http://%s in a browser", ip);
        }
    } else {
        snprintf(buf, sizeof(buf), "Running, but no IP yet");
    }
    lv_label_set_text(_webserver_label, buf);
    lv_obj_set_style_text_color(_webserver_label, lv_color_hex(0x6cf0c2), 0);
}

void SettingsApp::onBluetoothToggled(lv_event_t *e)
{
    SettingsApp *app = (SettingsApp *)lv_event_get_user_data(e);
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool turning_on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    if (!turning_on) {
        bt_shared_stop();
        app->updateBluetoothLabel();
        return;
    }

    if (!bt_shared_start()) {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
        lv_label_set_text(app->_bt_label, "Failed to start - see serial log");
        lv_obj_set_style_text_color(app->_bt_label, lv_color_hex(0xff8888), 0);
        return;
    }

    app->updateBluetoothLabel();
}

void SettingsApp::updateBluetoothLabel(void)
{
    if (_bt_label == nullptr) {
        return;
    }
    if (!bt_shared_is_running()) {
        lv_label_set_text(_bt_label, "Off");
        lv_obj_set_style_text_color(_bt_label, lv_color_hex(0xb8d2dd), 0);
        return;
    }

    lv_label_set_text(_bt_label, "Advertising as \"ESPWatchOS\" - works alongside WiFi");
    lv_obj_set_style_text_color(_bt_label, lv_color_hex(0x6cf0c2), 0);
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, SettingsApp, APP_NAME, []()
{
    return std::shared_ptr<SettingsApp>(SettingsApp::requestInstance(), [](SettingsApp *) {});
})

} // namespace esp_watchos::apps
