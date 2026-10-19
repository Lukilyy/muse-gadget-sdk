// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "driver/i2c_master.h"

// E1002 SHT40 (0x44) and PCF8563 (0x51) share GPIO19/20.
i2c_master_bus_handle_t e1002_i2c_bus_get(void);
