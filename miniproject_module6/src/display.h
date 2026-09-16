#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "common.h"

esp_err_t display_init(i2c_master_bus_handle_t bus);
void display_render(const display_msg_t *msg);
