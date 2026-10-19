// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool key0_down;
    bool key1_down;
    bool key2_down;
    uint32_t key0_presses;
    uint32_t key1_presses;
    uint32_t key2_presses;
} e1002_buttons_state_t;

bool e1002_buttons_init(void);
bool e1002_buttons_read(e1002_buttons_state_t *state);
