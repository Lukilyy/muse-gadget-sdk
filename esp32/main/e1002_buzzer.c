// SPDX-License-Identifier: Apache-2.0

#include "e1002_buzzer.h"

#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define BUZZER_GPIO 45
#define BUZZER_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_TIMER LEDC_TIMER_0
#define BUZZER_CHANNEL LEDC_CHANNEL_0

static SemaphoreHandle_t s_lock;
static esp_timer_handle_t s_stop_timer;
static bool s_ready;
static int64_t s_stop_at;

static void stop_tone(void *arg) {
    (void)arg;
    // A previous timer callback can race with a new beep. The new beep arms
    // its own timer; never let the old callback silence it.
    if (xSemaphoreTake(s_lock, 0) == pdTRUE) {
        if (esp_timer_get_time() >= s_stop_at) {
            ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
            ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
        }
        xSemaphoreGive(s_lock);
    }
}

bool e1002_buzzer_beep(int frequency_hz, int duration_ms) {
    if (frequency_hz < 100 || frequency_hz > 5000 || duration_ms < 20 || duration_ms > 1000) {
        return false;
    }
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock || xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    if (!s_ready) {
        ledc_timer_config_t timer = {
            .speed_mode = BUZZER_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT,
            .timer_num = BUZZER_TIMER,
            .freq_hz = 1000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ledc_channel_config_t channel = {
            .gpio_num = BUZZER_GPIO,
            .speed_mode = BUZZER_MODE,
            .channel = BUZZER_CHANNEL,
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = BUZZER_TIMER,
            .duty = 0,
        };
        const esp_timer_create_args_t stop = {
            .callback = stop_tone,
            .name = "e100x_buzzer_stop",
        };
        s_ready = ledc_timer_config(&timer) == ESP_OK
               && ledc_channel_config(&channel) == ESP_OK
               && esp_timer_create(&stop, &s_stop_timer) == ESP_OK;
    }
    bool ok = false;
    if (s_ready) {
        esp_timer_stop(s_stop_timer);
        s_stop_at = esp_timer_get_time() + (int64_t)duration_ms * 1000;
        ok = ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, frequency_hz) == ESP_OK
          && ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 512) == ESP_OK
          && ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL) == ESP_OK
          && esp_timer_start_once(s_stop_timer, (uint64_t)duration_ms * 1000) == ESP_OK;
        if (!ok) {
            ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
            ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
        }
    }
    xSemaphoreGive(s_lock);
    return ok;
}
