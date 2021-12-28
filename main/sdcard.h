#pragma once

#include <esp_err.h>

esp_err_t odroid_sdcard_open(const char* base_path);
esp_err_t odroid_sdcard_close(void);
esp_err_t odroid_sdcard_format(int fs_type);

int odroid_sdcard_files_get(const char* path, const char* extension, char*** filesOut);
void odroid_sdcard_files_free(char** files, int count);
