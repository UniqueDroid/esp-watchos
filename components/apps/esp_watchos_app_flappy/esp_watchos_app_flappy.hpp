#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_watchos::apps {

class FlappyApp: public esp_brookesia::systems::phone::App {
public:
    static FlappyApp *requestInstance();
    ~FlappyApp();

protected:
    FlappyApp();

    bool run(void) override;
    bool back(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static constexpr int PIPE_COUNT = 3;

    void buildUi(lv_obj_t *parent);
    void resetGame(void);
    void tick(void);
    void flap(void);
    void endGame(void);
    void spawnPipe(int index, int x);

    static void onTick(lv_timer_t *t);
    static void onAreaPressed(lv_event_t *e);
    static void onRestartClicked(lv_event_t *e);

    static FlappyApp *_instance;

    lv_obj_t *_play_area = nullptr;
    lv_obj_t *_bird = nullptr;
    lv_obj_t *_pipe_top[PIPE_COUNT] = {};
    lv_obj_t *_pipe_bottom[PIPE_COUNT] = {};
    int _pipe_x[PIPE_COUNT] = {};
    int _pipe_gap_y[PIPE_COUNT] = {};
    bool _pipe_scored[PIPE_COUNT] = {};

    lv_obj_t *_score_label = nullptr;
    lv_obj_t *_hint_label = nullptr;
    lv_obj_t *_game_over_panel = nullptr;
    lv_obj_t *_final_score_label = nullptr;

    lv_timer_t *_timer = nullptr;

    double _bird_y = 0;
    double _bird_vy = 0;
    int _score = 0;
    bool _started = false;
    bool _alive = true;
};

} // namespace esp_watchos::apps
