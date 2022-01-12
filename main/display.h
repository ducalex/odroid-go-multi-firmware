#pragma once

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

void ili9341_init(void);
void ili9341_deinit(void);
void ili9341_write_rectangle(int left, int top, int width, int height, const uint16_t* buffer);
