// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include "cJSON.h"

// Bounded, on-demand PCM capture. Caller serializes hardware operations.
bool e1002_microphone_init(void);
cJSON *e1002_microphone_record(const cJSON *params);
