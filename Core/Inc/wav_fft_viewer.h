#ifndef WAV_FFT_VIEWER_H
#define WAV_FFT_VIEWER_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define WAV_FFT_VIEWER_SIZE  2048U

typedef struct
{
    bool active;
    FRESULT result;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint32_t segment_count;
    uint32_t segment_index;
    uint32_t segment_start_sample;
    uint32_t segment_time_ms;
} WavFftViewerInfo;

void WavFftViewer_Init(void);
FRESULT WavFftViewer_Begin(const char *filename);
FRESULT WavFftViewer_LoadPrevious(void);
FRESULT WavFftViewer_LoadNext(void);
void WavFftViewer_End(void);
const WavFftViewerInfo *WavFftViewer_GetInfo(void);

#endif
