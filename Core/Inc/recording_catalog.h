#ifndef RECORDING_CATALOG_H
#define RECORDING_CATALOG_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define RECORDING_FILENAME_CAPACITY 13U
#define RECORDING_BROWSER_PAGE_SIZE  6U

/* RECORDING_SPACE_CHECK_PATCH */
#define RECORDING_WAV_HEADER_BYTES    44U
#define RECORDING_CHANNEL_COUNT        1U
#define RECORDING_BITS_PER_SAMPLE     16U
#define RECORDING_SPACE_RESERVE_KIB   64U

typedef struct
{
    char filename[RECORDING_FILENAME_CAPACITY];
    uint32_t file_size;
} RecordingCatalogEntry;

typedef struct
{
    uint32_t duration_seconds;
    uint32_t sample_rate_hz;
    uint32_t wav_kib;
    uint32_t reserve_kib;
    uint32_t required_kib;
    uint32_t available_kib;
    FRESULT filesystem_result;
    bool sufficient;
} RecordingSpaceCheck;

void RecordingCatalog_Init(void);
FRESULT RecordingCatalog_StartNext(void);
FRESULT RecordingCatalog_CheckSpace(
    uint32_t duration_seconds,
    uint32_t sample_rate_hz,
    RecordingSpaceCheck *check);
const char *RecordingCatalog_GetSelected(void);
bool RecordingCatalog_SetSelected(const char *filename);

/* UI_WAV_BROWSER_V2_PATCH */
FRESULT RecordingCatalog_LoadPage(uint32_t page_index);
uint32_t RecordingCatalog_GetPageIndex(void);
uint32_t RecordingCatalog_GetPageCount(void);
uint32_t RecordingCatalog_GetPageEntryCount(void);
const RecordingCatalogEntry *RecordingCatalog_GetPageEntry(uint32_t index);
bool RecordingCatalog_SelectPageEntry(uint32_t index);

/* UI_DEFAULT_WAV_SELECTION_PATCH */
bool RecordingCatalog_EnsureSelectedValid(void);

/* UI_DELETE_CONFIRMATION_PATCH */
FRESULT RecordingCatalog_DeleteFile(const char *filename);

/* CONSOLE_WAV_RENAME_PATCH */
bool RecordingCatalog_NormalizeWavFilename(
    const char *input,
    char *output);
FRESULT RecordingCatalog_RenameFile(
    const char *source,
    const char *target);

#endif
