#ifndef WAV_VIEWER_H
#define WAV_VIEWER_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define WAV_VIEWER_COLUMN_COUNT  320U

typedef struct
{
    bool valid;
    FRESULT result;
    uint32_t file_size;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint32_t duration_ms;
    uint32_t data_bytes;
    uint32_t peak_absolute;
    int16_t minimum[WAV_VIEWER_COLUMN_COUNT];
    int16_t maximum[WAV_VIEWER_COLUMN_COUNT];
} WavViewerData;

void WavViewer_Init(void);
FRESULT WavViewer_Load(const char *filename);
const WavViewerData *WavViewer_GetData(void);

#endif
