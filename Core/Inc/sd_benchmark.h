#ifndef SD_BENCHMARK_H
#define SD_BENCHMARK_H

#include "ff.h"

#include <stdint.h>

#define SD_BENCHMARK_BLOCK_SIZE   4096U
#define SD_BENCHMARK_TOTAL_BYTES  (512U * 1024U)

typedef struct
{
    FRESULT result;

    uint32_t block_size;
    uint32_t requested_bytes;
    uint32_t written_bytes;
    uint32_t write_count;

    uint32_t elapsed_ms;
    uint32_t sync_ms;
    uint32_t worst_write_ms;

    uint32_t average_bytes_per_second;
    uint32_t verified_file_size;

    uint32_t verified_bytes;
    uint32_t mismatch_count;
    uint32_t first_mismatch_offset;

    uint32_t disk_write_calls;
    uint32_t single_sector_writes;
    uint32_t multi_sector_writes;
    uint32_t total_sectors_written;
    uint32_t largest_sector_count;
} SD_BenchmarkResult;

FRESULT SD_Benchmark_Run(
    SD_BenchmarkResult *result);

#endif
