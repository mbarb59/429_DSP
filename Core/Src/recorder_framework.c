#include "recorder_framework.h"

#include "filesystem_service.h"

#include <stddef.h>
#include <string.h>

#define RECORDER_RAW_FILENAME  "CAPTURE.WAV"

static void wav_write_u16(
    uint8_t *p,
    uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void wav_write_u32(
    uint8_t *p,
    uint32_t value)
{
    p[0] = (uint8_t)(value & 0xFFU);
    p[1] = (uint8_t)((value >> 8) & 0xFFU);
    p[2] = (uint8_t)((value >> 16) & 0xFFU);
    p[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static FRESULT Recorder_WriteWavHeader(
    FIL *file,
    uint32_t sample_count)
{
    UINT written;
    uint8_t h[44];
    uint32_t data_bytes =
        sample_count * sizeof(uint16_t);
    uint32_t riff_size =
        data_bytes + 36U;

    memset(h, 0, sizeof(h));

    memcpy(&h[0],  "RIFF", 4);
    wav_write_u32(&h[4], riff_size);
    memcpy(&h[8],  "WAVE", 4);

    memcpy(&h[12], "fmt ", 4);
    wav_write_u32(&h[16], 16);
    wav_write_u16(&h[20], 1);
    wav_write_u16(&h[22], 1);

    wav_write_u32(
        &h[24],
        RECORDER_SAMPLE_RATE_HZ);

    wav_write_u32(
        &h[28],
        RECORDER_SAMPLE_RATE_HZ * 2U);

    wav_write_u16(&h[32], 2);
    wav_write_u16(&h[34], 16);

    memcpy(&h[36], "data", 4);
    wav_write_u32(&h[40], data_bytes);

    if (f_lseek(file, 0U) != FR_OK)
    {
        return FR_DISK_ERR;
    }

    if (f_write(
            file,
            h,
            sizeof(h),
            &written) != FR_OK)
    {
        return FR_DISK_ERR;
    }

    if (written != sizeof(h))
    {
        return FR_DISK_ERR;
    }

    return FR_OK;
}



static FIL recorder_file;
static RecorderStatus recorder_status;
static uint8_t recorder_queue[RECORDER_QUEUE_SIZE_BYTES];
static uint8_t recorder_write_buffer[RECORDER_WRITE_BLOCK_BYTES];
static uint32_t recorder_read_index;
static uint32_t recorder_write_index;

static uint32_t Recorder_FreeBytes(void)
{
    return RECORDER_QUEUE_SIZE_BYTES -
           recorder_status.queued_bytes;
}

static void Recorder_QueueByte(uint8_t value)
{
    recorder_queue[recorder_write_index] = value;
    recorder_write_index++;

    if (recorder_write_index >=
        RECORDER_QUEUE_SIZE_BYTES)
    {
        recorder_write_index = 0U;
    }

    recorder_status.queued_bytes++;

    if (recorder_status.queued_bytes >
        recorder_status.queue_high_water_bytes)
    {
        recorder_status.queue_high_water_bytes =
            recorder_status.queued_bytes;
    }
}

static uint32_t Recorder_Dequeue(
    uint8_t *destination,
    uint32_t byte_count)
{
    uint32_t copied = 0U;

    while ((copied < byte_count) &&
           (recorder_status.queued_bytes != 0U))
    {
        destination[copied] =
            recorder_queue[recorder_read_index];

        recorder_read_index++;

        if (recorder_read_index >=
            RECORDER_QUEUE_SIZE_BYTES)
        {
            recorder_read_index = 0U;
        }

        recorder_status.queued_bytes--;
        copied++;
    }

    return copied;
}

static FRESULT Recorder_WriteQueuedBlock(
    bool allow_partial)
{
    UINT bytes_written = 0U;
    uint32_t requested_bytes;
    FRESULT result;

    if (recorder_status.queued_bytes >=
        RECORDER_WRITE_BLOCK_BYTES)
    {
        requested_bytes =
            RECORDER_WRITE_BLOCK_BYTES;
    }
    else if (allow_partial &&
             (recorder_status.queued_bytes != 0U))
    {
        requested_bytes =
            recorder_status.queued_bytes;
    }
    else
    {
        return FR_OK;
    }

    requested_bytes = Recorder_Dequeue(
        recorder_write_buffer,
        requested_bytes);

    result = f_write(
        &recorder_file,
        recorder_write_buffer,
        (UINT)requested_bytes,
        &bytes_written);

    if ((result != FR_OK) ||
        (bytes_written != requested_bytes))
    {
        recorder_status.write_errors++;
        recorder_status.last_result =
            (result == FR_OK) ?
            FR_DISK_ERR :
            result;
        recorder_status.state =
            RECORDER_STATE_ERROR;

        return recorder_status.last_result;
    }

    recorder_status.written_bytes +=
        bytes_written;

    return FR_OK;
}

void Recorder_FrameworkInit(void)
{
    memset(
        &recorder_status,
        0,
        sizeof(recorder_status));

    recorder_status.state =
        RECORDER_STATE_IDLE;
    recorder_status.last_result = FR_OK;
    recorder_read_index = 0U;
    recorder_write_index = 0U;
}

FRESULT Recorder_StartRaw(void)
{
    FRESULT result;

    if (recorder_status.state !=
        RECORDER_STATE_IDLE)
    {
        return FR_LOCKED;
    }

    result = FileSystem_Mount();

    if (result != FR_OK)
    {
        recorder_status.last_result = result;
        recorder_status.state =
            RECORDER_STATE_ERROR;
        return result;
    }

    result = f_open(
        &recorder_file,
        RECORDER_RAW_FILENAME,
        FA_CREATE_ALWAYS | FA_WRITE);

    if (result != FR_OK)
    {
        recorder_status.last_result = result;
        recorder_status.state =
            RECORDER_STATE_ERROR;
        return result;
    }

    result =
        Recorder_WriteWavHeader(
            &recorder_file,
            0U);

    if (result != FR_OK)
    {
        f_close(&recorder_file);
        recorder_status.last_result =
            result;
        recorder_status.state =
            RECORDER_STATE_ERROR;
        return result;
    }

    recorder_status.captured_samples = 0U;
    recorder_status.queued_bytes = 0U;
    recorder_status.queue_high_water_bytes = 0U;
    recorder_status.written_bytes = 0U;
    recorder_status.dropped_samples = 0U;
    recorder_status.write_errors = 0U;
    recorder_status.last_result = FR_OK;
    recorder_read_index = 0U;
    recorder_write_index = 0U;
    
    f_lseek(
        &recorder_file,
        44U);

    recorder_status.state =
        RECORDER_STATE_RECORDING;


    return FR_OK;
}

FRESULT Recorder_StopRaw(void)
{
    if (recorder_status.state ==
        RECORDER_STATE_RECORDING)
    {
        recorder_status.state =
            RECORDER_STATE_STOPPING;
        return FR_OK;
    }

    if (recorder_status.state ==
        RECORDER_STATE_IDLE)
    {
        return FR_OK;
    }

    return recorder_status.last_result;
}

bool Recorder_SubmitAdcSamples(
    const uint16_t *samples,
    uint32_t sample_count)
{
    uint32_t required_bytes;

    if ((samples == NULL) ||
        (sample_count == 0U) ||
        (recorder_status.state !=
         RECORDER_STATE_RECORDING))
    {
        return false;
    }

    required_bytes = sample_count * 2U;

    if (Recorder_FreeBytes() < required_bytes)
    {
        recorder_status.dropped_samples +=
            sample_count;
        return false;
    }

    for (uint32_t index = 0U;
         index < sample_count;
         index++)
    {
        int32_t centered_sample;
        int16_t pcm_sample;
        uint16_t encoded_sample;

        /*
         * Convert the unsigned 12-bit ADC sample into
         * signed 16-bit PCM.
         *
         * ADC 0    becomes -32768
         * ADC 2048 becomes      0
         * ADC 4095 becomes  32752
         */
        centered_sample =
            (int32_t)(samples[index] & 0x0FFFU) -
            2048;

        pcm_sample =
            (int16_t)(centered_sample * 16);

        /*
         * Preserve the signed PCM bit pattern and queue
         * the sample in WAV little-endian byte order.
         */
        encoded_sample = (uint16_t)pcm_sample;

        Recorder_QueueByte(
            (uint8_t)(encoded_sample & 0xFFU));

        Recorder_QueueByte(
            (uint8_t)(encoded_sample >> 8U));
    }

    recorder_status.captured_samples +=
        sample_count;

    return true;
}

void Recorder_Process(void)
{
    if (recorder_status.state ==
        RECORDER_STATE_RECORDING)
    {
        (void)Recorder_WriteQueuedBlock(false);
        return;
    }

    if (recorder_status.state ==
        RECORDER_STATE_STOPPING)
    {
        FRESULT result;

        while ((recorder_status.queued_bytes != 0U) &&
               (recorder_status.state ==
                RECORDER_STATE_STOPPING))
        {
            result =
                Recorder_WriteQueuedBlock(true);

            if (result != FR_OK)
            {
                return;
            }
        }

        result =
            Recorder_WriteWavHeader(
                &recorder_file,
                recorder_status.captured_samples);

        if (result == FR_OK)
        {
            result = f_sync(&recorder_file);
        }

        if (result == FR_OK)
        {
            result = f_close(&recorder_file);
        }

        recorder_status.last_result = result;
        recorder_status.state =
            (result == FR_OK) ?
            RECORDER_STATE_IDLE :
            RECORDER_STATE_ERROR;

        if (result != FR_OK)
        {
            recorder_status.write_errors++;
        }
    }
}

void Recorder_GetStatus(
    RecorderStatus *status)
{
    if (status != NULL)
    {
        *status = recorder_status;
    }
}
