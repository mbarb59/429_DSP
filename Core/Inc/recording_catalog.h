#ifndef RECORDING_CATALOG_H
#define RECORDING_CATALOG_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define RECORDING_FILENAME_CAPACITY 13U

void RecordingCatalog_Init(void);
FRESULT RecordingCatalog_StartNext(void);
const char *RecordingCatalog_GetSelected(void);
bool RecordingCatalog_SetSelected(const char *filename);

#endif
