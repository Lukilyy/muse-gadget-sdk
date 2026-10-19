// SPDX-License-Identifier: Apache-2.0

#include "e1002_hardware.h"

#include <stdlib.h>
#include <string.h>

#include "battery_adc.h"
#include "e1002_buttons.h"
#include "e1002_buzzer.h"
#include "e1002_events.h"
#include "e1002_microphone.h"
#include "e1002_rtc.h"
#include "e1002_sd.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_busy, s_battery_valid;
static int s_battery_mv, s_battery_pct;
static int64_t s_battery_at;

static cJSON *failure(const char *code, const char *message) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON *error = cJSON_AddObjectToObject(r, "error");
    cJSON_AddStringToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message);
    return r;
}

static cJSON *success(void) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddObjectToObject(r, "payload");
    return r;
}

static void battery_task(void *arg) {
    (void)arg;
    for (;;) {
        int mv = 0, pct = 0;
        bool valid = battery_adc_read(&mv, &pct);
        int64_t at = esp_timer_get_time();
        portENTER_CRITICAL(&s_lock);
        s_battery_valid = valid;
        s_battery_mv = mv;
        s_battery_pct = pct;
        s_battery_at = at;
        portEXIT_CRITICAL(&s_lock);
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

static void battery_metrics(cJSON *payload) {
    portENTER_CRITICAL(&s_lock);
    bool valid = s_battery_valid;
    int mv = s_battery_mv, pct = s_battery_pct;
    int64_t at = s_battery_at;
    portEXIT_CRITICAL(&s_lock);
    valid = valid && esp_timer_get_time() - at < 60000000;
    if (valid) {
        cJSON_AddNumberToObject(payload, "battery_mv", mv);
        cJSON_AddNumberToObject(payload, "battery_pct", pct);
    } else {
        cJSON_AddNullToObject(payload, "battery_mv");
        cJSON_AddNullToObject(payload, "battery_pct");
    }
}

void e1002_hardware_health(cJSON *metrics) {
    cJSON_DeleteItemFromObjectCaseSensitive(metrics, "battery_mv");
    cJSON_DeleteItemFromObjectCaseSensitive(metrics, "battery_pct");
    battery_metrics(metrics);
}

void e1002_hardware_prepare_display(void) {
    if (!e1002_sd_prepare_bus()) ESP_LOGW("e100x", "SD bus pins unavailable");
}

void e1002_hardware_init(void) {
    if (!e1002_buttons_init()) ESP_LOGW("e100x", "button monitoring unavailable");
    if (!e1002_sd_monitor_init()) ESP_LOGW("e100x", "SD detection unavailable");
    if (!e1002_microphone_init()) ESP_LOGW("e100x", "microphone power control unavailable");
    if (xTaskCreate(battery_task, "e100x_battery", 3072, NULL, 2, NULL) != pdPASS) {
        ESP_LOGW("e100x", "battery sampling unavailable");
    }
}

static const char *const COMMANDS[] = {
    "buttons.read", "events.read", "events.wait", "buzzer.beep", "battery.read",
    "rtc.read", "rtc.set", "sd.status", "sd.list", "sd.read_text", "sd.write_text", "sd.read", "sd.write",
    "microphone.record",
};

bool e1002_hardware_handles(const char *command) {
    for (size_t i = 0; i < sizeof(COMMANDS) / sizeof(COMMANDS[0]); i++) {
        if (strcmp(command, COMMANDS[i]) == 0) return true;
    }
    return false;
}

static bool integer(const cJSON *params, const char *name, int low, int high, int *out) {
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(params, name);
    if (!cJSON_IsNumber(p) || p->valuedouble != p->valueint
        || p->valueint < low || p->valueint > high) return false;
    *out = p->valueint;
    return true;
}

static cJSON *execute(const char *command, const cJSON *params) {
    if (strcmp(command, "microphone.record") == 0) return e1002_microphone_record(params);
    if (strncmp(command, "sd.", 3) == 0) return e1002_sd_command(command, params);
    if (strcmp(command, "buzzer.beep") == 0) {
        int hz, ms;
        if (!integer(params, "frequency_hz", 100, 5000, &hz)
            || !integer(params, "duration_ms", 20, 1000, &ms)) {
            return failure("invalid_param", "frequency_hz 100..5000 and duration_ms 20..1000 required");
        }
        return e1002_buzzer_beep(hz, ms) ? success() : failure("unavailable", "buzzer unavailable");
    }
    if (strcmp(command, "rtc.set") == 0) {
        e1002_rtc_time_t t = {0};
        if (!integer(params, "year", 2000, 2099, &t.year)
            || !integer(params, "month", 1, 12, &t.month)
            || !integer(params, "day", 1, 31, &t.day)
            || !integer(params, "hour", 0, 23, &t.hour)
            || !integer(params, "minute", 0, 59, &t.minute)
            || !integer(params, "second", 0, 59, &t.second)) {
            return failure("invalid_param", "integer RTC calendar fields required");
        }
        return e1002_rtc_set(&t) ? success() : failure("unavailable", "invalid calendar date or RTC unavailable");
    }
    if (strcmp(command, "rtc.read") == 0) {
        e1002_rtc_time_t t;
        if (!e1002_rtc_read(&t)) return failure("unavailable", "RTC read failed");
        cJSON *r = success(), *p = cJSON_GetObjectItem(r, "payload");
        cJSON_AddBoolToObject(p, "valid", t.valid);
        cJSON_AddNumberToObject(p, "year", t.year);
        cJSON_AddNumberToObject(p, "month", t.month);
        cJSON_AddNumberToObject(p, "day", t.day);
        cJSON_AddNumberToObject(p, "hour", t.hour);
        cJSON_AddNumberToObject(p, "minute", t.minute);
        cJSON_AddNumberToObject(p, "second", t.second);
        return r;
    }
    return failure("unsupported", "unknown reTerminal command");
}

typedef struct {
    char command[32], request_id[64];
    cJSON *params;
    noise_ctrl_session_generation_t generation;
} command_args_t;

static void command_task(void *arg) {
    command_args_t *args = arg;
    cJSON *result = execute(args->command, args->params);
    if (!result) result = failure("out_of_memory", "hardware response allocation failed");
    noise_ctrl_send_command_result(args->generation, args->request_id, result);
    cJSON_Delete(args->params);
    free(args);
    portENTER_CRITICAL(&s_lock);
    s_busy = false;
    portEXIT_CRITICAL(&s_lock);
    vTaskDelete(NULL);
}

cJSON *e1002_hardware_command(const char *command, const cJSON *params,
                              const char *request_id,
                              noise_ctrl_session_generation_t generation) {
    if (strcmp(command, "events.read") == 0 || strcmp(command, "events.wait") == 0) {
        return e1002_events_command(command, params, request_id, generation);
    }
    if (strcmp(command, "battery.read") == 0) {
        cJSON *r = success();
        battery_metrics(cJSON_GetObjectItem(r, "payload"));
        return r;
    }
    if (strcmp(command, "buttons.read") == 0) {
        e1002_buttons_state_t state;
        if (!e1002_buttons_read(&state)) return failure("unavailable", "buttons unavailable");
        cJSON *r = success(), *p = cJSON_GetObjectItem(r, "payload");
        cJSON_AddBoolToObject(p, "key0_down", state.key0_down);
        cJSON_AddBoolToObject(p, "key1_down", state.key1_down);
        cJSON_AddBoolToObject(p, "key2_down", state.key2_down);
        cJSON_AddNumberToObject(p, "key0_presses", state.key0_presses);
        cJSON_AddNumberToObject(p, "key1_presses", state.key1_presses);
        cJSON_AddNumberToObject(p, "key2_presses", state.key2_presses);
        return r;
    }
    if (!request_id || !generation || strlen(request_id) >= 64 || strlen(command) >= 32) {
        return failure("invalid_param", "hardware operation requires a valid cloud request");
    }
    command_args_t *args = calloc(1, sizeof(*args));
    cJSON *async = cJSON_CreateObject();
    if (!args || !async || !cJSON_AddBoolToObject(async, "_async", true)) {
        free(args); cJSON_Delete(async);
        return failure("out_of_memory", "cannot allocate hardware task");
    }
    if (params && !(args->params = cJSON_Duplicate(params, true))) {
        free(args); cJSON_Delete(async);
        return failure("out_of_memory", "cannot copy hardware parameters");
    }
    strcpy(args->command, command);
    strcpy(args->request_id, request_id);
    args->generation = generation;
    portENTER_CRITICAL(&s_lock);
    bool busy = s_busy;
    if (!busy) s_busy = true;
    portEXIT_CRITICAL(&s_lock);
    if (!busy && xTaskCreate(command_task, "e100x_command", 8192, args, 2, NULL) == pdPASS) return async;
    if (!busy) {
        portENTER_CRITICAL(&s_lock);
        s_busy = false;
        portEXIT_CRITICAL(&s_lock);
    }
    cJSON_Delete(args->params); free(args); cJSON_Delete(async);
    return failure(busy ? "busy" : "out_of_memory", "hardware task busy or unavailable");
}

static cJSON *parameter(const char *type, const char *description) {
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", type);
    cJSON_AddStringToObject(p, "description", description);
    return p;
}

static void advertise(cJSON *commands, const char *name, const char *description,
                      cJSON *required, cJSON *optional) {
    cJSON *c = cJSON_AddObjectToObject(commands, name);
    cJSON_AddStringToObject(c, "description", description);
    cJSON_AddItemToObject(c, "required", required ? required : cJSON_CreateObject());
    cJSON_AddItemToObject(c, "optional", optional ? optional : cJSON_CreateObject());
}

void e1002_hardware_register(cJSON *commands) {
    advertise(commands, "buttons.read", "KEY0 GPIO3, KEY1 GPIO4, KEY2 GPIO5 state and press counts. KEY0 retains pairing/reset gestures. Other actions are caller-defined.", NULL, NULL);
    cJSON *cursor = cJSON_CreateObject();
    cJSON_AddItemToObject(cursor, "after", parameter("integer", "Last next_sequence; 0 replays history. Omit for current cursor; reset after reboot."));
    advertise(commands, "events.read", "Read retained button.changed (key,pressed,presses,gpio) and sd.changed (inserted) events. Payload: events,next_sequence,has_more,history_lost,cursor_reset. Latest 32 events in RAM, batches of 16.", NULL, cursor);
    cJSON *wait = cJSON_Duplicate(cursor, true);
    cJSON_AddItemToObject(wait, "timeout_ms", parameter("integer", "1..30000; default 25000."));
    advertise(commands, "events.wait", "Wait for changes; same payload as events.read, empty events on timeout. One wait at a time. Cloud listener repeats with next_sequence; pressed=true identifies a press. No local actions installed.", NULL, wait);
    cJSON_AddNumberToObject(cJSON_GetObjectItem(commands, "events.wait"), "timeout_ms", 35000);
    advertise(commands, "battery.read", "Cached battery_mv and estimated battery_pct using Seeed's E1001/E1002 voltage curve and averaged ADC samples. Sampled every 30s, null if unavailable. Not charging state.", NULL, NULL);
    cJSON *beep = cJSON_CreateObject();
    cJSON_AddItemToObject(beep, "frequency_hz", parameter("integer", "100..5000 Hz."));
    cJSON_AddItemToObject(beep, "duration_ms", parameter("integer", "20..1000 ms."));
    advertise(commands, "buzzer.beep", "Play one bounded tone.", beep, NULL);
    advertise(commands, "rtc.read", "Read PCF8563 calendar and valid flag. No timezone; no hardware alarm/wakeup connection.", NULL, NULL);
    cJSON *time = cJSON_CreateObject();
    cJSON_AddItemToObject(time, "year", parameter("integer", "2000..2099."));
    cJSON_AddItemToObject(time, "month", parameter("integer", "1..12."));
    cJSON_AddItemToObject(time, "day", parameter("integer", "Valid day of month."));
    cJSON_AddItemToObject(time, "hour", parameter("integer", "0..23."));
    cJSON_AddItemToObject(time, "minute", parameter("integer", "0..59."));
    cJSON_AddItemToObject(time, "second", parameter("integer", "0..59."));
    advertise(commands, "rtc.set", "Set RTC time in the caller's intended timezone.", time, NULL);
    advertise(commands, "sd.status", "Report FAT card detection, mount status and capacity. Never formats.", NULL, NULL);
    cJSON *list = cJSON_CreateObject();
    cJSON_AddItemToObject(list, "path", parameter("string", "Relative directory; default card root."));
    advertise(commands, "sd.list", "List up to 32 directory names.", NULL, list);
    cJSON *read = cJSON_CreateObject();
    cJSON_AddItemToObject(read, "path", parameter("string", "Relative file path on SD."));
    cJSON *write = cJSON_Duplicate(read, true);
    cJSON_AddItemToObject(write, "text", parameter("string", "UTF-8 text, at most 2048 bytes; overwrites file."));
    advertise(commands, "sd.read_text", "Read a text file, at most 2048 bytes.", read, NULL);
    advertise(commands, "sd.write_text", "Write a text file; parent directory must exist.", write, NULL);
    cJSON *path = cJSON_CreateObject();
    cJSON_AddItemToObject(path, "path", parameter("string", "Relative file path on SD."));
    cJSON *offset = cJSON_CreateObject();
    cJSON_AddItemToObject(offset, "offset", parameter("integer", "Byte offset 0..2147483647; default 0."));
    cJSON *binary_write = cJSON_Duplicate(path, true);
    cJSON_AddItemToObject(binary_write, "data_base64", parameter("string", "Base64 of 1..1024 binary bytes."));
    cJSON *write_offset = cJSON_Duplicate(offset, true);
    advertise(commands, "sd.read", "Read up to 1024 binary bytes. Payload: data_base64,bytes,next_offset,eof.", path, offset);
    advertise(commands, "sd.write", "Write binary chunk. Offset 0 truncates/creates file; positive offset writes into existing file. Payload: bytes,next_offset.", binary_write, write_offset);
    cJSON *mic = cJSON_CreateObject();
    cJSON_AddItemToObject(mic, "duration_ms", parameter("integer", "100..500 ms."));
    advertise(commands, "microphone.record", "On-demand U18 PDM capture. Payload: audio_base64, format=pcm_s16le, sample_rate_hz=16000, channels=1, duration_ms,bytes. Mic powers off after capture. No continuous recording or recognition.", mic, NULL);
}
