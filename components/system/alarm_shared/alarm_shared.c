#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "alarm_shared.h"
#include "os_fs.h"

#define ALARM_CONFIG_PATH OS_FS_ETC_DIR "/alarm.json"

static alarm_shared_config_t s_cfg = {.hour = 7, .minute = 0, .enabled = false};
static bool s_loaded = false;
static long s_last_fired_minute_id = -1;

static bool json_get_int(const char *json, const char *key, int *out)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    *out = atoi(p + 1);
    return true;
}

static bool json_get_bool(const char *json, const char *key, bool *out)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;
    while (*p == ' ') {
        p++;
    }
    *out = (strncmp(p, "true", 4) == 0);
    return true;
}

static void load_from_disk(void)
{
    FILE *f = fopen(ALARM_CONFIG_PATH, "r");
    if (f == NULL) {
        return;
    }
    char buf[128] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    int hour, minute;
    bool enabled;
    if (json_get_int(buf, "hour", &hour)) {
        s_cfg.hour = (uint8_t)hour;
    }
    if (json_get_int(buf, "minute", &minute)) {
        s_cfg.minute = (uint8_t)minute;
    }
    if (json_get_bool(buf, "enabled", &enabled)) {
        s_cfg.enabled = enabled;
    }
}

void alarm_shared_load(alarm_shared_config_t *out)
{
    if (!s_loaded) {
        load_from_disk();
        s_loaded = true;
    }
    *out = s_cfg;
}

void alarm_shared_save(const alarm_shared_config_t *cfg)
{
    s_cfg = *cfg;
    s_loaded = true;

    char buf[96];
    snprintf(buf, sizeof(buf), "{\"hour\":%u,\"minute\":%u,\"enabled\":%s}",
             cfg->hour, cfg->minute, cfg->enabled ? "true" : "false");

    FILE *f = fopen(ALARM_CONFIG_PATH, "w");
    if (f != NULL) {
        fwrite(buf, 1, strlen(buf), f);
        fclose(f);
    }
}

bool alarm_shared_check_and_consume(int hour, int minute)
{
    if (!s_loaded) {
        load_from_disk();
        s_loaded = true;
    }
    if (!s_cfg.enabled || hour != s_cfg.hour || minute != s_cfg.minute) {
        return false;
    }

    /* Debounce by absolute minute (not wall-clock minute-of-hour), so the
     * alarm fires once per day instead of being permanently suppressed
     * after the first time it matches. */
    long minute_id = (long)(time(NULL) / 60);
    if (minute_id == s_last_fired_minute_id) {
        return false;
    }
    s_last_fired_minute_id = minute_id;
    return true;
}
