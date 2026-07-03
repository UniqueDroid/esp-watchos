#pragma once

#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif
