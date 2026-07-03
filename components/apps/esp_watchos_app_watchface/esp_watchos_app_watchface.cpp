#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:Watchface"
#include "esp_lib_utils.h"
#include "esp_watchos_app_watchface.hpp"

#include "os_fs.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "wifi_shared.h"
#include "alarm_shared.h"

#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#define APP_NAME "Watchface"

#define FACE_NVS_NAMESPACE "appcfg"
#define FACE_NVS_KEY "facefile"
#define WATCHFACE_SEED_V2_MARKER OS_FS_ETC_DIR "/watchface_seed_v2.flag"

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_watchface_120_120);

namespace esp_watchos::apps {

static const char *WEEKDAY_NAMES[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *MONTH_NAMES[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static std::string load_selected_face_filename(void)
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

static void save_selected_face_filename(const std::string &filename)
{
    nvs_handle_t handle;
    if (nvs_open(FACE_NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_str(handle, FACE_NVS_KEY, filename.c_str());
        nvs_commit(handle);
        nvs_close(handle);
    }
}

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

static void write_file(const std::string &path, const char *content)
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f) {
        ESP_LOGW(ESP_UTILS_LOG_TAG, "Failed to write %s", path.c_str());
        return;
    }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
}

/* Minimal extraction of "key": "value" pairs from a flat JSON object - the
 * watchface descriptor format never nests, so a full JSON parser isn't needed. */
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

static lv_color_t parse_color(const std::string &s, uint32_t fallback)
{
    uint32_t value = s.empty() ? fallback : (uint32_t)strtoul(s.c_str(), nullptr, 16);
    return lv_color_hex(value);
}

static void seed_default_faces(const std::string &dir)
{
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

WatchfaceApp *WatchfaceApp::_instance = nullptr;

WatchfaceApp *WatchfaceApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new WatchfaceApp();
    }
    return _instance;
}

WatchfaceApp::WatchfaceApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_watchface_120_120, true, false, false)
{
}

WatchfaceApp::~WatchfaceApp()
{
}

void WatchfaceApp::loadFaces(void)
{
    _faces.clear();

    std::string apps_dir = OS_FS_APPS_DIR;
    std::string watchface_dir = apps_dir + "/watchface";
    std::string faces_dir = watchface_dir + "/faces";
    mkdir(watchface_dir.c_str(), 0755);
    mkdir(faces_dir.c_str(), 0755);

    /* One-time migration: the original default faces (classic/ocean/ember/
     * mono) were seeded before gradients/accents/marks existed. Overwrite
     * just those known filenames once with the upgraded versions, gated by
     * a marker file so it never re-runs and clobbers later user uploads of
     * the same names. */
    if (access(WATCHFACE_SEED_V2_MARKER, F_OK) != 0) {
        ESP_UTILS_LOGI("Upgrading default watchfaces to v2 styling in %s", faces_dir.c_str());
        seed_default_faces(faces_dir);
        FILE *marker = fopen(WATCHFACE_SEED_V2_MARKER, "w");
        if (marker != nullptr) {
            fwrite("1", 1, 1, marker);
            fclose(marker);
        }
    }

    bool has_any = false;
    DIR *d = opendir(faces_dir.c_str());
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

    if (!has_any) {
        ESP_UTILS_LOGI("No watchfaces found, seeding defaults into %s", faces_dir.c_str());
        seed_default_faces(faces_dir);
    }

    d = opendir(faces_dir.c_str());
    if (d != nullptr) {
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

            WatchfaceDescriptor desc;
            desc.filename = fname;
            if (!json_get(json, "name", desc.name)) {
                desc.name = fname;
            }
            if (!json_get(json, "type", desc.type)) {
                desc.type = "analog";
            }
            json_get(json, "bg_color", desc.bg_color);
            json_get(json, "ring_color", desc.ring_color);
            json_get(json, "major_color", desc.major_color);
            json_get(json, "minor_color", desc.minor_color);
            json_get(json, "hour_color", desc.hour_color);
            json_get(json, "minute_color", desc.minute_color);
            json_get(json, "second_color", desc.second_color);
            json_get(json, "time_color", desc.time_color);
            json_get(json, "date_color", desc.date_color);
            json_get(json, "bg_color2", desc.bg_color2);
            json_get(json, "accent_color", desc.accent_color);
            if (!json_get(json, "marks", desc.marks)) {
                desc.marks = "numbers";
            }
            _faces.push_back(desc);
        }
        closedir(d);
    }

    std::sort(_faces.begin(), _faces.end(), [](const WatchfaceDescriptor & a, const WatchfaceDescriptor & b) {
        return a.filename < b.filename;
    });

    if (_faces.empty()) {
        WatchfaceDescriptor fallback;
        fallback.name = "Classic";
        fallback.type = "analog";
        _faces.push_back(fallback);
    }

    ESP_UTILS_LOGI("Loaded %d watchface(s) from filesystem", (int)_faces.size());
}

bool WatchfaceApp::run(void)
{
    rtc_shared_init();
    loadFaces();

    std::string selected = load_selected_face_filename();
    _face_index = 0;
    for (size_t i = 0; i < _faces.size(); i++) {
        if (_faces[i].filename == selected) {
            _face_index = i;
            break;
        }
    }

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x05080a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, onScreenClicked, LV_EVENT_CLICKED, this);

    buildFace(scr);
    updateFace();
    _timer = lv_timer_create(onTick, 1000, this);

    return true;
}

bool WatchfaceApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool WatchfaceApp::pause(void)
{
    if (_timer != nullptr) {
        lv_timer_pause(_timer);
    }
    return true;
}

bool WatchfaceApp::resume(void)
{
    if (_timer != nullptr) {
        lv_timer_resume(_timer);
    }
    return true;
}

void WatchfaceApp::buildFace(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    _face_root = nullptr;
    _hour_hand = nullptr;
    _minute_hand = nullptr;
    _second_hand = nullptr;
    _analog_date_label = nullptr;
    _digital_time_label = nullptr;
    _digital_date_label = nullptr;
    _mod_icon = nullptr;
    _mod_time_label = nullptr;
    _mod_status_label = nullptr;
    _mod_date_label = nullptr;
    _mod_alarm_label = nullptr;
    _mod_chart = nullptr;
    _mod_chart_series = nullptr;

    const WatchfaceDescriptor &desc = _faces[_face_index];
    if (desc.type == "digital") {
        buildDigitalFace(scr, desc);
    } else if (desc.type == "modular") {
        buildModularFace(scr, desc);
    } else {
        buildAnalogFace(scr, desc);
    }

    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "tap to change face");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x445058), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

#define FACE_DIAMETER 340

/* Hands are drawn as simple lines from the center to a computed tip point,
 * recalculated on every tick. This avoids LVGL's transform/rotation path,
 * which allocates a temporary render layer per rotated widget per frame and
 * fragments the heap badly under sustained use (observed as a heap allocator
 * hang after several minutes while other apps were doing their own scans). */
static lv_obj_t *create_hand(lv_obj_t *parent, int width, lv_color_t color, lv_point_precise_t *points)
{
    lv_obj_t *hand = lv_line_create(parent);
    lv_obj_set_size(hand, FACE_DIAMETER, FACE_DIAMETER);
    lv_obj_center(hand);
    lv_obj_set_style_bg_opa(hand, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hand, 0, 0);
    lv_obj_set_style_line_color(hand, color, 0);
    lv_obj_set_style_line_width(hand, width, 0);
    lv_obj_set_style_line_rounded(hand, true, 0);
    lv_obj_clear_flag(hand, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hand, LV_OBJ_FLAG_SCROLLABLE);

    /* Points are local to the line's own (face-sized) bounding box, so the
     * pivot sits at its center; both start collapsed there. */
    int center = FACE_DIAMETER / 2;
    points[0].x = points[1].x = (lv_value_precise_t)center;
    points[0].y = points[1].y = (lv_value_precise_t)center;
    lv_line_set_points(hand, points, 2);
    return hand;
}

/* angle_deg: 0 = 12 o'clock, clockwise. Updates the line's tip point in place
 * and tells LVGL to redraw - no transform layer needed. */
static void set_hand_angle(lv_obj_t *hand, lv_point_precise_t *points, int length, double angle_deg)
{
    double rad = angle_deg * M_PI / 180.0;
    int center = FACE_DIAMETER / 2;
    points[1].x = (lv_value_precise_t)lround(center + sin(rad) * length);
    points[1].y = (lv_value_precise_t)lround(center - cos(rad) * length);
    lv_line_set_points(hand, points, 2);
}

void WatchfaceApp::buildAnalogFace(lv_obj_t *parent, const WatchfaceDescriptor &desc)
{
    lv_color_t bg_color = parse_color(desc.bg_color, 0x10181d);
    lv_color_t ring_color = parse_color(desc.ring_color, 0x355563);
    lv_color_t major_color = parse_color(desc.major_color, 0xd9f2ea);
    lv_color_t minor_color = parse_color(desc.minor_color, 0x7d96a0);
    lv_color_t hour_color = parse_color(desc.hour_color, 0xffffff);
    lv_color_t minute_color = parse_color(desc.minute_color, 0x6cf0c2);
    lv_color_t second_color = parse_color(desc.second_color, 0xff8844);
    bool has_accent = !desc.accent_color.empty();
    lv_color_t accent_color = has_accent ? parse_color(desc.accent_color, 0x6cf0c2) : major_color;

    _face_root = lv_obj_create(parent);
    lv_obj_remove_style_all(_face_root);
    lv_obj_set_size(_face_root, FACE_DIAMETER, FACE_DIAMETER);
    lv_obj_align(_face_root, LV_ALIGN_CENTER, 0, -28);
    lv_obj_set_style_radius(_face_root, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_face_root, bg_color, 0);
    lv_obj_set_style_bg_opa(_face_root, LV_OPA_COVER, 0);
    if (!desc.bg_color2.empty()) {
        /* Cheap built-in LVGL gradient fill - drawn directly into the small
         * partial buffer, no extra image/memory cost. */
        lv_obj_set_style_bg_grad_color(_face_root, parse_color(desc.bg_color2, 0x000000), 0);
        lv_obj_set_style_bg_grad_dir(_face_root, LV_GRAD_DIR_VER, 0);
    }
    /* Accent color tints the 12-o'clock mark only (see below) - deliberately
     * NOT a shadow/glow effect: LVGL shadows need a blurred temporary draw
     * layer, same per-frame allocation pattern that caused heap fragmentation
     * with rotated hands earlier, except here it would be recomputed on
     * every per-second hand tick since it sits on the always-redrawn face. */
    lv_obj_set_style_border_color(_face_root, has_accent ? accent_color : ring_color, 0);
    lv_obj_set_style_border_width(_face_root, has_accent ? 3 : 2, 0);
    lv_obj_clear_flag(_face_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_face_root, LV_OBJ_FLAG_SCROLLABLE);

    double radius = FACE_DIAMETER / 2.0;

    for (int h = 1; h <= 12; h++) {
        double angle = h * 30.0 * M_PI / 180.0;
        bool major = (h % 3 == 0);
        lv_color_t mark_color = (has_accent && h == 12) ? accent_color : (major ? major_color : minor_color);

        if (desc.marks == "ticks") {
            /* Each tick gets its own small bounding box sized just around
             * its segment (not the full face) - keeping these objects tiny
             * is what makes them as cheap as the hands, instead of forcing
             * LVGL to track/clip 12 face-sized layers on every hand tick. */
            static lv_point_precise_t tick_points[12][2];
            double r_out = radius - 10;
            double r_in = radius - (major ? 26 : 20);
            double x1 = sin(angle) * r_out, y1 = -cos(angle) * r_out;
            double x2 = sin(angle) * r_in, y2 = -cos(angle) * r_in;

            int margin = 3;
            int box_x = (int)lround(fmin(x1, x2)) - margin;
            int box_y = (int)lround(fmin(y1, y2)) - margin;
            int box_w = (int)lround(fabs(x1 - x2)) + margin * 2;
            int box_h = (int)lround(fabs(y1 - y2)) + margin * 2;

            lv_obj_t *tick = lv_line_create(_face_root);
            tick_points[h - 1][0].x = (lv_value_precise_t)lround(x1) - box_x;
            tick_points[h - 1][0].y = (lv_value_precise_t)lround(y1) - box_y;
            tick_points[h - 1][1].x = (lv_value_precise_t)lround(x2) - box_x;
            tick_points[h - 1][1].y = (lv_value_precise_t)lround(y2) - box_y;
            lv_obj_set_size(tick, box_w, box_h);
            lv_obj_align(tick, LV_ALIGN_CENTER, box_x + box_w / 2, box_y + box_h / 2);
            lv_obj_set_style_line_color(tick, mark_color, 0);
            lv_obj_set_style_line_width(tick, major ? 4 : 2, 0);
            lv_obj_set_style_line_rounded(tick, true, 0);
            lv_obj_clear_flag(tick, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
            lv_line_set_points(tick, tick_points[h - 1], 2);
        } else if (desc.marks == "dots") {
            lv_obj_t *dot = lv_obj_create(_face_root);
            lv_obj_remove_style_all(dot);
            int size = major ? 10 : 6;
            lv_obj_set_size(dot, size, size);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, mark_color, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            double r = radius - 22;
            int x = (int)lround(sin(angle) * r);
            int y = (int)lround(-cos(angle) * r);
            lv_obj_align(dot, LV_ALIGN_CENTER, x, y);
        } else {
            lv_obj_t *lbl = lv_label_create(_face_root);
            char buf[4];
            snprintf(buf, sizeof(buf), "%d", h);
            lv_label_set_text(lbl, buf);
            lv_obj_set_style_text_color(lbl, mark_color, 0);
            lv_obj_set_style_text_font(lbl, major ? &lv_font_montserrat_20 : &lv_font_montserrat_16, 0);

            double r = radius - 28;
            int x = (int)lround(sin(angle) * r);
            int y = (int)lround(-cos(angle) * r);
            lv_obj_align(lbl, LV_ALIGN_CENTER, x, y);
        }
    }

    _hour_hand = create_hand(_face_root, 8, hour_color, _hour_points);
    _minute_hand = create_hand(_face_root, 5, minute_color, _minute_points);
    _second_hand = create_hand(_face_root, 2, second_color, _second_points);

    lv_obj_t *hub = lv_obj_create(_face_root);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 14, 14);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, second_color, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
    lv_obj_center(hub);
    lv_obj_clear_flag(hub, LV_OBJ_FLAG_CLICKABLE);

    _analog_date_label = lv_label_create(parent);
    lv_label_set_text(_analog_date_label, "");
    lv_obj_set_style_text_color(_analog_date_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_analog_date_label, &lv_font_montserrat_18, 0);
    lv_obj_align_to(_analog_date_label, _face_root, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

void WatchfaceApp::buildDigitalFace(lv_obj_t *parent, const WatchfaceDescriptor &desc)
{
    lv_color_t time_color = parse_color(desc.time_color, 0xffffff);
    lv_color_t date_color = parse_color(desc.date_color, 0x6cf0c2);

    if (!desc.bg_color2.empty()) {
        lv_obj_set_style_bg_grad_color(parent, parse_color(desc.bg_color2, 0x000000), 0);
        lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);
    }

    _digital_time_label = lv_label_create(parent);
    lv_label_set_text(_digital_time_label, "--:--:--");
    lv_obj_set_style_text_color(_digital_time_label, time_color, 0);
    lv_obj_set_style_text_font(_digital_time_label, &lv_font_montserrat_44, 0);
    lv_obj_align(_digital_time_label, LV_ALIGN_CENTER, 0, -20);

    if (!desc.accent_color.empty()) {
        lv_obj_t *divider = lv_obj_create(parent);
        lv_obj_remove_style_all(divider);
        lv_obj_set_size(divider, 60, 3);
        lv_obj_set_style_bg_color(divider, parse_color(desc.accent_color, 0x6cf0c2), 0);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(divider, 2, 0);
        lv_obj_clear_flag(divider, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align_to(divider, _digital_time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }

    _digital_date_label = lv_label_create(parent);
    lv_label_set_text(_digital_date_label, "");
    lv_obj_set_style_text_color(_digital_date_label, date_color, 0);
    lv_obj_set_style_text_font(_digital_date_label, &lv_font_montserrat_20, 0);
    lv_obj_align_to(_digital_date_label, _digital_time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);
}

/* "Modular Duo"-inspired layout: icon + status top-left, big time top-right,
 * a WiFi signal-history chart (the only genuine time-series data this board
 * has - no heart-rate/activity sensors), date + next alarm at the bottom.
 * The chart reuses lv_chart exactly as Signal Tracker does, already proven
 * cheap on this hardware - no new widget types. */
void WatchfaceApp::buildModularFace(lv_obj_t *parent, const WatchfaceDescriptor &desc)
{
    lv_color_t time_color = parse_color(desc.time_color, 0xffffff);
    lv_color_t date_color = parse_color(desc.date_color, 0x6cf0c2);
    lv_color_t accent = !desc.accent_color.empty() ? parse_color(desc.accent_color, 0x6cf0c2) : date_color;

    if (!desc.bg_color2.empty()) {
        lv_obj_set_style_bg_grad_color(parent, parse_color(desc.bg_color2, 0x000000), 0);
        lv_obj_set_style_bg_grad_dir(parent, LV_GRAD_DIR_VER, 0);
    }

    // -- Top row: icon + status (left), big time (right) --
    _mod_icon = lv_obj_create(parent);
    lv_obj_remove_style_all(_mod_icon);
    lv_obj_set_size(_mod_icon, 56, 56);
    lv_obj_set_style_radius(_mod_icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_mod_icon, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_bg_opa(_mod_icon, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_mod_icon, accent, 0);
    lv_obj_set_style_border_width(_mod_icon, 2, 0);
    lv_obj_clear_flag(_mod_icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(_mod_icon, LV_ALIGN_TOP_LEFT, 24, 36);

    lv_obj_t *icon_lbl = lv_label_create(_mod_icon);
    lv_label_set_text(icon_lbl, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(icon_lbl, accent, 0);
    lv_obj_center(icon_lbl);

    _mod_status_label = lv_label_create(parent);
    lv_label_set_text(_mod_status_label, "...");
    lv_obj_set_style_text_color(_mod_status_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_mod_status_label, &lv_font_montserrat_12, 0);
    lv_obj_align_to(_mod_status_label, _mod_icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    _mod_time_label = lv_label_create(parent);
    lv_label_set_text(_mod_time_label, "--:--");
    lv_obj_set_style_text_color(_mod_time_label, time_color, 0);
    lv_obj_set_style_text_font(_mod_time_label, &lv_font_montserrat_48, 0);
    lv_obj_align(_mod_time_label, LV_ALIGN_TOP_RIGHT, -24, 34);

    // -- WiFi signal-history chart --
    lv_obj_t *chart_label = lv_label_create(parent);
    lv_label_set_text(chart_label, "WIFI SIGNAL");
    lv_obj_set_style_text_color(chart_label, lv_color_hex(0x7d96a0), 0);
    lv_obj_set_style_text_font(chart_label, &lv_font_montserrat_12, 0);
    lv_obj_align(chart_label, LV_ALIGN_TOP_LEFT, 28, 130);

    _mod_chart = lv_chart_create(parent);
    lv_obj_set_size(_mod_chart, 340, 130);
    lv_obj_align(_mod_chart, LV_ALIGN_TOP_MID, 0, 150);
    lv_chart_set_type(_mod_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(_mod_chart, WIFI_HISTORY_LEN);
    lv_chart_set_range(_mod_chart, LV_CHART_AXIS_PRIMARY_Y, -100, -20);
    lv_obj_set_style_bg_color(_mod_chart, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_border_color(_mod_chart, lv_color_hex(0x355563), 0);
    lv_obj_set_style_radius(_mod_chart, 12, 0);
    lv_obj_set_style_size(_mod_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(_mod_chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_mod_chart, LV_OBJ_FLAG_SCROLLABLE);
    _mod_chart_series = lv_chart_add_series(_mod_chart, accent, LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < WIFI_HISTORY_LEN; i++) {
        lv_chart_set_next_value(_mod_chart, _mod_chart_series, -100);
    }

    // -- Bottom row: date + next alarm --
    _mod_date_label = lv_label_create(parent);
    lv_label_set_text(_mod_date_label, "");
    lv_obj_set_style_text_color(_mod_date_label, date_color, 0);
    lv_obj_set_style_text_font(_mod_date_label, &lv_font_montserrat_18, 0);
    lv_obj_align(_mod_date_label, LV_ALIGN_BOTTOM_LEFT, 24, -50);

    _mod_alarm_label = lv_label_create(parent);
    lv_label_set_text(_mod_alarm_label, "...");
    lv_obj_set_style_text_color(_mod_alarm_label, lv_color_hex(0xb8d2dd), 0);
    lv_obj_set_style_text_font(_mod_alarm_label, &lv_font_montserrat_16, 0);
    lv_obj_align(_mod_alarm_label, LV_ALIGN_BOTTOM_RIGHT, -24, -50);
}

void WatchfaceApp::updateFace(void)
{
    rtc_shared_datetime_t dt = {};
    rtc_shared_get_datetime(&dt);

    int wd = rtc_shared_weekday(dt.year, dt.month, dt.day);
    char date_buf[48];
    snprintf(date_buf, sizeof(date_buf), "%s, %d %s %04d", WEEKDAY_NAMES[wd], dt.day, MONTH_NAMES[dt.month - 1], dt.year);

    const WatchfaceDescriptor &desc = _faces[_face_index];
    if (desc.type != "digital" && _hour_hand != nullptr) {
        double hour_angle = ((dt.hour % 12) + dt.min / 60.0) / 12.0 * 360.0;
        double minute_angle = (dt.min + dt.sec / 60.0) / 60.0 * 360.0;
        double second_angle = dt.sec / 60.0 * 360.0;

        double radius = FACE_DIAMETER / 2.0;
        set_hand_angle(_hour_hand, _hour_points, (int)(radius * 0.5), hour_angle);
        set_hand_angle(_minute_hand, _minute_points, (int)(radius * 0.72), minute_angle);
        set_hand_angle(_second_hand, _second_points, (int)(radius * 0.82), second_angle);
        lv_label_set_text(_analog_date_label, date_buf);
    } else if (_digital_time_label != nullptr) {
        char time_buf[16];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", dt.hour, dt.min, dt.sec);
        lv_label_set_text(_digital_time_label, time_buf);
        lv_label_set_text(_digital_date_label, date_buf);
    } else if (_mod_time_label != nullptr) {
        char time_buf[8];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", dt.hour, dt.min);
        lv_label_set_text(_mod_time_label, time_buf);
        lv_label_set_text(_mod_date_label, date_buf);

        int8_t rssi = 0;
        if (wifi_shared_get_rssi(&rssi)) {
            char status_buf[16];
            snprintf(status_buf, sizeof(status_buf), "%d dBm", rssi);
            lv_label_set_text(_mod_status_label, status_buf);
            if (_mod_chart_series != nullptr) {
                lv_chart_set_next_value(_mod_chart, _mod_chart_series, rssi);
            }
        } else {
            lv_label_set_text(_mod_status_label, "Offline");
            if (_mod_chart_series != nullptr) {
                lv_chart_set_next_value(_mod_chart, _mod_chart_series, -100);
            }
        }

        alarm_shared_config_t alarm = {};
        alarm_shared_load(&alarm);
        if (alarm.enabled) {
            char alarm_buf[8];
            snprintf(alarm_buf, sizeof(alarm_buf), "%02u:%02u", alarm.hour, alarm.minute);
            lv_label_set_text(_mod_alarm_label, alarm_buf);
        } else {
            lv_label_set_text(_mod_alarm_label, "Off");
        }
    }
}

void WatchfaceApp::cycleFace(void)
{
    if (_faces.empty()) {
        return;
    }
    _face_index = (_face_index + 1) % _faces.size();
    save_selected_face_filename(_faces[_face_index].filename);
    buildFace(lv_scr_act());
    updateFace();
}

void WatchfaceApp::onTick(lv_timer_t *t)
{
    WatchfaceApp *app = (WatchfaceApp *)lv_timer_get_user_data(t);
    app->updateFace();
}

void WatchfaceApp::onScreenClicked(lv_event_t *e)
{
    WatchfaceApp *app = (WatchfaceApp *)lv_event_get_user_data(e);
    app->cycleFace();
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, WatchfaceApp, APP_NAME, []()
{
    return std::shared_ptr<WatchfaceApp>(WatchfaceApp::requestInstance(), [](WatchfaceApp *) {});
})

} // namespace esp_watchos::apps
