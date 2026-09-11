#include "sd_diskio.h"
#include "storage_framework.h"
#include "sd_card.h"
#include <stddef.h>

static DSTATUS disk_state = STA_NOINIT;

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

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
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
        if (Storage_WriteSector(
                (uint32_t)sector + index,
                &buff[index * SD_CARD_SECTOR_SIZE]) != HAL_OK)
        {
            return RES_ERROR;
        }
    }

    return RES_OK;
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
