#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On-demand HTTP server (started/stopped explicitly, e.g. from a Settings
 * toggle) that lets a browser on the same WiFi network list, upload, and
 * delete watchface JSON files in /data/apps/watchface/faces/ - the missing
 * piece that let users add new watchfaces without reflashing the firmware.
 * Only listens on the STA interface; requires wifi_shared to be connected. */
bool webserver_shared_start(void);
void webserver_shared_stop(void);
bool webserver_shared_is_running(void);

/* Every request must present this device-generated password via HTTP Basic
 * Auth (username "admin") - generated once on first use and persisted, so
 * it's stable across reboots but unique per device. Copies it (8 chars +
 * NUL) into `out` so the Settings app can show it on-screen; returns false
 * only if it couldn't be generated/read (out is left untouched). */
bool webserver_shared_get_password(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
