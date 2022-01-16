#pragma once

#ifdef TARGET_MRGC_G32
#define LCD_PIN_NUM_MISO GPIO_NUM_NC
#define LCD_PIN_NUM_MOSI GPIO_NUM_23
#define LCD_PIN_NUM_CLK  GPIO_NUM_18
#define LCD_PIN_NUM_CS   GPIO_NUM_5
#define LCD_PIN_NUM_DC   GPIO_NUM_12
#define LCD_PIN_NUM_BCKL GPIO_NUM_27

#define SCREEN_OFFSET_TOP 28
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 224
#else
#define LCD_PIN_NUM_MISO GPIO_NUM_19
#define LCD_PIN_NUM_MOSI GPIO_NUM_23
#define LCD_PIN_NUM_CLK  GPIO_NUM_18
#define LCD_PIN_NUM_CS   GPIO_NUM_5
#define LCD_PIN_NUM_DC   GPIO_NUM_21
#define LCD_PIN_NUM_BCKL GPIO_NUM_14

#define SCREEN_OFFSET_TOP 0
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#endif

void ili9341_init(void);
void ili9341_deinit(void);
void ili9341_writeLE(const uint16_t *buffer);
void ili9341_writeBE(const uint16_t *buffer);
