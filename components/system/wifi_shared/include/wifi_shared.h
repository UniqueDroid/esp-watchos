#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SHARED_MAX_AP_RECORDS 40

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t primary;
    wifi_auth_mode_t authmode;
    uint8_t bssid[6];
} wifi_shared_ap_t;

/* Initializes netif/event loop/Wi-Fi STA once for the whole firmware. */
esp_err_t wifi_shared_init(void);

/* Attempts to reconnect using credentials stored in NVS, if any and not already connected.
 * Call this only from apps that actually need connectivity (e.g. Network Scanner, Port
 * Checker) - calling it unconditionally at boot would delay/block passive-scan apps. */
void wifi_shared_try_autoconnect(void);

/* Blocking active scan, returns the number of APs written into out (sorted by RSSI desc).
 * Do not call this from the LVGL/UI thread - it blocks for seconds. Use the async API below
 * from UI code instead. */
int wifi_shared_scan(wifi_shared_ap_t *out, int max);

/* Runs a Wi-Fi scan on a background task so the UI thread never blocks.
 * Returns false if a scan is already running (call is ignored). */
bool wifi_shared_scan_start_async(void);

bool wifi_shared_scan_is_running(void);

/* Non-blocking poll, intended to be called periodically from an lv_timer on the UI thread.
 * Returns true (and consumes the result) if a scan completed since the last poll. */
bool wifi_shared_scan_poll(wifi_shared_ap_t *out, int max, int *count);

/* Connects to an AP and persists the credentials to NVS for auto-reconnect on boot. */
esp_err_t wifi_shared_connect(const char *ssid, const char *password);

void wifi_shared_disconnect(void);

bool wifi_shared_is_connected(void);

/* Writes the dotted-decimal IP into buf, returns false if not connected. */
bool wifi_shared_get_ip(char *buf, size_t len);

/* Writes the SSID of the currently connected AP into buf, returns false if not connected. */
bool wifi_shared_get_ssid(char *buf, size_t len);

/* Writes the current connection's signal strength (no active scan needed -
 * cheap enough to poll once a second). Returns false if not connected. */
bool wifi_shared_get_rssi(int8_t *out_rssi);

#ifdef __cplusplus
}
#endif
