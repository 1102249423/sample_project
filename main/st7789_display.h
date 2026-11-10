#pragma once

#include <stdint.h>
#include "esp_err.h"

esp_err_t display_init(void);
void display_set_backlight(uint8_t percent);
void display_fill(uint16_t color);
void display_color_cycle(void);
void display_test_pattern(void);
