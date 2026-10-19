// SPDX-License-Identifier: MIT
/*
 * Voltage table adapted from Seeed-reTerminal-E10xx-Firmware,
 * src/boards/reterminal_e1001/config.h (E1001/E1002 BATTERY_CURVE).
 *
 * MIT License
 *
 * Copyright (c) 2025 Seeed-reTerminal-E10xx-Firmware
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <stdint.h>

// Index is state of charge (0..100); voltage is millivolts.
static const uint16_t E1002_BATTERY_CURVE_MV[] = {
    2795, 2795, 2990, 3107, 3189, 3252, 3300, 3334, 3354, 3367,
    3378, 3389, 3399, 3409, 3418, 3426, 3434, 3442, 3449, 3457,
    3464, 3471, 3478, 3485, 3491, 3498, 3504, 3510, 3516, 3522,
    3528, 3534, 3540, 3546, 3552, 3558, 3564, 3569, 3575, 3581,
    3587, 3592, 3598, 3603, 3609, 3614, 3620, 3625, 3631, 3636,
    3642, 3647, 3653, 3658, 3664, 3669, 3675, 3680, 3686, 3691,
    3697, 3703, 3709, 3715, 3721, 3727, 3733, 3739, 3746, 3752,
    3759, 3765, 3772, 3779, 3786, 3793, 3801, 3808, 3816, 3824,
    3832, 3840, 3849, 3858, 3867, 3876, 3886, 3896, 3906, 3916,
    3927, 3984, 3995, 4007, 4019, 4032, 4045, 4059, 4074, 4090,
    4111,
};

// Interpolate the official curve, rounded to the nearest whole percent.
static inline int e1002_battery_percent(int mv) {
    if (mv <= E1002_BATTERY_CURVE_MV[0]) return 0;
    if (mv >= E1002_BATTERY_CURVE_MV[100]) return 100;
    for (int i = 0; i < 100; i++) {
        int low = E1002_BATTERY_CURVE_MV[i];
        int high = E1002_BATTERY_CURVE_MV[i + 1];
        if (mv >= low && mv <= high && high > low) {
            return i + ((mv - low) + (high - low) / 2) / (high - low);
        }
    }
    return 0;
}
