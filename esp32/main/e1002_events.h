// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "noise_control.h"

// Fixed-size RAM history. Producers must call from a task, not an ISR.
// GPIO and value describe hardware only; no actions are bound to events.
void e1002_events_button(int key, int gpio, bool pressed, uint32_t presses);
void e1002_events_sd(bool inserted);

// events.read returns immediately. events.wait completes asynchronously through
// the existing command-result channel, without introducing a wire protocol.
cJSON *e1002_events_command(const char *command, const cJSON *params,
                            const char *request_id,
                            noise_ctrl_session_generation_t generation);
