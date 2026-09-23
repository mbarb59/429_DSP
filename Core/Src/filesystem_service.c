#include "filesystem_service.h"

#include "uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static FATFS filesystem;
static bool filesystem_mounted;

static const uint8_t test_payload[] =
{
    '4', '2', '9', '_', 'D', 'S', 'P', ' ',
    'F', 'a', 't', 'F', 's', ' ', 't', 'e',
    's', 't', '\r', '\n',
    'S', 'P', 'I', '2', ' ',
    'P', 'B', '1', '0', '/',
    'P', 'C', '2', '/',
    'P', 'C', '3', '\r', '\n'
};

FRESULT FileSystem_Mount(void)
{
    FRESULT result;

    if (filesystem_mounted)
    {
        return FR_OK;
    }

    result = f_mount(
        &filesystem,
        "0:",
        1U);

    if (result == FR_OK)
    {
        filesystem_mounted = true;
    }

    return result;
}

FRESULT FileSystem_Unmount(void)
{
    FRESULT result;

    result = f_mount(
        NULL,
        "0:",
        0U);

    if (result == FR_OK)
    {
        filesystem_mounted = false;
    }

    return result;
}

FRESULT FileSystem_ListRoot(void)
{
    DIR directory;
    FILINFO information;
    FRESULT result;

    result = FileSystem_Mount();

    if (result != FR_OK)
    {
        return result;
    }

    result = f_opendir(
        &directory,
        "0:/");

    if (result != FR_OK)
    {
        return result;
    }

    for (;;)
    {
        result = f_readdir(
            &directory,
            &information);

        if ((result != FR_OK) ||
            (information.fname[0] == '\0'))
        {
            break;
        }

        if ((information.fattrib & AM_DIR) != 0U)
        {
            UART_SendString("[DIR]  ");
            UART_SendString(information.fname);
            UART_SendString("\r\n");
        }
        else
        {
            /* SDLS_FILE_SIZES_PATCH */
            UART_SendString("[FILE] ");
            UART_SendString(information.fname);
            UART_SendString("  ");
            UART_SendDec((uint32_t)information.fsize);
            UART_SendString(" bytes\r\n");
        }
    }

    (void)f_closedir(&directory);

    return result;
}

FRESULT FileSystem_GetSpace(
    uint32_t *total_kib,
    uint32_t *free_kib)
{
    FATFS *mounted_filesystem;
    DWORD free_clusters;
    FRESULT result;

    if ((total_kib == NULL) ||
        (free_kib == NULL))
    {
        return FR_INVALID_PARAMETER;
    }

    result = FileSystem_Mount();

    if (result != FR_OK)
    {
        return result;
    }

    result = f_getfree(
        "0:",
        &free_clusters,
        &mounted_filesystem);

    if (result != FR_OK)
    {
        return result;
    }

    *total_kib =
        (uint32_t)(((mounted_filesystem->n_fatent - 2U) *
                    mounted_filesystem->csize) /
                   2U);

    *free_kib =
        (uint32_t)((free_clusters *
                    mounted_filesystem->csize) /
                   2U);

    return FR_OK;
}

FRESULT FileSystem_VerifiedFileTest(void)
{
    FIL file;
    FRESULT result;
    UINT transferred;
    uint8_t readback[sizeof(test_payload)];

    result = FileSystem_Mount();

    if (result != FR_OK)
    {
        return result;
    }

    /*
     * Delete only our known test file. No other card content is touched.
     */
    (void)f_unlink("0:/STM32.TXT");

    result = f_open(
        &file,
        "0:/STM32.TXT",
        FA_CREATE_ALWAYS | FA_WRITE);

    if (result != FR_OK)
    {
        return result;
    }

    transferred = 0U;

    result = f_write(
        &file,
        test_payload,
        sizeof(test_payload),
        &transferred);

    if ((result != FR_OK) ||
        (transferred != sizeof(test_payload)))
    {
        (void)f_close(&file);
        return (result == FR_OK) ?
            FR_DISK_ERR : result;
    }

    result = f_sync(&file);

    if (result != FR_OK)
    {
        (void)f_close(&file);
        return result;
    }

    result = f_close(&file);

    if (result != FR_OK)
    {
        return result;
    }

    result = f_open(
        &file,
        "0:/STM32.TXT",
        FA_READ);

    if (result != FR_OK)
    {
        return result;
    }

    memset(
        readback,
        0,
        sizeof(readback));

    transferred = 0U;

    result = f_read(
        &file,
        readback,
        sizeof(readback),
        &transferred);

    (void)f_close(&file);

    if ((result != FR_OK) ||
        (transferred != sizeof(readback)) ||
        (memcmp(
            readback,
            test_payload,
            sizeof(readback)) != 0))
    {
        return (result == FR_OK) ?
            FR_DISK_ERR : result;
    }

    return FR_OK;
}
