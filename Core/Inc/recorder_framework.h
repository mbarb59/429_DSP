#ifndef RECORDER_FRAMEWORK_H
#define RECORDER_FRAMEWORK_H

#include "ff.h"

#include <stdbool.h>
#include <stdint.h>

#define RECORDER_SAMPLE_RATE_HZ     48000U
#define RECORDER_QUEUE_SIZE_BYTES   (64U * 1024U)
#define RECORDER_WRITE_BLOCK_BYTES  4096U

typedef enum
{
    RECORDER_STATE_IDLE = 0,
    RECORDER_STATE_RECORDING,
    RECORDER_STATE_STOPPING,
    RECORDER_STATE_ERROR
} RecorderState;

typedef struct
{
    RecorderState state;
    FRESULT last_result;
    uint32_t captured_samples;
    uint32_t queued_bytes;
    uint32_t queue_high_water_bytes;
    uint32_t written_bytes;
    uint32_t dropped_samples;
    uint32_t write_errors;
} RecorderStatus;

void Recorder_FrameworkInit(void);

FRESULT Recorder_StartRaw(void);
FRESULT Recorder_StopRaw(void);
void Recorder_Process(void);

bool Recorder_SubmitAdcSamples(
    const uint16_t *samples,
    uint32_t sample_count);

void Recorder_GetStatus(
    RecorderStatus *status);

#endif
