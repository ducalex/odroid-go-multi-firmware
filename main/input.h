#pragma once

#include <stdint.h>

enum
{
	ODROID_INPUT_UP = 0,
    ODROID_INPUT_RIGHT,
    ODROID_INPUT_DOWN,
    ODROID_INPUT_LEFT,
    ODROID_INPUT_SELECT,
    ODROID_INPUT_START,
    ODROID_INPUT_A,
    ODROID_INPUT_B,
    ODROID_INPUT_MENU,
    ODROID_INPUT_VOLUME,

	ODROID_INPUT_MAX
};

typedef struct
{
    uint8_t values[ODROID_INPUT_MAX];
} odroid_gamepad_state;

void input_init();
void input_read(odroid_gamepad_state* out_state);
int input_wait_for_button_press(int ticks);
odroid_gamepad_state input_read_raw();
