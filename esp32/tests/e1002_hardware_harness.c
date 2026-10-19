#include <assert.h>
#include <string.h>
#include "e1002_hardware.h"
#include "e1002_buttons.h"
#include "e1002_rtc.h"
#include "freertos/task.h"

void check_registration(void);
void check_hardware_wire(cJSON *result);
static TaskFunction_t s_task;
static void *s_arg;
static cJSON *s_reply;
static bool s_task_fail;
static int s_hz, s_ms;

int64_t esp_timer_get_time(void) { return 1000; }
void vTaskDelay(int ms) { (void)ms; }
void vTaskDelete(void *task) { (void)task; }
int xTaskCreate(TaskFunction_t task, const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle) {
    (void)name; (void)stack; (void)priority; (void)handle;
    if (s_task_fail) return 0;
    s_task = task; s_arg = arg;
    return 1;
}
void noise_ctrl_send_command_result(noise_ctrl_session_generation_t gen,
                                    const char *id, cJSON *r) {
    assert(gen == 123 && strcmp(id, "hardware-id") == 0 && !s_reply);
    check_hardware_wire(r);
    s_reply = r;
}
bool battery_adc_read(int *mv, int *pct) { *mv = 3800; *pct = 66; return true; }
bool e1002_buttons_init(void) { return true; }
bool e1002_buttons_read(e1002_buttons_state_t *s) {
    *s = (e1002_buttons_state_t){.key1_down = true, .key1_presses = 2};
    return true;
}
bool e1002_sd_prepare_bus(void) { return true; }
bool e1002_sd_monitor_init(void) { return true; }
bool e1002_microphone_init(void) { return true; }
bool e1002_buzzer_beep(int hz, int ms) { s_hz = hz; s_ms = ms; return true; }
bool e1002_rtc_read(e1002_rtc_time_t *t) { *t = (e1002_rtc_time_t){2026,10,18,12,34,56,true}; return true; }
bool e1002_rtc_set(const e1002_rtc_time_t *t) { return t->year == 2026; }
cJSON *e1002_sd_command(const char *command, const cJSON *params) { (void)command; (void)params; return cJSON_CreateObject(); }
cJSON *e1002_microphone_record(const cJSON *params) { (void)params; return cJSON_CreateObject(); }
cJSON *e1002_events_command(const char *command, const cJSON *p, const char *id,
                            noise_ctrl_session_generation_t gen) {
    (void)command; (void)p; (void)id; (void)gen;
    return cJSON_CreateObject();
}
static cJSON *call(const char *command, cJSON *params) {
    return e1002_hardware_command(command, params, "hardware-id", 123);
}
static cJSON *run(const char *command, const char *json) {
    cJSON *params = json ? cJSON_Parse(json) : NULL;
    cJSON *r = call(command, params);
    assert(cJSON_IsTrue(cJSON_GetObjectItem(r, "_async")));
    cJSON_Delete(params); cJSON_Delete(r);
    s_task(s_arg);
    r = s_reply; s_reply = NULL;
    return r;
}
int main(void) {
    check_registration();
    assert(e1002_hardware_handles("microphone.record"));
    assert(!e1002_hardware_handles("display.pages.set"));
    cJSON *r = call("buttons.read", NULL);
    cJSON *p = cJSON_GetObjectItem(r, "payload");
    assert(cJSON_GetObjectItem(p, "key1_presses")->valueint == 2);
    check_hardware_wire(r); cJSON_Delete(r);
    r = call("battery.read", NULL);
    assert(cJSON_IsNull(cJSON_GetObjectItem(cJSON_GetObjectItem(r, "payload"), "battery_mv")));
    cJSON_Delete(r);
    r = run("rtc.read", NULL);
    assert(cJSON_GetObjectItem(cJSON_GetObjectItem(r, "payload"), "year")->valueint == 2026);
    cJSON_Delete(r);
    r = run("rtc.set", "{\"year\":2026.5}");
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok"))); cJSON_Delete(r);
    r = run("buzzer.beep", "{\"frequency_hz\":0,\"duration_ms\":20}");
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok"))); cJSON_Delete(r);
    // Parameters survive deletion/mutation of the original request.
    p = cJSON_Parse("{\"frequency_hz\":1000,\"duration_ms\":50}");
    r = call("buzzer.beep", p); cJSON_Delete(r);
    cJSON_SetNumberValue(cJSON_GetObjectItem(p, "frequency_hz"), 2000);
    r = call("rtc.read", NULL);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok"))); cJSON_Delete(r);
    cJSON_Delete(p);
    s_task(s_arg);
    assert(s_hz == 1000 && s_ms == 50);
    cJSON_Delete(s_reply); s_reply = NULL;
    s_task_fail = true;
    r = call("rtc.read", NULL);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "ok"))); cJSON_Delete(r);
    s_task_fail = false;
    r = run("rtc.read", NULL); cJSON_Delete(r);
    return 0;
}
