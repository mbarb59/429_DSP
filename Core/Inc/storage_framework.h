#ifndef STORAGE_FRAMEWORK_H
#define STORAGE_FRAMEWORK_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define STORAGE_SECTOR_SIZE            512U
#define STORAGE_SECTOR0_PREVIEW_BYTES   64U
#define STORAGE_RECORD_QUEUE_ADDR       0xD0040000U
#define STORAGE_RECORD_QUEUE_SIZE       (128U * 1024U)
#define STORAGE_FILE_CACHE_ADDR         0xD0060000U
#define STORAGE_FILE_CACHE_SIZE         (16U * 1024U)
#define STORAGE_WAVE_PREVIEW_ADDR       0xD0064000U
#define STORAGE_WAVE_PREVIEW_SIZE       (32U * 1024U)

typedef enum
{
    STORAGE_STATE_UNINITIALIZED = 0,
    STORAGE_STATE_READY,
    STORAGE_STATE_ERROR
} StorageState;

typedef struct
{
    StorageState state;
    HAL_StatusTypeDef last_status;
    uint32_t successful_reads;
    uint32_t successful_writes;
    uint32_t read_errors;
    uint32_t write_errors;
} StorageStatus;

HAL_StatusTypeDef Storage_Init(void);

HAL_StatusTypeDef Storage_ReadSector(
    uint32_t sector_number,
    uint8_t *buffer);

HAL_StatusTypeDef Storage_WriteSector(
    uint32_t sector_number,
    const uint8_t *buffer);

HAL_StatusTypeDef Storage_ReadSectorZeroPreview(
    uint8_t *preview,
    uint32_t preview_size);

const StorageStatus *Storage_GetStatus(void);

bool Storage_IsReady(void);

#endif
