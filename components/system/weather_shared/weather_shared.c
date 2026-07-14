#include "weather_shared.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "os_fs.h"
#include "wifi_shared.h"

static const char *TAG = "weather_shared";

#define WEATHER_CONFIG_PATH OS_FS_ETC_DIR "/weather.json"
#define HTTP_RESPONSE_BUF_SIZE 2048
#define STALE_AFTER_MS (30u * 60u * 1000u)

static weather_shared_location_t s_location = {0};
static weather_shared_data_t s_last_data = {0};
static uint32_t s_last_fetch_at_ms = 0;

static volatile bool s_geocode_running = false;
static volatile bool s_refresh_running = false;
static QueueHandle_t s_geocode_queue = NULL;
static QueueHandle_t s_refresh_queue = NULL;

/* -- Persistence (same hand-rolled JSON style as display_shared) --------- */

static void save_location(void)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "{\"city\":\"%s\",\"lat\":%f,\"lon\":%f}", s_location.city, s_location.latitude,
             s_location.longitude);
    FILE *f = fopen(WEATHER_CONFIG_PATH, "w");
    if (f != NULL) {
        fwrite(buf, 1, strlen(buf), f);
        fclose(f);
    }
}

static void load_location(void)
{
    FILE *f = fopen(WEATHER_CONFIG_PATH, "r");
    if (f == NULL) {
        return;
    }
    char buf[256] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    const char *city_key = strstr(buf, "\"city\"");
    const char *lat_key = strstr(buf, "\"lat\"");
    const char *lon_key = strstr(buf, "\"lon\"");
    if (city_key == NULL || lat_key == NULL || lon_key == NULL) {
        return;
    }

    const char *city_start = strchr(city_key, '"');
    city_start = city_start != NULL ? strchr(city_start + 1, '"') : NULL;  // skip the "city" key's own quotes
    city_start = city_start != NULL ? strchr(city_start + 1, '"') : NULL;  // opening quote of the value
    if (city_start == NULL) {
        return;
    }
    const char *city_end = strchr(city_start + 1, '"');
    if (city_end == NULL) {
        return;
    }
    size_t len = (size_t)(city_end - (city_start + 1));
    if (len >= sizeof(s_location.city)) {
        len = sizeof(s_location.city) - 1;
    }
    memcpy(s_location.city, city_start + 1, len);
    s_location.city[len] = '\0';

    s_location.latitude = strtod(strchr(lat_key, ':') + 1, NULL);
    s_location.longitude = strtod(strchr(lon_key, ':') + 1, NULL);
    s_location.valid = true;
}

esp_err_t weather_shared_init(void)
{
    load_location();
    s_geocode_queue = xQueueCreate(1, sizeof(weather_shared_location_t));
    s_refresh_queue = xQueueCreate(1, sizeof(weather_shared_data_t));
    return ESP_OK;
}

bool weather_shared_get_location(weather_shared_location_t *out)
{
    if (!s_location.valid) {
        return false;
    }
    *out = s_location;
    return true;
}

/* -- Minimal HTTP GET into a fixed buffer --------------------------------
 * Open-Meteo's geocoding/forecast responses for a single result/one
 * "current" block comfortably fit HTTP_RESPONSE_BUF_SIZE; truncation just
 * means the trailing fields (which we don't parse anyway) get cut off. */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} http_capture_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        http_capture_t *cap = (http_capture_t *)evt->user_data;
        size_t space = cap->cap - 1 - cap->len;
        size_t n = (size_t)evt->data_len < space ? (size_t)evt->data_len : space;
        if (n > 0) {
            memcpy(cap->buf + cap->len, evt->data, n);
            cap->len += n;
            cap->buf[cap->len] = '\0';
        }
    }
    return ESP_OK;
}

/* Blocking GET, meant to be called from a background task only. Returns
 * true on HTTP 200. */
static bool http_get(const char *url, char *out_buf, size_t out_cap)
{
    http_capture_t cap = {.buf = out_buf, .len = 0, .cap = out_cap};
    out_buf[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &cap,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        return false;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP GET %s -> status %d", url, status);
        return false;
    }
    return true;
}

/* Finds "key": <number>, tolerant of whitespace, no quotes on the value. */
static bool json_find_number(const char *json, const char *key, double *out)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == NULL) {
        return false;
    }
    char *end = NULL;
    double val = strtod(p + 1, &end);
    if (end == p + 1) {
        return false;
    }
    *out = val;
    return true;
}

/* -- Geocoding ------------------------------------------------------------ */

static void geocode_task(void *arg)
{
    char *city_name = (char *)arg;
    weather_shared_location_t result = {0};
    strncpy(result.city, city_name, sizeof(result.city) - 1);
    free(city_name);

    char url[256];
    char encoded[64];
    size_t j = 0;
    for (size_t i = 0; result.city[i] != '\0' && j + 4 < sizeof(encoded); i++) {
        unsigned char c = (unsigned char)result.city[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            encoded[j++] = (char)c;
        } else if (c == ' ') {
            encoded[j++] = '+';
        } else {
            j += snprintf(encoded + j, sizeof(encoded) - j, "%%%02X", c);
        }
    }
    encoded[j] = '\0';
    snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=en&format=json",
             encoded);

    char response[HTTP_RESPONSE_BUF_SIZE];
    double lat = 0, lon = 0;
    if (http_get(url, response, sizeof(response)) && strstr(response, "\"results\"") != NULL &&
        json_find_number(response, "latitude", &lat) && json_find_number(response, "longitude", &lon)) {
        result.latitude = lat;
        result.longitude = lon;
        result.valid = true;
    } else {
        ESP_LOGW(TAG, "Geocoding failed for \"%s\"", result.city);
        result.valid = false;
    }

    xQueueOverwrite(s_geocode_queue, &result);
    s_geocode_running = false;
    vTaskDelete(NULL);
}

bool weather_shared_geocode_city_async(const char *city_name)
{
    if (s_geocode_running || s_geocode_queue == NULL || !wifi_shared_is_connected()) {
        return false;
    }
    char *copy = strdup(city_name);
    if (copy == NULL) {
        return false;
    }
    s_geocode_running = true;
    if (xTaskCreate(geocode_task, "weather_geo", 8192, copy, 5, NULL) != pdPASS) {
        s_geocode_running = false;
        free(copy);
        return false;
    }
    return true;
}

bool weather_shared_geocode_is_running(void)
{
    return s_geocode_running;
}

bool weather_shared_geocode_poll(bool *out_ok)
{
    if (s_geocode_queue == NULL) {
        return false;
    }
    weather_shared_location_t result;
    if (xQueueReceive(s_geocode_queue, &result, 0) != pdTRUE) {
        return false;
    }
    *out_ok = result.valid;
    if (result.valid) {
        s_location = result;
        save_location();
    }
    return true;
}

/* -- Current weather -------------------------------------------------------- */

static void refresh_task(void *arg)
{
    (void)arg;
    weather_shared_data_t result = {0};

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&current=temperature_2m,weather_code",
             s_location.latitude, s_location.longitude);

    char response[HTTP_RESPONSE_BUF_SIZE];
    double temp = 0, code = 0;
    if (http_get(url, response, sizeof(response)) && json_find_number(response, "temperature_2m", &temp) &&
        json_find_number(response, "weather_code", &code)) {
        result.temperature_c = (float)temp;
        result.weather_code = (int)code;
        result.valid = true;
    } else {
        ESP_LOGW(TAG, "Weather refresh failed");
    }

    xQueueOverwrite(s_refresh_queue, &result);
    s_refresh_running = false;
    vTaskDelete(NULL);
}

bool weather_shared_refresh_async(void)
{
    if (s_refresh_running || s_refresh_queue == NULL || !s_location.valid || !wifi_shared_is_connected()) {
        return false;
    }
    s_refresh_running = true;
    if (xTaskCreate(refresh_task, "weather_fetch", 8192, NULL, 5, NULL) != pdPASS) {
        s_refresh_running = false;
        return false;
    }
    return true;
}

bool weather_shared_refresh_is_running(void)
{
    return s_refresh_running;
}

bool weather_shared_poll(weather_shared_data_t *out)
{
    if (s_refresh_queue == NULL) {
        return false;
    }
    weather_shared_data_t result;
    if (xQueueReceive(s_refresh_queue, &result, 0) != pdTRUE) {
        return false;
    }
    if (result.valid) {
        s_last_data = result;
        s_last_fetch_at_ms = (uint32_t)(esp_timer_get_time() / 1000);
    }
    *out = result;
    return true;
}

weather_shared_data_t weather_shared_get_last(void)
{
    return s_last_data;
}

bool weather_shared_is_stale(void)
{
    if (!s_last_data.valid) {
        return true;
    }
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    return (now - s_last_fetch_at_ms) > STALE_AFTER_MS;
}

const char *weather_shared_describe(int weather_code)
{
    switch (weather_code) {
        case 0:
            return "Clear";
        case 1:
        case 2:
        case 3:
            return "Cloudy";
        case 45:
        case 48:
            return "Fog";
        case 51:
        case 53:
        case 55:
        case 56:
        case 57:
            return "Drizzle";
        case 61:
        case 63:
        case 65:
        case 66:
        case 67:
        case 80:
        case 81:
        case 82:
            return "Rain";
        case 71:
        case 73:
        case 75:
        case 77:
        case 85:
        case 86:
            return "Snow";
        case 95:
        case 96:
        case 99:
            return "Storm";
        default:
            return "--";
    }
}
