/*
 * Minimal STM32F429 hardware-SPI diagnostic using alternate SPI2 pins.
 *
 * Wiring:
 *   PA5  -> SD CS
 *   PB10 -> SD CLK / SPI2_SCK
 *   PC2  <- SD DO  / SPI2_MISO
 *   PC3  -> SD DI  / SPI2_MOSI
 *
 * Loopback wiring:
 *   Disconnect the SD breakout.
 *   Connect PC3 directly to PC2.
 *
 * SPI2 configuration:
 *   Mode 0, MSB first, 8-bit, approximately 164 kHz with 42 MHz APB1.
 */

#include "sd_raw_test.h"

#include "stm32f429xx.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SD_CS_PORT              GPIOA
#define SD_CS_PIN               GPIO_PIN_5

#define SD_SPI_SCK_PORT         GPIOB
#define SD_SPI_SCK_PIN          GPIO_PIN_10

#define SD_SPI_DATA_PORT        GPIOC
#define SD_SPI_MISO_PIN         GPIO_PIN_2
#define SD_SPI_MOSI_PIN         GPIO_PIN_3

#define SD_IDLE_BYTE            0xFFU
#define SD_COMMAND_PACKET_SIZE  6U
#define SD_RESPONSE_ATTEMPTS    16U
#define SD_SPI_TIMEOUT_MS       100U

static SPI_HandleTypeDef sd_raw_spi;

static HAL_StatusTypeDef SD_RawSpi_Init(void);
static HAL_StatusTypeDef SD_RawSpi_Transfer(
    uint8_t transmit_byte,
    uint8_t *receive_byte);
static HAL_StatusTypeDef SD_RawSpi_Transmit(
    const uint8_t *buffer,
    uint16_t length);
static void SD_Raw_Select(void);
static void SD_Raw_Deselect(void);
static HAL_StatusTypeDef SD_Raw_SendStartupClocks(void);
static HAL_StatusTypeDef SD_Raw_SendCommand(
    const uint8_t *command_packet,
    uint8_t *r1_response);
static HAL_StatusTypeDef SD_Raw_ReadR7(
    uint32_t *r7_response);

static HAL_StatusTypeDef SD_RawSpi_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    SD_CS_PORT->BSRR = SD_CS_PIN;

    gpio.Pin = SD_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SD_CS_PORT, &gpio);

    gpio.Pin = SD_SPI_SCK_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(SD_SPI_SCK_PORT, &gpio);

    gpio.Pin = SD_SPI_MOSI_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(SD_SPI_DATA_PORT, &gpio);

    gpio.Pin = SD_SPI_MISO_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(SD_SPI_DATA_PORT, &gpio);

    __HAL_RCC_SPI2_FORCE_RESET();
    __HAL_RCC_SPI2_RELEASE_RESET();

    memset(&sd_raw_spi, 0, sizeof(sd_raw_spi));

    sd_raw_spi.Instance = SPI2;
    sd_raw_spi.Init.Mode = SPI_MODE_MASTER;
    sd_raw_spi.Init.Direction = SPI_DIRECTION_2LINES;
    sd_raw_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    sd_raw_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    sd_raw_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    sd_raw_spi.Init.NSS = SPI_NSS_SOFT;
    sd_raw_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    sd_raw_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    sd_raw_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    sd_raw_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    sd_raw_spi.Init.CRCPolynomial = 7U;

    if (HAL_SPI_Init(&sd_raw_spi) != HAL_OK)
    {
        return HAL_ERROR;
    }

    SD_Raw_Deselect();

    return HAL_OK;
}

HAL_StatusTypeDef SD_RawTest_LoopbackBuffer(
    const uint8_t *transmit_buffer,
    uint8_t *receive_buffer,
    uint16_t length)
{
    HAL_StatusTypeDef status;

    if ((transmit_buffer == NULL) ||
        (receive_buffer == NULL) ||
        (length == 0U))
    {
        return HAL_ERROR;
    }

    status = SD_RawSpi_Init();

    if (status != HAL_OK)
    {
        return status;
    }

    SD_Raw_Deselect();
    memset(receive_buffer, 0, length);

    return HAL_SPI_TransmitReceive(
        &sd_raw_spi,
        (uint8_t *)transmit_buffer,
        receive_buffer,
        length,
        SD_SPI_TIMEOUT_MS);
}

void SD_RawTest_GetIoInfo(SD_RawIoInfo *info)
{
    if (info == NULL)
    {
        return;
    }

    info->pb10_mode =
        (GPIOB->MODER >> (10U * 2U)) & 0x03U;
    info->pc2_mode =
        (GPIOC->MODER >> (2U * 2U)) & 0x03U;
    info->pc3_mode =
        (GPIOC->MODER >> (3U * 2U)) & 0x03U;

    info->pb10_af =
        (GPIOB->AFR[1] >> ((10U - 8U) * 4U)) & 0x0FU;
    info->pc2_af =
        (GPIOC->AFR[0] >> (2U * 4U)) & 0x0FU;
    info->pc3_af =
        (GPIOC->AFR[0] >> (3U * 4U)) & 0x0FU;

    info->spi2_cr1 = SPI2->CR1;
    info->spi2_cr2 = SPI2->CR2;
    info->spi2_sr = SPI2->SR;
}

static HAL_StatusTypeDef SD_RawSpi_Transfer(
    uint8_t transmit_byte,
    uint8_t *receive_byte)
{
    if (receive_byte == NULL)
    {
        return HAL_ERROR;
    }

    return HAL_SPI_TransmitReceive(
        &sd_raw_spi,
        &transmit_byte,
        receive_byte,
        1U,
        SD_SPI_TIMEOUT_MS);
}

static HAL_StatusTypeDef SD_RawSpi_Transmit(
    const uint8_t *buffer,
    uint16_t length)
{
    if ((buffer == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    return HAL_SPI_Transmit(
        &sd_raw_spi,
        (uint8_t *)buffer,
        length,
        SD_SPI_TIMEOUT_MS);
}

static void SD_Raw_Select(void)
{
    SD_CS_PORT->BSRR = (uint32_t)SD_CS_PIN << 16U;
    HAL_Delay(1U);
}

static void SD_Raw_Deselect(void)
{
    SD_CS_PORT->BSRR = SD_CS_PIN;
    HAL_Delay(1U);
}

static HAL_StatusTypeDef SD_Raw_SendStartupClocks(void)
{
    SD_Raw_Deselect();

    for (uint32_t index = 0U; index < 10U; index++)
    {
        uint8_t received;
        HAL_StatusTypeDef status = SD_RawSpi_Transfer(
            SD_IDLE_BYTE,
            &received);

        if (status != HAL_OK)
        {
            return status;
        }
    }

    return HAL_OK;
}

static HAL_StatusTypeDef SD_Raw_SendCommand(
    const uint8_t *command_packet,
    uint8_t *r1_response)
{
    HAL_StatusTypeDef status;

    if ((command_packet == NULL) || (r1_response == NULL))
    {
        return HAL_ERROR;
    }

    *r1_response = SD_IDLE_BYTE;
    SD_Raw_Select();

    status = SD_RawSpi_Transmit(
        command_packet,
        SD_COMMAND_PACKET_SIZE);

    if (status != HAL_OK)
    {
        SD_Raw_Deselect();
        return status;
    }

    for (uint32_t attempt = 0U;
         attempt < SD_RESPONSE_ATTEMPTS;
         attempt++)
    {
        uint8_t response = SD_IDLE_BYTE;

        status = SD_RawSpi_Transfer(
            SD_IDLE_BYTE,
            &response);

        if (status != HAL_OK)
        {
            SD_Raw_Deselect();
            return status;
        }

        if ((response & 0x80U) == 0U)
        {
            *r1_response = response;
            return HAL_OK;
        }
    }

    SD_Raw_Deselect();
    return HAL_TIMEOUT;
}

static HAL_StatusTypeDef SD_Raw_ReadR7(
    uint32_t *r7_response)
{
    uint8_t r7_bytes[4];

    if (r7_response == NULL)
    {
        return HAL_ERROR;
    }

    *r7_response = 0U;

    for (uint32_t index = 0U; index < 4U; index++)
    {
        HAL_StatusTypeDef status = SD_RawSpi_Transfer(
            SD_IDLE_BYTE,
            &r7_bytes[index]);

        if (status != HAL_OK)
        {
            return status;
        }
    }

    *r7_response =
        ((uint32_t)r7_bytes[0] << 24U) |
        ((uint32_t)r7_bytes[1] << 16U) |
        ((uint32_t)r7_bytes[2] << 8U) |
        ((uint32_t)r7_bytes[3]);

    return HAL_OK;
}

HAL_StatusTypeDef SD_RawTest_Run(SD_RawTestResult *result)
{
    static const uint8_t cmd0_packet[SD_COMMAND_PACKET_SIZE] =
    {
        0x40U, 0x00U, 0x00U, 0x00U, 0x00U, 0x95U
    };

    static const uint8_t cmd8_packet[SD_COMMAND_PACKET_SIZE] =
    {
        0x48U, 0x00U, 0x00U, 0x01U, 0xAAU, 0x87U
    };

    HAL_StatusTypeDef status;

    if (result == NULL)
    {
        return HAL_ERROR;
    }

    memset(result, 0, sizeof(*result));
    result->cmd0_r1 = SD_IDLE_BYTE;
    result->cmd8_r1 = SD_IDLE_BYTE;
    result->cmd0_status = HAL_TIMEOUT;
    result->cmd8_status = HAL_TIMEOUT;

    result->spi_init_status = SD_RawSpi_Init();

    if (result->spi_init_status != HAL_OK)
    {
        return result->spi_init_status;
    }

    HAL_Delay(100U);

    status = SD_Raw_SendStartupClocks();

    if (status != HAL_OK)
    {
        return status;
    }

    result->cmd0_status = SD_Raw_SendCommand(
        cmd0_packet,
        &result->cmd0_r1);

    SD_Raw_Deselect();

    if (result->cmd0_status != HAL_OK)
    {
        return result->cmd0_status;
    }

    if (result->cmd0_r1 != 0x01U)
    {
        return HAL_ERROR;
    }

    result->cmd8_status = SD_Raw_SendCommand(
        cmd8_packet,
        &result->cmd8_r1);

    if (result->cmd8_status != HAL_OK)
    {
        SD_Raw_Deselect();
        return result->cmd8_status;
    }

    status = SD_Raw_ReadR7(&result->cmd8_r7);
    SD_Raw_Deselect();

    if (status != HAL_OK)
    {
        result->cmd8_status = status;
        return status;
    }

    if ((result->cmd8_r1 != 0x01U) ||
        (result->cmd8_r7 != 0x000001AAUL))
    {
        result->cmd8_status = HAL_ERROR;
        return HAL_ERROR;
    }

    result->cmd8_status = HAL_OK;
    return HAL_OK;
}
