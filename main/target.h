// REF: https://wiki.odroid.com/odroid_go/odroid_go
#include "sdkconfig.h"
#if defined(CONFIG_TARGET_ODROID_GO)
// Target definition
#define RG_TARGET_NAME             "ODROID-GO"

// Storage and Settings
#define RG_STORAGE_DRIVER           1   // 1 = SDSPI, 2 = SDMMC, 3 = USB
#define RG_SETTINGS_USE_NVS         0

// Video
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341
#define RG_SCREEN_TYPE              32
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_MARGIN_TOP        0
#define RG_SCREEN_MARGIN_BOTTOM     0
#define RG_SCREEN_MARGIN_LEFT       0
#define RG_SCREEN_MARGIN_RIGHT      20

// Input
#define RG_GAMEPAD_DRIVER           1   // 1 = ODROID-GO, 2 = Serial, 3 = MRGC-IO
#define RG_GAMEPAD_HAS_MENU_BTN     1
#define RG_GAMEPAD_HAS_OPTION_BTN   0
#define RG_BATTERY_ADC_CHANNEL      ADC1_CHANNEL_0
#define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3500.f) / (4200.f - 3500.f) * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)

// Status LED
#define RG_GPIO_LED                 GPIO_NUM_2

// I2C BUS
// #define RG_GPIO_I2C_SDA             GPIO_NUM_15
// #define RG_GPIO_I2C_SCL             GPIO_NUM_4

// Built-in gamepad
#define RG_GPIO_GAMEPAD_X           ADC1_CHANNEL_6
#define RG_GPIO_GAMEPAD_Y           ADC1_CHANNEL_7
#define RG_GPIO_GAMEPAD_SELECT      GPIO_NUM_27
#define RG_GPIO_GAMEPAD_START       GPIO_NUM_39
#define RG_GPIO_GAMEPAD_A           GPIO_NUM_32
#define RG_GPIO_GAMEPAD_B           GPIO_NUM_33
#define RG_GPIO_GAMEPAD_MENU        GPIO_NUM_13
#define RG_GPIO_GAMEPAD_OPTION      GPIO_NUM_0

// SNES-style gamepad
// #define RG_GPIO_GAMEPAD_LATCH       GPIO_NUM_NC
// #define RG_GPIO_GAMEPAD_CLOCK       GPIO_NUM_NC
// #define RG_GPIO_GAMEPAD_DATA        GPIO_NUM_NC

// SPI Display
#define RG_GPIO_LCD_HOST            SPI2_HOST
#define RG_GPIO_LCD_MISO            GPIO_NUM_19
#define RG_GPIO_LCD_MOSI            GPIO_NUM_23
#define RG_GPIO_LCD_CLK             GPIO_NUM_18
#define RG_GPIO_LCD_CS              GPIO_NUM_5
#define RG_GPIO_LCD_DC              GPIO_NUM_21
#define RG_GPIO_LCD_BCKL            GPIO_NUM_14

// SPI SD Card
#define RG_GPIO_SDSPI_HOST          SPI2_HOST
#define RG_GPIO_SDSPI_MISO          GPIO_NUM_19
#define RG_GPIO_SDSPI_MOSI          GPIO_NUM_23
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_18
#define RG_GPIO_SDSPI_CS            GPIO_NUM_22
#elif defined(CONFIG_TARGET_MRGC_G32)
// Parts:
// - ESP32-WROVER-B (SoC)
// - STM32F071cbu7 (Apparently buttons, charging, LED, backlight?)
// - NXP 1334A (I2S DAC)
// - CS5082E (Power controller)
// - P8302E (Amplifier)
// - YT280S002 (ILI9341 LCD)
//

/**
 * IO35 - MENU BTN
 * IO25 - I2S DAC
 * IO26 - I2S DAC
 * IO15 - SD CARD
 * IO2 - SD CARD
 * IO0 - SELECT BTN
 * IO4 - AMP EN
 * IO5 - LCD SPI CS
 * IO12 - LCD DC
 * IO18 - SPI CLK
 * IO23 - SPI MOSI
 * IO21 - STM32F (I2C)
 * IO22 - STM32F (I2C)
 *
 * IO27 - resistor then STM32?
 *
 * Power LED is connected to the STM32
 */

// Target definition
#define RG_TARGET_NAME             "MRGC-G32"

// Storage
#define RG_STORAGE_DRIVER           2   // 1 = SDSPI, 2 = SDMMC, 3 = USB

// Video
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341
#define RG_SCREEN_TYPE              1
#define RG_SCREEN_WIDTH             240
#define RG_SCREEN_HEIGHT            320
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_MARGIN_TOP        28
#define RG_SCREEN_MARGIN_BOTTOM     68
#define RG_SCREEN_MARGIN_LEFT       0
#define RG_SCREEN_MARGIN_RIGHT      0

// Input
#define RG_GAMEPAD_DRIVER           3   // 1 = ODROID-GO, 2 = Serial, 3 = MRGC-IO
#define RG_GAMEPAD_HAS_MENU_BTN     1
#define RG_GAMEPAD_HAS_OPTION_BTN   0
// #define RG_BATTERY_ADC_CHANNEL      ADC1_CHANNEL_0
#define RG_BATTERY_CALC_PERCENT(raw) (((raw) - 170) / 30.f * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw) (0)

// Status LED
// #define RG_GPIO_LED                 GPIO_NUM_NC

// I2C BUS
#define RG_GPIO_I2C_SDA             GPIO_NUM_21
#define RG_GPIO_I2C_SCL             GPIO_NUM_22

// Display
#define RG_GPIO_LCD_HOST            SPI2_HOST
#define RG_GPIO_LCD_MISO            GPIO_NUM_NC
#define RG_GPIO_LCD_MOSI            GPIO_NUM_23
#define RG_GPIO_LCD_CLK             GPIO_NUM_18
#define RG_GPIO_LCD_CS              GPIO_NUM_5
#define RG_GPIO_LCD_DC              GPIO_NUM_12
#define RG_GPIO_LCD_BCKL            GPIO_NUM_27
#elif defined(CONFIG_TARGET_RETRO_ESP32)
// Target definition
#define RG_TARGET_NAME             "ODROID-GO"

// Storage and Settings
#define RG_STORAGE_DRIVER           1   // 1 = SDSPI, 2 = SDMMC, 3 = USB
#define RG_SETTINGS_USE_NVS         0

// Video
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341
#define RG_SCREEN_TYPE              32
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_MARGIN_TOP        0
#define RG_SCREEN_MARGIN_BOTTOM     0
#define RG_SCREEN_MARGIN_LEFT       0
#define RG_SCREEN_MARGIN_RIGHT      20

// Input
#define RG_GAMEPAD_DRIVER           1   // 1 = ODROID-GO, 2 = Serial, 3 = MRGC-IO
#define RG_GAMEPAD_HAS_MENU_BTN     1
#define RG_GAMEPAD_HAS_OPTION_BTN   0
#define RG_BATTERY_ADC_CHANNEL      ADC1_CHANNEL_0
#define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3500.f) / (4200.f - 3500.f) * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)

// Status LED
#define RG_GPIO_LED                 GPIO_NUM_2

// I2C BUS
// #define RG_GPIO_I2C_SDA             GPIO_NUM_15
// #define RG_GPIO_I2C_SCL             GPIO_NUM_4

// Built-in gamepad
#define RG_GPIO_GAMEPAD_X           ADC1_CHANNEL_6
#define RG_GPIO_GAMEPAD_Y           ADC1_CHANNEL_7
#define RG_GPIO_GAMEPAD_SELECT      GPIO_NUM_27
#define RG_GPIO_GAMEPAD_START       GPIO_NUM_39
#define RG_GPIO_GAMEPAD_A           GPIO_NUM_32
#define RG_GPIO_GAMEPAD_B           GPIO_NUM_33
#define RG_GPIO_GAMEPAD_MENU        GPIO_NUM_13
#define RG_GPIO_GAMEPAD_OPTION      GPIO_NUM_0

// SPI Display
#define RG_GPIO_LCD_HOST            SPI2_HOST
#define RG_GPIO_LCD_MISO            GPIO_NUM_19
#define RG_GPIO_LCD_MOSI            GPIO_NUM_23
#define RG_GPIO_LCD_CLK             GPIO_NUM_18
#define RG_GPIO_LCD_CS              GPIO_NUM_5
#define RG_GPIO_LCD_DC              GPIO_NUM_21
#define RG_GPIO_LCD_BCKL            GPIO_NUM_14

// SPI SD Card
#define RG_GPIO_SDSPI_HOST          SPI2_HOST
#define RG_GPIO_SDSPI_MISO          GPIO_NUM_19
#define RG_GPIO_SDSPI_MOSI          GPIO_NUM_23
#define RG_GPIO_SDSPI_CLK           GPIO_NUM_18
#define RG_GPIO_SDSPI_CS            GPIO_NUM_22
#elif defined(CONFIG_TARGET_ESPLAY_S3)
// Target definition
#define RG_TARGET_NAME             "ESPLAY-S3"

// Storage
#define RG_STORAGE_DRIVER           2   // 1 = SDSPI, 2 = SDMMC, 3 = USB

// Video
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341
#define RG_SCREEN_TYPE              4   // 4 = ESPLAY-ST7789V2
#define RG_SCREEN_WIDTH             320
#define RG_SCREEN_HEIGHT            240
#define RG_SCREEN_ROTATE            0
#define RG_SCREEN_MARGIN_TOP        0
#define RG_SCREEN_MARGIN_BOTTOM     0
#define RG_SCREEN_MARGIN_LEFT       0
#define RG_SCREEN_MARGIN_RIGHT      0

// Input
#define RG_GAMEPAD_DRIVER           5   // 1 = ODROID-GO, 2 = Serial, 3 = MRGC-IO 5 = ESPLAY
#define RG_GAMEPAD_HAS_MENU_BTN     1
#define RG_GAMEPAD_HAS_OPTION_BTN   1
#define RG_BATTERY_ADC_CHANNEL      ADC1_CHANNEL_3
#define RG_BATTERY_CALC_PERCENT(raw) (((raw) * 2.f - 3500.f) / (4200.f - 3500.f) * 100.f)
#define RG_BATTERY_CALC_VOLTAGE(raw) ((raw) * 2.f * 0.001f)

// Status LED
#define RG_GPIO_LED                 GPIO_NUM_38

// I2C BUS
#define RG_GPIO_I2C_SDA             GPIO_NUM_48
#define RG_GPIO_I2C_SCL             GPIO_NUM_47

// Built-in gamepad
#define RG_GPIO_GAMEPAD_L           GPIO_NUM_2
#define RG_GPIO_GAMEPAD_R           GPIO_NUM_5
#define RG_GPIO_GAMEPAD_MENU        GPIO_NUM_9

// SPI Display
#define RG_GPIO_LCD_HOST            SPI2_HOST
#define RG_GPIO_LCD_MISO            GPIO_NUM_NC
#define RG_GPIO_LCD_MOSI            GPIO_NUM_11
#define RG_GPIO_LCD_CLK             GPIO_NUM_14
#define RG_GPIO_LCD_CS              GPIO_NUM_12
#define RG_GPIO_LCD_DC              GPIO_NUM_13
#define RG_GPIO_LCD_BCKL            GPIO_NUM_10
#define RG_GPIO_LCD_RST             GPIO_NUM_21

// SPI SD Card
#define RG_GPIO_SDSPI_CMD          GPIO_NUM_41
#define RG_GPIO_SDSPI_CLK          GPIO_NUM_40
#define RG_GPIO_SDSPI_D0           GPIO_NUM_39
#define RG_SDSPI_HIGHSPEED         0
#endif
