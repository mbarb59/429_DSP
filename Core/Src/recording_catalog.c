#include "recording_catalog.h"

#include "filesystem_service.h"
#include "recorder_framework.h"

#include <stddef.h>
#include <string.h>

static char selected_filename[RECORDING_FILENAME_CAPACITY];

/* UI_WAV_BROWSER_V2_PATCH */
/* FFT_DISPLAY_CSV_BROWSER_COMBINED_PATCH */
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

static bool recording_duration_is_valid(
    uint32_t duration_seconds)
{
    return (duration_seconds == 5U) ||
           (duration_seconds == 10U) ||
           (duration_seconds == 30U) ||
           (duration_seconds == 60U);
}

FRESULT RecordingCatalog_CheckSpace(
    uint32_t duration_seconds,
    uint32_t sample_rate_hz,
    RecordingSpaceCheck *check)
{
    uint32_t total_kib = 0U;
    uint32_t free_kib = 0U;
    uint64_t bits_per_second;
    uint64_t data_bytes;
    uint64_t wav_bytes;
    uint64_t wav_kib;
    uint64_t required_kib;
    FRESULT result;

    if (check == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    memset(check, 0, sizeof(*check));
    check->duration_seconds = duration_seconds;
    check->sample_rate_hz = sample_rate_hz;
    check->reserve_kib = RECORDING_SPACE_RESERVE_KIB;
    check->filesystem_result = FR_OK;
    check->sufficient = false;

    if (!recording_duration_is_valid(duration_seconds) ||
        (sample_rate_hz == 0U))
    {
        check->filesystem_result = FR_INVALID_PARAMETER;
        return FR_INVALID_PARAMETER;
    }

    bits_per_second =
        (uint64_t)sample_rate_hz *
        RECORDING_CHANNEL_COUNT *
        RECORDING_BITS_PER_SAMPLE;

    data_bytes =
        (bits_per_second / 8U) * duration_seconds;
    wav_bytes =
        (uint64_t)RECORDING_WAV_HEADER_BYTES + data_bytes;
    wav_kib = (wav_bytes + 1023U) / 1024U;
    required_kib = wav_kib + RECORDING_SPACE_RESERVE_KIB;

    if ((wav_kib > UINT32_MAX) ||
        (required_kib > UINT32_MAX))
    {
        check->filesystem_result = FR_INVALID_PARAMETER;
        return FR_INVALID_PARAMETER;
    }

    check->wav_kib = (uint32_t)wav_kib;
    check->required_kib = (uint32_t)required_kib;

    result = FileSystem_GetSpace(&total_kib, &free_kib);
    check->filesystem_result = result;

    if (result != FR_OK)
    {
        return result;
    }

    check->available_kib = free_kib;
    check->sufficient = free_kib >= check->required_kib;
    return FR_OK;
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

static bool ext3(const char *name, char a, char b, char d)
{
    size_t n; char x, y, z;
    if (name == NULL) return false;
    n = strlen(name); if (n < 4U) return false;
    x=name[n-3U]; y=name[n-2U]; z=name[n-1U];
    if (x>='a'&&x<='z') x=(char)(x-'a'+'A');
    if (y>='a'&&y<='z') y=(char)(y-'a'+'A');
    if (z>='a'&&z<='z') z=(char)(z-'a'+'A');
    return name[n-4U]=='.' && x==a && y==b && z==d;
}
RecordingFileType RecordingCatalog_GetFileType(const char *name)
{
    if (ext3(name,'W','A','V')) return RECORDING_FILE_TYPE_WAV;
    if (ext3(name,'C','S','V')) return RECORDING_FILE_TYPE_CSV;
    return RECORDING_FILE_TYPE_NONE;
}
RecordingFileType RecordingCatalog_GetSelectedType(void)
{ return RecordingCatalog_GetFileType(selected_filename); }
bool RecordingCatalog_SelectedIsWav(void)
{ return RecordingCatalog_GetSelectedType()==RECORDING_FILE_TYPE_WAV; }
bool RecordingCatalog_SelectedIsCsv(void)
{ return RecordingCatalog_GetSelectedType()==RECORDING_FILE_TYPE_CSV; }
static bool filename_is_wav(const char *name)
{ return RecordingCatalog_GetFileType(name)==RECORDING_FILE_TYPE_WAV; }
static bool filename_is_browsable(const char *name)
{ return RecordingCatalog_GetFileType(name)!=RECORDING_FILE_TYPE_NONE; }

FRESULT RecordingCatalog_LoadPage(uint32_t page_index)
{
    DIR directory;
    FILINFO information;
    FRESULT result;
    uint32_t file_count = 0U;
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
            !filename_is_browsable(information.fname))
        {
            continue;
        }

        if ((file_count >= first_entry) &&
            (browser_entry_count < RECORDING_BROWSER_PAGE_SIZE))
        {
            RecordingCatalogEntry *entry =
                &browser_entries[browser_entry_count];

            strncpy(entry->filename,
                    information.fname,
                    RECORDING_FILENAME_CAPACITY - 1U);
            entry->filename[RECORDING_FILENAME_CAPACITY - 1U] = '\0';
            entry->file_size = (uint32_t)information.fsize;
            entry->file_type = RecordingCatalog_GetFileType(information.fname);
            browser_entry_count++;
        }

        file_count++;
    }

    (void)f_closedir(&directory);
    if (result != FR_OK)
    {
        return result;
    }

    browser_page_count =
        (file_count + RECORDING_BROWSER_PAGE_SIZE - 1U) /
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
            filename_is_browsable(selected_filename))
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

static bool filename_character_is_valid(char character)
{
    return ((character >= 'A') && (character <= 'Z')) ||
           ((character >= '0') && (character <= '9')) ||
           (character == '_');
}

bool RecordingCatalog_NormalizeWavFilename(
    const char *input,
    char *output)
{
    size_t length;
    size_t base_length;

    if ((input == NULL) || (output == NULL))
    {
        return false;
    }

    length = strlen(input);
    if ((length < 5U) || (length >= RECORDING_FILENAME_CAPACITY))
    {
        return false;
    }

    base_length = length - 4U;
    if ((base_length == 0U) || (base_length > 8U) ||
        (input[base_length] != '.'))
    {
        return false;
    }

    for (size_t index = 0U; index < base_length; index++)
    {
        char character = input[index];
        if ((character >= 'a') && (character <= 'z'))
        {
            character = (char)(character - 'a' + 'A');
        }
        if (!filename_character_is_valid(character))
        {
            return false;
        }
        output[index] = character;
    }

    if (((input[base_length + 1U] != 'W') &&
         (input[base_length + 1U] != 'w')) ||
        ((input[base_length + 2U] != 'A') &&
         (input[base_length + 2U] != 'a')) ||
        ((input[base_length + 3U] != 'V') &&
         (input[base_length + 3U] != 'v')))
    {
        return false;
    }

    output[base_length] = '.';
    output[base_length + 1U] = 'W';
    output[base_length + 2U] = 'A';
    output[base_length + 3U] = 'V';
    output[base_length + 4U] = '\0';
    return true;
}

FRESULT RecordingCatalog_RenameFile(
    const char *source,
    const char *target)
{
    FILINFO information;
    FRESULT result;
    char normalized_source[RECORDING_FILENAME_CAPACITY];
    char normalized_target[RECORDING_FILENAME_CAPACITY];

    if (!RecordingCatalog_NormalizeWavFilename(
            source, normalized_source) ||
        !RecordingCatalog_NormalizeWavFilename(
            target, normalized_target))
    {
        return FR_INVALID_NAME;
    }

    if (strcmp(normalized_source, normalized_target) == 0)
    {
        return FR_INVALID_NAME;
    }

    result = FileSystem_Mount();
    if (result != FR_OK) return result;

    result = f_stat(normalized_source, &information);
    if (result != FR_OK) return result;
    if ((information.fattrib & AM_DIR) != 0U) return FR_DENIED;

    result = f_stat(normalized_target, &information);
    if (result == FR_OK) return FR_EXIST;
    if (result != FR_NO_FILE) return result;

    result = f_rename(normalized_source, normalized_target);
    if ((result == FR_OK) &&
        (strcmp(selected_filename, normalized_source) == 0))
    {
        strcpy(selected_filename, normalized_target);
    }
    return result;
}

FRESULT RecordingCatalog_DeleteFile(const char *filename)
{
    FILINFO information;
    FRESULT result;
    size_t length;

    if (filename == NULL)
    {
        return FR_INVALID_NAME;
    }

    length = strlen(filename);
    if ((length == 0U) ||
        (length >= RECORDING_FILENAME_CAPACITY) ||
        !filename_is_browsable(filename))
    {
        return FR_INVALID_NAME;
    }

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        return result;
    }

    result = f_stat(filename, &information);
    if (result != FR_OK)
    {
        return result;
    }

    if ((information.fattrib & AM_DIR) != 0U)
    {
        return FR_DENIED;
    }

    result = f_unlink(filename);
    if ((result == FR_OK) &&
        (strcmp(selected_filename, filename) == 0))
    {
        selected_filename[0] = '\0';
    }

    return result;
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
