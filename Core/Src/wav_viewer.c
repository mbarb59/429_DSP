#include "wav_viewer.h"

#include "filesystem_service.h"

#include <stddef.h>
#include <string.h>

#define WAV_VIEWER_HEADER_SIZE       44U
#define WAV_VIEWER_READ_SAMPLES      256U

static WavViewerData viewer;
static uint8_t read_buffer[WAV_VIEWER_READ_SAMPLES * 2U];

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

void WavViewer_Init(void)
{
    memset(&viewer, 0, sizeof(viewer));
    viewer.result = FR_OK;
}

const WavViewerData *WavViewer_GetData(void)
{
    return &viewer;
}

FRESULT WavViewer_Load(const char *filename)
{
    FIL file;
    FRESULT result;
    UINT bytes_read = 0U;
    uint8_t header[WAV_VIEWER_HEADER_SIZE];
    uint32_t sample_index = 0U;
    uint32_t column = 0U;
    uint32_t column_end = 0U;

    WavViewer_Init();

    if ((filename == NULL) || (filename[0] == '\0'))
    {
        viewer.result = FR_INVALID_NAME;
        return viewer.result;
    }

    result = FileSystem_Mount();
    if (result != FR_OK)
    {
        viewer.result = result;
        return result;
    }

    result = f_open(&file, filename, FA_READ);
    if (result != FR_OK)
    {
        viewer.result = result;
        return result;
    }

    viewer.file_size = (uint32_t)f_size(&file);

    result = f_read(&file, header, sizeof(header), &bytes_read);
    if ((result != FR_OK) || (bytes_read != sizeof(header)))
    {
        viewer.result = (result != FR_OK) ? result : FR_DISK_ERR;
        (void)f_close(&file);
        return viewer.result;
    }

    if ((memcmp(&header[0], "RIFF", 4U) != 0) ||
        (memcmp(&header[8], "WAVE", 4U) != 0) ||
        (memcmp(&header[12], "fmt ", 4U) != 0) ||
        (memcmp(&header[36], "data", 4U) != 0) ||
        (read_le32(&header[16]) != 16U) ||
        (read_le16(&header[20]) != 1U) ||
        (read_le16(&header[22]) != 1U) ||
        (read_le16(&header[34]) != 16U))
    {
        viewer.result = FR_INVALID_OBJECT;
        (void)f_close(&file);
        return viewer.result;
    }

    viewer.sample_rate = read_le32(&header[24]);
    viewer.data_bytes = read_le32(&header[40]);
    viewer.sample_count = viewer.data_bytes / 2U;

    if ((viewer.sample_rate == 0U) ||
        (viewer.sample_count == 0U) ||
        (viewer.file_size != (WAV_VIEWER_HEADER_SIZE + viewer.data_bytes)))
    {
        viewer.result = FR_INVALID_OBJECT;
        (void)f_close(&file);
        return viewer.result;
    }

    viewer.duration_ms = (uint32_t)(
        ((uint64_t)viewer.sample_count * 1000ULL) /
        viewer.sample_rate);

    for (column = 0U; column < WAV_VIEWER_COLUMN_COUNT; column++)
    {
        viewer.minimum[column] = INT16_MAX;
        viewer.maximum[column] = INT16_MIN;
    }

    column = 0U;
    column_end = (uint32_t)(
        ((uint64_t)(column + 1U) * viewer.sample_count) /
        WAV_VIEWER_COLUMN_COUNT);

    while (sample_index < viewer.sample_count)
    {
        uint32_t remaining = viewer.sample_count - sample_index;
        uint32_t request_samples = WAV_VIEWER_READ_SAMPLES;

        if (request_samples > remaining)
        {
            request_samples = remaining;
        }

        result = f_read(
            &file,
            read_buffer,
            request_samples * 2U,
            &bytes_read);

        if ((result != FR_OK) ||
            (bytes_read != (request_samples * 2U)))
        {
            viewer.result = (result != FR_OK) ? result : FR_DISK_ERR;
            (void)f_close(&file);
            return viewer.result;
        }

        for (uint32_t index = 0U; index < request_samples; index++)
        {
            int16_t sample = (int16_t)read_le16(&read_buffer[index * 2U]);
            uint32_t absolute = (sample < 0) ?
                (uint32_t)(-(int32_t)sample) : (uint32_t)sample;

            while ((sample_index >= column_end) &&
                   (column < (WAV_VIEWER_COLUMN_COUNT - 1U)))
            {
                column++;
                column_end = (uint32_t)(
                    ((uint64_t)(column + 1U) * viewer.sample_count) /
                    WAV_VIEWER_COLUMN_COUNT);
            }

            if (sample < viewer.minimum[column])
            {
                viewer.minimum[column] = sample;
            }

            if (sample > viewer.maximum[column])
            {
                viewer.maximum[column] = sample;
            }

            if (absolute > viewer.peak_absolute)
            {
                viewer.peak_absolute = absolute;
            }

            sample_index++;
        }
    }

    result = f_close(&file);
    viewer.result = result;
    viewer.valid = (result == FR_OK);
    return result;
}
