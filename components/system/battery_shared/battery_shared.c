#include "battery_shared.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "battery_shared";

/* AXP2101 PMIC, present on the Waveshare ESP32-C6-Touch-AMOLED-2.06 board.
 * Register addresses/bits verified against the AXP2101 register map (e.g.
 * https://github.com/protoconcept/axp2101/blob/main/axp2101_registers.h) -
 * not vendored as a dependency since only these four registers are needed. */
#define AXP2101_I2C_ADDR 0x34
#define AXP2101_REG_CHIP_ID 0x03
#define AXP2101_CHIP_ID 0x4A
#define AXP2101_REG_PMU_STATUS_2 0x01
#define AXP2101_REG_BATT_PERCENT 0xA4

#define AXP2101_STATUS2_BATT_PRESENT_BIT (1u << 5)
#define AXP2101_STATUS2_CHARGED_BIT (1u << 3)
#define AXP2101_STATUS2_CHARGING_BIT (1u << 2)

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_ready = false;

static esp_err_t read_reg(uint8_t reg, uint8_t *out)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, out, 1, 1000);
}

esp_err_t battery_shared_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bsp_i2c_get_handle(), &dev_config, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add AXP2101 I2C device: %s", esp_err_to_name(err));
        s_dev = NULL;
        return err;
    }

    uint8_t chip_id = 0;
    err = read_reg(AXP2101_REG_CHIP_ID, &chip_id);
    if (err != ESP_OK || chip_id != AXP2101_CHIP_ID) {
        ESP_LOGW(TAG, "AXP2101 not detected (err=%s, chip_id=0x%02X) - battery reporting disabled",
                 esp_err_to_name(err), chip_id);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    s_ready = true;
    ESP_LOGI(TAG, "AXP2101 detected");
    return ESP_OK;
}

bool battery_shared_is_ready(void)
{
    return s_ready;
}

int battery_shared_get_percent(void)
{
    if (!s_ready) {
        return -1;
    }
    uint8_t val = 0;
    if (read_reg(AXP2101_REG_BATT_PERCENT, &val) != ESP_OK || val > 100) {
        return -1;
    }
    return val;
}

battery_shared_state_t battery_shared_get_state(void)
{
    if (!s_ready) {
        return BATTERY_SHARED_STATE_UNKNOWN;
    }
    uint8_t status2 = 0;
    if (read_reg(AXP2101_REG_PMU_STATUS_2, &status2) != ESP_OK) {
        return BATTERY_SHARED_STATE_UNKNOWN;
    }
    if (!(status2 & AXP2101_STATUS2_BATT_PRESENT_BIT)) {
        return BATTERY_SHARED_STATE_UNKNOWN;
    }
    if (status2 & AXP2101_STATUS2_CHARGING_BIT) {
        return BATTERY_SHARED_STATE_CHARGING;
    }
    if (status2 & AXP2101_STATUS2_CHARGED_BIT) {
        return BATTERY_SHARED_STATE_CHARGED;
    }
    return BATTERY_SHARED_STATE_DISCHARGING;
}
