#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BATTERY_SHARED_STATE_UNKNOWN = 0,
    BATTERY_SHARED_STATE_DISCHARGING,
    BATTERY_SHARED_STATE_CHARGING,
    BATTERY_SHARED_STATE_CHARGED,
} battery_shared_state_t;

/* Probes the AXP2101 PMIC on the shared I2C bus (same bus as the PCF85063
 * RTC - see bsp_i2c_get_handle()). Verifies the chip ID before marking
 * ready, so a board without this chip just leaves the reader in the
 * "not ready" state instead of returning garbage. Safe to call multiple
 * times. */
esp_err_t battery_shared_init(void);

bool battery_shared_is_ready(void);

/* Returns 0-100, or -1 if not ready or the read failed. */
int battery_shared_get_percent(void);

battery_shared_state_t battery_shared_get_state(void);

#ifdef __cplusplus
}
#endif
