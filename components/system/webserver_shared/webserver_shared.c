#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "webserver_shared.h"
#include "os_fs.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "display_shared.h"
#include "homescreen_shared.h"

#define TAG "webserver_shared"
#define FACES_DIR OS_FS_APPS_DIR "/watchface/faces"
#define MAX_UPLOAD_BYTES 8192

/* esp_http_server + lwIP need a few contiguous KB of headroom to allocate
 * socket/pbuf buffers; starting it below this threshold doesn't fail
 * cleanly - it leaves requests hanging/retrying forever instead, which is
 * far more confusing than refusing to start. Observed on this board: stable
 * page loads need at least this much free heap. */
#define MIN_FREE_HEAP_TO_START (20 * 1024)

static httpd_handle_t s_server = NULL;

/* Only lowercase/uppercase letters, digits, '_', '-', must end in ".json" -
 * blocks path traversal ("..", "/") and anything outside the faces dir. */
static bool is_valid_face_filename(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= 32) {
        return false;
    }
    if (len <= 5 || strcmp(name + len - 5, ".json") != 0) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/* Minimal "key": "value" extraction - the watchface descriptor format never
 * nests, so a full JSON parser isn't needed (same approach used by the
 * Settings and Watchface apps for the same files). */
static void json_get_str(const char *json, const char *key, char *out, size_t out_len)
{
    out[0] = '\0';
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return;
    }
    p++;
    const char *end = strchr(p, '"');
    if (end == NULL) {
        return;
    }
    size_t len = (size_t)(end - p);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, p, len);
    out[len] = '\0';
}

#define WEBUI_STYLE \
    "<style>" \
    "body{font-family:-apple-system,Helvetica,sans-serif;background:#0b1115;color:#fff;" \
    "padding:16px;max-width:480px;margin:0 auto}" \
    "h2{color:#6cf0c2;margin:0 0 2px}" \
    ".sub{color:#7d96a0;font-size:14px;margin-bottom:18px}" \
    "h3{color:#6cf0c2;margin-top:24px}" \
    ".card{background:#141c22;border:1px solid #355563;border-radius:10px;padding:12px 14px;" \
    "margin-bottom:10px;display:flex;align-items:center;justify-content:space-between}" \
    ".swatch{width:30px;height:30px;border-radius:7px;margin-right:12px;flex-shrink:0;" \
    "border:1px solid #355563}" \
    ".info{flex-grow:1}" \
    ".face-name{font-weight:600;font-size:16px}" \
    ".face-meta{color:#7d96a0;font-size:13px}" \
    ".del{color:#ff8888;text-decoration:none;font-size:13px;padding:6px 10px;" \
    "border:1px solid #552222;border-radius:6px;margin-left:8px}" \
    ".upload-box{background:#141c22;border:1px dashed #355563;border-radius:10px;" \
    "padding:18px;text-align:center;margin-top:8px}" \
    "input[type=file]{color:#fff;margin-bottom:10px}" \
    "button{background:#6cf0c2;color:#0b1115;border:none;border-radius:6px;" \
    "padding:10px 18px;font-size:15px;font-weight:600;display:block;margin:0 auto}" \
    "#msg{color:#6cf0c2;margin-top:10px;font-size:14px}" \
    ".empty{color:#7d96a0;font-size:14px}" \
    "input[type=range]{width:100%;margin:8px 0}" \
    ".colors{display:flex;flex-wrap:wrap;gap:10px;margin-top:8px}" \
    ".colorbtn{width:36px;height:36px;border-radius:8px;border:2px solid #355563;" \
    "padding:0;display:inline-block}" \
    "</style>"

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req,
                              "<!doctype html><html><head><meta name=viewport "
                              "content='width=device-width,initial-scale=1'>"
                              "<title>ESPWatchOS Settings</title>" WEBUI_STYLE
                              "</head><body>"
                              "<h2>ESPWatchOS</h2>"
                              "<div class='sub'>Settings</div>"
                              "<h3>Display</h3>"
                              "<div class='card' style='flex-direction:column;align-items:stretch'>"
                              "<div>Brightness</div>");

    {
        int brightness = display_shared_get_saved_brightness();
        if (brightness <= 0) {
            brightness = 100;
        }
        char slider[160];
        snprintf(slider, sizeof(slider),
                 "<input type=range min=10 max=100 value=%d id=bri "
                 "onchange=\"fetch('/brightness?value='+this.value,{method:'POST'})\">",
                 brightness);
        httpd_resp_sendstr_chunk(req, slider);
    }

    httpd_resp_sendstr_chunk(req, "</div>"
                              "<div class='card' style='flex-direction:column;align-items:stretch'>"
                              "<div>Home Screen Background</div><div class='colors'>");

    {
        static const uint32_t HOME_COLORS[] = {
            0x1a1a1a, 0x10181d, 0x031a26, 0x1a0d05, 0x0d1a0d, 0x1a0d1a, 0x0d0d1a, 0x000000,
        };
        char btn[160];
        for (size_t i = 0; i < sizeof(HOME_COLORS) / sizeof(HOME_COLORS[0]); i++) {
            snprintf(btn, sizeof(btn),
                     "<button class='colorbtn' style='background:#%06lx' "
                     "onclick=\"fetch('/bgcolor?value=%06lx',{method:'POST'})\"></button>",
                     (unsigned long)HOME_COLORS[i], (unsigned long)HOME_COLORS[i]);
            httpd_resp_sendstr_chunk(req, btn);
        }
    }

    httpd_resp_sendstr_chunk(req, "</div>"
                              "<div class='card' style='flex-direction:column;align-items:stretch'>"
                              "<div>AOD Clock Color</div><div class='colors'>");

    {
        static const uint32_t AOD_COLORS[] = {
            0x6cf0c2, 0xffffff, 0xff8844, 0xff4444, 0xffd166, 0x6cc8f0, 0xc46cf0, 0x6cf06c,
        };
        char btn[160];
        for (size_t i = 0; i < sizeof(AOD_COLORS) / sizeof(AOD_COLORS[0]); i++) {
            snprintf(btn, sizeof(btn),
                     "<button class='colorbtn' style='background:#%06lx' "
                     "onclick=\"fetch('/aodcolor?value=%06lx',{method:'POST'})\"></button>",
                     (unsigned long)AOD_COLORS[i], (unsigned long)AOD_COLORS[i]);
            httpd_resp_sendstr_chunk(req, btn);
        }
    }

    httpd_resp_sendstr_chunk(req, "</div></div>"
                              "<h3>Watchfaces</h3>");

    DIR *d = opendir(FACES_DIR);
    bool any = false;
    if (d != NULL) {
        struct dirent *entry;
        char path[80];
        char json[512];
        char name[40];
        char type[16];
        char bg_color[16];
        char row[640];
        while ((entry = readdir(d)) != NULL) {
            if (!is_valid_face_filename(entry->d_name)) {
                continue;
            }
            any = true;

            snprintf(path, sizeof(path), "%s/%s", FACES_DIR, entry->d_name);
            json[0] = '\0';
            FILE *f = fopen(path, "r");
            if (f != NULL) {
                size_t n = fread(json, 1, sizeof(json) - 1, f);
                json[n] = '\0';
                fclose(f);
            }

            json_get_str(json, "name", name, sizeof(name));
            json_get_str(json, "type", type, sizeof(type));
            json_get_str(json, "bg_color", bg_color, sizeof(bg_color));
            if (name[0] == '\0') {
                strncpy(name, entry->d_name, sizeof(name) - 1);
                name[sizeof(name) - 1] = '\0';
            }
            if (type[0] == '\0') {
                strncpy(type, "analog", sizeof(type) - 1);
            }
            if (bg_color[0] == '\0') {
                strncpy(bg_color, "0x10181d", sizeof(bg_color) - 1);
            }

            /* bg_color is stored as "0xRRGGBB" - CSS wants "#RRGGBB". */
            const char *hex = (bg_color[0] == '0' && bg_color[1] == 'x') ? bg_color + 2 : bg_color;

            snprintf(row, sizeof(row),
                     "<div class='card'>"
                     "<div class='swatch' style='background:#%s'></div>"
                     "<div class='info'><div class='face-name'>%s</div>"
                     "<div class='face-meta'>%s &middot; %s</div></div>"
                     "<a class='del' href='/delete?name=%s'>Delete</a>"
                     "</div>",
                     hex, name, type, entry->d_name, entry->d_name);
            httpd_resp_sendstr_chunk(req, row);
        }
        closedir(d);
    }

    if (!any) {
        httpd_resp_sendstr_chunk(req, "<div class='empty'>No watchfaces found.</div>");
    }

    httpd_resp_sendstr_chunk(req,
                              "<h3>Upload new face (.json)</h3>"
                              "<div class='upload-box'>"
                              "<input type=file id=f accept='.json'><br>"
                              "<button onclick='up()'>Upload</button>"
                              "<div id=msg></div>"
                              "</div>"
                              "<script>function up(){"
                              "var file=document.getElementById('f').files[0];"
                              "if(!file){return;}"
                              "fetch('/upload?name='+encodeURIComponent(file.name),"
                              "{method:'POST',body:file})"
                              ".then(r=>r.text()).then(t=>{"
                              "document.getElementById('msg').innerText=t;location.reload();})"
                              ".catch(e=>{document.getElementById('msg').innerText='Error: '+e;});"
                              "}</script></body></html>");

    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char name[40];
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
            httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK ||
            !is_valid_face_filename(name)) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid filename");
        return ESP_OK;
    }

    if (req->content_len <= 0 || req->content_len > MAX_UPLOAD_BYTES) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "File too large or empty");
        return ESP_OK;
    }

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", FACES_DIR, name);

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Failed to open file for write");
        return ESP_OK;
    }

    char buf[256];
    int remaining = req->content_len;
    while (remaining > 0) {
        int to_read = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int n = httpd_req_recv(req, buf, to_read);
        if (n <= 0) {
            fclose(f);
            remove(path);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "Upload aborted");
            return ESP_OK;
        }
        fwrite(buf, 1, n, f);
        remaining -= n;
    }
    fclose(f);

    ESP_LOGI(TAG, "Saved watchface %s", path);
    httpd_resp_sendstr(req, "Saved");
    return ESP_OK;
}

static esp_err_t delete_get_handler(httpd_req_t *req)
{
    char name[40];
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
            httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK &&
            is_valid_face_filename(name)) {
        char path[80];
        snprintf(path, sizeof(path), "%s/%s", FACES_DIR, name);
        remove(path);
        ESP_LOGI(TAG, "Deleted watchface %s", path);
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t brightness_post_handler(httpd_req_t *req)
{
    char query[32];
    char value_str[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
            httpd_query_key_value(query, "value", value_str, sizeof(value_str)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Missing value");
        return ESP_OK;
    }

    int percent = atoi(value_str);
    if (percent < 1 || percent > 100) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid value");
        return ESP_OK;
    }

    display_shared_set_brightness(percent);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t bgcolor_post_handler(httpd_req_t *req)
{
    char query[32];
    char value_str[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
            httpd_query_key_value(query, "value", value_str, sizeof(value_str)) != ESP_OK ||
            strlen(value_str) != 6) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid value");
        return ESP_OK;
    }

    for (int i = 0; i < 6; i++) {
        char c = value_str[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "Invalid value");
            return ESP_OK;
        }
    }

    uint32_t color = (uint32_t)strtoul(value_str, NULL, 16);
    homescreen_shared_set_bg_color(color);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

static esp_err_t aodcolor_post_handler(httpd_req_t *req)
{
    char query[32];
    char value_str[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
            httpd_query_key_value(query, "value", value_str, sizeof(value_str)) != ESP_OK ||
            strlen(value_str) != 6) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "Invalid value");
        return ESP_OK;
    }

    for (int i = 0; i < 6; i++) {
        char c = value_str[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ok) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "Invalid value");
            return ESP_OK;
        }
    }

    uint32_t color = (uint32_t)strtoul(value_str, NULL, 16);
    display_shared_set_aod_color(color);
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

bool webserver_shared_start(void)
{
    if (s_server != NULL) {
        return true;
    }

    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (free_heap < MIN_FREE_HEAP_TO_START) {
        ESP_LOGW(TAG, "Refusing to start - free heap %u below %u byte threshold",
                 (unsigned)free_heap, (unsigned)MIN_FREE_HEAP_TO_START);
        return false;
    }

    mkdir(FACES_DIR, 0755);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 7;
    config.max_open_sockets = 3;
    config.lru_purge_enable = true;
    config.stack_size = 4096;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        s_server = NULL;
        return false;
    }

    httpd_uri_t root_uri = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
    httpd_uri_t upload_uri = { .uri = "/upload", .method = HTTP_POST, .handler = upload_post_handler };
    httpd_uri_t delete_uri = { .uri = "/delete", .method = HTTP_GET, .handler = delete_get_handler };
    httpd_uri_t brightness_uri = { .uri = "/brightness", .method = HTTP_POST, .handler = brightness_post_handler };
    httpd_uri_t bgcolor_uri = { .uri = "/bgcolor", .method = HTTP_POST, .handler = bgcolor_post_handler };
    httpd_uri_t aodcolor_uri = { .uri = "/aodcolor", .method = HTTP_POST, .handler = aodcolor_post_handler };
    httpd_register_uri_handler(s_server, &root_uri);
    httpd_register_uri_handler(s_server, &upload_uri);
    httpd_register_uri_handler(s_server, &delete_uri);
    httpd_register_uri_handler(s_server, &brightness_uri);
    httpd_register_uri_handler(s_server, &bgcolor_uri);
    httpd_register_uri_handler(s_server, &aodcolor_uri);

    ESP_LOGI(TAG, "Settings web server started");
    return true;
}

void webserver_shared_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Watchface web server stopped");
    }
}

bool webserver_shared_is_running(void)
{
    return s_server != NULL;
}
