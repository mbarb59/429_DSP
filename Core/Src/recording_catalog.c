#include "recording_catalog.h"

#include "filesystem_service.h"
#include "recorder_framework.h"

#include <stddef.h>
#include <string.h>

static char selected_filename[RECORDING_FILENAME_CAPACITY];

/* UI_WAV_BROWSER_V2_PATCH */
static RecordingCatalogEntry browser_entries[RECORDING_BROWSER_PAGE_SIZE];
static uint32_t browser_page_index;
static uint32_t browser_page_count;
static uint32_t browser_entry_count;

static void build_filename(uint32_t number, char *filename)
{
    filename[0] = 'R';
    filename[1] = 'E';
    filename[2] = 'C';
    filename[3] = (char)('0' + ((number / 1000U) % 10U));
    filename[4] = (char)('0' + ((number / 100U) % 10U));
    filename[5] = (char)('0' + ((number / 10U) % 10U));
    filename[6] = (char)('0' + (number % 10U));
    filename[7] = '.';
    filename[8] = 'W';
    filename[9] = 'A';
    filename[10] = 'V';
    filename[11] = '\0';
}

void RecordingCatalog_Init(void)
{
    strcpy(selected_filename, "CAPTURE.WAV");
    memset(browser_entries, 0, sizeof(browser_entries));
    browser_page_index = 0U;
    browser_page_count = 0U;
    browser_entry_count = 0U;
}

const char *RecordingCatalog_GetSelected(void)
{
    return selected_filename;
}

bool RecordingCatalog_SetSelected(const char *filename)
{
    size_t length;

    if (filename == NULL)
    {
        return false;
    }

    length = strlen(filename);
    if ((length == 0U) || (length >= sizeof(selected_filename)))
    {
        return false;
    }

    strcpy(selected_filename, filename);
    return true;
}

static bool filename_is_wav(const char *filename)
{
    size_t length;

    if (filename == NULL)
    {
        return false;
    }

    length = strlen(filename);
    if (length < 4U)
    {
        return false;
    }

    return (filename[length - 4U] == '.') &&
           ((filename[length - 3U] == 'W') ||
            (filename[length - 3U] == 'w')) &&
           ((filename[length - 2U] == 'A') ||
            (filename[length - 2U] == 'a')) &&
           ((filename[length - 1U] == 'V') ||
            (filename[length - 1U] == 'v'));
}

FRESULT RecordingCatalog_LoadPage(uint32_t page_index)
{
    DIR directory;
    FILINFO information;
    FRESULT result;
    uint32_t wav_count = 0U;
    uint32_t first_entry = page_index * RECORDING_BROWSER_PAGE_SIZE;

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        return result;
    }

    result = f_opendir(&directory, "0:/");
    if (result != FR_OK)
    {
        return result;
    }

    memset(browser_entries, 0, sizeof(browser_entries));
    browser_entry_count = 0U;

    for (;;)
    {
        result = f_readdir(&directory, &information);
        if ((result != FR_OK) || (information.fname[0] == '\0'))
        {
            break;
        }

        if (((information.fattrib & AM_DIR) != 0U) ||
            !filename_is_wav(information.fname))
        {
            continue;
        }

        if ((wav_count >= first_entry) &&
            (browser_entry_count < RECORDING_BROWSER_PAGE_SIZE))
        {
            RecordingCatalogEntry *entry =
                &browser_entries[browser_entry_count];

            strncpy(entry->filename,
                    information.fname,
                    RECORDING_FILENAME_CAPACITY - 1U);
            entry->filename[RECORDING_FILENAME_CAPACITY - 1U] = '\0';
            entry->file_size = (uint32_t)information.fsize;
            browser_entry_count++;
        }

        wav_count++;
    }

    (void)f_closedir(&directory);
    if (result != FR_OK)
    {
        return result;
    }

    browser_page_count =
        (wav_count + RECORDING_BROWSER_PAGE_SIZE - 1U) /
        RECORDING_BROWSER_PAGE_SIZE;

    if (browser_page_count == 0U)
    {
        browser_page_index = 0U;
    }
    else if (page_index >= browser_page_count)
    {
        return RecordingCatalog_LoadPage(browser_page_count - 1U);
    }
    else
    {
        browser_page_index = page_index;
    }

    return FR_OK;
}

uint32_t RecordingCatalog_GetPageIndex(void)
{
    return browser_page_index;
}

uint32_t RecordingCatalog_GetPageCount(void)
{
    return browser_page_count;
}

uint32_t RecordingCatalog_GetPageEntryCount(void)
{
    return browser_entry_count;
}

const RecordingCatalogEntry *RecordingCatalog_GetPageEntry(uint32_t index)
{
    if (index >= browser_entry_count)
    {
        return NULL;
    }

    return &browser_entries[index];
}

bool RecordingCatalog_SelectPageEntry(uint32_t index)
{
    const RecordingCatalogEntry *entry =
        RecordingCatalog_GetPageEntry(index);

    if (entry == NULL)
    {
        return false;
    }

    return RecordingCatalog_SetSelected(entry->filename);
}

bool RecordingCatalog_EnsureSelectedValid(void)
{
    FILINFO information;
    FRESULT result;

    if (selected_filename[0] != '\0')
    {
        result = f_stat(selected_filename, &information);

        if ((result == FR_OK) &&
            ((information.fattrib & AM_DIR) == 0U) &&
            filename_is_wav(selected_filename))
        {
            return true;
        }
    }

    if (browser_entry_count != 0U)
    {
        return RecordingCatalog_SelectPageEntry(0U);
    }

    selected_filename[0] = '\0';
    return false;
}

FRESULT RecordingCatalog_StartNext(void)
{
    FILINFO info;
    FRESULT result;
    char candidate[RECORDING_FILENAME_CAPACITY];

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        return result;
    }

    for (uint32_t number = 1U; number <= 9999U; number++)
    {
        build_filename(number, candidate);
        result = f_stat(candidate, &info);

        if (result == FR_NO_FILE)
        {
            result = Recorder_StartFile(candidate);
            if (result == FR_OK)
            {
                strcpy(selected_filename, candidate);
            }
            return result;
        }

        if (result != FR_OK)
        {
            return result;
        }
    }

    return FR_DENIED;
}
