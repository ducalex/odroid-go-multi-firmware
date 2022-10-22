#include <freertos/FreeRTOS.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_adc_cal.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_flash_data_types.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <driver/gpio.h>
#include <driver/adc.h>

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 4
#include <esp32/rom/crc.h>
#else
#include <rom/crc.h>
#endif

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>

#include "sdcard.h"
#include "display.h"
#include "input.h"

#include "ugui/ugui.h"


#define MFW_NVS_PARTITION  "mfw_nvs"
#define MFW_DATA_PARTITION "mfw_data"

#ifndef PROJECT_VER
    #define PROJECT_VER "n/a"
#endif

#define APP_TABLE_MAGIC             0x1207
#define APP_NVS_SIZE                0x3000

#define FLASH_BLOCK_SIZE            (64 * 1024)
#define ERASE_BLOCK_SIZE            (4 * 1024)

#define LIST_SORT_OFFSET            0b0000
#define LIST_SORT_SEQUENCE          0b0010
#define LIST_SORT_DESCRIPTION       0b0100
#define LIST_SORT_MAX               0b0101
#define LIST_SORT_DIR_ASC           0b0000
#define LIST_SORT_DIR_DESC          0b0001

#define FIRMWARE_PARTS_MAX          (20)
#define FIRMWARE_TILE_WIDTH         (86)
#define FIRMWARE_TILE_HEIGHT        (48)

#define BATTERY_VMAX                (4.20f)
#define BATTERY_VMIN                (3.30f)

#define ITEM_COUNT                  ((RG_SCREEN_HEIGHT-32)/52)

#define ALIGN_ADDRESS(val, alignment) (((val & (alignment-1)) != 0) ? (val & ~(alignment-1)) + alignment : val)
#define SET_STATUS_LED(on) gpio_set_level(GPIO_NUM_2, on);
#define RG_MIN(a, b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a < _b ? _a : _b; })
#define RG_MAX(a, b) ({__typeof__(a) _a = (a); __typeof__(b) _b = (b);_a > _b ? _a : _b; })

#ifdef TARGET_MRGC_G32
#define HEADER_LENGTH 22
#define HEADER_V00_01 "ESPLAY_FIRMWARE_V00_01"
#define FIRMWARE_PATH SDCARD_BASE_PATH "/espgbc/firmware"
#else
#define HEADER_LENGTH 24
#define HEADER_V00_01 "ODROIDGO_FIRMWARE_V00_01"
#define FIRMWARE_PATH SDCARD_BASE_PATH "/odroid/firmware"
#endif

typedef struct
{
    uint8_t type;
    uint8_t subtype;
    uint8_t _reserved0;
    uint8_t _reserved1;
    char     label[16];
    uint32_t flags;
    uint32_t length;
    uint32_t dataLength;
} odroid_partition_t; // __attribute__((packed))

typedef struct
{
    uint16_t magic;
    uint16_t flags;
    uint32_t startOffset;
    uint32_t endOffset;
    char     description[40];
    char     filename[40];
    uint16_t tile[FIRMWARE_TILE_WIDTH * FIRMWARE_TILE_HEIGHT];
    odroid_partition_t parts[FIRMWARE_PARTS_MAX];
    uint8_t parts_count;
    uint8_t _reserved0;
    uint16_t installSeq;
} odroid_app_t;

typedef struct
{
    struct __attribute__((packed))
    {
        char version[HEADER_LENGTH];
        char description[40];
        uint16_t tile[FIRMWARE_TILE_WIDTH * FIRMWARE_TILE_HEIGHT];
    } header;
    odroid_partition_t parts[FIRMWARE_PARTS_MAX];
    uint8_t parts_count;
    size_t flashSize;
    size_t fileSize;
    size_t dataOffset;
    uint32_t checksum;
} odroid_fw_t;

typedef struct
{
    size_t offset;
    size_t size;
} odroid_flash_block_t;

typedef struct
{
    long id;
    char label[32];
    bool enabled;
} dialog_option_t;

static odroid_app_t *apps;
static int apps_count = -1;
static int apps_max = 4;
static int apps_seq = 0;
static int firstAppOffset = 0x100000; // We scan the table to find the real value but this is a reasonable default
static uint16_t fb[RG_SCREEN_WIDTH * RG_SCREEN_HEIGHT];
static UG_GUI gui;
static esp_err_t sdcardret;
static nvs_handle nvs_h;

static float read_battery(void);

static void pset(UG_S16 x, UG_S16 y, UG_COLOR color)
{
    fb[y * RG_SCREEN_WIDTH + x] = color;
}

static void UpdateDisplay(void)
{
    ili9341_writeLE(fb);
}

static void DisplayCenter(int top, const char *str)
{
    const int maxlen = RG_SCREEN_WIDTH / (gui.font.char_width + 1);
    int left = RG_MAX(0, (RG_SCREEN_WIDTH - strlen(str) * (gui.font.char_width + 1)) / 2);
    int height = gui.font.char_height + 4 + 4;
    char tempstring[128] = {0};

    UG_FillFrame(0, top, RG_SCREEN_WIDTH-1, top + height - 1, UG_GetBackcolor());
    UG_PutString(left, top + 4 , strncpy(tempstring, str, maxlen));
}

static void DisplayPage(const char *title, const char *footer)
{
    UG_FillFrame(0, 0, RG_SCREEN_WIDTH-1, RG_SCREEN_HEIGHT-1, C_WHITE);
    UG_FontSelect(&FONT_8X8);
    UG_SetBackcolor(C_MIDNIGHT_BLUE);
    UG_SetForecolor(C_WHITE);
    DisplayCenter(0, title);
    UG_SetForecolor(C_LIGHT_GRAY);
    DisplayCenter(RG_SCREEN_HEIGHT - 16, footer);
}

static void DisplayIndicators(int page, int totalPages)
{
    char tempstring[128];

    UG_FontSelect(&FONT_8X8);
    UG_SetForecolor(0x8C51);

    // Page indicator
    sprintf(tempstring, "%d/%d", page, totalPages);
    UG_PutString(4, 4, tempstring);

    // Battery indicator
    int percent = (read_battery() - BATTERY_VMIN) / (BATTERY_VMAX - BATTERY_VMIN) * 100.f;
    sprintf(tempstring, "%d%%", RG_MIN(100, RG_MAX(0, percent)));
    UG_PutString(RG_SCREEN_WIDTH - (9 * strlen(tempstring)) - 4, 4, tempstring);
}

static void DisplayError(const char *message)
{
    UG_FontSelect(&FONT_8X12);
    UG_SetForecolor(C_RED);
    UG_SetBackcolor(C_WHITE);
    DisplayCenter((RG_SCREEN_HEIGHT / 2) - (12 / 2), message);
    UpdateDisplay();
}

static void DisplayMessage(const char *message)
{
    UG_FontSelect(&FONT_8X12);
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    DisplayCenter((RG_SCREEN_HEIGHT / 2) + 8 + (12 / 2) + 16, message);
    UpdateDisplay();
}

static void DisplayNotification(const char *message)
{
    UG_FontSelect(&FONT_8X8);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLUE);
    DisplayCenter(RG_SCREEN_HEIGHT - 16, message);
    UpdateDisplay();
}

static void DisplayProgress(int percent)
{
    const int WIDTH = 200;
    const int HEIGHT = 12;
    const int FILL_WIDTH = WIDTH * ((percent > 100 ? 100 : percent) / 100.0f);
    int left = (RG_SCREEN_WIDTH / 2) - (WIDTH / 2);
    int top = (RG_SCREEN_HEIGHT / 2) - (HEIGHT / 2) + 16;
    UG_FillFrame(left - 1, top - 1, left + WIDTH + 1, top + HEIGHT + 1, C_WHITE);
    UG_DrawFrame(left - 1, top - 1, left + WIDTH + 1, top + HEIGHT + 1, C_BLACK);
    if (FILL_WIDTH > 0)
        UG_FillFrame(left, top, left + FILL_WIDTH, top + HEIGHT, C_GREEN);
}

static void DisplayFooter(const char *message)
{
    int left = (RG_SCREEN_WIDTH / 2) - (strlen(message) * 9 / 2);
    int top = RG_SCREEN_HEIGHT - (16 * 2) - 8;
    UG_FontSelect(&FONT_8X12);
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, RG_SCREEN_WIDTH-1, top + 12, C_WHITE);
    UG_PutString(left, top, message);
}

static void DisplayHeader(const char *message)
{
    int left = (RG_SCREEN_WIDTH / 2) - (strlen(message) * 9 / 2);
    int top = (16 + 8);
    UG_FontSelect(&FONT_8X12);
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, RG_SCREEN_WIDTH-1, top + 12, C_WHITE);
    UG_PutString(left, top, message);
}

static void DisplayRow(int line, const char *line1, const char *line2, uint16_t color, const uint16_t *tile, bool selected)
{
    const int margin = RG_SCREEN_WIDTH > 240 ? 6 : 2;
    const int itemHeight = 52;
    const int textLeft = margin + FIRMWARE_TILE_WIDTH + margin;
    const int top = 16 + (line * itemHeight) - 1;

    UG_FontSelect(&FONT_8X12);
    UG_SetBackcolor(selected ? C_YELLOW : C_WHITE);
    UG_FillFrame(0, top + 2, RG_SCREEN_WIDTH-1, top + itemHeight - 1 - 1, UG_GetBackcolor());
    UG_SetForecolor(C_BLACK);
    UG_PutString(textLeft, top + 2 + 2 + 7, line1);
    UG_SetForecolor(color);
    UG_PutString(textLeft, top + 2 + 2 + 23, line2);

    if (tile) // Draw Tile at the end
    {
        for (int i = 0 ; i < FIRMWARE_TILE_HEIGHT; ++i)
            for (int j = 0; j < FIRMWARE_TILE_WIDTH; ++j)
                UG_DrawPixel(margin + j, top + 2 + i, tile[i * FIRMWARE_TILE_WIDTH + j]);
    }
}

//---------------
static float read_battery(void)
{
    static esp_adc_cal_characteristics_t adc_cal;
    static float batteryVoltage = -1;

    if (batteryVoltage < 0)
    {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
        esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_cal);
        batteryVoltage = esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_0), &adc_cal) * 2.f / 1000.f;
    }

    batteryVoltage += esp_adc_cal_raw_to_voltage(adc1_get_raw(ADC1_CHANNEL_0), &adc_cal) * 2.f / 1000.f;
    batteryVoltage /= 2;

    return batteryVoltage;
}

static void panic_abort(const char *reason)
{
    ESP_LOGE(__func__, "Panic: %s", reason);
    DisplayError(reason);
    int level = 0;
    while (true) {
        SET_STATUS_LED(level);
        level = !level;
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

static void *safe_alloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr)
        panic_abort("MEMORY ALLOCATION ERROR");
    return ptr;
}

static void cleanup_and_restart(void)
{
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_INPUT);
    odroid_sdcard_close();
    nvs_close(nvs_h);
    nvs_flash_deinit_partition(MFW_NVS_PARTITION);
    ili9341_writeLE(memset(fb, 0, sizeof(fb)));
    ili9341_deinit();
    esp_restart();
}


static int sort_app_table_by_offset(const void * a, const void * b)
{
    if ( (*(odroid_app_t*)a).startOffset < (*(odroid_app_t*)b).startOffset ) return -1;
    if ( (*(odroid_app_t*)a).startOffset > (*(odroid_app_t*)b).startOffset ) return 1;
    return 0;
}

static int sort_app_table_by_sequence(const void * a, const void * b)
{
    return (*(odroid_app_t*)a).installSeq - (*(odroid_app_t*)b).installSeq;
}

static int sort_app_table_by_alphabet(const void * a, const void * b)
{
    return strcasecmp((*(odroid_app_t*)a).description, (*(odroid_app_t*)b).description);
}

static void sort_app_table(int newMode)
{
    switch(newMode & ~1) {
        case LIST_SORT_SEQUENCE:
            qsort(apps, apps_count, sizeof(odroid_app_t), &sort_app_table_by_sequence);
            break;
        case LIST_SORT_DESCRIPTION:
            qsort(apps, apps_count, sizeof(odroid_app_t), &sort_app_table_by_alphabet);
            break;
        case LIST_SORT_OFFSET:
        default:
            qsort(apps, apps_count, sizeof(odroid_app_t), &sort_app_table_by_offset);
            break;
    }

    if (newMode & 1) { // Reverse array. Very inefficient.
        odroid_app_t *tmp = safe_alloc(sizeof(odroid_app_t));
        int i = apps_count - 1, j = 0;
        while (i > j)
        {
            memcpy(tmp, &apps[i], sizeof(odroid_app_t));
            memcpy(&apps[i], &apps[j], sizeof(odroid_app_t));
            memcpy(&apps[j], tmp, sizeof(odroid_app_t));
            i--;
            j++;
        }
        free(tmp);
    }
}


static void read_app_table(void)
{
    const esp_partition_t *app_table_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, MFW_DATA_PARTITION);

    if (!app_table_part)
    {
        panic_abort("NO APP TABLE ERROR");
    }
    else if (!apps)
    {
        apps = safe_alloc(app_table_part->size);
    }

    apps_max = (app_table_part->size / sizeof(odroid_app_t));
    apps_count = 0;
    apps_seq = 0;

    if (esp_partition_read(app_table_part, 0, apps, app_table_part->size) != ESP_OK)
    {
        panic_abort("APP TABLE READ ERROR");
    }

    for (int i = 0; i < apps_max; i++)
    {
        if (apps[i].magic != APP_TABLE_MAGIC)
            break;
        if (apps[i].installSeq >= apps_seq)
            apps_seq = apps[i].installSeq + 1;
        apps_count++;
    }

    //64K align the address (https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/partition-tables.html#offset-size)
    firstAppOffset = app_table_part->address + app_table_part->size;
    firstAppOffset = ALIGN_ADDRESS(firstAppOffset, FLASH_BLOCK_SIZE);

    ESP_LOGI(__func__, "Read app table (%d apps)", apps_count);
}


static void write_app_table()
{
    const esp_partition_t *app_table_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, MFW_DATA_PARTITION);

    if (!apps || !app_table_part)
    {
        panic_abort("NO APP TABLE ERROR");
    }

    for (int i = apps_count; i < apps_max; ++i)
    {
        memset(&apps[i], 0xff, sizeof(odroid_app_t));
    }

    if (esp_partition_erase_range(app_table_part, 0, app_table_part->size) != ESP_OK)
    {
        panic_abort("APP TABLE ERASE ERROR");
    }

    if (esp_partition_write(app_table_part, 0, apps, app_table_part->size) != ESP_OK)
    {
        panic_abort("APP TABLE WRITE ERROR");
    }

    ESP_LOGI(__func__, "Written app table (%d apps)", apps_count);
}


static void write_partition_table(const odroid_app_t *app)
{
    esp_partition_info_t partitionTable[ESP_PARTITION_TABLE_MAX_ENTRIES];
    size_t nextPart = 0;

    if (spi_flash_read(ESP_PARTITION_TABLE_OFFSET, &partitionTable, sizeof(partitionTable)) != ESP_OK)
    {
        panic_abort("PART TABLE READ ERROR");
    }

    // Keep only the valid system partitions
    for (int i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; ++i)
    {
        esp_partition_info_t *part = &partitionTable[i];
        if (part->magic == 0xFFFF)
            break;
        if (part->magic != ESP_PARTITION_MAGIC)
            continue;
        if (part->pos.offset >= firstAppOffset)
            continue;
        partitionTable[nextPart++] = *part;
        ESP_LOGI(__func__, "Keeping partition #%d '%s'", nextPart - 1, part->label);
    }

    // Append app's partitions, if any
    if (app)
    {
        size_t flashOffset = app->startOffset;

        for (int i = 0; i < app->parts_count && i < ESP_PARTITION_TABLE_MAX_ENTRIES; ++i)
        {
            esp_partition_info_t* part = &partitionTable[nextPart++];
            part->magic = ESP_PARTITION_MAGIC;
            part->type = app->parts[i].type;
            part->subtype = app->parts[i].subtype;
            part->pos.offset = flashOffset;
            part->pos.size = app->parts[i].length;
            memcpy(&part->label, app->parts[i].label, 16);
            part->flags = app->parts[i].flags;

            flashOffset += app->parts[i].length;

            ESP_LOGI(__func__, "Added partition #%d '%s'", nextPart - 1, part->label);
        }
    }

    // We must fill the rest with 0xFF, the boot loader checks magic = 0xFFFF, type = 0xFF, subtype = 0xFF
    while (nextPart < ESP_PARTITION_TABLE_MAX_ENTRIES)
    {
        memset(&partitionTable[nextPart++], 0xFF, sizeof(esp_partition_info_t));
    }

    if (spi_flash_erase_range(ESP_PARTITION_TABLE_OFFSET, ERASE_BLOCK_SIZE) != ESP_OK)
    {
        panic_abort("PART TABLE ERASE ERROR");
    }

    if (spi_flash_write(ESP_PARTITION_TABLE_OFFSET, partitionTable, sizeof(partitionTable)) != ESP_OK)
    {
        panic_abort("PART TABLE WRITE ERROR");
    }

    // esp_partition_reload_table();
}


static void boot_application(odroid_app_t *app)
{
    ESP_LOGI(__func__, "Booting application.");

    if (app)
    {
        DisplayMessage("Updating partitions ...");
        write_partition_table(app);
    }

    DisplayMessage("Setting boot partition ...");

// This is the correct way of doing things, but we must patch esp-idf to allow us to reload the partition table
// So, now, instead we just write the OTA partition ourselves. It is always the same (boot app OTA+0).
#if 0
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);

    if (!partition)
    {
        DisplayError("NO BOOT PART ERROR");
        panic_abort();
    }

    if (esp_ota_set_boot_partition(partition) != ESP_OK)
    {
        DisplayError("BOOT SET ERROR");
        panic_abort();
    }
#else
    uint32_t ota_data[8] = {1, 0, 0, 0, 0, 0, 0xFFFFFFFFU, 0x4743989A};

    if (spi_flash_erase_range(0xD000, 0x1000) != ESP_OK
        || spi_flash_write(0xD000, &ota_data, sizeof(ota_data)) != ESP_OK)
    {
        panic_abort("BOOT SET ERROR");
    }
#endif

    cleanup_and_restart();
}


static void defrag_flash(void)
{
    size_t nextStartOffset = firstAppOffset;
    size_t totalBytesToMove = 0;
    size_t totalBytesMoved = 0;
    char tempstring[128];

    sort_app_table(LIST_SORT_OFFSET);

    // First loop to get total for the progress bar
    for (int i = 0; i < apps_count; i++)
    {
        if (apps[i].startOffset > nextStartOffset)
        {
            totalBytesToMove += (apps[i].endOffset - apps[i].startOffset);
        } else {
            nextStartOffset = apps[i].endOffset + 1;
        }
    }

    sprintf(tempstring, "Moving: %.2f MB", (float)totalBytesToMove / 1024 / 1024);
    DisplayPage("Defragmenting flash", tempstring);
    DisplayHeader("Making some space...");
    UpdateDisplay();

    void *dataBuffer = safe_alloc(FLASH_BLOCK_SIZE);

    for (int i = 0; i < apps_count; i++)
    {
        if (apps[i].startOffset > nextStartOffset)
        {
            SET_STATUS_LED(1);

            size_t app_size = apps[i].endOffset - apps[i].startOffset;
            size_t newOffset = nextStartOffset, oldOffset = apps[i].startOffset;
            // move
            for (size_t i = 0; i < app_size; i += FLASH_BLOCK_SIZE)
            {
                ESP_LOGI(__func__, "Moving 0x%x to 0x%x", oldOffset + i, newOffset + i);

                DisplayMessage("Defragmenting ... (E)");
                spi_flash_erase_range(newOffset + i, FLASH_BLOCK_SIZE);

                DisplayMessage("Defragmenting ... (R)");
                spi_flash_read(oldOffset + i, dataBuffer, FLASH_BLOCK_SIZE);

                DisplayMessage("Defragmenting ... (W)");
                spi_flash_write(newOffset + i, dataBuffer, FLASH_BLOCK_SIZE);

                totalBytesMoved += FLASH_BLOCK_SIZE;

                DisplayProgress((float) totalBytesMoved / totalBytesToMove  * 100.0);
            }

            apps[i].startOffset = newOffset;
            apps[i].endOffset = newOffset + app_size;

            SET_STATUS_LED(0);
        }

        nextStartOffset = apps[i].endOffset + 1;
    }

    free(dataBuffer);

    write_app_table();
}


static void find_free_blocks(odroid_flash_block_t **blocks, size_t *count, size_t *totalFreeSpace)
{
    size_t flashSize = spi_flash_get_chip_size();
    size_t previousBlockEnd = firstAppOffset;

    *blocks = safe_alloc(sizeof(odroid_flash_block_t) * 32);
    *totalFreeSpace = 0;
    *count = 0;

    sort_app_table(LIST_SORT_OFFSET);

    for (int i = 0; i < apps_count; i++)
    {
        size_t free_space = apps[i].startOffset - previousBlockEnd;

        if (free_space > 0) {
            odroid_flash_block_t *block = &(*blocks)[(*count)++];
            block->offset = previousBlockEnd;
            block->size = free_space;
            *totalFreeSpace += block->size;
            ESP_LOGI(__func__, "Found free block: %d 0x%x %d", i, block->offset, free_space / 1024);
        }

        previousBlockEnd = apps[i].endOffset + 1;
    }

    if (((int)flashSize - previousBlockEnd) > 0) {
        odroid_flash_block_t *block = &(*blocks)[(*count)++];
        block->offset = previousBlockEnd;
        block->size = (flashSize - previousBlockEnd);
        *totalFreeSpace += block->size;
        ESP_LOGI(__func__, "Found free block: end 0x%x %d", block->offset, block->size / 1024);
    }
}

static int find_free_block(size_t size, bool defragIfNeeded)
{
    odroid_flash_block_t *blocks;
    size_t count, totalFreeSpace;

    find_free_blocks(&blocks, &count, &totalFreeSpace);

    int result = -1;

    for (int i = 0; i < count; i++)
    {
        if (blocks[i].size >= size) {
            result = blocks[i].offset;
            break;
        }
    }

    if (result < 0 && totalFreeSpace >= size) {
        defrag_flash();
        result = find_free_block(size, false);
    }

    free(blocks);
    return result;
}


static odroid_fw_t *firmware_get_info(const char *filename)
{
    odroid_fw_t *outData = safe_alloc(sizeof(odroid_fw_t));

    FILE* file = fopen(filename, "rb");
    if (!file)
        goto firmware_get_info_err;

    size_t file_size;

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (!fread(&outData->header, sizeof(outData->header), 1, file))
    {
        goto firmware_get_info_err;
    }

    if (memcmp(HEADER_V00_01, outData->header.version, HEADER_LENGTH) != 0)
    {
        goto firmware_get_info_err;
    }

    outData->header.description[sizeof(outData->header.description) - 1] = 0;
    outData->parts_count = 0;
    outData->flashSize = 0;
    outData->dataOffset = ftell(file);
    outData->fileSize = file_size;

    while (ftell(file) < (file_size - 4))
    {
        // Partition information
        odroid_partition_t *part = &outData->parts[outData->parts_count];

        if (fread(part, sizeof(odroid_partition_t), 1, file) != 1)
            goto firmware_get_info_err;

        // Check if dataLength is valid
        if (ftell(file) + part->dataLength > file_size || part->dataLength > part->length)
            goto firmware_get_info_err;

        // Check partition subtype
        if (part->type == 0xff)
            goto firmware_get_info_err;

        // 4KB align the partition length, this is needed for erasing
        part->length = ALIGN_ADDRESS(part->length, ERASE_BLOCK_SIZE);

        outData->flashSize += part->length;
        outData->parts_count++;

        fseek(file, part->dataLength, SEEK_CUR);
    }

    if (outData->parts_count >= FIRMWARE_PARTS_MAX)
        goto firmware_get_info_err;

    fseek(file, file_size - sizeof(outData->checksum), SEEK_SET);
    fread(&outData->checksum, sizeof(outData->checksum), 1, file);

    // We try to steal some unused space if possible, otherwise we might waste up to 48K
    odroid_partition_t *part = &outData->parts[outData->parts_count - 1];
    if (part->type == ESP_PARTITION_TYPE_APP && (part->length - part->dataLength) >= APP_NVS_SIZE) {
        ESP_LOGI(__func__, "Found room for NVS partition, reducing last partition size by %d", APP_NVS_SIZE);
        part->length -= APP_NVS_SIZE;
        outData->flashSize -= APP_NVS_SIZE;
    }
    // Add an application-specific NVS partition.
    odroid_partition_t *nvs_part = &outData->parts[outData->parts_count];
    strcpy(nvs_part->label, "nvs");
    nvs_part->dataLength = 0;
    nvs_part->length = APP_NVS_SIZE;
    nvs_part->type = ESP_PARTITION_TYPE_DATA;
    nvs_part->subtype = ESP_PARTITION_SUBTYPE_DATA_NVS;
    outData->flashSize += nvs_part->length;
    outData->parts_count++;

    fclose(file);
    return outData;

firmware_get_info_err:
    free(outData);
    fclose(file);
    return NULL;
}


static void flash_firmware(const char *fullPath)
{
    odroid_app_t *app = memset(&apps[apps_count], 0x00, sizeof(*app));
    odroid_fw_t *fw = firmware_get_info(fullPath);
    void *dataBuffer = safe_alloc(FLASH_BLOCK_SIZE);
    char tempstring[128];

    ESP_LOGI(__func__, "Flashing file: %s", fullPath);

    sort_app_table(LIST_SORT_OFFSET);
    DisplayPage("Install Application", "Destination: Pending");
    DisplayFooter("[B] Go Back");
    UpdateDisplay();
    SET_STATUS_LED(0);

    if (!fw)
    {
        DisplayError("INVALID FIRMWARE FILE"); // To do: Make it show what is invalid
        while (input_wait_for_button_press(-1) != ODROID_INPUT_B);
        free(dataBuffer), free(fw);
        return;
    }

    int currentFlashAddress = find_free_block(fw->flashSize, true);
    if (currentFlashAddress == -1)
    {
        DisplayError("NOT ENOUGH FREE SPACE");
        while (input_wait_for_button_press(-1) != ODROID_INPUT_B);
        free(dataBuffer), free(fw);
        return;
    }

    strncpy(app->description, fw->header.description, sizeof(app->description)-1);
    strncpy(app->filename, strrchr(fullPath, '/'), sizeof(app->filename)-1);
    memcpy(app->tile, fw->header.tile, sizeof(app->tile));
    memcpy(app->parts, fw->parts, sizeof(app->parts));
    app->parts_count = fw->parts_count;

    ESP_LOGI(__func__, "Destination: 0x%x", currentFlashAddress);
    ESP_LOGI(__func__, "Description: '%s'", app->description);

    sprintf(tempstring, "Destination: 0x%x", currentFlashAddress);
    DisplayPage("Install Application", tempstring);
    DisplayHeader(app->description);
    DisplayMessage("[START]");
    DisplayFooter("[B] Cancel");

    int tileLeft = (RG_SCREEN_WIDTH / 2) - (FIRMWARE_TILE_WIDTH / 2);
    int tileTop = (16 + 16 + 16);

    for (int i = 0 ; i < FIRMWARE_TILE_HEIGHT; ++i)
        for (int j = 0; j < FIRMWARE_TILE_WIDTH; ++j)
            UG_DrawPixel(tileLeft + j, tileTop + i, app->tile[i * FIRMWARE_TILE_WIDTH + j]);

    UG_DrawFrame(tileLeft - 1, tileTop - 1, tileLeft + FIRMWARE_TILE_WIDTH, tileTop + FIRMWARE_TILE_HEIGHT, C_BLACK);
    UpdateDisplay();

    while (1)
    {
        int btn = input_wait_for_button_press(-1);
        if (btn == ODROID_INPUT_START) break;
        if (btn == ODROID_INPUT_B) return;
    }

    DisplayMessage("Verifying ...");
    DisplayFooter("");
    UpdateDisplay();

    SET_STATUS_LED(1);

    FILE *file = fopen(fullPath, "rb");
    if (file == NULL)
    {
        panic_abort("FILE OPEN ERROR");
    }

    uint32_t checksum = 0;
    while (true)
    {
        size_t count = fread(dataBuffer, 1, FLASH_BLOCK_SIZE, file);
        if (ftell(file) == fw->fileSize)
        {
            count -= 4;
        }

        checksum = crc32_le(checksum, dataBuffer, count);

        if (count < FLASH_BLOCK_SIZE) break;
    }

    if (checksum != fw->checksum)
    {
        ESP_LOGE(__func__, "Checksum mismatch: expected: %#010x, computed:%#010x", fw->checksum, checksum);
        panic_abort("CHECKSUM MISMATCH ERROR");
    }
    ESP_LOGI(__func__, "Checksum OK: %#010x", checksum);

    // restore location to end of description
    fseek(file, fw->dataOffset, SEEK_SET);

    app->magic = APP_TABLE_MAGIC;
    app->startOffset = currentFlashAddress;

    // Copy the firmware
    for (int i = 0; i < app->parts_count; i++)
    {
        odroid_partition_t *slot = &app->parts[i];

        // Skip header, firmware_get_info prepared everything for us
        fseek(file, sizeof(odroid_partition_t), SEEK_CUR);

        SET_STATUS_LED(0);

        // Erase target partition space
        sprintf(tempstring, "Erasing ... (%d/%d)", i+1, app->parts_count);
        ESP_LOGI(__func__, "%s", tempstring);

        DisplayProgress(0);
        DisplayMessage(tempstring);

        int eraseBlocks = slot->length / ERASE_BLOCK_SIZE;
        if (eraseBlocks * ERASE_BLOCK_SIZE < slot->length) ++eraseBlocks;

        if (spi_flash_erase_range(currentFlashAddress, eraseBlocks * ERASE_BLOCK_SIZE) != ESP_OK)
        {
            ESP_LOGE(__func__, "spi_flash_erase_range failed. eraseBlocks=%d", eraseBlocks);
            panic_abort("ERASE ERROR");
        }

        if (slot->dataLength > 0)
        {
            size_t nextEntry = ftell(file) + slot->dataLength;

            SET_STATUS_LED(1);

            // Write data
            int totalCount = 0;
            for (int offset = 0; offset < slot->dataLength; offset += FLASH_BLOCK_SIZE)
            {
                sprintf(tempstring, "Writing (%d/%d)", i+1, app->parts_count);
                ESP_LOGI(__func__, "%s", tempstring);
                DisplayProgress((float)offset / (float)(slot->dataLength - FLASH_BLOCK_SIZE) * 100.0f);
                DisplayMessage(tempstring);

                // read
                size_t count = fread(dataBuffer, 1, FLASH_BLOCK_SIZE, file);
                if (count <= 0)
                {
                    panic_abort("DATA READ ERROR");
                }

                if (offset + count >= slot->dataLength)
                {
                    count = slot->dataLength - offset;
                }

                // flash
                if (spi_flash_write(currentFlashAddress + offset, dataBuffer, count) != ESP_OK)
        		{
        			ESP_LOGE(__func__, "spi_flash_write failed. address=%#08x", currentFlashAddress + offset);
                    panic_abort("WRITE ERROR");
        		}

                totalCount += count;
            }

            SET_STATUS_LED(0);

            if (totalCount != slot->dataLength)
            {
                ESP_LOGE(__func__, "Size mismatch: length=%#08x, totalCount=%#08x", slot->dataLength, totalCount);
                panic_abort("DATA SIZE ERROR");
            }

            fseek(file, nextEntry, SEEK_SET);
            // TODO: verify
        }

        // Notify OK
        ESP_LOGI(__func__, "Partition(%d): OK. Length=%#08x", i, slot->length);
        currentFlashAddress += slot->length;
    }

    fclose(file);
    free(fw);
    free(dataBuffer);

    // 64K align our endOffset
    app->endOffset = ALIGN_ADDRESS(currentFlashAddress, FLASH_BLOCK_SIZE) - 1;

    // Remember the install order, for display sorting
    app->installSeq = apps_seq++;

    // Write app table
    apps_count++; // Everything went well, acknowledge the new app
    write_app_table();

    DisplayMessage("Ready !");
    DisplayFooter("[B] Go Back  |  [A] Boot");
    UpdateDisplay();

    while (1)
    {
        int btn = input_wait_for_button_press(-1);
        if (btn == ODROID_INPUT_START) break;
        if (btn == ODROID_INPUT_A) break;
        if (btn == ODROID_INPUT_B) return;
    }

    boot_application(app);
}


static char *ui_choose_file(const char *path)
{
    char tempstring[128];

    // Check SD card
    if (sdcardret != ESP_OK)
    {
        DisplayPage("Error", "Error");
        DisplayError("SD CARD ERROR");
        vTaskDelay(200);
        return NULL;
    }

    char **files = NULL;
    char *result = NULL;
    int fileCount = odroid_sdcard_files_get(path, ".fw", &files);
    int currentItem = 0;

    ESP_LOGI(__func__, "fileCount=%d", fileCount);

    while (true)
    {
        int page = (currentItem / ITEM_COUNT) * ITEM_COUNT;
        size_t count, totalFreeSpace;
        odroid_flash_block_t *blocks;

        find_free_blocks(&blocks, &count, &totalFreeSpace);
        free(blocks);

        sprintf(tempstring, "Free space: %.2fMB (%d block)", (double)totalFreeSpace / 1024 / 1024, count);

        DisplayPage("Select a file", tempstring);
        DisplayIndicators(page / ITEM_COUNT + 1, (int)ceil((double)fileCount / ITEM_COUNT));

        for (int line = 0; line < ITEM_COUNT && (page + line) < fileCount; ++line)
        {
            char *fileName = files[page + line];
            bool selected = (page + line) == currentItem;

            sprintf(tempstring, "%s/%s", FIRMWARE_PATH, fileName);

            odroid_fw_t *fw = firmware_get_info(tempstring);
            if (fw) {
                sprintf(tempstring, "%.2f MB", (float)fw->flashSize / 1024 / 1024);
                DisplayRow(line, fileName, tempstring, C_GRAY, fw->header.tile, selected);
            } else {
                DisplayRow(line, fileName, "Invalid firmware", C_RED, NULL, selected);
            }
            free(fw);
        }

        if (fileCount == 0)
            DisplayMessage("SD Card Empty");

        UpdateDisplay();

        // Wait for input but refresh display after 1000 ticks if no input
        int btn = input_wait_for_button_press(1000);

        if (fileCount > 0)
        {
            if (btn == ODROID_INPUT_DOWN)
            {
                if (++currentItem >= fileCount) currentItem = 0;
            }
            else if (btn == ODROID_INPUT_UP)
            {
                if (--currentItem < 0) currentItem = fileCount - 1;
            }
            else if (btn == ODROID_INPUT_RIGHT)
            {
                if (page + ITEM_COUNT < fileCount) currentItem = page + ITEM_COUNT;
                else currentItem = 0;
            }
            else if (btn == ODROID_INPUT_LEFT)
            {
                if (page - ITEM_COUNT >= 0) currentItem = page - ITEM_COUNT;
                else currentItem = (fileCount - 1) / ITEM_COUNT * ITEM_COUNT;
            }
            else if (btn == ODROID_INPUT_A)
            {
                size_t fullPathLength = strlen(path) + 1 + strlen(files[currentItem]) + 1;
                char *fullPath = safe_alloc(fullPathLength);

                strcpy(fullPath, path);
                strcat(fullPath, "/");
                strcat(fullPath, files[currentItem]);

                result = fullPath;
                break;
            }
        }

        if (btn == ODROID_INPUT_B)
        {
            break;
        }
    }

    odroid_sdcard_files_free(files, fileCount);

    return result;
}

static int ui_choose_dialog(dialog_option_t *options, int optionCount, bool cancellable)
{
    const int border = 3;
    const int itemWidth = 190;
    const int itemHeight = 20;
    const int width = itemWidth + (border * 2);
    const int height = ((optionCount+1) * itemHeight) + (border *  2);
    int currentItem = 0;

    while (true)
    {
        int top = (RG_SCREEN_HEIGHT - height) / 2;
        int left  = (RG_SCREEN_WIDTH - width) / 2;

        UG_FillFrame(left, top, left + width, top + height, C_BLUE);
        UG_FillFrame(left + border, top + border, left + width - border, top + height - border, C_WHITE);

        top += border;
        left += border;

        for (int i = 0; i < optionCount; i++) {
            int fg = (i == currentItem) ? C_WHITE : C_BLACK;
            int bg = (i == currentItem) ? C_BLUE : C_WHITE;

            if (!options[i].enabled) {
                fg = C_GRAY;
            }

            UG_SetForecolor(fg);
            UG_SetBackcolor(bg);
            UG_FillFrame(left, top, left + itemWidth, top + itemHeight, bg);
            UG_FontSelect(&FONT_8X12);
            UG_PutString(left + 2, top + 3, (const char*)options[i].label);

            top += itemHeight;
        }

        // Display version at the bottom
        UG_SetForecolor(C_GRAY);
        UG_SetBackcolor(C_WHITE);
        UG_FontSelect(&FONT_8X8);
        UG_PutString(left + 2, top + 2, "Multi-firmware build:\n " PROJECT_VER);
        UpdateDisplay();

        int btn = input_wait_for_button_press(-1);

        if (btn == ODROID_INPUT_DOWN)
        {
            if (++currentItem >= optionCount) currentItem = 0;
        }
        else if (btn == ODROID_INPUT_UP)
        {
            if (--currentItem < 0) currentItem = optionCount - 1;
        }
        else if (btn == ODROID_INPUT_A)
        {
            if (options[currentItem].enabled) {
                return options[currentItem].id;
            }
        }
        else if (btn == ODROID_INPUT_B)
        {
            if (cancellable) break;
        }
    }

    return -1;
}

static void ui_draw_app_page(int currentItem)
{
    int page = (currentItem / ITEM_COUNT) * ITEM_COUNT;
    char tempstring[128];

    DisplayPage("MULTI-FIRMWARE", "[MENU] Menu  |  [A] Boot App");
    DisplayIndicators(page / ITEM_COUNT + 1, (int)ceil((double)apps_count / ITEM_COUNT));

    for (int line = 0; line < ITEM_COUNT && (page + line) < apps_count; ++line)
    {
        odroid_app_t *app = &apps[page + line];
        sprintf(tempstring, "0x%x - 0x%x", app->startOffset, app->endOffset);
        DisplayRow(line, app->description, tempstring, C_GRAY, app->tile, (page + line) == currentItem);
    }

	if (apps_count == 0)
        DisplayMessage("No apps have been flashed yet!");

    UpdateDisplay();
}


static void start_normal(void)
{
    char tempstring[128];
    int displayOrder = 0;
    int currentItem = 0;
    int queuedBtn = -1;

    nvs_flash_init_partition(MFW_NVS_PARTITION);
    if (nvs_open("settings", NVS_READWRITE, &nvs_h) != ESP_OK) {
        nvs_flash_erase();
        nvs_open("settings", NVS_READWRITE, &nvs_h);
    }
    nvs_get_i32(nvs_h, "display_order", &displayOrder);

    read_app_table();
    sort_app_table(displayOrder);

    while (true)
    {
        ui_draw_app_page(currentItem);

        int page = (currentItem / ITEM_COUNT) * ITEM_COUNT;

        // Wait for input but refresh display after 1000 ticks if no input
        int btn = (queuedBtn != -1) ? queuedBtn : input_wait_for_button_press(1000);
        queuedBtn = -1;

		if (apps_count > 0)
		{
            if (btn == ODROID_INPUT_DOWN)
	        {
                if (++currentItem >= apps_count) currentItem = 0;
	        }
	        else if (btn == ODROID_INPUT_UP)
	        {
                if (--currentItem < 0) currentItem = apps_count - 1;
	        }
	        else if (btn == ODROID_INPUT_RIGHT)
	        {
                if (page + ITEM_COUNT < apps_count) currentItem = page + ITEM_COUNT;
                else currentItem = 0;
	        }
	        else if (btn == ODROID_INPUT_LEFT)
	        {
                if (page - ITEM_COUNT >= 0) currentItem = page - ITEM_COUNT;
                else currentItem = (apps_count - 1) / ITEM_COUNT * ITEM_COUNT;
	        }
	        else if (btn == ODROID_INPUT_A)
	        {
                DisplayPage("MULTI-FIRMWARE", PROJECT_VER);
                boot_application(apps + currentItem);
	        }
            else if (btn == ODROID_INPUT_SELECT)
            {
                do {
                    if ((++displayOrder) > LIST_SORT_MAX)
                        displayOrder = (displayOrder & 1);

                    sort_app_table(displayOrder);
                    ui_draw_app_page(currentItem);

                    char descriptions[][16] = {"OFFSET", "INSTALL", "NAME"};
                    char order[][5] = {"ASC", "DESC"};
                    sprintf(tempstring, "NOW SORTING BY %s %s", descriptions[(displayOrder >> 1)], order[displayOrder & 1]);

                    DisplayNotification(tempstring);
                    queuedBtn = input_wait_for_button_press(200);
                }
                while (queuedBtn == ODROID_INPUT_SELECT);

                nvs_set_i32(nvs_h, "display_order", displayOrder);
                nvs_commit(nvs_h);
            }
        }

        if (btn == ODROID_INPUT_MENU || btn == ODROID_INPUT_START)
        {
            dialog_option_t options[] = {
                {0, "Install from SD Card", true},
                {1, "Erase selected app", apps_count > 0},
                {2, "Erase selected NVS", apps_count > 0},
                {3, "Erase all apps", apps_count > 0},
                {4, "Format SD Card", true},
                {5, "Restart System", true}
            };

            odroid_app_t *app = &apps[currentItem];
            char *fileName;
            size_t offset;

            switch (ui_choose_dialog(options, 6, true))
            {
                case 0: // Install from SD Card
                    if ((fileName = ui_choose_file(FIRMWARE_PATH))) {
                        flash_firmware(fileName);
                        free(fileName);
                    }
                    break;
                case 1: // Remove selected app
                    memmove(app, app + 1, (apps_max - currentItem) * sizeof(odroid_app_t));
                    apps_count--;
                    write_app_table();
                    break;
                case 2: // Erase selected app's NVS
                    offset = app->startOffset;
                    for (int i = 0; i < app->parts_count; i++)
                    {
                        odroid_partition_t *part = &app->parts[i];
                        if (part->type == 1 && part->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS)
                        {
                            if (spi_flash_erase_range(offset, part->length) == ESP_OK)
                                DisplayNotification("Operation successful!");
                            else
                                DisplayNotification("An error has occurred!");
                        }
                        offset += part->length;
                    }
                    queuedBtn = input_wait_for_button_press(200);
                    break;
                case 3: // Erase all apps
                    memset(apps, 0xFF, apps_max * sizeof(odroid_app_t));
                    apps_count = 0;
                    currentItem = 0;
                    app = &apps[0];
                    write_app_table();
                    write_partition_table(NULL);
                    break;
                case 4: // Format SD Card
                    DisplayPage("Format SD Card", PROJECT_VER);
                    DisplayMessage("Press start to begin");
                    if (input_wait_for_button_press(50000) != ODROID_INPUT_START) {
                        break;
                    }
                    DisplayMessage("Formatting... (be patient)");
                    sdcardret = odroid_sdcard_format(0);
                    if (sdcardret == ESP_OK) {
                        sdcardret = odroid_sdcard_open();
                    }
                    if (sdcardret == ESP_OK) {
                        char path[32] = SDCARD_BASE_PATH "/odroid";
                        mkdir(path, 0777);
                        strcat(path, "/firmware");
                        mkdir(path, 0777);
                        DisplayMessage("Card formatted!");
                    } else {
                        DisplayError("Format failed!");
                    }
                    input_wait_for_button_press(50000);
                    break;
                case 5: // Restart
                    cleanup_and_restart();
                    break;
            }

            sort_app_table(displayOrder);
        }
        else if (btn == ODROID_INPUT_B)
        {
            DisplayNotification("Press B again to reboot.");
            queuedBtn = input_wait_for_button_press(100);
            if (queuedBtn == ODROID_INPUT_B) {
                boot_application(NULL);
            }
        }
    }
}


static void start_install(void)
{
    DisplayPage("Multi-firmware Installer", "");

    for (int i = 5; i > 0; --i)
    {
        char tempstring[128];
        sprintf(tempstring, "Installing in %d seconds...", i);
        DisplayMessage(tempstring);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    DisplayMessage("Installing...");

    const esp_partition_t *payload = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "payload");

    // We could fall back to copying the current partition and creating the partition table
    // ourselves but we'd be missing the calibration data. Might handle this later...
    if (!payload)
    {
        panic_abort("CORRUPT INSTALLER ERROR");
    }

    SET_STATUS_LED(1);

    size_t size = ALIGN_ADDRESS(payload->size, FLASH_BLOCK_SIZE);
    void *data = safe_alloc(size);

    // We must copy the data to RAM first because our data address space is full, can't mmap
    if (spi_flash_read(payload->address, data, payload->size) != ESP_OK)
    {
        panic_abort("PART TABLE WRITE ERROR");
    }

    // It would be nicer to do the erase/write in blocks to be able to show progress
    // but, because of the shared SPI bus, I think it is safer to do it in one go.

    if (spi_flash_erase_range(0x0, size) != ESP_OK)
    {
        panic_abort("PART TABLE ERASE ERROR");
    }

    if (spi_flash_write(0x0, data, size) != ESP_OK)
    {
        panic_abort("PART TABLE WRITE ERROR");
    }

    // The above code will clear the ota partition, no need to set boot app
    cleanup_and_restart();
}


void app_main(void)
{
    printf("\n\n############### odroid-go-multi-firmware (Ver: "PROJECT_VER") ###############\n\n");

    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    SET_STATUS_LED(1);

    // Has to be before LCD
    sdcardret = odroid_sdcard_open();
    ili9341_init();
    input_init();

    UG_Init(&gui, pset, RG_SCREEN_WIDTH, RG_SCREEN_HEIGHT);

    SET_STATUS_LED(0);

    // Start the installation process if we didn't boot from the factory app partition
    if (esp_ota_get_running_partition()->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY)
    {
        ESP_LOGI(__func__, "Non-factory startup, launching installer...");
        start_install();
    }
    else
    {
        ESP_LOGI(__func__, "Factory startup, launching normally...");
        start_normal();
    }

    panic_abort("???");
}
