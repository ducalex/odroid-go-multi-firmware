#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_flash_data_types.h"
#include "esp_log.h"
#include "rom/crc.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>

#include "odroid_sdcard.h"
#include "odroid_display.h"
#include "input.h"

#include "../components/ugui/ugui.h"

#define ALIGN_ADDRESS(val, alignment) (((val & (alignment-1)) != 0) ? (val & ~(alignment-1)) + alignment : val)

#define ESP_PARTITION_TABLE_OFFSET CONFIG_PARTITION_TABLE_OFFSET /* Offset of partition table. Backwards-compatible name.*/
#define ESP_PARTITION_TABLE_MAX_LEN 0xC00 /* Maximum length of partition table data */
#define ESP_PARTITION_TABLE_MAX_ENTRIES (ESP_PARTITION_TABLE_MAX_LEN / sizeof(esp_partition_info_t)) /* Maximum length of partition table data, including terminating entry */

#define PART_SUBTYPE_FACTORY 0x00
#define PART_SUBTYPE_FACTORY_DATA 0xFE

#define TILE_WIDTH (86)
#define TILE_HEIGHT (48)

#define APP_MAGIC 0x1207

#define APP_SORT_OFFSET      0b0000
#define APP_SORT_SEQUENCE    0b0010
#define APP_SORT_DESCRIPTION 0b0100
#define APP_SORT_MAX         0b0101
#define APP_SORT_DIR_ASC     0b0000
#define APP_SORT_DIR_DESC    0b0001

#define FLASH_SIZE (16 * 1024 * 1024)
#define FLASH_BLOCK_SIZE (64 * 1024)
#define ERASE_BLOCK_SIZE (4 * 1024)

#define APP_NVS_SIZE 0x3000

#define NVS_PART_NAME "nvs_fw"

#ifndef COMPILEDATE
#define COMPILEDATE "none"
#endif

#ifndef GITREV
#define GITREV "none"
#endif

#define VERSION COMPILEDATE "-" GITREV

#define FIRMWARE_HEADER_SIZE (24)
#define FIRMWARE_DESCRIPTION_SIZE (40)
#define FIRMWARE_PARTS_MAX (20)
#define FIRMWARE_TILE_SIZE (TILE_WIDTH * TILE_HEIGHT)

#define BATTERY_VMAX 420
#define BATTERY_VMIN 330

#define ITEM_COUNT (4)

#define LED_ON() gpio_set_level(GPIO_NUM_2, 1);
#define LED_OFF() gpio_set_level(GPIO_NUM_2, 0);

const char* SD_CARD = "/sd";
const char* FIRMWARE_PATH = "/sd/odroid/firmware";

//const char* HEADER = "ODROIDGO_FIRMWARE_V00_00";
const char* HEADER_V00_01 = "ODROIDGO_FIRMWARE_V00_01";


// <partition type=0x00 subtype=0x00 label='name' flags=0x00000000 length=0x00000000>
// 	<data length=0x00000000>
// 		[...]
// 	</data>
// </partition>
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
    char     description[FIRMWARE_DESCRIPTION_SIZE];
    char     filename[FIRMWARE_DESCRIPTION_SIZE];
    uint16_t tile[FIRMWARE_TILE_SIZE];
    odroid_partition_t parts[FIRMWARE_PARTS_MAX];
    uint8_t parts_count;
    uint8_t _reserved0;
    uint16_t installSeq;
} odroid_app_t; // __attribute__((packed))

typedef struct
{
    char header[FIRMWARE_HEADER_SIZE];
    char description[FIRMWARE_DESCRIPTION_SIZE];
    uint16_t tile[FIRMWARE_TILE_SIZE];
} odroid_fw_header_t;

typedef struct
{
    odroid_fw_header_t fileHeader;
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
// ------

typedef struct
{
    long id;
    char label[32];
    bool enabled;
} dialog_option_t;

static odroid_app_t* apps;
static int apps_count = -1;
static int apps_max = 4;
static int nextInstallSeq = 0;
static int displayOrder = 0;

static esp_partition_info_t* partition_data;
static int partition_count = -1;
static int startTableEntry = -1;
static int startFlashAddress = -1;

static odroid_fw_t *fwInfoBuffer;
static uint8_t *dataBuffer;

static uint16_t fb[320 * 240];
static UG_GUI gui;
static char tempstring[512];

static esp_err_t sdcardret;

static nvs_handle nvs_h;

static int batteryPercent = 0;


static void battery_task(void *arg)
{
    // Some of that code is from Odroid GO Arduino library. Added rolling average for better results
    esp_adc_cal_characteristics_t adc_cal;
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_cal);

    const int count = 128;
    uint32_t samples[count];
    uint32_t loops = 0;

    while(1)
    {
        samples[loops % count] = adc1_get_raw((adc1_channel_t) ADC1_CHANNEL_0);

        uint32_t total = 0;
        for (int i = 0; i < count; i++)
        {
            total += samples[i];
        }

        double voltage = (double) esp_adc_cal_raw_to_voltage(total / count, &adc_cal) * 2 / 1000;

        batteryPercent = 101 - (101 / pow(1 + pow(1.33 * ((int)(voltage * 100) - BATTERY_VMIN)
                                                            / (BATTERY_VMAX - BATTERY_VMIN), 4.5), 3));

        if (batteryPercent >= 100)
            batteryPercent = 100;

        if (++loops > count)
            vTaskDelay(25);
    }
}


void indicate_error()
{
    int level = 0;
    while (true) {
        gpio_set_level(GPIO_NUM_2, level);
        level = !level;
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

static void pset(UG_S16 x, UG_S16 y, UG_COLOR color)
{
    fb[y * 320 + x] = color;
}

static void ui_update_display()
{
    ili9341_write_frame_rectangleLE(0, 0, 320, 240, fb);
}

static void ui_draw_image(short x, short y, short width, short height, uint16_t* data)
{
    for (short i = 0 ; i < height; ++i)
    {
        for (short j = 0; j < width; ++j)
        {
            uint16_t pixel = data[i * width + j];
            UG_DrawPixel(x + j, y + i, pixel);
        }
    }
}

static void ui_draw_title(char*, char*);

static void ClearScreen()
{
}

static void UpdateDisplay()
{
    ui_update_display();
}

static void DisplayError(char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = (240 / 2) - (12 / 2);
    UG_SetForecolor(C_RED);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, 319, top + 12, C_WHITE);
    UG_PutString(left, top, message);

    UpdateDisplay();
}

static void DisplayMessage(char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = (240 / 2) + 8 + (12 / 2) + 16;
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, 319, top + 12, C_WHITE);
    UG_PutString(left, top, message);

    UpdateDisplay();
}

static void DisplayNotification(char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = 239 - 16;
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_BLUE);
    UG_FillFrame(0, top, 319, top + 16, C_BLUE);
    UG_PutString(left, top + 3, message);
    UpdateDisplay();
}

static void DisplayProgress(int percent)
{
    if (percent > 100) percent = 100;

    const int WIDTH = 200;
    const int HEIGHT = 12;
    const int FILL_WIDTH = WIDTH * (percent / 100.0f);

    short left = (320 / 2) - (WIDTH / 2);
    short top = (240 / 2) - (HEIGHT / 2) + 16;
    UG_FillFrame(left - 1, top - 1, left + WIDTH + 1, top + HEIGHT + 1, C_WHITE);
    UG_DrawFrame(left - 1, top - 1, left + WIDTH + 1, top + HEIGHT + 1, C_BLACK);

    if (FILL_WIDTH > 0)
    {
        UG_FillFrame(left, top, left + FILL_WIDTH, top + HEIGHT, C_GREEN);
    }

    //UpdateDisplay();
}

static void DisplayFooter(char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = 240 - (16 * 2) - 8;
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, 319, top + 12, C_WHITE);
    UG_PutString(left, top, message);

    UpdateDisplay();
}

static void DisplayHeader(char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = (16 + 8);
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, 319, top + 12, C_WHITE);
    UG_PutString(left, top, message);

    UpdateDisplay();
}

static void DisplayTile(uint16_t *tileData)
{
    const uint16_t tileLeft = (320 / 2) - (TILE_WIDTH / 2);
    const uint16_t tileTop = (16 + 16 + 16);
    ui_draw_image(tileLeft, tileTop, TILE_WIDTH, TILE_HEIGHT, tileData);

    // Tile border
    UG_DrawFrame(tileLeft - 1, tileTop - 1, tileLeft + TILE_WIDTH, tileTop + TILE_HEIGHT, C_BLACK);
    UpdateDisplay();
}


//---------------
void cleanup_and_restart()
{
    // Turn off LED pin
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_INPUT);

    // clear and deinit display
    ili9341_clear(0x0000);
    ili9341_deinit();

    // Close SD card
    odroid_sdcard_close();

    // Close NVS
    nvs_close(nvs_h);
    nvs_flash_deinit_partition(NVS_PART_NAME);

    esp_restart();
}


void boot_application()
{
    ESP_LOGI(__func__, "Booting application.");

    // Set firmware active
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (partition == NULL)
    {
        DisplayError("NO BOOT PART ERROR");
        indicate_error();
    }

    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK)
    {
        DisplayError("BOOT SET ERROR");
        indicate_error();
    }

    cleanup_and_restart();
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
        case APP_SORT_SEQUENCE:
            qsort(apps, apps_count, sizeof(odroid_app_t), &sort_app_table_by_sequence);
            break;
        case APP_SORT_DESCRIPTION:
            qsort(apps, apps_count, sizeof(odroid_app_t), &sort_app_table_by_alphabet);
            break;
        case APP_SORT_OFFSET:
        default:
            qsort(apps, apps_count, sizeof(odroid_app_t), &sort_app_table_by_offset);
            break;
    }

    if (newMode & 1) { // Reverse array. Very inefficient.
        odroid_app_t *tmp = malloc(sizeof(odroid_app_t));
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


static void read_app_table()
{
    const esp_partition_t *app_table_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                     PART_SUBTYPE_FACTORY_DATA, NULL);

    if (!app_table_part) {
        abort();
    }

    startFlashAddress = app_table_part->address + app_table_part->size;

    apps_max = (app_table_part->size / sizeof(odroid_app_t));

    apps_count = 0;

    if (!apps) {
        apps = malloc(app_table_part->size);
    }

    if (!apps) {
        DisplayError("APP TABLE ALLOC ERROR");
        indicate_error();
    }

    esp_err_t err = esp_partition_read(app_table_part, 0, (void*)apps, app_table_part->size);
    if (err != ESP_OK)
    {
        DisplayError("APP TABLE READ ERROR");
        indicate_error();
    }

    for (int i = 0; i < apps_max; i++) {
        if (apps[i].magic != APP_MAGIC) {
            break;
        }
        if (apps[i].installSeq >= nextInstallSeq) {
            nextInstallSeq = apps[i].installSeq + 1;
        }
        apps_count++;
    }

    //64K align the address (https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/partition-tables.html#offset-size)
    startFlashAddress = ALIGN_ADDRESS(startFlashAddress, 0x10000);

    ESP_LOGI(__func__, "Read app table (%d apps)", apps_count);
}


static void write_app_table()
{
    esp_err_t err;

    const esp_partition_t *app_table_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                     PART_SUBTYPE_FACTORY_DATA, NULL);

    if (!app_table_part || !apps) {
        read_app_table();
    }

    for (int i = apps_count; i < apps_max; ++i)
    {
        memset(&apps[i], 0xff, sizeof(odroid_app_t));
    }

    //sort_app_table(APP_SORT_OFFSET); // Causes issues in flash_firmware

    err = esp_partition_erase_range(app_table_part, 0, app_table_part->size);
    if (err != ESP_OK)
    {
        DisplayError("APP TABLE ERASE ERROR");
        indicate_error();
    }

    err = esp_partition_write(app_table_part, 0, (void*)apps, app_table_part->size);
    if (err != ESP_OK)
    {
        DisplayError("APP TABLE WRITE ERROR");
        indicate_error();
    }

    ESP_LOGI(__func__, "Written app table (%d apps)", apps_count);
}


static void read_partition_table()
{
    esp_err_t err;

    partition_count = 0;

    if (!partition_data)
    {
        partition_data = (esp_partition_info_t*)malloc(ESP_PARTITION_TABLE_MAX_LEN);
    }

    // Read table
    err = spi_flash_read(ESP_PARTITION_TABLE_OFFSET, (void*)partition_data, ESP_PARTITION_TABLE_MAX_LEN);
    if (err != ESP_OK)
    {
        DisplayError("TABLE READ ERROR");
        indicate_error();
    }

    // Find end of first partitioned

    for (int i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; ++i)
    {
        const esp_partition_info_t *part = &partition_data[i];
        if (part->magic == 0xffff) break;

        if (part->magic == ESP_PARTITION_MAGIC)
        {
            partition_count++;

            if (part->type == PART_TYPE_DATA &&
                part->subtype == PART_SUBTYPE_FACTORY_DATA)
            {
                startTableEntry = i + 1;
                break;
            }
        }
    }
}


static void write_partition_table(odroid_partition_t* parts, size_t parts_count, size_t flashOffset)
{
    esp_err_t err;

    if (!partition_data) {
        read_partition_table();
    }

    // Find end of first partitioned
    if (startTableEntry < 0)
    {
        DisplayError("NO FACTORY PARTITION ERROR");
        indicate_error();
    }

    ESP_LOGI(__func__, "startTableEntry=%d, startFlashAddress=%#08x", startTableEntry, flashOffset);

    // blank partition table entries
    for (int i = startTableEntry; i < ESP_PARTITION_TABLE_MAX_ENTRIES; ++i)
    {
        memset(&partition_data[i], 0xff, sizeof(esp_partition_info_t));
    }

    // Add partitions
    size_t offset = 0;
    for (int i = 0; i < parts_count; ++i)
    {
        esp_partition_info_t* part = &partition_data[startTableEntry + i];
        part->magic = ESP_PARTITION_MAGIC;
        part->type = parts[i].type;
        part->subtype = parts[i].subtype;
        part->pos.offset = flashOffset + offset;
        part->pos.size = parts[i].length;
        memcpy(&part->label, parts[i].label, 16);
        part->flags = parts[i].flags;

        offset += parts[i].length;
    }

    // Erase partition table
    if (ESP_PARTITION_TABLE_MAX_LEN > 4096)
    {
        DisplayError("TABLE SIZE ERROR");
        indicate_error();
    }

    err = spi_flash_erase_range(ESP_PARTITION_TABLE_OFFSET, 4096);
    if (err != ESP_OK)
    {
        DisplayError("TABLE ERASE ERROR");
        indicate_error();
    }

    // Write new table
    err = spi_flash_write(ESP_PARTITION_TABLE_OFFSET, (void*)partition_data, ESP_PARTITION_TABLE_MAX_LEN);
    if (err != ESP_OK)
    {
        DisplayError("TABLE WRITE ERROR");
        indicate_error();
    }

    esp_partition_reload_table();
}



void defrag_flash()
{
    size_t nextStartOffset = startFlashAddress;
    size_t totalBytesToMove = 0;
    size_t totalBytesMoved = 0;

    sort_app_table(APP_SORT_OFFSET);

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
    ui_draw_title("Defragmenting flash", tempstring);
    DisplayHeader("Making some space...");

    for (int i = 0; i < apps_count; i++)
    {
        if (apps[i].startOffset > nextStartOffset)
        {
            LED_ON();

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

            LED_OFF();
        }

        nextStartOffset = apps[i].endOffset + 1;
    }

    write_app_table();
}


void find_free_blocks(odroid_flash_block_t **blocks, size_t *count, size_t *totalFreeSpace)
{
    size_t previousBlockEnd = startFlashAddress;

    (*blocks) = malloc(sizeof(odroid_flash_block_t) * 32);

    (*totalFreeSpace) = 0;
    (*count) = 0;

    sort_app_table(APP_SORT_OFFSET);

    for (int i = 0; i < apps_count; i++)
    {
        size_t free_space = apps[i].startOffset - previousBlockEnd;

        if (free_space > 0) {
            odroid_flash_block_t *block = &(*blocks)[(*count)++];
            block->offset = previousBlockEnd;
            block->size = free_space;
            (*totalFreeSpace) += block->size;
            ESP_LOGI(__func__, "Found free block: %d 0x%x %d", i, block->offset, free_space / 1024);
        }

        previousBlockEnd = apps[i].endOffset + 1;
    }

    if ((FLASH_SIZE - previousBlockEnd) > 0) {
        odroid_flash_block_t *block = &(*blocks)[(*count)++];
        block->offset = previousBlockEnd;
        block->size = (FLASH_SIZE - previousBlockEnd);
        (*totalFreeSpace) += block->size;
        ESP_LOGI(__func__, "Found free block: end 0x%x %d", block->offset, block->size / 1024);
    }
}


int find_free_block(size_t size, bool defragIfNeeded)
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


bool firmware_get_info(const char* filename, odroid_fw_t* outData)
{
    size_t count, file_size;

    FILE* file = fopen(filename, "rb");
    if (!file)
    {
        return false;
    }

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    count = fread(outData, sizeof(outData->fileHeader), 1, file);
    if (count != 1)
    {
        goto firmware_get_info_err;
    }

    if (memcmp(HEADER_V00_01, outData->fileHeader.header, strlen(HEADER_V00_01)) != 0)
    {
        goto firmware_get_info_err;
    }

    outData->fileHeader.description[FIRMWARE_DESCRIPTION_SIZE - 1] = 0;
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
        part->length = ALIGN_ADDRESS(part->length, 0x1000);

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
    return true;

firmware_get_info_err:
    fclose(file);
    return false;
}


void flash_utility()
{
    // Code to flash utility.bin.
    // Because leaving it in flash_firmware results in multiple copies being flashed.
    //FILE* util = fopen("/sd/odroid/utility.bin", "rb");
    //util_part.type = ESP_PARTITION_TYPE_APP;
    //util_part.subtype = ESP_PARTITION_SUBTYPE_APP_TEST;
}


void flash_firmware(const char* fullPath)
{
    ESP_LOGD(__func__, "HEAP=%#010x", esp_get_free_heap_size());
    LED_OFF();

    size_t count;
    bool can_proceed = true;

    sort_app_table(APP_SORT_OFFSET);

    ui_draw_title("Install Application", "Destination: Pending");
    UpdateDisplay();

    ESP_LOGI(__func__, "Flashing file: %s", fullPath);

    FILE* file = fopen(fullPath, "rb");
    if (file == NULL)
    {
        DisplayError("FILE OPEN ERROR");
        indicate_error();
    }

    odroid_fw_t *fw = fwInfoBuffer;

    if (!firmware_get_info(fullPath, fw))
    {
        // To do: Make it show what is invalid
        DisplayError("INVALID FIRMWARE FILE");
        can_proceed = false;
    }

    int currentFlashAddress = find_free_block(fw->flashSize, true);

    odroid_app_t *app = &apps[apps_count];
    memset(app, 0x00, sizeof(odroid_app_t));

    strncpy(app->description, fw->fileHeader.description, FIRMWARE_DESCRIPTION_SIZE-1);
    strncpy(app->filename, strrchr(fullPath, '/'), FIRMWARE_DESCRIPTION_SIZE-1);
    memcpy(app->tile, fw->fileHeader.tile, FIRMWARE_TILE_SIZE * 2);
    memcpy(app->parts, fw->parts, sizeof(app->parts));
    app->parts_count = fw->parts_count;

    ESP_LOGI(__func__, "Destination: 0x%x", currentFlashAddress);
    ESP_LOGI(__func__, "Description: '%s'", app->description);

    sprintf(tempstring, "Destination: 0x%x", currentFlashAddress);
    ui_draw_title("Install Application", tempstring);
    DisplayHeader(app->description);
    DisplayTile(app->tile);

    if (currentFlashAddress == -1)
    {
        DisplayError("NOT ENOUGH FREE SPACE");
        can_proceed = false;
    }

    if (can_proceed)
    {
        DisplayMessage("[START]");
    }

    DisplayFooter("[B] Cancel");

    while (1) {
        int btn = wait_for_button_press(-1);

        if (btn == ODROID_INPUT_START && can_proceed) break;
        if (btn == ODROID_INPUT_B)
        {
            fclose(file);
            return;
        }
    }

    LED_ON();

    DisplayMessage("Verifying ...");
    DisplayFooter("");


    // Verify file integerity
    ESP_LOGI(__func__, "Expected checksum: %#010x",fw->checksum);

    fseek(file, 0, SEEK_SET);

    uint32_t checksum = 0;
    while(true)
    {
        count = fread(dataBuffer, 1, FLASH_BLOCK_SIZE, file);
        if (ftell(file) == fw->fileSize)
        {
            count -= 4;
        }

        checksum = crc32_le(checksum, dataBuffer, count);

        if (count < FLASH_BLOCK_SIZE) break;
    }

    ESP_LOGI(__func__, "Computed checksum: %#010x", checksum);

    if (checksum != fw->checksum)
    {
        DisplayError("CHECKSUM MISMATCH ERROR");
        indicate_error();
    }

    // restore location to end of description
    fseek(file, fw->dataOffset, SEEK_SET);

    app->magic = APP_MAGIC;
    app->startOffset = currentFlashAddress;

    // Copy the firmware
    for (int i = 0; i < app->parts_count; i++)
    {
        odroid_partition_t *slot = &app->parts[i];

        // Skip header, firmware_get_info prepared everything for us
        fseek(file, sizeof(odroid_partition_t), SEEK_CUR);

        LED_OFF();

        // Erase target partition space
        ESP_LOGI(__func__, "Erasing ... (%d)", i);
        sprintf(tempstring, "Erasing ... (%d/%d)", i+1, app->parts_count);
        DisplayProgress(0);
        DisplayMessage(tempstring);

        int eraseBlocks = slot->length / ERASE_BLOCK_SIZE;
        if (eraseBlocks * ERASE_BLOCK_SIZE < slot->length) ++eraseBlocks;

        esp_err_t ret = spi_flash_erase_range(currentFlashAddress, eraseBlocks * ERASE_BLOCK_SIZE);
        if (ret != ESP_OK)
        {
            ESP_LOGE(__func__, "spi_flash_erase_range failed. eraseBlocks=%d", eraseBlocks);
            DisplayError("ERASE ERROR");
            indicate_error();
        }

        if (slot->dataLength > 0)
        {
            size_t nextEntry = ftell(file) + slot->dataLength;

            LED_ON();

            // Write data
            int totalCount = 0;
            for (int offset = 0; offset < slot->dataLength; offset += FLASH_BLOCK_SIZE)
            {
                ESP_LOGI(__func__, "Writing (%d) at %#08x", i, offset);

                sprintf(tempstring, "Writing (%d/%d)", i+1, app->parts_count);
                DisplayProgress((float)offset / (float)(slot->dataLength - FLASH_BLOCK_SIZE) * 100.0f);
                DisplayMessage(tempstring);

                // read
                count = fread(dataBuffer, 1, FLASH_BLOCK_SIZE, file);
                if (count <= 0)
                {
                    DisplayError("DATA READ ERROR");
                    indicate_error();
                }

                if (offset + count >= slot->dataLength)
                {
                    count = slot->dataLength - offset;
                }

                // flash
                ret = spi_flash_write(currentFlashAddress + offset, dataBuffer, count);
                if (ret != ESP_OK)
        		{
        			ESP_LOGE(__func__, "spi_flash_write failed. address=%#08x", currentFlashAddress + offset);
                    DisplayError("WRITE ERROR");
                    indicate_error();
        		}

                totalCount += count;
            }

            LED_OFF();

            if (totalCount != slot->dataLength)
            {
                ESP_LOGE(__func__, "Size mismatch: length=%#08x, totalCount=%#08x", slot->dataLength, totalCount);
                DisplayError("DATA SIZE ERROR");
                indicate_error();
            }

            fseek(file, nextEntry, SEEK_SET);
            // TODO: verify
        }

        // Notify OK
        ESP_LOGI(__func__, "Partition(%d): OK. Length=%#08x", i, slot->length);
        currentFlashAddress += slot->length;
    }

    fclose(file);

    // 64K align our endOffset
    app->endOffset = ALIGN_ADDRESS(currentFlashAddress, 0x10000) - 1;

    // Remember the install order, for display sorting
    app->installSeq = nextInstallSeq++;

    // Write app table
    apps_count++; // Everything went well, acknowledge the new app
    write_app_table();

    DisplayMessage("Ready !");
    DisplayFooter("[B] Go Back   |   [A] Boot");

    while (1) {
        int btn = wait_for_button_press(-1);

        if (btn == ODROID_INPUT_A) break;
        if (btn == ODROID_INPUT_B) return;
    }

    // Write partition table
    write_partition_table(app->parts, app->parts_count, app->startOffset);

    // boot firmware
    boot_application();
}


static void ui_draw_title(char* TITLE, char* FOOTER)
{
    UG_FillFrame(0, 0, 319, 239, C_WHITE);

    // Header
    UG_FillFrame(0, 0, 319, 15, C_MIDNIGHT_BLUE);
    UG_FontSelect(&FONT_8X8);
    const short titleLeft = (320 / 2) - (strlen(TITLE) * 9 / 2);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_MIDNIGHT_BLUE);
    UG_PutString(titleLeft, 4, TITLE);

    // Footer
    UG_FontSelect(&FONT_8X8);
    UG_SetBackcolor(C_MIDNIGHT_BLUE);
    UG_SetForecolor(C_LIGHT_GRAY);
    UG_FillFrame(0, 239 - 16, 319, 239, C_MIDNIGHT_BLUE);
    const short footerLeft = (320 / 2) - (strlen(FOOTER) * 9 / 2);
    UG_PutString(footerLeft, 240 - 4 - 8, FOOTER);
}


static void ui_draw_indicators(int page, int totalPages)
{
    UG_FontSelect(&FONT_8X8);
    UG_SetForecolor(0x8C51);

    // Page indicator
    sprintf(tempstring, "%d/%d", page, totalPages);
    UG_PutString(4, 4, tempstring);

    // Battery indicator
    sprintf(tempstring, "%d%%", batteryPercent);
    UG_PutString(320 - (9 * strlen(tempstring)) - 4, 4, tempstring);
}


static void ui_draw_row(int line, char *line1, char* line2, uint16_t color, uint16_t *tile, bool selected)
{
    const int innerHeight = 240 - (16 * 2); // 208
    const int itemHeight = innerHeight / ITEM_COUNT; // 52

    const int rightWidth = (213); // 320 * (2.0 / 3.0)
    const int leftWidth = 320 - rightWidth;

    // Tile width = 86, height = 48 (16:9)
    const short imageLeft = (leftWidth / 2) - (86 / 2);
    const short textLeft = 320 - rightWidth;

    short top = 16 + (line * itemHeight) - 1;

    UG_FontSelect(&FONT_8X12);

    UG_SetBackcolor(selected ? C_YELLOW : C_WHITE);
    UG_FillFrame(0, top + 2, 319, top + itemHeight - 1 - 1, UG_GetBackcolor());

    ui_draw_image(imageLeft, top + 2, TILE_WIDTH, TILE_HEIGHT, tile);

    UG_SetForecolor(C_BLACK);
    UG_PutString(textLeft, top + 2 + 2 + 7, line1);

    UG_SetForecolor(color);
    UG_PutString(textLeft, top + 2 + 2 + 23, line2);
}


static void ui_draw_page(char** files, int fileCount, int currentItem)
{
    int page = (currentItem / ITEM_COUNT) * ITEM_COUNT;

    odroid_flash_block_t *blocks;
    size_t count, totalFreeSpace;

    find_free_blocks(&blocks, &count, &totalFreeSpace);
    free(blocks);

    sprintf(tempstring, "Free space: %.2fMB (%d block)", (double)totalFreeSpace / 1024 / 1024, count);

    ui_draw_title("Select a file", tempstring);
    ui_draw_indicators(page / ITEM_COUNT + 1, (int)ceil((double)fileCount / ITEM_COUNT));

	if (fileCount < 1)
	{
        DisplayMessage("SD Card Empty");
        return;
	}

    char line1[64], line2[64];
    uint16_t color = C_GRAY;

    for (int line = 0; line < ITEM_COUNT && (page + line) < fileCount; ++line)
    {
        char* fileName = files[page + line];
        if (!fileName) abort();

        sprintf(tempstring, "%s/%s", FIRMWARE_PATH, fileName);
        bool valid = firmware_get_info(tempstring, fwInfoBuffer);

        strcpy(line1, fileName);
        line1[strlen(fileName) - 3] = 0; // ".fw" = 3

        if (valid) {
            color = C_GRAY;
            sprintf(line2, "%.2f MB", (float)fwInfoBuffer->flashSize / 1024 / 1024);
        } else {
            color = C_RED;
            sprintf(line2, "Invalid firmware");
        }

        ui_draw_row(line, line1, line2, color,
                        fwInfoBuffer->fileHeader.tile, (page + line) == currentItem);
    }

    UpdateDisplay();
}

char* ui_choose_file(const char* path)
{
    ESP_LOGD(__func__, "HEAP=%#010x", esp_get_free_heap_size());

    // Check SD card
    if (sdcardret != ESP_OK)
    {
        ui_draw_title("Error", "Error");
        DisplayError("SD CARD ERROR");
        vTaskDelay(200);
        return NULL;
    }

    char* result = NULL;

    char** files = NULL;
    int fileCount = odroid_sdcard_files_get(path, ".fw", &files);
    ESP_LOGI(__func__, "fileCount=%d", fileCount);

    // Selection
    int currentItem = 0;

    while (true)
    {
        ui_draw_page(files, fileCount, currentItem);

        int page = (currentItem / ITEM_COUNT) * ITEM_COUNT;

        // Wait for input but refresh display after 1000 ticks if no input
        int btn = wait_for_button_press(1000);

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

                char* fullPath = (char*)malloc(fullPathLength);
                if (!fullPath) abort();

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



static void ui_draw_dialog(dialog_option_t *options, int optionCount, int currentItem)
{
    int border = 3;
    int itemWidth = 190;
    int itemHeight = 20;
    int width = itemWidth + (border * 2);
    int height = ((optionCount+1) * itemHeight) + (border *  2);
    int top = (240 - height) / 2;
    int left  = (320 - width) / 2;

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
    UG_PutString(left + 2, top + 2, "Version:\n " VERSION);

    UpdateDisplay();
}


static int ui_choose_dialog(dialog_option_t *options, int optionCount, bool cancellable)
{
    ESP_LOGD(__func__, "HEAP=%#010x", esp_get_free_heap_size());

    int currentItem = 0;

    while (true)
    {
        ui_draw_dialog(options, optionCount, currentItem);

        int btn = wait_for_button_press(-1);

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

    ui_draw_title("ODROID-GO", "[MENU] Menu   |   [A] Boot App");
    ui_draw_indicators(page / ITEM_COUNT + 1, (int)ceil((double)apps_count / ITEM_COUNT));

	if (apps_count < 1)
	{
        DisplayMessage("No apps have been flashed yet!");
        return;
	}

    for (int line = 0; line < ITEM_COUNT && (page + line) < apps_count; ++line)
    {
        odroid_app_t *app = &apps[page + line];

        sprintf(tempstring, "0x%x - 0x%x", app->startOffset, app->endOffset);
        ui_draw_row(line, app->description, (char*)tempstring, C_GRAY, app->tile, (page + line) == currentItem);
    }

    UpdateDisplay();
}


void ui_choose_app()
{
    ESP_LOGD(__func__, "HEAP=%#010x", esp_get_free_heap_size());

    nvs_get_i32(nvs_h, "display_order", &displayOrder);

    sort_app_table(displayOrder);

    int currentItem = 0;
    int queuedBtn = -1;

    while (true)
    {
        ui_draw_app_page(currentItem);

        int page = (currentItem / ITEM_COUNT) * ITEM_COUNT;

        // Wait for input but refresh display after 1000 ticks if no input
        int btn = (queuedBtn != -1) ? queuedBtn : wait_for_button_press(1000);
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
                ui_draw_title("ODROID-GO", VERSION);
                DisplayMessage("Updating partitions ...");

                odroid_app_t *app = &apps[currentItem];
                write_partition_table(app->parts, app->parts_count, app->startOffset);

                DisplayMessage("Setting boot partition ...");
                boot_application();
	        }
            else if (btn == ODROID_INPUT_SELECT)
            {
                do {
                    if ((++displayOrder) > APP_SORT_MAX)
                        displayOrder = (displayOrder & 1);

                    sort_app_table(displayOrder);
                    ui_draw_app_page(currentItem);

                    char descriptions[][16] = {"OFFSET", "INSTALL", "NAME"};
                    char order[][5] = {"ASC", "DESC"};
                    sprintf(tempstring, "NOW SORTING BY %s %s", descriptions[(displayOrder >> 1)], order[displayOrder & 1]);

                    DisplayNotification(tempstring);
                    queuedBtn = wait_for_button_press(200);
                }
                while (queuedBtn == ODROID_INPUT_SELECT);

                nvs_set_i32(nvs_h, "display_order", displayOrder);
                nvs_commit(nvs_h);
            }
        }

        if (btn == ODROID_INPUT_MENU)
        {
            dialog_option_t options[] = {
                {0, "Install from SD Card", true},
                {1, "Erase selected app", apps_count > 0},
                {2, "Erase selected NVS", apps_count > 0},
                {3, "Erase all apps", apps_count > 0},
                {4, "Restart System", true}
            };

            int choice = ui_choose_dialog(options, 5, true);
            char* fileName;

            switch(choice) {
                case 0: // Install from SD Card
                    fileName = ui_choose_file(FIRMWARE_PATH);
                    if (fileName) {
                        flash_firmware(fileName);
                        free(fileName);
                    }
                    break;
                case 1: // Remove selected app
                    memmove(&apps[currentItem], &apps[currentItem + 1],
                            (apps_max - currentItem) * sizeof(odroid_app_t));
                    apps_count--;
                    write_app_table();
                    break;
                case 2: // Erase selected app's NVS
                    write_partition_table(apps[currentItem].parts,
                        apps[currentItem].parts_count, apps[currentItem].startOffset);
                    if (nvs_flash_erase() == ESP_OK) {
                        DisplayNotification("Operation successful!");
                        queuedBtn = wait_for_button_press(100);
                    } else {
                        DisplayNotification("An error has occurred!");
                        queuedBtn = wait_for_button_press(200);
                    }
                    break;
                case 3: // Erase all apps
                    memset(apps, 0xFF, apps_max * sizeof(odroid_app_t));
                    apps_count = 0;
                    write_app_table();
                    write_partition_table(NULL, 0, 0);
                    break;
                case 4: // Restart
                    cleanup_and_restart();
                    break;
            }

            sort_app_table(displayOrder);
        }
        else if (btn == ODROID_INPUT_B)
        {
            DisplayNotification("Press B again to boot last app.");
            queuedBtn = wait_for_button_press(100);
            if (queuedBtn == ODROID_INPUT_B) {
                esp_ota_set_boot_partition(esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                    ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL)); // Restore OTA data if possible and reboot
                cleanup_and_restart();
            }
        }
    }
}


void app_main(void)
{
    printf("\n\n#################### odroid-go-firmware (Ver: "VERSION") ####################\n\n");

    // Init NVS
    nvs_flash_init_partition(NVS_PART_NAME);
    if (nvs_open_from_partition(NVS_PART_NAME, "settings", NVS_READWRITE, &nvs_h) != ESP_OK) {
        nvs_flash_erase_partition(NVS_PART_NAME);
        nvs_open_from_partition(NVS_PART_NAME, "settings", NVS_READWRITE, &nvs_h);
    }

    // Init gamepad
    input_init();

    // turn LED on
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 1);

    // Has to be before LCD
    sdcardret = odroid_sdcard_open(SD_CARD);

    ili9341_init();
    ili9341_clear(0xffff);

    UG_Init(&gui, pset, 320, 240);

    // Start battery monitor
    xTaskCreate(&battery_task, "battery_task", 4096, NULL, 5, NULL);

    fwInfoBuffer = malloc(sizeof(odroid_fw_t));
    dataBuffer = malloc(FLASH_BLOCK_SIZE);

    // If we can't allocate our basic buffers we might as well give up now
    if (!fwInfoBuffer || !dataBuffer)
    {
        DisplayError("MEMORY ALLOCATION ERROR");
        indicate_error();
    }

    read_partition_table();
    read_app_table();

    ui_choose_app();

    indicate_error();
}
