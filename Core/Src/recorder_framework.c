#include "recorder_framework.h"

#include <string.h>

static RecorderStatus recorder_status;

void Recorder_FrameworkInit(void)
{
    memset(&recorder_status, 0, sizeof(recorder_status));
    recorder_status.state = RECORDER_STATE_STOPPED;
    recorder_status.sample_rate_hz = RECORDER_DEFAULT_SAMPLE_RATE_HZ;
    memcpy(recorder_status.filename, "REC0001.WAV", 12U);
    recorder_status.filename[12] = '\0';
}

void Recorder_Process(void)
{
    /*
     * Framework placeholder.
     *
     * The next milestone will move completed ADC/DMA blocks into the
     * SDRAM recording queue and drain full chunks through FatFs.
     * No filesystem work will be performed in an interrupt callback.
     */
}

const RecorderStatus *Recorder_GetStatus(void)
{
    return &recorder_status;
}

bool Recorder_IsActive(void)
{
    return recorder_status.state == RECORDER_STATE_RECORDING;
}
