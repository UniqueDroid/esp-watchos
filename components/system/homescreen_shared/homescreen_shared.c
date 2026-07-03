#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "homescreen_shared.h"
#include "os_fs.h"
#include "esp_log.h"

#define HOMESCREEN_CONFIG_PATH OS_FS_ETC_DIR "/homescreen.json"

static const char *TAG = "homescreen_shared";
static lv_obj_t *s_main_screen = NULL;

void homescreen_shared_register_main_screen(lv_obj_t *scr)
{
    s_main_screen = scr;
}

void homescreen_shared_set_bg_color(uint32_t color)
{
    if (s_main_screen != NULL) {
        lv_obj_set_style_bg_color(s_main_screen, lv_color_hex(color), 0);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "{\"bg_color\":\"0x%06lx\"}", (unsigned long)(color & 0xFFFFFFu));

    FILE *f = fopen(HOMESCREEN_CONFIG_PATH, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "Failed to save homescreen config to %s", HOMESCREEN_CONFIG_PATH);
        return;
    }
    fwrite(buf, 1, strlen(buf), f);
    fclose(f);
}

bool homescreen_shared_get_saved_color(uint32_t *out_color)
{
    FILE *f = fopen(HOMESCREEN_CONFIG_PATH, "r");
    if (f == NULL) {
        return false;
    }
    char json[64] = {0};
    size_t n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';

    const char *p = strstr(json, "\"bg_color\"");
    if (p == NULL) {
        return false;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t' || *p == '"') {
        p++;
    }
    *out_color = (uint32_t)strtoul(p, NULL, 16);
    return true;
}

void homescreen_shared_apply_saved(void)
{
    uint32_t color;
    if (homescreen_shared_get_saved_color(&color) && s_main_screen != NULL) {
        lv_obj_set_style_bg_color(s_main_screen, lv_color_hex(color), 0);
    }
}
