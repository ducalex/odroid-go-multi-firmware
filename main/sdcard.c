#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <sdmmc_cmd.h>
#include <diskio.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include <dirent.h>
#include <strings.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "sdcard.h"

extern esp_err_t ff_diskio_get_drive(BYTE* out_pdrv);
extern void ff_diskio_register_sdmmc(unsigned char pdrv, sdmmc_card_t* card);

#ifdef CONFIG_TARGET_MRGC_G32
#define DECLARE_SDCARD_CONFIG() \
        sdmmc_host_t host_config = SDMMC_HOST_DEFAULT(); \
        host_config.flags = SDMMC_HOST_FLAG_1BIT; \
        host_config.max_freq_khz = SDMMC_FREQ_HIGHSPEED; \
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT(); \
        slot_config.width = 1;
#elif CONFIG_TARGET_ESPLAY_S3
#define DECLARE_SDCARD_CONFIG() \
		sdmmc_host_t host_config = SDMMC_HOST_DEFAULT(); \
        host_config.flags = SDMMC_HOST_FLAG_1BIT;  \
        host_config.max_freq_khz = SDMMC_FREQ_DEFAULT; \
        sdmmc_slot_config_t slot_config = { \
		.width = 1, .flags = 0, \
		.d0 = RG_GPIO_SDSPI_D0, .d1 = -1, .d2 = -1, .d3 = -1, .d4 = -1, .d5 = -1, .d6 = -1, .d7 = -1, \
		.clk = RG_GPIO_SDSPI_CLK, .cmd = RG_GPIO_SDSPI_CMD, .cd = -1, .wp = -1,}; \
        slot_config.width = 1;
#else
#define DECLARE_SDCARD_CONFIG() \
        sdmmc_host_t host_config = SDSPI_HOST_DEFAULT(); \
        host_config.slot = HSPI_HOST; \
        host_config.max_freq_khz = SDMMC_FREQ_DEFAULT; \
        sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT(); \
        slot_config.gpio_miso = GPIO_NUM_19; \
        slot_config.gpio_mosi = GPIO_NUM_23; \
        slot_config.gpio_sck  = GPIO_NUM_18; \
        slot_config.gpio_cs = GPIO_NUM_22; \
        //slot_config.dma_channel = 2;
#endif


inline static void swap(char** a, char** b)
{
    char* t = *a;
    *a = *b;
    *b = t;
}

static int strcicmp(char const *a, char const *b)
{
    for (;; a++, b++)
    {
        int d = tolower((int)*a) - tolower((int)*b);
        if (d != 0 || !*a) return d;
    }
}

static int partition (char* arr[], int low, int high)
{
    char* pivot = arr[high];
    int i = (low - 1);

    for (int j = low; j <= high- 1; j++)
    {
        if (strcicmp(arr[j], pivot) < 0)
        {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[high]);
    return (i + 1);
}

static void quick_sort(char* arr[], int low, int high)
{
    if (low < high)
    {
        int pi = partition(arr, low, high);

        quick_sort(arr, low, pi - 1);
        quick_sort(arr, pi + 1, high);
    }
}

static void sort_files(char** files, int count)
{
    if (count > 1)
    {
        quick_sort(files, 0, count - 1);
    }
}


int odroid_sdcard_files_get(const char* path, const char* extension, char*** filesOut)
{
    const int MAX_FILES = 1024;

    int count = 0;
    char** result = (char**)malloc(MAX_FILES * sizeof(void*));
    if (!result) abort();


    DIR *dir = opendir(path);
    if( dir == NULL )
    {
        ESP_LOGE(__func__, "opendir failed.");
        return 0;
    }

    int extensionLength = strlen(extension);
    if (extensionLength < 1) abort();

    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        size_t len = strlen(entry->d_name);

        if (len < extensionLength)
            continue;

        if (entry->d_name[0] == '.')
            continue;

        if (strcasecmp(extension, &entry->d_name[len - extensionLength]) != 0)
            continue;

        if (!(result[count++] = strdup(entry->d_name)))
            abort();

        if (count >= MAX_FILES)
            break;
    }

    closedir(dir);

    sort_files(result, count);

    *filesOut = result;
    return count;
}

void odroid_sdcard_files_free(char** files, int count)
{
    for (int i = 0; i < count; ++i)
    {
        free(files[i]);
    }

    free(files);
}

esp_err_t odroid_sdcard_open(void)
{
    DECLARE_SDCARD_CONFIG();
#if RG_SDSPI_HIGHSPEED == 1
	host_config.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#endif
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
#ifdef CONFIG_CONFIG_TARGET_ESPLAY_S3
		.allocation_unit_size = 32 * 1024
#endif
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SDCARD_BASE_PATH, &host_config, &slot_config, &mount_config, NULL);
#ifdef CONFIG_CONFIG_TARGET_ESPLAY_S3
	if (ret != ESP_OK){
		ESP_LOGE(__func__, "esp_vfs_fat_sdmmc_mount failed (%d)", ret);
	}
#else
    if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE)
    {
        ret = ESP_OK;
    }
    else
    {
        ESP_LOGE(__func__, "esp_vfs_fat_sdmmc_mount failed (%d)", ret);
    }
#endif
    return ret;
}

esp_err_t odroid_sdcard_close(void)
{
    esp_err_t ret = esp_vfs_fat_sdmmc_unmount();

    if (ret != ESP_OK)
    {
        ESP_LOGE(__func__, "esp_vfs_fat_sdmmc_unmount failed (%d)", ret);
    }

    return ret;
}

esp_err_t odroid_sdcard_format(int fs_type)
{
    esp_err_t err = ESP_FAIL;
    const char *errmsg = "success!";
    sdmmc_card_t card;
    void *buffer = malloc(4096);
    DWORD partitions[] = {100, 0, 0, 0};
    BYTE drive = 0xFF;

    DECLARE_SDCARD_CONFIG();

    if (buffer == NULL) {
        return false;
    }

    odroid_sdcard_close();

    err = ff_diskio_get_drive(&drive);
    if (drive == 0xFF) {
        errmsg = "ff_diskio_get_drive() failed";
        goto _cleanup;
    }

    err = (*host_config.init)();
    if (err != ESP_OK) {
        errmsg = "host_config.init() failed";
        goto _cleanup;
    }

#ifdef CONFIG_TARGET_MRGC_G32
    err = sdmmc_host_init_slot(host_config.slot, &slot_config);
#elif CONFIG_CONFIG_TARGET_ESPLAY_S3
	err = sdmmc_host_init_slot(host_config.slot,&slot_config);
#else
    err = sdspi_host_init_slot(host_config.slot, &slot_config);
#endif

    if (err != ESP_OK) {
        errmsg = "sdspi_host_init_slot() failed";
        goto _cleanup;
    }

    err = sdmmc_card_init(&host_config, &card);
    if (err != ESP_OK) {
        errmsg = "sdmmc_card_init() failed";
        goto _cleanup;
    }

    ff_diskio_register_sdmmc(drive, &card);

    ESP_LOGI(__func__, "partitioning card %d", drive);
    if (f_fdisk(drive, partitions, buffer) != FR_OK) {
        errmsg = "f_fdisk() failed";
        err = ESP_FAIL;
        goto _cleanup;
    }

    ESP_LOGI(__func__, "formatting card %d", drive);
    char path[3] = {(char)('0' + drive), ':', 0};
    if (f_mkfs(path, fs_type ? FM_EXFAT : FM_FAT32, 0, buffer, 4096) != FR_OK) {
        errmsg = "f_mkfs() failed";
        err = ESP_FAIL;
        goto _cleanup;
    }

    err = ESP_OK;

_cleanup:

    if (err == ESP_OK) {
        ESP_LOGI(__func__, "%s", errmsg);
    } else {
        ESP_LOGE(__func__, "%s (%d)", errmsg, err);
    }

    free(buffer);
    host_config.deinit();
    ff_diskio_register_sdmmc(drive, NULL);

    return err;
}
