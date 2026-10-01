#ifndef WAV_FFT_VIEWER_H
#define WAV_FFT_VIEWER_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define WAV_FFT_VIEWER_SIZE  2048U
#define WAV_FFT_SCAN_MAX_EVENTS  5U
#define WAV_FFT_SCAN_STEP_SEGMENTS  4U
#define WAV_FFT_SCAN_MIN_SEPARATION_SEGMENTS  12U

typedef struct
{
    bool valid;
    uint32_t segment_index;
    uint32_t time_ms;
    uint32_t peak_frequency_hz;
    int16_t peak_db_tenths;
} WavFftPeakEvent;

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

/* RECORDED_FFT_TIMELINE_SCRUBBER_PATCH */
FRESULT WavFftViewer_LoadSegment(uint32_t segment_index);
void WavFftViewer_End(void);
const WavFftViewerInfo *WavFftViewer_GetInfo(void);

/* FFT_EVENT_SCAN_MARKERS_PATCH */
FRESULT WavFftViewer_ScanEvents(void);
uint32_t WavFftViewer_GetEventCount(void);
const WavFftPeakEvent *WavFftViewer_GetEvent(uint32_t index);

#endif
