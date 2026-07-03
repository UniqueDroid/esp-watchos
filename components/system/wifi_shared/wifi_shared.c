#include <string.h>
#include <stdlib.h>

#include "wifi_shared.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "psa/crypto.h"
#include "os_fs.h"

#include <stdio.h>

static const char *TAG = "wifi_shared";

#define WIFI_CONFIG_PATH OS_FS_ETC_DIR "/wifi.json"

/* The stored password is AES-128-CTR encrypted (via PSA Crypto) with a key
 * derived from this device's factory-programmed MAC address
 * (SHA-256(mac || context) -> first 16 bytes). This keeps the credential
 * out of the plaintext file a casual look at the filesystem (e.g. a pulled
 * LittleFS image) would see - it does NOT protect against an attacker who
 * also has the firmware source, since the key is fully derivable from a MAC
 * address readable via the same physical/USB access that would expose the
 * file in the first place. True protection against that threat model needs
 * flash encryption, which is a one-way, per-device provisioning step
 * outside what this function can do.
 */
static bool ensure_psa_crypto_ready(void)
{
    static bool s_ready = false;
    if (!s_ready) {
        s_ready = (psa_crypto_init() == PSA_SUCCESS);
    }
    return s_ready;
}

static void derive_wifi_key(uint8_t out_key[16])
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    static const char kContext[] = "esp-watchos-wifi-cred-v1";
    uint8_t input[sizeof(mac) + sizeof(kContext) - 1];
    memcpy(input, mac, sizeof(mac));
    memcpy(input + sizeof(mac), kContext, sizeof(kContext) - 1);

    uint8_t digest[32];
    size_t digest_len = 0;
    psa_hash_compute(PSA_ALG_SHA_256, input, sizeof(input), digest, sizeof(digest), &digest_len);

    memcpy(out_key, digest, 16);
}

static void hex_encode(const uint8_t *in, size_t in_len, char *out)
{
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < in_len; i++) {
        out[i * 2] = kHex[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[in[i] & 0xF];
    }
    out[in_len * 2] = '\0';
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex_decode(const char *in, uint8_t *out, size_t out_len)
{
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_nibble(in[i * 2]);
        int lo = hex_nibble(in[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool import_wifi_key(psa_key_id_t *key_id, psa_key_usage_t usage)
{
    uint8_t key[16];
    derive_wifi_key(key);

    psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attrs, usage);
    psa_set_key_algorithm(&attrs, PSA_ALG_CTR);
    psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attrs, 128);

    return psa_import_key(&attrs, key, sizeof(key), key_id) == PSA_SUCCESS;
}

/* Encrypts `password` (up to 64 bytes, matching wifi_config_t::sta.password)
 * with AES-128-CTR under a fresh random IV (PSA generates and prepends it
 * automatically), and hex-encodes "iv || ciphertext" into `out`
 * (caller-sized for 2*(16+64)+1 bytes). */
static void encrypt_password_hex(const char *password, char *out, size_t out_size)
{
    out[0] = '\0';
    if (!ensure_psa_crypto_ready()) {
        return;
    }

    psa_key_id_t key_id;
    if (!import_wifi_key(&key_id, PSA_KEY_USAGE_ENCRYPT)) {
        return;
    }

    size_t pw_len = strlen(password);
    uint8_t blob[16 + 64] = {0};
    size_t blob_len = 0;
    psa_status_t st = psa_cipher_encrypt(key_id, PSA_ALG_CTR,
                                         (const uint8_t *)password, pw_len,
                                         blob, sizeof(blob), &blob_len);
    psa_destroy_key(key_id);

    if (st != PSA_SUCCESS || blob_len * 2 + 1 > out_size) {
        return;
    }
    hex_encode(blob, blob_len, out);
}

/* Reverses encrypt_password_hex(). `hex` is "iv || ciphertext" hex, `out`
 * receives the decrypted password (NUL-terminated, out_size-capped). An
 * empty `hex` decodes to an empty (open-network) password, matching the
 * pre-encryption on-disk format. */
static bool decrypt_password_hex(const char *hex, char *out, size_t out_size)
{
    size_t hex_len = strlen(hex);
    if (hex_len == 0) {
        out[0] = '\0';
        return true;
    }
    if ((hex_len % 2) != 0 || hex_len / 2 > (16 + 64)) {
        out[0] = '\0';
        return false;
    }

    uint8_t blob[16 + 64];
    size_t blob_len = hex_len / 2;
    if (!hex_decode(hex, blob, blob_len)) {
        out[0] = '\0';
        return false;
    }

    if (!ensure_psa_crypto_ready()) {
        out[0] = '\0';
        return false;
    }

    psa_key_id_t key_id;
    if (!import_wifi_key(&key_id, PSA_KEY_USAGE_DECRYPT)) {
        out[0] = '\0';
        return false;
    }

    uint8_t plain[64] = {0};
    size_t plain_len = 0;
    psa_status_t st = psa_cipher_decrypt(key_id, PSA_ALG_CTR, blob, blob_len,
                                         plain, sizeof(plain), &plain_len);
    psa_destroy_key(key_id);

    if (st != PSA_SUCCESS || plain_len >= out_size) {
        out[0] = '\0';
        return false;
    }
    memcpy(out, plain, plain_len);
    out[plain_len] = '\0';
    return true;
}

static bool s_connected = false;
static char s_ip[16] = {0};
static char s_ssid[33] = {0};

typedef struct {
    wifi_shared_ap_t aps[WIFI_SHARED_MAX_AP_RECORDS];
    int count;
} wifi_shared_scan_result_t;

static QueueHandle_t s_scan_result_queue = NULL;
static volatile bool s_scan_running = false;

static void apply_config_and_connect(const char *ssid, const char *password)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (password != NULL) {
        strncpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    }
    cfg.sta.threshold.authmode = (password != NULL && password[0] != '\0') ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_disconnect();

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }
}

static bool json_get_string(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p = strchr(p + strlen(pattern), ':');
    if (p == NULL) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;
    const char *end = strchr(p, '"');
    if (end == NULL) {
        return false;
    }
    size_t len = (size_t)(end - p);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

/* Naive JSON string escaping - good enough for SSIDs/passwords, which can't
 * contain control characters per the WiFi spec. */
static void json_escape_append(char *buf, size_t buf_size, const char *s)
{
    size_t len = strlen(buf);
    for (; *s != '\0' && len + 2 < buf_size; s++) {
        if (*s == '"' || *s == '\\') {
            buf[len++] = '\\';
        }
        buf[len++] = *s;
    }
    buf[len] = '\0';
}

static void save_credentials(const char *ssid, const char *password)
{
    char enc_password[161];
    encrypt_password_hex(password != NULL ? password : "", enc_password, sizeof(enc_password));

    char buf[320] = "{\"ssid\":\"";
    json_escape_append(buf, sizeof(buf), ssid);
    strncat(buf, "\",\"password\":\"", sizeof(buf) - strlen(buf) - 1);
    json_escape_append(buf, sizeof(buf), enc_password);
    strncat(buf, "\"}", sizeof(buf) - strlen(buf) - 1);

    FILE *f = fopen(WIFI_CONFIG_PATH, "w");
    if (f == NULL) {
        ESP_LOGW(TAG, "Failed to save wifi config to %s", WIFI_CONFIG_PATH);
        return;
    }
    fwrite(buf, 1, strlen(buf), f);
    fclose(f);
}

static bool load_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    FILE *f = fopen(WIFI_CONFIG_PATH, "r");
    if (f == NULL) {
        return false;
    }
    char json[320] = {0};
    size_t n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';

    if (!json_get_string(json, "ssid", ssid, ssid_len) || ssid[0] == '\0') {
        return false;
    }
    char enc_password[161] = {0};
    if (!json_get_string(json, "password", enc_password, sizeof(enc_password))) {
        password[0] = '\0';
        return true;
    }
    if (!decrypt_password_hex(enc_password, password, password_len)) {
        ESP_LOGW(TAG, "Stored WiFi password could not be decrypted, treating as none");
        password[0] = '\0';
    }
    return true;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = '\0';
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            memset(s_ssid, 0, sizeof(s_ssid));
            memcpy(s_ssid, ap_info.ssid, sizeof(ap_info.ssid));
            s_ssid[sizeof(s_ssid) - 1] = '\0';
        }
        ESP_LOGI(TAG, "Got IP: %s", s_ip);
    }
}

esp_err_t wifi_shared_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_scan_result_queue = xQueueCreate(1, sizeof(wifi_shared_scan_result_t *));

    return ESP_OK;
}

void wifi_shared_try_autoconnect(void)
{
    if (s_connected) {
        return;
    }

    char ssid[33] = {0};
    char password[64] = {0};
    if (load_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        ESP_LOGI(TAG, "Auto-connecting to saved SSID '%s'", ssid);
        apply_config_and_connect(ssid, password);
    }
}

int wifi_shared_scan(wifi_shared_ap_t *out, int max)
{
    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err == ESP_ERR_WIFI_STATE) {
        /* Driver was mid-connect; give it a moment to settle and try once more. */
        vTaskDelay(pdMS_TO_TICKS(150));
        err = esp_wifi_scan_start(&scan_cfg, true);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t ap_count = (uint16_t)(max < WIFI_SHARED_MAX_AP_RECORDS ? max : WIFI_SHARED_MAX_AP_RECORDS);
    wifi_ap_record_t records[WIFI_SHARED_MAX_AP_RECORDS];
    esp_err_t get_err = esp_wifi_scan_get_ap_records(&ap_count, records);
    if (get_err != ESP_OK) {
        return 0;
    }

    for (uint16_t i = 0; i < ap_count; i++) {
        memset(out[i].ssid, 0, sizeof(out[i].ssid));
        memcpy(out[i].ssid, records[i].ssid, sizeof(records[i].ssid));
        out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
        out[i].rssi = records[i].rssi;
        out[i].primary = records[i].primary;
        out[i].authmode = records[i].authmode;
        memcpy(out[i].bssid, records[i].bssid, sizeof(out[i].bssid));
    }

    for (int i = 1; i < ap_count; i++) {
        wifi_shared_ap_t key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < key.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }

    return ap_count;
}

static void scan_task(void *arg)
{
    (void)arg;

    wifi_shared_scan_result_t *result = malloc(sizeof(wifi_shared_scan_result_t));
    if (result == NULL) {
        s_scan_running = false;
        vTaskDelete(NULL);
        return;
    }

    result->count = wifi_shared_scan(result->aps, WIFI_SHARED_MAX_AP_RECORDS);
    xQueueOverwrite(s_scan_result_queue, &result);
    s_scan_running = false;
    vTaskDelete(NULL);
}

bool wifi_shared_scan_start_async(void)
{
    if (s_scan_running || s_scan_result_queue == NULL) {
        return false;
    }

    s_scan_running = true;
    if (xTaskCreate(scan_task, "wifi_scan", 8192, NULL, 5, NULL) != pdPASS) {
        s_scan_running = false;
        return false;
    }
    return true;
}

bool wifi_shared_scan_is_running(void)
{
    return s_scan_running;
}

bool wifi_shared_scan_poll(wifi_shared_ap_t *out, int max, int *count)
{
    if (s_scan_result_queue == NULL) {
        return false;
    }

    wifi_shared_scan_result_t *result = NULL;
    if (xQueueReceive(s_scan_result_queue, &result, 0) != pdTRUE) {
        return false;
    }

    int n = result->count < max ? result->count : max;
    memcpy(out, result->aps, n * sizeof(wifi_shared_ap_t));
    *count = n;
    free(result);
    return true;
}

esp_err_t wifi_shared_connect(const char *ssid, const char *password)
{
    apply_config_and_connect(ssid, password);
    save_credentials(ssid, password);
    return ESP_OK;
}

void wifi_shared_disconnect(void)
{
    esp_wifi_disconnect();
    s_connected = false;
    s_ip[0] = '\0';
}

bool wifi_shared_is_connected(void)
{
    return s_connected;
}

bool wifi_shared_get_ip(char *buf, size_t len)
{
    if (!s_connected) {
        return false;
    }
    strncpy(buf, s_ip, len - 1);
    buf[len - 1] = '\0';
    return true;
}

bool wifi_shared_get_ssid(char *buf, size_t len)
{
    if (!s_connected) {
        return false;
    }
    strncpy(buf, s_ssid, len - 1);
    buf[len - 1] = '\0';
    return true;
}

bool wifi_shared_get_rssi(int8_t *out_rssi)
{
    if (!s_connected) {
        return false;
    }
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) {
        return false;
    }
    *out_rssi = info.rssi;
    return true;
}
