#include "sd_diskio.h"
#include "storage_framework.h"
#include "sd_card.h"
#include <stddef.h>
#include <string.h>

static DSTATUS disk_state = STA_NOINIT;

static SD_DiskIoWriteStats sd_diskio_write_stats;

void SD_DiskIo_ResetWriteStats(void)
{
    memset(
        &sd_diskio_write_stats,
        0,
        sizeof(sd_diskio_write_stats));
}

void SD_DiskIo_GetWriteStats(
    SD_DiskIoWriteStats *stats)
{
    if (stats != NULL)
    {
        *stats = sd_diskio_write_stats;
    }
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0U)
    {
        return STA_NOINIT;
    }

    disk_state = (Storage_Init() == HAL_OK) ? 0U : STA_NOINIT;
    return disk_state;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0U)
    {
        return STA_NOINIT;
    }

    return Storage_IsReady() ? 0U : disk_state;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if ((pdrv != 0U) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }

    if (disk_initialize(pdrv) != 0U)
    {
        return RES_NOTRDY;
    }

    for (UINT index = 0U; index < count; index++)
    {
        if (Storage_ReadSector(
                (uint32_t)sector + index,
                &buff[index * SD_CARD_SECTOR_SIZE]) != HAL_OK)
        {
            return RES_ERROR;
        }
    }

    return RES_OK;
}

DRESULT disk_write(
    BYTE pdrv,
    const BYTE *buffer,
    LBA_t sector,
    UINT count)
{
    HAL_StatusTypeDef status;

    if (pdrv != 0U)
    {
        return RES_PARERR;
    }

    if ((buffer == NULL) ||
        (count == 0U))
    {
        return RES_PARERR;
    }

    sd_diskio_write_stats.write_calls++;
    sd_diskio_write_stats.total_sectors +=
        (uint32_t)count;

    if (count == 1U)
    {
        sd_diskio_write_stats.single_sector_calls++;
    }
    else
    {
        sd_diskio_write_stats.multi_sector_calls++;
    }

    if ((uint32_t)count >
        sd_diskio_write_stats.largest_sector_count)
    {
        sd_diskio_write_stats.largest_sector_count =
            (uint32_t)count;
    }

    status = Storage_WriteSectors(
        (uint32_t)sector,
        buffer,
        (uint32_t)count);

    return (status == HAL_OK) ?
           RES_OK :
           RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0U)
    {
        return RES_PARERR;
    }

    if (disk_initialize(pdrv) != 0U)
    {
        return RES_NOTRDY;
    }

    if (cmd == CTRL_SYNC)
    {
        return RES_OK;
    }

    if ((cmd == GET_SECTOR_SIZE) && (buff != NULL))
    {
        *(WORD *)buff = SD_CARD_SECTOR_SIZE;
        return RES_OK;
    }

    if ((cmd == GET_BLOCK_SIZE) && (buff != NULL))
    {
        *(DWORD *)buff = 1U;
        return RES_OK;
    }

    return RES_PARERR;
}

DWORD get_fattime(void)
{
    return ((DWORD)(2026U - 1980U) << 25U) |
           ((DWORD)1U << 21U) |
           ((DWORD)1U << 16U);
}
