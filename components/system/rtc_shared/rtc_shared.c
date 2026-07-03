#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "rtc_shared.h"
#include "pcf85063a.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "rtc_shared";

static bool s_ready = false;
static pcf85063a_dev_t s_rtc = {0};

esp_err_t rtc_shared_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_err_t err = pcf85063a_init(&s_rtc, bsp_i2c_get_handle(), PCF85063A_ADDRESS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PCF85063 init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;

    pcf85063a_datetime_t current = {0};
    pcf85063a_get_time_date(&s_rtc, &current);
    if (current.year < 2024 || current.year > 2099) {
        pcf85063a_datetime_t seed = {
            .year = 2026, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = 0
        };
        pcf85063a_set_time_date(&s_rtc, seed);
        current = seed;
    }

    /* Sync the system clock (used by plain time()/localtime_r(), e.g. the
     * status bar clock in main.cpp) from the RTC chip - without this the
     * system clock stays at the epoch until something happens to call
     * settimeofday() via rtc_shared_set_datetime(). */
    struct tm timeinfo = {0};
    timeinfo.tm_year = current.year - 1900;
    timeinfo.tm_mon = current.month - 1;
    timeinfo.tm_mday = current.day;
    timeinfo.tm_hour = current.hour;
    timeinfo.tm_min = current.min;
    timeinfo.tm_sec = current.sec;
    time_t t = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, NULL);

    return ESP_OK;
}

bool rtc_shared_is_ready(void)
{
    return s_ready;
}

void rtc_shared_get_datetime(rtc_shared_datetime_t *out)
{
    pcf85063a_datetime_t dt = {0};

    if (s_ready && pcf85063a_get_time_date(&s_rtc, &dt) == ESP_OK) {
        out->year = dt.year;
        out->month = dt.month;
        out->day = dt.day;
        out->hour = dt.hour;
        out->min = dt.min;
        out->sec = dt.sec;
        return;
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    out->year = timeinfo.tm_year + 1900;
    out->month = timeinfo.tm_mon + 1;
    out->day = timeinfo.tm_mday;
    out->hour = timeinfo.tm_hour;
    out->min = timeinfo.tm_min;
    out->sec = timeinfo.tm_sec;
}

void rtc_shared_set_datetime(const rtc_shared_datetime_t *in)
{
    if (s_ready) {
        pcf85063a_datetime_t dt = {0};
        dt.year = in->year;
        dt.month = in->month;
        dt.day = in->day;
        dt.hour = in->hour;
        dt.min = in->min;
        dt.sec = in->sec;
        pcf85063a_set_time_date(&s_rtc, dt);
    }

    struct tm timeinfo = {0};
    timeinfo.tm_year = in->year - 1900;
    timeinfo.tm_mon = in->month - 1;
    timeinfo.tm_mday = in->day;
    timeinfo.tm_hour = in->hour;
    timeinfo.tm_min = in->min;
    timeinfo.tm_sec = in->sec;
    time_t t = mktime(&timeinfo);
    struct timeval tv = {.tv_sec = t, .tv_usec = 0};
    settimeofday(&tv, NULL);
}

int rtc_shared_weekday(int year, int month, int day)
{
    /* Sakamoto's algorithm, 0=Sunday */
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) {
        year -= 1;
    }
    return (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
}
