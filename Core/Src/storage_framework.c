#include "storage_framework.h"

#include "sd_card.h"

#include <stddef.h>
#include <string.h>

static StorageStatus storage_status;
static uint8_t sector_buffer[STORAGE_SECTOR_SIZE];

HAL_StatusTypeDef Storage_Init(void)
{
    HAL_StatusTypeDef status;

    if (storage_status.state == STORAGE_STATE_READY)
    {
        return HAL_OK;
    }

    status = SD_Card_Init();
    storage_status.last_status = status;

    if (status == HAL_OK)
    {
        storage_status.state = STORAGE_STATE_READY;
    }
    else
    {
        storage_status.state = STORAGE_STATE_ERROR;
    }

    return status;
}

HAL_StatusTypeDef Storage_ReadSector(
    uint32_t sector_number,
    uint8_t *buffer)
{
    HAL_StatusTypeDef status;

    if (buffer == NULL)
    {
        return HAL_ERROR;
    }

    status = Storage_Init();

    if (status != HAL_OK)
    {
        storage_status.read_errors++;
        return status;
    }

    status = SD_Card_ReadSector(sector_number, buffer);
    storage_status.last_status = status;

    if (status == HAL_OK)
    {
        storage_status.successful_reads++;
    }
    else
    {
        storage_status.read_errors++;
        storage_status.state = STORAGE_STATE_ERROR;
    }

    return status;
}

HAL_StatusTypeDef Storage_WriteSector(
    uint32_t sector_number,
    const uint8_t *buffer)
{
    HAL_StatusTypeDef status;

    if (buffer == NULL)
    {
        return HAL_ERROR;
    }

    status = Storage_Init();

    if (status != HAL_OK)
    {
        storage_status.write_errors++;
        return status;
    }

    status = SD_Card_WriteSector(sector_number, buffer);
    storage_status.last_status = status;

    if (status == HAL_OK)
    {
        storage_status.successful_writes++;
    }
    else
    {
        storage_status.write_errors++;
        storage_status.state = STORAGE_STATE_ERROR;
    }

    return status;
}

HAL_StatusTypeDef Storage_ReadSectorZeroPreview(
    uint8_t *preview,
    uint32_t preview_size)
{
    HAL_StatusTypeDef status;

    if ((preview == NULL) ||
        (preview_size == 0U) ||
        (preview_size > STORAGE_SECTOR0_PREVIEW_BYTES))
    {
        return HAL_ERROR;
    }

    status = Storage_ReadSector(0U, sector_buffer);

    if (status != HAL_OK)
    {
        return status;
    }

    memcpy(preview, sector_buffer, preview_size);
    return HAL_OK;
}

const StorageStatus *Storage_GetStatus(void)
{
    return &storage_status;
}

bool Storage_IsReady(void)
{
    return storage_status.state == STORAGE_STATE_READY;
}
