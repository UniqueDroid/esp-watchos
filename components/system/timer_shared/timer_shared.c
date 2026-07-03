#include <time.h>

#include "timer_shared.h"

static time_t s_end_time = 0;
static bool s_fired = false;

void timer_shared_start(int seconds_from_now)
{
    s_end_time = time(NULL) + seconds_from_now;
    s_fired = false;
}

void timer_shared_cancel(void)
{
    s_end_time = 0;
    s_fired = false;
}

bool timer_shared_is_running(void)
{
    return s_end_time != 0 && !s_fired;
}

int timer_shared_remaining_seconds(void)
{
    if (s_end_time == 0) {
        return -1;
    }
    long remaining = (long)(s_end_time - time(NULL));
    return remaining > 0 ? (int)remaining : 0;
}

bool timer_shared_check_and_consume(void)
{
    if (s_end_time == 0 || s_fired) {
        return false;
    }
    if (time(NULL) >= s_end_time) {
        s_fired = true;
        return true;
    }
    return false;
}
