// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

typedef struct {
    int year, month, day, hour, minute, second;
    bool valid;
} e1002_rtc_time_t;

bool e1002_rtc_read(e1002_rtc_time_t *out);
bool e1002_rtc_set(const e1002_rtc_time_t *time);
