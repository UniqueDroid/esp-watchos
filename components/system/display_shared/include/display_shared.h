#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Applies + persists brightness (1-100) to /data/etc/display.json - without
 * this, brightness reset to the bsp default on every reboot. */
void display_shared_set_brightness(int percent);

/* Returns the saved brightness (1-100), or -1 if never saved. */
int display_shared_get_saved_brightness(void);

/* Applies the saved brightness (if any) - call once at boot after the
 * display/backlight is initialized. */
void display_shared_apply_saved(void);

/* Persists the AOD (always-on-display) clock's segment/colon color. */
void display_shared_set_aod_color(uint32_t color);

/* Returns the saved AOD color, or false if never saved. */
bool display_shared_get_saved_aod_color(uint32_t *out_color);

#ifdef __cplusplus
}
#endif
