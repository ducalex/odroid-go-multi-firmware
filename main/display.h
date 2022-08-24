#pragma once
#include "target.h"

void ili9341_init(void);
void ili9341_deinit(void);
void ili9341_writeLE(const uint16_t *buffer);
void ili9341_writeBE(const uint16_t *buffer);
