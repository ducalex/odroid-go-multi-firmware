#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_log.h>
#include <driver/spi_master.h>
#include <driver/rtc_io.h>
#include <string.h>

#include "display.h"

static spi_device_handle_t spi;
static DMA_ATTR uint16_t dma_buffer[SCREEN_WIDTH];

static const struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes;
} ili_init_cmds[] = {
    {0x01, {0}, 0x80},
    {0x3A, {0x55}, 1}, // Pixel Format Set RGB565
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
    {0x36, {(0x20 | 0x80 | 0x08)}, 1},    // Memory Access Control
    {0xB1, {0x00, 0x1B}, 2},  // Frame Rate Control (1B=70, 1F=61, 10=119)
    {0xB6, {0x0A, 0xA2}, 2},    // Display Function Control
    {0xF6, {0x01, 0x30}, 2},
    {0xF2, {0x00}, 1},    // 3Gamma Function Disable
    {0x26, {0x01}, 1},     //Gamma curve selected
    {0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 15},
    {0XE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 15},
    {0x11, {0}, 0x80},    //Exit Sleep
    {0x29, {0}, 0x80},    //Display on
};


static void ili_cmd(spi_device_handle_t spi, const uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,        // In bits
        .tx_buffer = &cmd,
        .user = (void*)0,   // DC line
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    assert(ret==ESP_OK);
}

static void ili_data(spi_device_handle_t spi, const void *data, int len)
{
    spi_transaction_t t = {
        .length = len * 8,  // In bits
        .tx_buffer = data,
        .user = (void*)1,   // DC Line
    };
    esp_err_t ret = spi_device_transmit(spi, &t);
    assert(ret==ESP_OK);
}

static void ili_spi_pre_transfer_callback(spi_transaction_t *t)
{
    gpio_set_level(LCD_PIN_NUM_DC, (int)t->user & 1);
}

void ili9341_writeLE(const uint16_t *buffer)
{
    const int left = 0;
    const int top = SCREEN_OFFSET_TOP;
    const int width = SCREEN_WIDTH;
    const int height = SCREEN_HEIGHT;

    uint8_t tx_data[4];

    tx_data[0] = (left) >> 8;              //Start Col High
    tx_data[1] = (left) & 0xff;              //Start Col Low
    tx_data[2] = (left + width - 1) >> 8;       //End Col High
    tx_data[3] = (left + width - 1) & 0xff;     //End Col Low
    ili_cmd(spi, 0x2A);
    ili_data(spi, tx_data, 4);

    tx_data[0] = top >> 8;        //Start page high
    tx_data[1] = top & 0xff;      //start page low
    tx_data[2] = (top + height - 1)>>8;    //end page high
    tx_data[3] = (top + height - 1)&0xff;  //end page low
    ili_cmd(spi, 0x2B);
    ili_data(spi, tx_data, 4);

    ili_cmd(spi, 0x2C);

    for (int y = 0; y < height; y++)
    {
        for (int i = 0; i < width; ++i)
        {
            uint16_t pixel = buffer[y * width + i];
            dma_buffer[i] = pixel << 8 | pixel >> 8;
        }
        ili_data(spi, dma_buffer, width * 2);
    }
}

void ili9341_deinit()
{
    spi_bus_remove_device(spi);
    gpio_reset_pin(LCD_PIN_NUM_DC);
    gpio_reset_pin(LCD_PIN_NUM_BCKL);
}

void ili9341_init()
{
    esp_err_t ret;

    //Initialize non-SPI GPIOs
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[LCD_PIN_NUM_DC], PIN_FUNC_GPIO);
    gpio_set_direction(LCD_PIN_NUM_BCKL, GPIO_MODE_OUTPUT);
    gpio_set_direction(LCD_PIN_NUM_DC, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_PIN_NUM_BCKL, 1);
    gpio_set_level(LCD_PIN_NUM_DC, 1);

    // Initialize SPI
    spi_bus_config_t buscfg = {
        .miso_io_num = LCD_PIN_NUM_MISO,
        .mosi_io_num = LCD_PIN_NUM_MOSI,
        .sclk_io_num = LCD_PIN_NUM_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_MASTER_FREQ_40M,
        .mode = 0,
        .spics_io_num = LCD_PIN_NUM_CS,
        .queue_size = 4,
        .pre_cb = ili_spi_pre_transfer_callback,
        .flags = SPI_DEVICE_NO_DUMMY,
    };

    //Initialize the SPI bus
    ret = spi_bus_initialize(HSPI_HOST, &buscfg, 1);
    //assert(ret==ESP_OK);

    //Attach the LCD to the SPI bus
    ret = spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
    assert(ret==ESP_OK);

    //Initialize the LCD
    ESP_LOGI(__func__, "LCD: ili_init.");
    for (int cmd = 0; cmd < sizeof(ili_init_cmds)/sizeof(ili_init_cmds[0]); cmd++)
    {
        size_t datalen = ili_init_cmds[cmd].databytes & 0x7f;
        ili_cmd(spi, ili_init_cmds[cmd].cmd);
        if (datalen > 0)
        {
            memcpy(dma_buffer, ili_init_cmds[cmd].data, datalen);
            ili_data(spi, dma_buffer, datalen);
        }
        if (ili_init_cmds[cmd].databytes & 0x80)
            vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(__func__, "LCD Initialized.");
}
