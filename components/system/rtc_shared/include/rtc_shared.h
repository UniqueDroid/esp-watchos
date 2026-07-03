#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
} rtc_shared_datetime_t;

/* Initializes the PCF85063 over the shared I2C bus. Safe to call multiple times. */
esp_err_t rtc_shared_init(void);

bool rtc_shared_is_ready(void);

/* Reads from the RTC if ready, otherwise falls back to the system clock. */
void rtc_shared_get_datetime(rtc_shared_datetime_t *out);

/* Writes to the RTC (if ready) and syncs the system clock so the rest of the firmware stays consistent. */
void rtc_shared_set_datetime(const rtc_shared_datetime_t *dt);

/* 0=Sunday ... 6=Saturday */
int rtc_shared_weekday(int year, int month, int day);

#ifdef __cplusplus
}
#endif
