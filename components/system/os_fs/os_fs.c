#include <sys/stat.h>
#include <errno.h>

#include "os_fs.h"
#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "os_fs";
static bool s_mounted = false;

static void ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return;
    }
    if (mkdir(path, 0755) != 0) {
        ESP_LOGW(TAG, "mkdir(%s) failed: %d", path, errno);
    }
}

esp_err_t os_fs_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = OS_FS_ROOT,
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "Mounted at %s (%u/%u KB used)", OS_FS_ROOT, (unsigned)(used / 1024), (unsigned)(total / 1024));

    ensure_dir(OS_FS_SYSTEM_DIR);
    ensure_dir(OS_FS_APPS_DIR);
    ensure_dir(OS_FS_ETC_DIR);
    ensure_dir(OS_FS_TMP_DIR);

    s_mounted = true;
    return ESP_OK;
}
