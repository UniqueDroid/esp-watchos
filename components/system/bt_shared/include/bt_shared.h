#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal BLE presence: initializes NimBLE (once, lazily, on first start)
 * and starts/stops undirected-connectable advertising as "ESPWatchOS". No
 * GATT services yet - this exists to prove out BLE + WiFi STA coexistence
 * on the ESP32-C6's single shared 2.4GHz radio before building an actual
 * feature (notifications, a BLE scanner app, etc.) on top of it. */
bool bt_shared_start(void);
void bt_shared_stop(void);
bool bt_shared_is_running(void);

#ifdef __cplusplus
}
#endif
