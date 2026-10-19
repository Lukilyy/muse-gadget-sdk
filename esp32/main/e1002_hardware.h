// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "cJSON.h"
#include "noise_control.h"

#ifdef __cplusplus
extern "C" {
#endif

void e1002_hardware_prepare_display(void);
void e1002_hardware_init(void);
void e1002_hardware_register(cJSON *commands);
bool e1002_hardware_handles(const char *command);
cJSON *e1002_hardware_command(const char *command, const cJSON *params,
                              const char *request_id,
                              noise_ctrl_session_generation_t generation);
// Cached battery metrics; no ADC conversion runs on the control task.
void e1002_hardware_health(cJSON *metrics);

#ifdef __cplusplus
}
#endif
