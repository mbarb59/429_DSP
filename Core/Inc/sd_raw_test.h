#ifndef SD_RAW_TEST_H
#define SD_RAW_TEST_H

#include "stm32f4xx_hal.h"

#include <stdint.h>

typedef struct
{
    HAL_StatusTypeDef spi_init_status;
    HAL_StatusTypeDef cmd0_status;
    uint8_t cmd0_r1;
    HAL_StatusTypeDef cmd8_status;
    uint8_t cmd8_r1;
    uint32_t cmd8_r7;
} SD_RawTestResult;

typedef struct
{
    uint32_t pb10_mode;
    uint32_t pc2_mode;
    uint32_t pc3_mode;
    uint32_t pb10_af;
    uint32_t pc2_af;
    uint32_t pc3_af;
    uint32_t spi2_cr1;
    uint32_t spi2_cr2;
    uint32_t spi2_sr;
} SD_RawIoInfo;

HAL_StatusTypeDef SD_RawTest_Run(
    SD_RawTestResult *result);

HAL_StatusTypeDef SD_RawTest_LoopbackBuffer(
    const uint8_t *transmit_buffer,
    uint8_t *receive_buffer,
    uint16_t length);

void SD_RawTest_GetIoInfo(SD_RawIoInfo *info);

#endif
