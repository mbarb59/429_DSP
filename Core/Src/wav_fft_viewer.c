#include "wav_fft_viewer.h"

#include "capture.h"
#include "fft.h"
#include "filesystem_service.h"

#include <stddef.h>
#include <string.h>

#define WAV_FFT_HEADER_SIZE   44U
#define WAV_FFT_READ_SAMPLES  256U

static WavFftViewerInfo info;
static char active_filename[32];
static uint8_t read_buffer[WAV_FFT_READ_SAMPLES * 2U];
static uint32_t previous_capture_count;
static uint32_t previous_fft_size;

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] |
           ((uint16_t)data[1] << 8U);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static FRESULT load_segment(uint32_t segment_index)
{
    FIL file;
    FRESULT result;
    UINT bytes_read = 0U;
    uint32_t segment_start;
    uint32_t available_samples;
    uint32_t loaded_samples = 0U;

    if (!info.active || (segment_index >= info.segment_count))
    {
        info.result = FR_INVALID_OBJECT;
        return info.result;
    }

    result = f_open(&file, active_filename, FA_READ);
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    segment_start = segment_index * WAV_FFT_VIEWER_SIZE;
    available_samples = info.sample_count - segment_start;

    result = f_lseek(
        &file,
        WAV_FFT_HEADER_SIZE + (segment_start * 2U));

    if (result != FR_OK)
    {
        info.result = result;
        (void)f_close(&file);
        return result;
    }

    while ((loaded_samples < WAV_FFT_VIEWER_SIZE) &&
           (loaded_samples < available_samples))
    {
        uint32_t remaining_segment =
            WAV_FFT_VIEWER_SIZE - loaded_samples;
        uint32_t remaining_file =
            available_samples - loaded_samples;
        uint32_t request_samples = WAV_FFT_READ_SAMPLES;

        if (request_samples > remaining_segment)
        {
            request_samples = remaining_segment;
        }
        if (request_samples > remaining_file)
        {
            request_samples = remaining_file;
        }

        result = f_read(
            &file,
            read_buffer,
            request_samples * 2U,
            &bytes_read);

        if ((result != FR_OK) ||
            (bytes_read != (request_samples * 2U)))
        {
            info.result = (result != FR_OK) ? result : FR_DISK_ERR;
            (void)f_close(&file);
            return info.result;
        }

        for (uint32_t index = 0U; index < request_samples; index++)
        {
            int16_t pcm =
                (int16_t)read_le16(&read_buffer[index * 2U]);
            int32_t adc = 2048 + ((int32_t)pcm / 16);

            if (adc < 0)
            {
                adc = 0;
            }
            else if (adc > 4095)
            {
                adc = 4095;
            }

            sampleBuffer[loaded_samples + index] = (uint16_t)adc;
        }

        loaded_samples += request_samples;
    }

    while (loaded_samples < WAV_FFT_VIEWER_SIZE)
    {
        sampleBuffer[loaded_samples] = 2048U;
        loaded_samples++;
    }

    result = f_close(&file);
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    sampleCount = WAV_FFT_VIEWER_SIZE;
    sampleRate = info.sample_rate;
    captureComplete = 1;

    if (!FFT_Process())
    {
        info.result = FR_INT_ERR;
        return info.result;
    }

    info.segment_index = segment_index;
    info.segment_start_sample = segment_start;
    info.segment_time_ms = (uint32_t)(
        ((uint64_t)segment_start * 1000ULL) /
        info.sample_rate);
    info.result = FR_OK;
    return FR_OK;
}

void WavFftViewer_Init(void)
{
    memset(&info, 0, sizeof(info));
    memset(active_filename, 0, sizeof(active_filename));
    info.result = FR_OK;
}

const WavFftViewerInfo *WavFftViewer_GetInfo(void)
{
    return &info;
}

FRESULT WavFftViewer_Begin(const char *filename)
{
    FIL file;
    FRESULT result;
    UINT bytes_read = 0U;
    uint8_t header[WAV_FFT_HEADER_SIZE];
    uint32_t file_size;
    uint32_t data_bytes;

    WavFftViewer_Init();

    if ((filename == NULL) ||
        (filename[0] == '\0') ||
        (strlen(filename) >= sizeof(active_filename)))
    {
        info.result = FR_INVALID_NAME;
        return info.result;
    }

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    result = f_open(&file, filename, FA_READ);
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    file_size = (uint32_t)f_size(&file);
    result = f_read(&file, header, sizeof(header), &bytes_read);

    if ((result != FR_OK) || (bytes_read != sizeof(header)))
    {
        info.result = (result != FR_OK) ? result : FR_DISK_ERR;
        (void)f_close(&file);
        return info.result;
    }

    data_bytes = read_le32(&header[40]);

    if ((memcmp(&header[0], "RIFF", 4U) != 0) ||
        (memcmp(&header[8], "WAVE", 4U) != 0) ||
        (memcmp(&header[12], "fmt ", 4U) != 0) ||
        (memcmp(&header[36], "data", 4U) != 0) ||
        (read_le32(&header[16]) != 16U) ||
        (read_le16(&header[20]) != 1U) ||
        (read_le16(&header[22]) != 1U) ||
        (read_le16(&header[34]) != 16U) ||
        (read_le32(&header[24]) == 0U) ||
        (data_bytes == 0U) ||
        (file_size != (WAV_FFT_HEADER_SIZE + data_bytes)))
    {
        info.result = FR_INVALID_OBJECT;
        (void)f_close(&file);
        return info.result;
    }

    result = f_close(&file);
    if (result != FR_OK)
    {
        info.result = result;
        return result;
    }

    previous_capture_count = Capture_GetSampleCount();
    previous_fft_size = FFT_GetSize();

    if (!Capture_SetSampleCount(WAV_FFT_VIEWER_SIZE))
    {
        info.result = FR_INT_ERR;
        return info.result;
    }

    if (!FFT_SetSize(WAV_FFT_VIEWER_SIZE))
    {
        (void)Capture_SetSampleCount(previous_capture_count);
        info.result = FR_INT_ERR;
        return info.result;
    }

    strcpy(active_filename, filename);
    info.sample_rate = read_le32(&header[24]);
    info.sample_count = data_bytes / 2U;
    info.segment_count =
        (info.sample_count + WAV_FFT_VIEWER_SIZE - 1U) /
        WAV_FFT_VIEWER_SIZE;
    info.active = true;

    result = load_segment(0U);
    if (result != FR_OK)
    {
        WavFftViewer_End();
    }

    return result;
}

FRESULT WavFftViewer_LoadPrevious(void)
{
    if (!info.active)
    {
        return FR_INVALID_OBJECT;
    }

    if (info.segment_index == 0U)
    {
        return load_segment(0U);
    }

    return load_segment(info.segment_index - 1U);
}

FRESULT WavFftViewer_LoadNext(void)
{
    if (!info.active)
    {
        return FR_INVALID_OBJECT;
    }

    if ((info.segment_index + 1U) >= info.segment_count)
    {
        return load_segment(info.segment_index);
    }

    return load_segment(info.segment_index + 1U);
}

void WavFftViewer_End(void)
{
    if (!info.active)
    {
        return;
    }

    info.active = false;
    (void)Capture_SetSampleCount(previous_capture_count);
    (void)FFT_SetSize(previous_fft_size);
    sampleCount = 0U;
    captureComplete = 1;
    FFT_Invalidate();
}
