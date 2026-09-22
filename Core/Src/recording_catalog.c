#include "recording_catalog.h"

#include "filesystem_service.h"
#include "recorder_framework.h"

#include <stddef.h>
#include <string.h>

static char selected_filename[RECORDING_FILENAME_CAPACITY];

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
