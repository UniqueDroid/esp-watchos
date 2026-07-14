#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char city[48];
    double latitude;
    double longitude;
    bool valid;
} weather_shared_location_t;

typedef struct {
    float temperature_c;
    int weather_code;  // WMO code, see weather_shared_describe()
    bool valid;
} weather_shared_data_t;

/* Loads the persisted location (if any) from /data/etc/weather.json. Call
 * once at boot, after os_fs_mount(). */
esp_err_t weather_shared_init(void);

/* Returns the currently saved location. false if none saved yet. */
bool weather_shared_get_location(weather_shared_location_t *out);

/* Resolves a city name to coordinates via Open-Meteo's free geocoding API
 * and persists the result, on a background task (needs Wi-Fi - caller
 * should check wifi_shared_is_connected() first). Returns false if a
 * lookup is already running or Wi-Fi isn't up.
 * Poll with weather_shared_geocode_poll() from the UI thread. */
bool weather_shared_geocode_city_async(const char *city_name);

bool weather_shared_geocode_is_running(void);

/* Non-blocking poll for a finished geocode lookup. Returns true (and
 * consumes the result) once since the last call that returned true.
 * *out_ok reports whether the city was actually found. */
bool weather_shared_geocode_poll(bool *out_ok);

/* Fetches current temperature + condition for the saved location on a
 * background task. Returns false if no location is saved, a fetch is
 * already running, or Wi-Fi isn't connected. */
bool weather_shared_refresh_async(void);

bool weather_shared_refresh_is_running(void);

/* Non-blocking poll, intended for an lv_timer on the UI thread - same
 * pattern as wifi_shared_scan_poll(). Returns true (and consumes the
 * result) if a fetch completed since the last poll. */
bool weather_shared_poll(weather_shared_data_t *out);

/* Last successfully fetched weather, if any (does not require a fresh poll -
 * safe to call every frame for display). */
weather_shared_data_t weather_shared_get_last(void);

/* True if the last fetch is missing or older than 30 minutes - callers
 * (e.g. the AOD screen) can use this to decide whether to call
 * weather_shared_refresh_async() again. */
bool weather_shared_is_stale(void);

/* Short human-readable label for a WMO weather code ("Clear", "Cloudy",
 * "Rain", "Snow", "Storm", "Fog", ...). Never returns NULL. */
const char *weather_shared_describe(int weather_code);

#ifdef __cplusplus
}
#endif
