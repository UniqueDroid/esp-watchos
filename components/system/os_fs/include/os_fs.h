#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ESPWatchOS on-device filesystem layout (LittleFS, partition "storage"):
 *
 *   /data/system   - reserved for core OS state
 *   /data/apps     - per-app data, loadable content
 *   /data/etc      - user settings
 *   /data/tmp      - scratch space, cleared on demand
 */
#define OS_FS_ROOT       "/data"
#define OS_FS_SYSTEM_DIR "/data/system"
#define OS_FS_APPS_DIR   "/data/apps"
#define OS_FS_ETC_DIR    "/data/etc"
#define OS_FS_TMP_DIR    "/data/tmp"

/* Mounts the LittleFS partition at OS_FS_ROOT and ensures the standard
 * directory layout exists. Safe to call multiple times. */
esp_err_t os_fs_mount(void);

#ifdef __cplusplus
}
#endif
