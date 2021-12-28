#pragma once

#include "esp_err.h"

#define SDCARD_BASE_PATH "/sd"

esp_err_t odroid_sdcard_open(void);
esp_err_t odroid_sdcard_close(void);
esp_err_t odroid_sdcard_format(int fs_type);

int odroid_sdcard_files_get(const char* path, const char* extension, char*** filesOut);
void odroid_sdcard_files_free(char** files, int count);
