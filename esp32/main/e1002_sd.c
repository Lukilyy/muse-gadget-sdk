// SPDX-License-Identifier: Apache-2.0

#include "e1002_sd.h"
#include "e1002_events.h"
#include "e1002_reply.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "mbedtls/base64.h"

#ifndef SD_MOUNT
#define SD_MOUNT "/sdcard"
#endif
#define SD_CS GPIO_NUM_14
#define SD_DET GPIO_NUM_15
#define SD_EN GPIO_NUM_16
#define SD_MAX_TEXT 2048
#define SD_MAX_BINARY 1024

static SemaphoreHandle_t s_lock;
static sdmmc_card_t *s_card;
static bool s_pins_ready;
static portMUX_TYPE s_detect_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_removals, s_mounted_removals;
static const char *TAG = "e100x.sd";

static bool configure_pins(void) {
    const gpio_config_t output = {
        .pin_bit_mask = (1ULL << SD_CS) | (1ULL << SD_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    const gpio_config_t detect = {
        .pin_bit_mask = 1ULL << SD_DET,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    // Keep the card deselected before the shared SPI bus is used by the panel.
    gpio_set_level(SD_CS, 1);
    gpio_set_level(SD_EN, 0);
    return gpio_config(&output) == ESP_OK && gpio_config(&detect) == ESP_OK;
}

bool e1002_sd_prepare_bus(void) {
    return configure_pins();
}

// Observe detection only; insertion does not mount or open files.
static void monitor_card(void *arg) {
    (void)arg;
    bool stable = gpio_get_level(SD_DET) == 0;
    unsigned changes = 0;
    e1002_events_sd(stable);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        bool present = gpio_get_level(SD_DET) == 0;
        if (present == stable) changes = 0;
        else if (++changes == 3) {
            stable = present;
            changes = 0;
            if (!present) {
                portENTER_CRITICAL(&s_detect_lock);
                s_removals++;
                portEXIT_CRITICAL(&s_detect_lock);
            }
            e1002_events_sd(stable);
        }
    }
}

bool e1002_sd_monitor_init(void) {
    static bool started;
    if (started) return true;
    if (!configure_pins()) return false;
    started = xTaskCreate(monitor_card, "e100x_sd_det", 2048, NULL, 2, NULL) == pdPASS;
    return started;
}

static cJSON *failure(const char *code, const char *message) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", false);
    cJSON *err = cJSON_AddObjectToObject(result, "error");
    cJSON_AddStringToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    return result;
}

static cJSON *success(void) {
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", true);
    return result;
}

static bool safe_path(const char *path, char *full, size_t capacity) {
    if (!path || !path[0] || path[0] == '/' || strlen(path) > 120) return false;
    const char *part = path;
    for (const char *p = path;; p++) {
        if (*p == '\\' || *p == ':' || *p == '\n' || *p == '\r') return false;
        if (*p == '/' || *p == 0) {
            size_t len = (size_t)(p - part);
            if (len == 0 || (len == 1 && part[0] == '.')
                || (len == 2 && part[0] == '.' && part[1] == '.')) return false;
            if (*p == 0) break;
            part = p + 1;
        }
    }
    return snprintf(full, capacity, SD_MOUNT "/%s", path) < (int)capacity;
}

static bool init_pins(void) {
    if (s_pins_ready) return true;
    if (!configure_pins()) return false;
    gpio_set_level(SD_CS, 1);
    gpio_set_level(SD_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(300));
    s_pins_ready = true;
    return true;
}

static bool inserted(void) {
    return gpio_get_level(SD_DET) == 0;
}

static bool mount_card(void) {
    if (!init_pins()) return false;
    portENTER_CRITICAL(&s_detect_lock);
    uint32_t removals = s_removals;
    portEXIT_CRITICAL(&s_detect_lock);
    if (s_card && s_mounted_removals != removals) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT, s_card);
        s_card = NULL;
    }
    if (!inserted()) {
        if (s_card) {
            esp_vfs_fat_sdcard_unmount(SD_MOUNT, s_card);
            s_card = NULL;
        }
        return false;
    }
    if (s_card) return true;
    // The selected E1001/E1002 panel owns SPI2 and connects MISO for SD.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    host.max_freq_khz = 10000;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI2_HOST;
    slot.gpio_cs = SD_CS;
    const esp_vfs_fat_sdmmc_mount_config_t config = {
        .format_if_mount_failed = false,
        .max_files = 4,
    };
    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &config, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "FAT card mounted at " SD_MOUNT);
    s_mounted_removals = removals;
    return true;
}

cJSON *e1002_sd_command(const char *command, const cJSON *params) {
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return failure("busy", "SD card is busy");
    }
    bool mounted = mount_card();
    cJSON *result = NULL;
    if (strcmp(command, "sd.status") == 0) {
        result = success();
        cJSON_AddBoolToObject(result, "inserted", s_pins_ready && inserted());
        cJSON_AddBoolToObject(result, "mounted", mounted);
        if (mounted) {
            cJSON_AddNumberToObject(result, "capacity_bytes",
                                    (double)s_card->csd.capacity * s_card->csd.sector_size);
        }
    } else if (!mounted) {
        result = failure("unavailable", "no readable FAT SD card");
    } else if (strcmp(command, "sd.list") == 0) {
        const cJSON *dir_param = cJSON_GetObjectItem(params, "path");
        const char *path = cJSON_IsString(dir_param) ? dir_param->valuestring : NULL;
        char full[144];
        if (!path || !path[0]) strcpy(full, SD_MOUNT);
        else if (!safe_path(path, full, sizeof(full))) result = failure("invalid_param", "invalid path");
        if (!result) {
            DIR *dir = opendir(full);
            if (!dir) result = failure("not_found", "directory unavailable");
            else {
                result = success();
                cJSON *entries = cJSON_AddArrayToObject(result, "entries");
                struct dirent *entry = NULL;
                int count = 0;
                while (count < 32 && (entry = readdir(dir)) != NULL) {
                    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                    cJSON_AddItemToArray(entries, cJSON_CreateString(entry->d_name));
                    count++;
                }
                cJSON_AddBoolToObject(result, "truncated", count == 32 && readdir(dir) != NULL);
                closedir(dir);
            }
        }
    } else if (strcmp(command, "sd.read") == 0 || strcmp(command, "sd.write") == 0) {
        const cJSON *path_param = cJSON_GetObjectItem(params, "path");
        const char *path = cJSON_IsString(path_param) ? path_param->valuestring : NULL;
        const cJSON *offset_param = cJSON_GetObjectItem(params, "offset");
        int offset = 0;
        char full[144];
        if (!safe_path(path, full, sizeof(full)) || (offset_param &&
            (!cJSON_IsNumber(offset_param) || offset_param->valuedouble != offset_param->valueint
             || offset_param->valueint < 0))) {
            result = failure("invalid_param", "relative path and nonnegative 32-bit offset required");
        } else {
            if (offset_param) offset = offset_param->valueint;
            unsigned char binary[SD_MAX_BINARY];
            unsigned char encoded[4 * ((SD_MAX_BINARY + 2) / 3) + 1];
            size_t count = 0;
            const cJSON *data = cJSON_GetObjectItem(params, "data_base64");
            bool writing = strcmp(command, "sd.write") == 0;
            if (writing && (!cJSON_IsString(data) || !data->valuestring[0]
                || strlen(data->valuestring) > sizeof(encoded) - 1
                || mbedtls_base64_decode(binary, sizeof(binary), &count,
                       (const unsigned char *)data->valuestring, strlen(data->valuestring)) != 0
                || !count)) {
                result = failure("invalid_param", "data_base64 must decode to 1..1024 bytes");
            } else {
                // Offset zero starts a new file; following chunks extend it.
                FILE *file = fopen(full, writing ? (offset == 0 ? "wb" : "r+b") : "rb");
                if (!file) result = failure("storage_error", "cannot open binary file");
                else {
                    bool ok = fseek(file, offset, SEEK_SET) == 0;
                    bool eof = false;
                    if (ok && writing) ok = fwrite(binary, 1, count, file) == count;
                    else if (ok) {
                        count = fread(binary, 1, sizeof(binary), file);
                        ok = !ferror(file);
                        if (ok) {
                            int next = fgetc(file);
                            eof = next == EOF;
                            ok = !ferror(file);
                        }
                    }
                    if (fclose(file) != 0) ok = false;
                    if (!ok) result = failure("storage_error", "binary file operation failed");
                    else {
                        result = success();
                        cJSON_AddNumberToObject(result, "bytes", count);
                        cJSON_AddNumberToObject(result, "next_offset", (double)offset + count);
                        if (!writing) {
                            size_t len = 0;
                            if (mbedtls_base64_encode(encoded, sizeof(encoded), &len, binary, count) != 0) {
                                cJSON_Delete(result);
                                result = failure("storage_error", "base64 encoding failed");
                            } else {
                                encoded[len] = 0;
                                cJSON_AddStringToObject(result, "data_base64", (const char *)encoded);
                                cJSON_AddBoolToObject(result, "eof", eof);
                            }
                        }
                    }
                }
            }
        }
    } else if (strcmp(command, "sd.read_text") == 0 || strcmp(command, "sd.write_text") == 0) {
        const cJSON *path_param = cJSON_GetObjectItem(params, "path");
        const char *path = cJSON_IsString(path_param) ? path_param->valuestring : NULL;
        char full[144];
        if (!safe_path(path, full, sizeof(full))) result = failure("invalid_param", "invalid path");
        else if (strcmp(command, "sd.read_text") == 0) {
            FILE *file = fopen(full, "rb");
            if (!file) result = failure("not_found", "file unavailable");
            else {
                char text[SD_MAX_TEXT + 1];
                size_t n = fread(text, 1, SD_MAX_TEXT + 1, file);
                bool read_ok = !ferror(file);
                fclose(file);
                if (!read_ok) {
                    result = failure("storage_error", "read failed");
                } else if (n > SD_MAX_TEXT || memchr(text, 0, n)) {
                    result = failure("invalid_file", "text file is binary or exceeds 2048 bytes");
                } else {
                    text[n] = 0;
                    result = success();
                    cJSON_AddStringToObject(result, "text", text);
                }
            }
        } else {
            const cJSON *text_param = cJSON_GetObjectItem(params, "text");
            const char *text = cJSON_IsString(text_param) ? text_param->valuestring : NULL;
            if (!text || strlen(text) > SD_MAX_TEXT) {
                result = failure("invalid_param", "text must be at most 2048 bytes");
            } else {
                FILE *file = fopen(full, "wb");
                if (!file) result = failure("storage_error", "cannot open file for writing");
                else {
                    size_t len = strlen(text);
                    bool ok = fwrite(text, 1, len, file) == len;
                    if (fclose(file) != 0) ok = false;
                    if (!ok) result = failure("storage_error", "write failed");
                    else {
                        result = success();
                        cJSON_AddNumberToObject(result, "bytes", len);
                    }
                }
            }
        }
    } else {
        result = failure("unsupported", "unknown SD command");
    }
    xSemaphoreGive(s_lock);
    return e1002_data_reply(result);
}
