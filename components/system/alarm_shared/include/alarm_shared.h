#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t hour;
    uint8_t minute;
    bool enabled;
} alarm_shared_config_t;

/* Loads the persisted alarm config from /data/etc/alarm.json, or the
 * built-in default (07:00, disabled) if none was ever saved. */
void alarm_shared_load(alarm_shared_config_t *out);

void alarm_shared_save(const alarm_shared_config_t *cfg);

/* Call once per second (e.g. from main.cpp's existing clock timer) with the
 * current wall-clock hour/minute. Returns true exactly once per matching
 * minute so the caller can show a global notification regardless of which
 * app is currently in the foreground. */
bool alarm_shared_check_and_consume(int hour, int minute);

#ifdef __cplusplus
}
#endif
