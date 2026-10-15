#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#define CAPTURE_SAMPLE_COUNT_DEFAULT  256U
#define CAPTURE_SAMPLE_COUNT_MAX      2048U
#define CAPTURE_DEFAULT_RATE_HZ       48000U

/*
 * Temporary compatibility alias for modules that are still fixed at
 * 256 samples. Remove this after fft.c and the UI become runtime-sized.
 */
#define SAMPLE_COUNT  CAPTURE_SAMPLE_COUNT_DEFAULT

extern uint16_t sampleBuffer[CAPTURE_SAMPLE_COUNT_MAX];
extern uint32_t sampleCount;
extern volatile int captureComplete;
extern uint32_t dmaCaptureCycles;
extern uint32_t sampleRate;

#define CAPTURE_XYZ_FRAME_COUNT 2048U
#define CAPTURE_XYZ_AXIS_COUNT  3U
#define CAPTURE_XYZ_VALUE_COUNT (CAPTURE_XYZ_FRAME_COUNT * CAPTURE_XYZ_AXIS_COUNT)
extern uint16_t xyzSampleBuffer[CAPTURE_XYZ_VALUE_COUNT];
extern uint32_t xyzFrameCount;
void DMA_CaptureXYZSamples(void);
bool Capture_IsXYZActive(void);

void Capture_Samples(void);
void DMA_Init(void);
void DMA_CaptureSamples(void);


bool Capture_SetSampleRate(uint32_t rate_hz);
uint32_t Capture_GetSampleRate(void);

bool Capture_SetSampleCount(uint32_t count);
uint32_t Capture_GetSampleCount(void);
bool Capture_IsValidSampleCount(uint32_t count);


/*
 * REC5_CONTINUOUS_WAV_API
 *
 * Separate continuous recording mode. The existing snapshot
 * capture interface remains unchanged.
 */
bool Capture_StartRecorder(
    uint32_t duration_seconds);

void Capture_StopRecorder(void);
void Capture_ProcessRecorder(void);

bool Capture_IsRecorderActive(void);

uint32_t Capture_GetRecorderOverruns(void);

/* RECORDER_RMS_CLIPPING_PATCH */
uint32_t Capture_GetRecorderRms(void);
uint32_t Capture_GetRecorderClipCount(void);
bool Capture_IsRecorderClipping(void);

#endif /* CAPTURE_H */
