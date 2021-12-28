#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <driver/spi_master.h>
#include <driver/ledc.h>
#include <driver/rtc_io.h>
#include <string.h>

#include "display.h"

#define SPI_PIN_NUM_MISO GPIO_NUM_19
#define SPI_PIN_NUM_MOSI GPIO_NUM_23
#define SPI_PIN_NUM_CLK  GPIO_NUM_18
#define LCD_PIN_NUM_CS   GPIO_NUM_5
#define LCD_PIN_NUM_DC   GPIO_NUM_21
#define LCD_PIN_NUM_BCKL GPIO_NUM_14

#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_ML  0x10
#define MADCTL_MH 0x04
#define TFT_RGB_BGR 0x08

/*
 The ILI9341 needs a bunch of command/argument values to be initialized. They are stored in this struct.
*/
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; //No of data in data; bit 7 = delay after set; 0xFF = end of cmds.
} ili_init_cmd_t;

// 2.4" LCD
static const ili_init_cmd_t ili_init_cmds[] = {
    // VCI=2.8V
    //************* Start Initial Sequence **********//
    {0x01, {0}, 0x80},
    {0xCF, {0x00, 0xc3, 0x30}, 3},
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4},
    {0xE8, {0x85, 0x00, 0x78}, 3},
    {0xCB, {0x39, 0x2c, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x1B}, 1},    //Power control   //VRH[5:0]
    {0xC1, {0x12}, 1},    //Power control   //SAP[2:0];BT[3:0]
    {0xC5, {0x32, 0x3C}, 2},    //VCM control
    {0xC7, {0x91}, 1},    //VCM control2
    //{0x36, {(MADCTL_MV | MADCTL_MX | TFT_RGB_BGR)}, 1},    // Memory Access Control
    {0x36, {(MADCTL_MV | MADCTL_MY | TFT_RGB_BGR)}, 1},    // Memory Access Control
    {0x3A, {0x55}, 1},
    {0xB1, {0x00, 0x1B}, 2},  // Frame Rate Control (1B=70, 1F=61, 10=119)
    {0xB6, {0x0A, 0xA2}, 2},    // Display Function Control
    {0xF6, {0x01, 0x30}, 2},
    {0xF2, {0x00}, 1},    // 3Gamma Function Disable
    {0x26, {0x01}, 1},     //Gamma curve selected

    //Set Gamma
    {0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15},
    {0XE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15},

    {0x11, {0}, 0x80},    //Exit Sleep
    {0x29, {0}, 0x80},    //Display on

    {0, {0}, 0xff}
};

static spi_device_handle_t spi;
static DMA_ATTR uint16_t dma_buffer[SCREEN_WIDTH];


//Send a command to the ILI9341. Uses spi_device_transmit, which waits until the transfer is complete.
static void ili_cmd(spi_device_handle_t spi, const uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,        // In bits
        .tx_buffer = &cmd,
        .user = (void*)0,   // DC line
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    assert(ret==ESP_OK);            //Should have had no issues.
}

//Send data to the ILI9341. Uses spi_device_transmit, which waits until the transfer is complete.
static void ili_data(spi_device_handle_t spi, const void *data, int len)
{
    spi_transaction_t t = {
        .length = len * 8,  // In bits
        .tx_buffer = data,
        .user = (void*)1,   // DC Line
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    assert(ret==ESP_OK);            //Should have had no issues.
}

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
static void ili_spi_pre_transfer_callback(spi_transaction_t *t)
{
    gpio_set_level(LCD_PIN_NUM_DC, (int)t->user);
}

//Initialize the display
static void ili_init(void)
{
    //Initialize non-SPI GPIOs
    gpio_set_direction(LCD_PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(LCD_PIN_NUM_BCKL, GPIO_MODE_OUTPUT);

    //Send all the commands
    for (int cmd = 0; ili_init_cmds[cmd].databytes!=0xff; cmd++) {
        ili_cmd(spi, ili_init_cmds[cmd].cmd);
        size_t datalen = ili_init_cmds[cmd].databytes & 0x7f;
        if (datalen > 0) {
            memcpy(dma_buffer, ili_init_cmds[cmd].data, datalen);
            ili_data(spi, (uint8_t*)dma_buffer, datalen);
        }
        if (ili_init_cmds[cmd].databytes&0x80) {
            vTaskDelay(100 / portTICK_RATE_MS);
        }
    }
}

static void send_reset_drawing(int left, int top, int width, int height)
{
    uint8_t tx_data[4];

    tx_data[0]=(left) >> 8;              //Start Col High
    tx_data[1]=(left) & 0xff;              //Start Col Low
    tx_data[2]=(left + width - 1) >> 8;       //End Col High
    tx_data[3]=(left + width - 1) & 0xff;     //End Col Low
    ili_cmd(spi, 0x2A);
    ili_data(spi, tx_data, 4);

    tx_data[0]=top >> 8;        //Start page high
    tx_data[1]=top & 0xff;      //start page low
    tx_data[2]=(top + height - 1)>>8;    //end page high
    tx_data[3]=(top + height - 1)&0xff;  //end page low
    ili_cmd(spi, 0x2B);
    ili_data(spi, tx_data, 4);

    ili_cmd(spi, 0x2C);
}

static void send_continue_line(const uint16_t *line, int width)
{
    ili_cmd(spi, 0x3C);
    ili_data(spi, line, width * 2);
}

static void backlight_init()
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_13_BIT, //set timer counter bit number
        .freq_hz = 5000,              //set frequency of pwm
        .speed_mode = LEDC_LOW_SPEED_MODE,   //timer mode,
        .timer_num = LEDC_TIMER_0,    //timer index
    };

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL_0,
        .duty = 0x1fff,
        .gpio_num = LCD_PIN_NUM_BCKL,
        .intr_type = LEDC_INTR_FADE_END,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        //set LEDC timer source, if different channel use one timer,
        //the frequency and bit_num of these channels should be the same
        .timer_sel = LEDC_TIMER_0,
    };

    ledc_timer_config(&ledc_timer);
    ledc_channel_config(&ledc_channel);
    ledc_fade_func_install(0);
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0x1fff, 500);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
}

static void backlight_deinit(void)
{
    ledc_fade_func_uninstall();
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
}

void ili9341_clear(uint16_t color)
{
    send_reset_drawing(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    color = color << 8 | color >> 8;

    for (int i = 0; i < SCREEN_WIDTH; ++i)
        dma_buffer[i] = color;

    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        send_continue_line(dma_buffer, SCREEN_WIDTH);
}

void ili9341_write_rectangle(int left, int top, int width, int height, const uint16_t *buffer)
{
    assert(left >= 0 && top >= 0);
    assert(width > 0 && height > 0);

    send_reset_drawing(left, top, width, height);

    for (int y = 0; y < height; y++)
    {
        for (int i = 0; i < width; ++i)
        {
            uint16_t pixel = buffer[y * width + i];
            dma_buffer[i] = pixel << 8 | pixel >> 8;
        }
        send_continue_line(dma_buffer, width);
    }
}

void ili9341_deinit()
{
    spi_bus_remove_device(spi);
    backlight_deinit();
    gpio_reset_pin(LCD_PIN_NUM_DC);
    gpio_reset_pin(LCD_PIN_NUM_BCKL);
}

void ili9341_init()
{
	esp_err_t ret;

    // Initialize SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = SPI_PIN_NUM_MISO,
        .mosi_io_num = SPI_PIN_NUM_MOSI,
        .sclk_io_num = SPI_PIN_NUM_CLK,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 40000000,
        .mode = 0,                                //SPI mode 0
        .spics_io_num = LCD_PIN_NUM_CS,               //CS pin
        .queue_size = 7,                          //We want to be able to queue 7 transactions at a time
        .pre_cb = ili_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
        .flags = SPI_DEVICE_NO_DUMMY,//SPI_DEVICE_HALFDUPLEX;
    };

    //Initialize the SPI bus
    ret=spi_bus_initialize(HSPI_HOST, &buscfg, 1);
    //assert(ret==ESP_OK);

    //Attach the LCD to the SPI bus
    ret=spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    assert(ret==ESP_OK);


    //Initialize the LCD
	ESP_LOGI(__func__, "LCD: calling ili_init.");
    ili_init();

	ESP_LOGI(__func__, "LCD: calling backlight_init.");
    backlight_init();

    ESP_LOGI(__func__, "LCD Initialized.");
}
