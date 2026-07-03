#include <string.h>

#include "bt_shared.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "bt_shared";
static const char *kDeviceName = "ESPWatchOS";

static bool s_host_started = false;
static bool s_advertising = false;
static uint8_t s_addr_type;

static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)kDeviceName;
    fields.name_len = strlen(kDeviceName);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "adv_start failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising as \"%s\"", kDeviceName);
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE connect, status=%d", event->connect.status);
        if (event->connect.status != 0 && s_advertising) {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnect, reason=%d", event->disconnect.reason);
        if (s_advertising) {
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_advertising) {
            start_advertising();
        }
        break;
    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_addr_type);
    if (s_advertising) {
        /* bt_shared_start() was called before the host finished syncing -
         * this is the first chance to actually begin advertising. */
        start_advertising();
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
}

static void host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool ensure_host_started(void)
{
    if (s_host_started) {
        return true;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    if (nimble_port_init() != ESP_OK) {
        ESP_LOGW(TAG, "nimble_port_init failed");
        return false;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_svc_gap_device_name_set(kDeviceName);

    nimble_port_freertos_init(host_task);
    s_host_started = true;
    return true;
}

bool bt_shared_start(void)
{
    if (s_advertising) {
        return true;
    }
    if (!ensure_host_started()) {
        return false;
    }
    s_advertising = true;
    if (ble_hs_synced()) {
        /* Host was already up from a previous start/stop cycle - kick
         * advertising directly. On the very first ever start, on_sync()
         * (above) does this instead once the host finishes initializing. */
        start_advertising();
    }
    return true;
}

void bt_shared_stop(void)
{
    if (!s_advertising) {
        return;
    }
    s_advertising = false;
    ble_gap_adv_stop();
    ESP_LOGI(TAG, "Advertising stopped");
}

bool bt_shared_is_running(void)
{
    return s_advertising;
}
