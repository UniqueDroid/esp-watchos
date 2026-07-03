#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WOS:Flappy"
#include "esp_lib_utils.h"
#include "esp_watchos_app_flappy.hpp"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>

LV_IMG_DECLARE(esp_watchos_app_flappy_bird_32);

#define APP_NAME "Flappy"
#define DISPLAY_HEIGHT 502
#define STATUS_BAR_HEIGHT 52
#define CONTENT_HEIGHT (DISPLAY_HEIGHT - STATUS_BAR_HEIGHT)
#define CONTENT_WIDTH 410

#define GROUND_HEIGHT 30
#define PLAY_HEIGHT (CONTENT_HEIGHT - GROUND_HEIGHT)

#define BIRD_X 70
#define BIRD_SIZE 30

#define PIPE_WIDTH 56
#define PIPE_GAP 150
#define PIPE_SPACING 190
#define PIPE_SPEED 3.2

#define GRAVITY 0.55
#define FLAP_VY (-7.6)

#define TICK_MS 30

using namespace esp_brookesia::systems;

LV_IMG_DECLARE(esp_watchos_app_icon_launcher_flappy_120_120);

namespace esp_watchos::apps {

FlappyApp *FlappyApp::_instance = nullptr;

FlappyApp *FlappyApp::requestInstance()
{
    if (_instance == nullptr) {
        _instance = new FlappyApp();
    }
    return _instance;
}

FlappyApp::FlappyApp():
    esp_brookesia::systems::phone::App(APP_NAME, &esp_watchos_app_icon_launcher_flappy_120_120, true, true, false)
{
}

FlappyApp::~FlappyApp()
{
}

bool FlappyApp::run(void)
{
    srand((unsigned)time(nullptr));

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1115), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), CONTENT_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, STATUS_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);

    buildUi(content);
    resetGame();
    _timer = lv_timer_create(onTick, TICK_MS, this);

    return true;
}

bool FlappyApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool FlappyApp::pause(void)
{
    if (_timer != nullptr) {
        lv_timer_pause(_timer);
    }
    return true;
}

bool FlappyApp::resume(void)
{
    if (_timer != nullptr) {
        lv_timer_resume(_timer);
    }
    return true;
}

void FlappyApp::buildUi(lv_obj_t *parent)
{
    // -- Sky play area (everything below is a plain lv_obj, no images/transforms) --
    _play_area = lv_obj_create(parent);
    lv_obj_remove_style_all(_play_area);
    lv_obj_set_size(_play_area, LV_PCT(100), CONTENT_HEIGHT);
    lv_obj_set_pos(_play_area, 0, 0);
    lv_obj_set_style_bg_color(_play_area, lv_color_hex(0x6cc8f0), 0);
    lv_obj_set_style_bg_opa(_play_area, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_play_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_play_area, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_play_area, onAreaPressed, LV_EVENT_PRESSED, this);

    lv_obj_t *ground = lv_obj_create(_play_area);
    lv_obj_remove_style_all(ground);
    lv_obj_set_size(ground, LV_PCT(100), GROUND_HEIGHT);
    lv_obj_set_pos(ground, 0, PLAY_HEIGHT);
    lv_obj_set_style_bg_color(ground, lv_color_hex(0xb49640), 0);
    lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ground, LV_OBJ_FLAG_CLICKABLE);

    // -- Pipe pool: fixed objects, recycled by repositioning (no per-frame alloc) --
    for (int i = 0; i < PIPE_COUNT; i++) {
        _pipe_top[i] = lv_obj_create(_play_area);
        lv_obj_remove_style_all(_pipe_top[i]);
        lv_obj_set_size(_pipe_top[i], PIPE_WIDTH, 10);
        lv_obj_set_style_bg_color(_pipe_top[i], lv_color_hex(0x4ab45a), 0);
        lv_obj_set_style_bg_opa(_pipe_top[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(_pipe_top[i], lv_color_hex(0x2a6e35), 0);
        lv_obj_set_style_border_width(_pipe_top[i], 2, 0);
        lv_obj_clear_flag(_pipe_top[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(_pipe_top[i], LV_OBJ_FLAG_SCROLLABLE);

        _pipe_bottom[i] = lv_obj_create(_play_area);
        lv_obj_remove_style_all(_pipe_bottom[i]);
        lv_obj_set_size(_pipe_bottom[i], PIPE_WIDTH, 10);
        lv_obj_set_style_bg_color(_pipe_bottom[i], lv_color_hex(0x4ab45a), 0);
        lv_obj_set_style_bg_opa(_pipe_bottom[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(_pipe_bottom[i], lv_color_hex(0x2a6e35), 0);
        lv_obj_set_style_border_width(_pipe_bottom[i], 2, 0);
        lv_obj_clear_flag(_pipe_bottom[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(_pipe_bottom[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    // -- Bird --
    _bird = lv_image_create(_play_area);
    lv_image_set_src(_bird, &esp_watchos_app_flappy_bird_32);
    lv_obj_set_size(_bird, BIRD_SIZE, BIRD_SIZE);
    lv_obj_clear_flag(_bird, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_bird, LV_OBJ_FLAG_SCROLLABLE);

    // -- Score / hint --
    _score_label = lv_label_create(_play_area);
    lv_label_set_text(_score_label, "0");
    lv_obj_set_style_text_color(_score_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_score_label, &lv_font_montserrat_36, 0);
    lv_obj_align(_score_label, LV_ALIGN_TOP_MID, 0, 12);

    _hint_label = lv_label_create(_play_area);
    lv_label_set_text(_hint_label, "Tap to start");
    lv_obj_set_style_text_color(_hint_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_hint_label, &lv_font_montserrat_20, 0);
    lv_obj_align(_hint_label, LV_ALIGN_CENTER, 0, -30);
}

void FlappyApp::spawnPipe(int index, int x)
{
    int margin = 40;
    int min_center = PIPE_GAP / 2 + margin;
    int max_center = PLAY_HEIGHT - PIPE_GAP / 2 - margin;
    int gap_center = min_center + (rand() % (max_center - min_center + 1));

    _pipe_x[index] = x;
    _pipe_gap_y[index] = gap_center;
    _pipe_scored[index] = false;
}

void FlappyApp::resetGame(void)
{
    _bird_y = PLAY_HEIGHT / 2.0;
    _bird_vy = 0;
    _score = 0;
    _started = false;
    _alive = true;

    lv_label_set_text(_score_label, "0");
    lv_label_set_text(_hint_label, "Tap to start");
    lv_obj_clear_flag(_hint_label, LV_OBJ_FLAG_HIDDEN);

    if (_game_over_panel != nullptr) {
        lv_obj_del(_game_over_panel);
        _game_over_panel = nullptr;
    }

    for (int i = 0; i < PIPE_COUNT; i++) {
        spawnPipe(i, CONTENT_WIDTH + 60 + i * PIPE_SPACING);
    }
}

void FlappyApp::flap(void)
{
    if (!_alive) {
        return;
    }
    if (!_started) {
        _started = true;
        lv_obj_add_flag(_hint_label, LV_OBJ_FLAG_HIDDEN);
    }
    _bird_vy = FLAP_VY;
}

void FlappyApp::endGame(void)
{
    _alive = false;

    _game_over_panel = lv_obj_create(_play_area);
    lv_obj_remove_style_all(_game_over_panel);
    lv_obj_set_size(_game_over_panel, 260, 160);
    lv_obj_center(_game_over_panel);
    lv_obj_set_style_bg_color(_game_over_panel, lv_color_hex(0x141c22), 0);
    lv_obj_set_style_bg_opa(_game_over_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_game_over_panel, lv_color_hex(0x6cf0c2), 0);
    lv_obj_set_style_border_width(_game_over_panel, 2, 0);
    lv_obj_set_style_radius(_game_over_panel, 12, 0);
    lv_obj_set_flex_flow(_game_over_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_game_over_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_game_over_panel, 8, 0);
    lv_obj_clear_flag(_game_over_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(_game_over_panel);
    lv_label_set_text(title, "Game Over");
    lv_obj_set_style_text_color(title, lv_color_hex(0xff8888), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    _final_score_label = lv_label_create(_game_over_panel);
    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %d", _score);
    lv_label_set_text(_final_score_label, buf);
    lv_obj_set_style_text_color(_final_score_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(_final_score_label, &lv_font_montserrat_18, 0);

    lv_obj_t *restart_btn = lv_button_create(_game_over_panel);
    lv_obj_set_size(restart_btn, 140, 44);
    lv_obj_add_event_cb(restart_btn, onRestartClicked, LV_EVENT_CLICKED, this);
    lv_obj_t *restart_label = lv_label_create(restart_btn);
    lv_label_set_text(restart_label, "Restart");
    lv_obj_center(restart_label);
}

void FlappyApp::tick(void)
{
    if (!_alive) {
        return;
    }

    if (_started) {
        _bird_vy += GRAVITY;
        _bird_y += _bird_vy;
        if (_bird_y < BIRD_SIZE / 2.0) {
            _bird_y = BIRD_SIZE / 2.0;
            _bird_vy = 0;
        }
    }
    lv_obj_set_pos(_bird, BIRD_X - BIRD_SIZE / 2, (int)lround(_bird_y) - BIRD_SIZE / 2);

    int bird_left = BIRD_X - BIRD_SIZE / 2;
    int bird_right = BIRD_X + BIRD_SIZE / 2;
    int bird_top = (int)lround(_bird_y) - BIRD_SIZE / 2;
    int bird_bottom = (int)lround(_bird_y) + BIRD_SIZE / 2;

    if (bird_bottom >= PLAY_HEIGHT) {
        endGame();
        return;
    }

    if (_started) {
        for (int i = 0; i < PIPE_COUNT; i++) {
            _pipe_x[i] -= (int)lround(PIPE_SPEED);
            if (_pipe_x[i] + PIPE_WIDTH < 0) {
                int max_x = 0;
                for (int j = 0; j < PIPE_COUNT; j++) {
                    if (_pipe_x[j] > max_x) {
                        max_x = _pipe_x[j];
                    }
                }
                spawnPipe(i, max_x + PIPE_SPACING);
            }

            int gap_top = _pipe_gap_y[i] - PIPE_GAP / 2;
            int gap_bottom = _pipe_gap_y[i] + PIPE_GAP / 2;

            lv_obj_set_pos(_pipe_top[i], _pipe_x[i], 0);
            lv_obj_set_size(_pipe_top[i], PIPE_WIDTH, gap_top);
            lv_obj_set_pos(_pipe_bottom[i], _pipe_x[i], gap_bottom);
            lv_obj_set_size(_pipe_bottom[i], PIPE_WIDTH, PLAY_HEIGHT - gap_bottom);

            bool x_overlap = (bird_right > _pipe_x[i]) && (bird_left < _pipe_x[i] + PIPE_WIDTH);
            if (x_overlap && (bird_top < gap_top || bird_bottom > gap_bottom)) {
                endGame();
                return;
            }

            if (!_pipe_scored[i] && (_pipe_x[i] + PIPE_WIDTH) < bird_left) {
                _pipe_scored[i] = true;
                _score++;
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", _score);
                lv_label_set_text(_score_label, buf);
            }
        }
    }
}

void FlappyApp::onTick(lv_timer_t *t)
{
    FlappyApp *app = (FlappyApp *)lv_timer_get_user_data(t);
    app->tick();
}

void FlappyApp::onAreaPressed(lv_event_t *e)
{
    FlappyApp *app = (FlappyApp *)lv_event_get_user_data(e);
    app->flap();
}

void FlappyApp::onRestartClicked(lv_event_t *e)
{
    FlappyApp *app = (FlappyApp *)lv_event_get_user_data(e);
    app->resetGame();
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(esp_brookesia::systems::base::App, FlappyApp, APP_NAME, []()
{
    return std::shared_ptr<FlappyApp>(FlappyApp::requestInstance(), [](FlappyApp *) {});
})

} // namespace esp_watchos::apps
