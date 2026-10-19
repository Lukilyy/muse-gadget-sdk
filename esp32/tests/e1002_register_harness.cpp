#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "e1002_hardware.h"

#define CONFIG_HOMEHUB_RETERMINAL_E100X_HARDWARE 1
#define CONFIG_HOMEHUB_DISPLAY_COMMANDS 1
#define CONFIG_HOMEHUB_RETERMINAL_SHT4X 1
#define CONFIG_HOMEHUB_TUNNEL 1
static char s_wifi_ssid[33], s_register_req_id[40];
static char s_node_id[65], s_display_name[65];
struct esp_app_desc_t { char version[32]; };
static const esp_app_desc_t *esp_app_get_description() {
    static esp_app_desc_t desc = {"999.0.0"};
    return &desc;
}
static void copy_wifi_ssid(char *dst, size_t size) { snprintf(dst, size, "%s", s_wifi_ssid); }
static void make_uuid(char *dst, size_t size) { snprintf(dst, size, "00000000-0000-0000-0000-000000000000"); }
static bool led_status_display_info(int *w, int *h) { *w = 800; *h = 480; return true; }
static int led_status_display_bits() { return 4; }
static bool ota_is_enabled() { return true; } // Include optional OTA in size budget.
#include "noise_register.inc"
#include "noise_result.inc"

extern "C" void check_registration(void) {
    memset(s_wifi_ssid, 's', sizeof(s_wifi_ssid) - 1);
    memset(s_node_id, 'n', sizeof(s_node_id) - 1);
    memset(s_display_name, 'd', sizeof(s_display_name) - 1);
    char *json = build_register_json();
    assert(json && strlen(json) < 8192);
    cJSON *root = cJSON_Parse(json);
    cJSON *commands = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "params"), "commands_v2");
    assert(cJSON_GetObjectItem(commands, "microphone.record"));
    assert(cJSON_GetObjectItem(commands, "events.wait"));
    assert(!cJSON_GetObjectItem(commands, "display.pages.set"));
    assert(!cJSON_GetObjectItem(commands, "sd.display"));
    cJSON_Delete(root);
    free(json);
}

extern "C" void check_hardware_wire(cJSON *result) {
    cJSON *expected = cJSON_GetObjectItem(result, "payload");
    char *wire = wrap_result_json("hardware-id", cJSON_Duplicate(result, true));
    assert(wire);
    cJSON *actual = cJSON_Parse(wire);
    if (expected) assert(cJSON_Compare(expected, cJSON_GetObjectItem(actual, "payload"), true));
    cJSON_Delete(actual);
    free(wire);
}
