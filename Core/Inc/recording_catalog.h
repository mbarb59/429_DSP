#ifndef RECORDING_CATALOG_H
#define RECORDING_CATALOG_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define RECORDING_FILENAME_CAPACITY 13U
#define RECORDING_BROWSER_PAGE_SIZE  6U

typedef struct
{
    char filename[RECORDING_FILENAME_CAPACITY];
    uint32_t file_size;
} RecordingCatalogEntry;

void RecordingCatalog_Init(void);
FRESULT RecordingCatalog_StartNext(void);
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

#endif
