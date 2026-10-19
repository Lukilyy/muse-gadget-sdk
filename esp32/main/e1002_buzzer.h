// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

// Passive buzzer, GPIO45. Nonblocking: a timer stops the tone.
bool e1002_buzzer_beep(int frequency_hz, int duration_ms);
