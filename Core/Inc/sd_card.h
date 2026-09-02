#ifndef SD_CARD_H
#define SD_CARD_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Fundamental SD-card sector size.
 */
#define SD_CARD_SECTOR_SIZE  512U

/*
 * Card capacity and addressing type.
 */
typedef enum
{
    SD_CARD_TYPE_UNKNOWN = 0,
    SD_CARD_TYPE_SDSC,
    SD_CARD_TYPE_SDHC
} SD_CardType;

/*
 * Information collected during card initialization.
 */
typedef struct
{
    bool initialized;
    bool version_2;
    bool power_ready;
    bool supports_3v3;
    SD_CardType type;
    uint32_t ocr;
} SD_CardInfo;

/*
 * Configure the software-SPI pins, send the required startup clocks,
 * and issue CMD0.
 *
 * Expected successful response:
 *     R1 = 0x01
 */
HAL_StatusTypeDef SD_Card_Reset(
    uint8_t *r1_response);

/*
 * Issue CMD8 with argument 0x000001AA.
 *
 * A modern SD version 2 or later card should return:
 *     R1 = 0x01
 *     R7 = 0x000001AA
 */
HAL_StatusTypeDef SD_Card_CheckInterface(
    uint8_t *r1_response,
    uint32_t *r7_response);

/*
 * Perform the complete SD-card initialization sequence:
 *
 *     CMD0
 *     CMD8
 *     CMD55 plus ACMD41
 *     CMD58
 *
 * On success, SD_Card_GetInfo() returns the detected card
 * version, OCR value, power status, and addressing type.
 */
HAL_StatusTypeDef SD_Card_Init(void);

/*
 * Return the information collected during card initialization.
 *
 * The returned pointer remains valid for the lifetime of the program.
 * The caller must not modify the structure.
 */
HAL_StatusTypeDef SD_Card_ReadOcr(
    uint8_t *r1_response,
    uint32_t *ocr_response);

const SD_CardInfo *SD_Card_GetInfo(void);

/*
 * Read one 512-byte sector.
 *
 * For SDHC cards, sector_number is sent directly as the command
 * argument. For SDSC cards, the driver converts it to a byte address.
 *
 * This function will be implemented after initialization is stable.
 */
HAL_StatusTypeDef SD_Card_ReadSector(
    uint32_t sector_number,
    uint8_t *buffer);

/*
 * Write one 512-byte sector.
 *
 * Do not use this function for raw testing on a formatted card unless
 * the target sector is known to be safe. FatFs will eventually call
 * this through its disk I/O layer.
 *
 * This function will be implemented after sector reading is stable.
 */
HAL_StatusTypeDef SD_Card_WriteSector(
    uint32_t sector_number,
    const uint8_t *buffer);

#endif