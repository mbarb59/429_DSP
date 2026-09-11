#ifndef SD_CARD_H
#define SD_CARD_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define SD_CARD_SECTOR_SIZE  512U

typedef enum
{
    SD_CARD_TYPE_UNKNOWN = 0,
    SD_CARD_TYPE_SDSC,
    SD_CARD_TYPE_SDHC
} SD_CardType;

typedef struct
{
    bool initialized;
    bool version_2;
    bool power_ready;
    bool supports_3v3;

    SD_CardType type;
    uint32_t ocr;

    uint8_t cmd0_r1;
    uint8_t cmd8_r1;
    uint8_t cmd55_r1;
    uint8_t acmd41_r1;

    uint32_t cmd8_r7;
    uint32_t cmd0_attempts;
    uint32_t acmd41_attempts;

    HAL_StatusTypeDef cmd8_status;
    HAL_StatusTypeDef cmd55_status;
    HAL_StatusTypeDef acmd41_status;
} SD_CardInfo;

HAL_StatusTypeDef SD_Card_Reset(
    uint8_t *r1_response);

HAL_StatusTypeDef SD_Card_CheckInterface(
    uint8_t *r1_response,
    uint32_t *r7_response);

HAL_StatusTypeDef SD_Card_ReadOcr(
    uint8_t *r1_response,
    uint32_t *ocr_response);

HAL_StatusTypeDef SD_Card_Init(void);

const SD_CardInfo *SD_Card_GetInfo(void);

HAL_StatusTypeDef SD_Card_ReadSector(
    uint32_t sector_number,
    uint8_t *buffer);

HAL_StatusTypeDef SD_Card_WriteSector(
    uint32_t sector_number,
    const uint8_t *buffer);

#endif
