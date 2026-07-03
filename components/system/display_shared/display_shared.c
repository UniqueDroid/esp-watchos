#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "display_shared.h"
#include "os_fs.h"
#include "bsp/esp-bsp.h"

#define DISPLAY_CONFIG_PATH OS_FS_ETC_DIR "/display.json"

void display_shared_set_brightness(int percent)
{
    if (percent < 1) {
        percent = 1;
    } else if (percent > 100) {
        percent = 100;
    }
    bsp_display_brightness_set(percent);

    char buf[32];
    snprintf(buf, sizeof(buf), "{\"brightness\":%d}", percent);
    FILE *f = fopen(DISPLAY_CONFIG_PATH, "w");
    if (f != NULL) {
        fwrite(buf, 1, strlen(buf), f);
        fclose(f);
    }
}

int display_shared_get_saved_brightness(void)
{
    FILE *f = fopen(DISPLAY_CONFIG_PATH, "r");
    if (f == NULL) {
        return -1;
    }
    char buf[64] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    const char *p = strstr(buf, "\"brightness\"");
    if (p == NULL) {
        return -1;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return -1;
    }
    return atoi(p + 1);
}

void display_shared_apply_saved(void)
{
    int saved = display_shared_get_saved_brightness();
    if (saved > 0) {
        bsp_display_brightness_set(saved);
    }
}

#define AOD_COLOR_CONFIG_PATH OS_FS_ETC_DIR "/aod_color.json"

void display_shared_set_aod_color(uint32_t color)
{
    color &= 0xFFFFFFu;

    char buf[32];
    snprintf(buf, sizeof(buf), "{\"aod_color\":%lu}", (unsigned long)color);
    FILE *f = fopen(AOD_COLOR_CONFIG_PATH, "w");
    if (f != NULL) {
        fwrite(buf, 1, strlen(buf), f);
        fclose(f);
    }
}

bool display_shared_get_saved_aod_color(uint32_t *out_color)
{
    FILE *f = fopen(AOD_COLOR_CONFIG_PATH, "r");
    if (f == NULL) {
        return false;
    }
    char buf[64] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    const char *p = strstr(buf, "\"aod_color\"");
    if (p == NULL) {
        return false;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    *out_color = (uint32_t)strtoul(p + 1, NULL, 10) & 0xFFFFFFu;
    return true;
}
