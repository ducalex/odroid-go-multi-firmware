#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/adc.h>
#include <driver/i2c.h>
#include <esp_log.h>

#include "input.h"

#ifdef TARGET_MRGC_G32
#define TRY(x) if ((err = (x)) != ESP_OK) { goto fail; }

static bool rg_i2c_read(uint8_t addr, int reg, void *read_data, size_t read_len)
{
    esp_err_t err = ESP_FAIL;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (reg >= 0)
    {
        TRY(i2c_master_start(cmd));
        TRY(i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true));
        TRY(i2c_master_write_byte(cmd, (uint8_t)reg, true));
    }
    TRY(i2c_master_start(cmd));
    TRY(i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true));
    TRY(i2c_master_read(cmd, read_data, read_len, I2C_MASTER_LAST_NACK));
    TRY(i2c_master_stop(cmd));
    TRY(i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(500)));
    i2c_cmd_link_delete(cmd);
    return true;

fail:
    ESP_LOGE(__func__, "Read from 0x%02x failed. reg=%d, err=0x%x\n", addr, reg, err);
    return false;
}
#else
#define ODROID_GAMEPAD_IO_X ADC1_CHANNEL_6
#define ODROID_GAMEPAD_IO_Y ADC1_CHANNEL_7
#define ODROID_GAMEPAD_IO_SELECT GPIO_NUM_27
#define ODROID_GAMEPAD_IO_START GPIO_NUM_39
#define ODROID_GAMEPAD_IO_A GPIO_NUM_32
#define ODROID_GAMEPAD_IO_B GPIO_NUM_33
#define ODROID_GAMEPAD_IO_MENU GPIO_NUM_13
#define ODROID_GAMEPAD_IO_VOLUME GPIO_NUM_0
#endif

static uint32_t gamepad_state = 0;


uint32_t input_read_raw(void)
{
    uint32_t state = 0;

#ifdef TARGET_MRGC_G32
    uint8_t data[5];
    if (rg_i2c_read(0x20, -1, &data, 5))
    {
        int buttons = ~((data[2] << 8) | data[1]);

        if (buttons & (1 << 2)) state |= (1 << ODROID_INPUT_UP);
        if (buttons & (1 << 3)) state |= (1 << ODROID_INPUT_DOWN);
        if (buttons & (1 << 4)) state |= (1 << ODROID_INPUT_LEFT);
        if (buttons & (1 << 5)) state |= (1 << ODROID_INPUT_RIGHT);
        if (buttons & (1 << 8)) state |= (1 << ODROID_INPUT_MENU);
        // if (buttons & (1 << 0)) state |= (1 << ODROID_INPUT_OPTION);
        if (buttons & (1 << 1)) state |= (1 << ODROID_INPUT_SELECT);
        if (buttons & (1 << 0)) state |= (1 << ODROID_INPUT_START);
        if (buttons & (1 << 6)) state |= (1 << ODROID_INPUT_A);
        if (buttons & (1 << 7)) state |= (1 << ODROID_INPUT_B);
    }
#else
    int joyX = adc1_get_raw(ODROID_GAMEPAD_IO_X);
    int joyY = adc1_get_raw(ODROID_GAMEPAD_IO_Y);

    if (joyX > 2048 + 1024)
        state |= (1 << ODROID_INPUT_LEFT);
    else if (joyX > 1024)
        state |= (1 << ODROID_INPUT_RIGHT);

    if (joyY > 2048 + 1024)
        state |= (1 << ODROID_INPUT_UP);
    else if (joyY > 1024)
        state |= (1 << ODROID_INPUT_DOWN);

    state |= (!gpio_get_level(ODROID_GAMEPAD_IO_SELECT)) ? (1 << ODROID_INPUT_SELECT) : 0;
    state |= (!gpio_get_level(ODROID_GAMEPAD_IO_START)) ? (1 << ODROID_INPUT_START) : 0;
    state |= (!gpio_get_level(ODROID_GAMEPAD_IO_A)) ? (1 << ODROID_INPUT_A) : 0;
    state |= (!gpio_get_level(ODROID_GAMEPAD_IO_B)) ? (1 << ODROID_INPUT_B) : 0;
    state |= (!gpio_get_level(ODROID_GAMEPAD_IO_MENU)) ? (1 << ODROID_INPUT_MENU) : 0;
    state |= (!gpio_get_level(ODROID_GAMEPAD_IO_VOLUME)) ? (1 << ODROID_INPUT_VOLUME) : 0;
#endif

    return state;
}

int input_wait_for_button_press(int ticks)
{
    uint32_t previousState = gamepad_state;
    uint32_t timeout = xTaskGetTickCount() + ticks;

    while (true)
    {
        uint32_t state = gamepad_state;

        for (int i = 0; i < ODROID_INPUT_MAX; i++)
        {
            if (!(previousState & (1 << i)) && (state & (1 << i))) {
                return i;
            }
        }

        if (ticks > 0 && timeout < xTaskGetTickCount()) {
            break;
        }

        previousState = state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return -1;
}

static void input_task(void *arg)
{
    uint8_t debounce[ODROID_INPUT_MAX];

    // Initialize state
    for (int i = 0; i < ODROID_INPUT_MAX; ++i)
    {
        debounce[i] = 0xff;
    }

    while (1)
    {
        // Read hardware
        uint32_t state = input_read_raw();

        // Debounce
        for (int i = 0; i < ODROID_INPUT_MAX; ++i)
        {
            debounce[i] <<= 1;
            debounce[i] |= (state >> i) & 1;
            switch (debounce[i] & 0x03)
            {
                case 0x00:
                    gamepad_state &= ~(1 << i);
                    break;

                case 0x03:
                    gamepad_state |= (1 << i);
                    break;

                default:
                    // ignore
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(NULL);
}

void input_init(void)
{
#ifdef TARGET_MRGC_G32
    const i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_NUM_21,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = GPIO_NUM_22,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 200000,
    };
    esp_err_t err = ESP_FAIL;

    TRY(i2c_param_config(I2C_NUM_0, &i2c_config));
    TRY(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    ESP_LOGI(__func__, "I2C driver ready (SDA:%d SCL:%d).\n", i2c_config.sda_io_num, i2c_config.scl_io_num);
    fail:
#else
    gpio_set_direction(ODROID_GAMEPAD_IO_SELECT, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ODROID_GAMEPAD_IO_SELECT, GPIO_PULLUP_ONLY);

    gpio_set_direction(ODROID_GAMEPAD_IO_START, GPIO_MODE_INPUT);

    gpio_set_direction(ODROID_GAMEPAD_IO_A, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ODROID_GAMEPAD_IO_A, GPIO_PULLUP_ONLY);

    gpio_set_direction(ODROID_GAMEPAD_IO_B, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ODROID_GAMEPAD_IO_B, GPIO_PULLUP_ONLY);

    adc1_config_width(ADC_WIDTH_12Bit);
    adc1_config_channel_atten(ODROID_GAMEPAD_IO_X, ADC_ATTEN_11db);
    adc1_config_channel_atten(ODROID_GAMEPAD_IO_Y, ADC_ATTEN_11db);

    gpio_set_direction(ODROID_GAMEPAD_IO_MENU, GPIO_MODE_INPUT);
    gpio_set_pull_mode(ODROID_GAMEPAD_IO_MENU, GPIO_PULLUP_ONLY);

    gpio_set_direction(ODROID_GAMEPAD_IO_VOLUME, GPIO_MODE_INPUT);
#endif

    // Start background polling
    xTaskCreatePinnedToCore(&input_task, "input_task", 1024 * 2, NULL, 5, NULL, 1);

    ESP_LOGI(__func__, "done.");
}
