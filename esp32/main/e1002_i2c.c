// SPDX-License-Identifier: Apache-2.0

#include "e1002_i2c.h"

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static i2c_master_bus_handle_t s_bus;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_initializing;

i2c_master_bus_handle_t e1002_i2c_bus_get(void) {
    // Initialization happens once; callers may come from health and command tasks.
    for (;;) {
        portENTER_CRITICAL(&s_init_lock);
        if (s_bus) {
            i2c_master_bus_handle_t bus = s_bus;
            portEXIT_CRITICAL(&s_init_lock);
            return bus;
        }
        if (!s_initializing) {
            s_initializing = true;
            portEXIT_CRITICAL(&s_init_lock);
            break;
        }
        portEXIT_CRITICAL(&s_init_lock);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = 19,
        .scl_io_num = 20,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    portENTER_CRITICAL(&s_init_lock);
    if (err == ESP_OK) s_bus = bus;
    s_initializing = false;
    portEXIT_CRITICAL(&s_init_lock);
    if (err != ESP_OK) ESP_LOGW("e100x.i2c", "bus init: %s", esp_err_to_name(err));
    return err == ESP_OK ? bus : NULL;
}
