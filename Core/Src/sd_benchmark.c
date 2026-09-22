#include "sd_benchmark.h"

#include "filesystem_service.h"
#include "sd_diskio.h"
#include "stm32f4xx_hal.h"

#include <stddef.h>
#include <string.h>

#define SD_BENCHMARK_FILENAME  "BENCH.BIN"

static uint8_t benchmark_write_buffer[
    SD_BENCHMARK_BLOCK_SIZE];

static uint8_t benchmark_read_buffer[
    SD_BENCHMARK_BLOCK_SIZE];

static void SD_Benchmark_FillBuffer(
    uint32_t file_offset)
{
    for (uint32_t index = 0U;
         index < SD_BENCHMARK_BLOCK_SIZE;
         index++)
    {
        uint32_t absolute_offset =
            file_offset + index;

        benchmark_write_buffer[index] =
            (uint8_t)(((absolute_offset * 37U) ^
                       (absolute_offset >> 8U) ^
                       0xA5U) & 0xFFU);
    }
}

FRESULT SD_Benchmark_Run(
    SD_BenchmarkResult *result)
{
    FIL file;
    FILINFO file_info;
    FRESULT fat_result;
    UINT bytes_transferred;
    uint32_t start_ms;
    uint32_t write_start_ms;
    uint32_t write_elapsed_ms;
    uint32_t sync_start_ms;
    SD_DiskIoWriteStats disk_stats;

    if (result == NULL)
    {
        return FR_INVALID_PARAMETER;
    }

    memset(result, 0, sizeof(*result));
    result->result = FR_NOT_READY;
    result->block_size = SD_BENCHMARK_BLOCK_SIZE;
    result->requested_bytes =
        SD_BENCHMARK_TOTAL_BYTES;
    result->first_mismatch_offset =
        0xFFFFFFFFUL;

    fat_result = FileSystem_Mount();

    if (fat_result != FR_OK)
    {
        result->result = fat_result;
        return fat_result;
    }

    fat_result = f_unlink(
        SD_BENCHMARK_FILENAME);

    if ((fat_result != FR_OK) &&
        (fat_result != FR_NO_FILE))
    {
        result->result = fat_result;
        return fat_result;
    }

    fat_result = f_open(
        &file,
        SD_BENCHMARK_FILENAME,
        FA_CREATE_ALWAYS | FA_WRITE);

    if (fat_result != FR_OK)
    {
        result->result = fat_result;
        return fat_result;
    }

    SD_DiskIo_ResetWriteStats();
    start_ms = HAL_GetTick();

    while (result->written_bytes <
           result->requested_bytes)
    {
        uint32_t remaining =
            result->requested_bytes -
            result->written_bytes;

        UINT write_size =
            (remaining < SD_BENCHMARK_BLOCK_SIZE) ?
            (UINT)remaining :
            (UINT)SD_BENCHMARK_BLOCK_SIZE;

        SD_Benchmark_FillBuffer(
            result->written_bytes);

        bytes_transferred = 0U;
        write_start_ms = HAL_GetTick();

        fat_result = f_write(
            &file,
            benchmark_write_buffer,
            write_size,
            &bytes_transferred);

        write_elapsed_ms =
            HAL_GetTick() - write_start_ms;

        if (write_elapsed_ms >
            result->worst_write_ms)
        {
            result->worst_write_ms =
                write_elapsed_ms;
        }

        if (fat_result != FR_OK)
        {
            (void)f_close(&file);
            result->result = fat_result;
            return fat_result;
        }

        if (bytes_transferred != write_size)
        {
            (void)f_close(&file);
            result->result = FR_DISK_ERR;
            return FR_DISK_ERR;
        }

        result->written_bytes +=
            bytes_transferred;
        result->write_count++;
    }

    result->elapsed_ms =
        HAL_GetTick() - start_ms;

    sync_start_ms = HAL_GetTick();
    fat_result = f_sync(&file);
    result->sync_ms =
        HAL_GetTick() - sync_start_ms;

    if (fat_result != FR_OK)
    {
        (void)f_close(&file);
        result->result = fat_result;
        return fat_result;
    }

    fat_result = f_close(&file);

    if (fat_result != FR_OK)
    {
        result->result = fat_result;
        return fat_result;
    }

    SD_DiskIo_GetWriteStats(&disk_stats);
    result->disk_write_calls =
        disk_stats.write_calls;
    result->single_sector_writes =
        disk_stats.single_sector_calls;
    result->multi_sector_writes =
        disk_stats.multi_sector_calls;
    result->total_sectors_written =
        disk_stats.total_sectors;
    result->largest_sector_count =
        disk_stats.largest_sector_count;

    fat_result = f_stat(
        SD_BENCHMARK_FILENAME,
        &file_info);

    if (fat_result != FR_OK)
    {
        result->result = fat_result;
        return fat_result;
    }

    result->verified_file_size =
        (uint32_t)file_info.fsize;

    if (result->verified_file_size !=
        result->written_bytes)
    {
        result->result = FR_INT_ERR;
        return FR_INT_ERR;
    }

    if (result->elapsed_ms != 0U)
    {
        result->average_bytes_per_second =
            (uint32_t)(
                ((uint64_t)result->written_bytes *
                 1000ULL) /
                result->elapsed_ms);
    }

    fat_result = f_open(
        &file,
        SD_BENCHMARK_FILENAME,
        FA_READ);

    if (fat_result != FR_OK)
    {
        result->result = fat_result;
        return fat_result;
    }

    while (result->verified_bytes <
           result->written_bytes)
    {
        uint32_t remaining =
            result->written_bytes -
            result->verified_bytes;

        UINT read_size =
            (remaining < SD_BENCHMARK_BLOCK_SIZE) ?
            (UINT)remaining :
            (UINT)SD_BENCHMARK_BLOCK_SIZE;

        SD_Benchmark_FillBuffer(
            result->verified_bytes);

        bytes_transferred = 0U;

        fat_result = f_read(
            &file,
            benchmark_read_buffer,
            read_size,
            &bytes_transferred);

        if (fat_result != FR_OK)
        {
            (void)f_close(&file);
            result->result = fat_result;
            return fat_result;
        }

        if (bytes_transferred != read_size)
        {
            (void)f_close(&file);
            result->result = FR_INT_ERR;
            return FR_INT_ERR;
        }

        for (UINT index = 0U;
             index < read_size;
             index++)
        {
            if (benchmark_read_buffer[index] !=
                benchmark_write_buffer[index])
            {
                if (result->mismatch_count == 0U)
                {
                    result->first_mismatch_offset =
                        result->verified_bytes +
                        (uint32_t)index;
                }

                result->mismatch_count++;
            }
        }

        result->verified_bytes +=
            bytes_transferred;
    }

    fat_result = f_close(&file);

    if (fat_result != FR_OK)
    {
        result->result = fat_result;
        return fat_result;
    }

    if (result->mismatch_count != 0U)
    {
        result->result = FR_INT_ERR;
        return FR_INT_ERR;
    }

    fat_result = f_unlink(
        SD_BENCHMARK_FILENAME);

    if (fat_result != FR_OK)
    {
        result->result = fat_result;
        return fat_result;
    }

    result->result = FR_OK;
    return FR_OK;
}
