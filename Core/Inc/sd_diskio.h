#ifndef SD_DISKIO_H
#define SD_DISKIO_H
#include "diskio.h"
DSTATUS disk_initialize(BYTE pdrv);
DSTATUS disk_status(BYTE pdrv);
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);
DWORD get_fattime(void);
#endif
