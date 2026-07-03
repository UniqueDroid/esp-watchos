#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* In-RAM only (intentionally not persisted - a running countdown lost on
 * reboot is an acceptable kitchen-timer tradeoff). Backed by an absolute
 * end timestamp rather than a decrementing counter, so the remaining time
 * stays correct even while the Timer app isn't the foreground app. */
void timer_shared_start(int seconds_from_now);
void timer_shared_cancel(void);
bool timer_shared_is_running(void);

/* -1 if no timer is set. */
int timer_shared_remaining_seconds(void);

/* Call once per second (e.g. from main.cpp's existing clock timer). Returns
 * true exactly once when the timer reaches zero. */
bool timer_shared_check_and_consume(void);

#ifdef __cplusplus
}
#endif
