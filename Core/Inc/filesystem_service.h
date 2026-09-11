#ifndef FILESYSTEM_SERVICE_H
#define FILESYSTEM_SERVICE_H

#include "ff.h"

#include <stdint.h>

FRESULT FileSystem_Mount(void);
FRESULT FileSystem_Unmount(void);
FRESULT FileSystem_ListRoot(void);
FRESULT FileSystem_GetSpace(
    uint32_t *total_kib,
    uint32_t *free_kib);
FRESULT FileSystem_VerifiedFileTest(void);

#endif
