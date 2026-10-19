#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "e1002_sd.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"

static bool s_inserted = true;
int gpio_config(const gpio_config_t *config) { (void)config; return 0; }
int gpio_get_level(int gpio) { assert(gpio == 15); return !s_inserted; }
int gpio_set_level(int gpio, int level) {
    assert((gpio == 14 && level == 1) || (gpio == 16 && (level == 0 || level == 1)));
    return 0;
}
const char *esp_err_to_name(int error) { (void)error; return "fake"; }
void e1002_events_sd(bool inserted) { (void)inserted; }
int esp_vfs_fat_sdspi_mount(const char *path, const sdmmc_host_t *host,
                          const sdspi_device_config_t *slot,
                          const esp_vfs_fat_sdmmc_mount_config_t *config, sdmmc_card_t **card) {
    static sdmmc_card_t fake = {.csd = {8192,512}};
    assert(strcmp(path, "sdcard") == 0 && host->slot == SPI2_HOST);
    assert(slot->host_id == SPI2_HOST && slot->gpio_cs == 14);
    assert(!config->format_if_mount_failed);
    *card = &fake; return 0;
}
int esp_vfs_fat_sdcard_unmount(const char *path, sdmmc_card_t *card) {
    assert(path && card); return 0;
}
int mbedtls_base64_decode(unsigned char *out, size_t cap, size_t *len,
                          const unsigned char *src, size_t bytes) {
    if (bytes != 8 || memcmp(src, "AAEC/w==", 8) != 0 || cap < 4) return -1;
    out[0] = 0; out[1] = 1; out[2] = 2; out[3] = 255; *len = 4;
    return 0;
}
int mbedtls_base64_encode(unsigned char *out, size_t cap, size_t *len,
                          const unsigned char *src, size_t bytes) {
    // All exercised nonempty binary reads are exactly this four-byte vector.
    if (!bytes) { *len = 0; return 0; }
    assert(bytes == 4 && src[0] == 0 && src[3] == 255 && cap > 8);
    memcpy(out, "AAEC/w==", 8); *len = 8; return 0;
}
static cJSON *call(const char *name, const char *json) {
    cJSON *p = json ? cJSON_Parse(json) : NULL;
    cJSON *r = e1002_sd_command(name, p);
    cJSON_Delete(p); assert(r); return r;
}
static cJSON *payload(cJSON *r) {
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "ok")));
    cJSON *p = cJSON_GetObjectItem(r, "payload"); assert(p); return p;
}
int main(void) {
    cJSON *r = call("sd.write", "{\"path\":\"binary.dat\",\"data_base64\":\"AAEC/w==\"}");
    assert(cJSON_GetObjectItem(payload(r), "bytes")->valueint == 4); cJSON_Delete(r);
    r = call("sd.write", "{\"path\":\"binary.dat\",\"offset\":4,\"data_base64\":\"AAEC/w==\"}");
    assert(cJSON_GetObjectItem(payload(r), "next_offset")->valueint == 8); cJSON_Delete(r);
    FILE *f = fopen("sdcard/binary.dat", "rb"); assert(f);
    unsigned char raw[9]; assert(fread(raw,1,9,f) == 8 && raw[0] == 0 && raw[3] == 255 && raw[7] == 255);
    fclose(f);
    r = call("sd.read", "{\"path\":\"binary.dat\",\"offset\":4}");
    assert(cJSON_IsTrue(cJSON_GetObjectItem(payload(r), "eof")));
    assert(strcmp(cJSON_GetObjectItem(payload(r), "data_base64")->valuestring, "AAEC/w==") == 0);
    cJSON_Delete(r);
    r = call("sd.read", "{\"path\":\"binary.dat\",\"offset\":8}");
    assert(cJSON_GetObjectItem(payload(r), "bytes")->valueint == 0); cJSON_Delete(r);
    r = call("sd.write_text", "{\"path\":\"note.txt\",\"text\":\"hello\"}");
    assert(cJSON_GetObjectItem(payload(r), "bytes")->valueint == 5); cJSON_Delete(r);
    r = call("sd.read_text", "{\"path\":\"note.txt\"}");
    assert(strcmp(cJSON_GetObjectItem(payload(r), "text")->valuestring, "hello") == 0); cJSON_Delete(r);
    r = call("sd.write_text", "{\"path\":\"../escape\",\"text\":\"bad\"}");
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok"))); cJSON_Delete(r);
    r = call("sd.write", "{\"path\":\"binary.dat\",\"offset\":-1,\"data_base64\":\"AAEC/w==\"}");
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok"))); cJSON_Delete(r);
    s_inserted = false;
    r = call("sd.status", NULL);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(payload(r), "mounted"))); cJSON_Delete(r);
    return 0;
}
