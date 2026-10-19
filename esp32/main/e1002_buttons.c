// SPDX-License-Identifier: Apache-2.0

#include "e1002_buttons.h"
#include "e1002_events.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// E1001/E1002 KEY1 (GPIO4) and KEY2 (GPIO5) are active-low inputs.
#define KEY1_GPIO GPIO_NUM_4
#define KEY2_GPIO GPIO_NUM_5

typedef struct {
    bool stable;
    bool sample;
    uint8_t same;
    uint32_t presses;
} key_t;

static key_t s_keys[3];
static bool s_ready;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static bool sample_key(key_t *key, bool down) {
    if (down != key->sample) {
        key->sample = down;
        key->same = 1;
    } else if (key->same < 3) {
        key->same++;
    }
    if (key->same == 3 && key->stable != down) {
        key->stable = down;
        if (down) key->presses++;
        return true;
    }
    return false;
}

static void poll_keys(void *arg) {
    (void)arg;
    for (;;) {
        static const int gpios[] = {3, KEY1_GPIO, KEY2_GPIO};
        for (int i = 0; i < 3; i++) {
            bool down = gpio_get_level(gpios[i]) == 0;
            portENTER_CRITICAL(&s_lock);
            bool changed = sample_key(&s_keys[i], down);
            uint32_t presses = s_keys[i].presses;
            portEXIT_CRITICAL(&s_lock);
            if (changed) {
                e1002_events_button(i, gpios[i], down, presses);
                ESP_LOGI("e100x.buttons", "KEY%d GPIO%d %s count=%lu",
                         i, gpios[i], down ? "pressed" : "released",
                         (unsigned long)presses);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool e1002_buttons_init(void) {
    if (s_ready) return true;
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << 3) | (1ULL << KEY1_GPIO) | (1ULL << KEY2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) return false;
    s_ready = xTaskCreate(poll_keys, "e100x_keys", 2048, NULL, 2, NULL) == pdPASS;
    return s_ready;
}

bool e1002_buttons_read(e1002_buttons_state_t *state) {
    if (!state || !s_ready) return false;
    portENTER_CRITICAL(&s_lock);
    state->key0_down = s_keys[0].stable;
    state->key1_down = s_keys[1].stable;
    state->key2_down = s_keys[2].stable;
    state->key0_presses = s_keys[0].presses;
    state->key1_presses = s_keys[1].presses;
    state->key2_presses = s_keys[2].presses;
    portEXIT_CRITICAL(&s_lock);
    return true;
}
