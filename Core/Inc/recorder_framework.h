#ifndef RECORDER_FRAMEWORK_H
#define RECORDER_FRAMEWORK_H

#include <stdbool.h>
#include <stdint.h>

#define RECORDER_DEFAULT_SAMPLE_RATE_HZ  20000U
#define RECORDER_CHANNEL_COUNT               1U
#define RECORDER_BITS_PER_SAMPLE            16U
#define RECORDER_WRITE_CHUNK_BYTES         4096U

typedef enum
{
    RECORDER_STATE_STOPPED = 0,
    RECORDER_STATE_STARTING,
    RECORDER_STATE_RECORDING,
    RECORDER_STATE_STOPPING,
    RECORDER_STATE_ERROR
} RecorderState;

typedef struct
{
    RecorderState state;
    uint32_t sample_rate_hz;
    uint32_t samples_captured;
    uint32_t bytes_queued;
    uint32_t bytes_written;
    uint32_t queue_high_water;
    uint32_t dropped_buffers;
    uint32_t write_errors;
    char filename[13];
} RecorderStatus;

void Recorder_FrameworkInit(void);
void Recorder_Process(void);
const RecorderStatus *Recorder_GetStatus(void);
bool Recorder_IsActive(void);

#endif
