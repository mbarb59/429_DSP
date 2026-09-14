/*
 * Hardware-SPI SD-card driver for STM32F429.
 *
 * SPI2 wiring:
 *   PB10 / SPI2_SCK  -> SD CLK
 *   PC2  / SPI2_MISO <- SD DO / MISO
 *   PC3  / SPI2_MOSI -> SD DI / MOSI
 *   PA5              -> SD CS
 *
 * Use a 10 kOhm pull-up from SD DO / PC2 to 3.3 V.
 * The Adafruit 5 V-ready breakout is powered from 5 V.
 *
 * SPI2 runs in Mode 0 at approximately 164 kHz during card
 * initialization. After successful initialization, the driver
 * switches SPI2 to 5.25 MHz for normal sector transfers.
 */

#include "sd_card.h"

#include "stm32f429xx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SD_CMD0_GO_IDLE_STATE          0U
#define SD_CMD8_SEND_IF_COND           8U
#define SD_CMD16_SET_BLOCKLEN         16U
#define SD_CMD17_READ_SINGLE_BLOCK    17U
#define SD_CMD24_WRITE_SINGLE_BLOCK   24U
#define SD_CMD25_WRITE_MULTIPLE_BLOCK  25U
#define SD_CMD55_APP_CMD              55U
#define SD_CMD58_READ_OCR             58U
#define SD_ACMD41_SEND_OP_COND        41U

#define SD_COMMAND_PREFIX          0x40U
#define SD_IDLE_BYTE               0xFFU

#define SD_R1_READY                0x00U
#define SD_R1_IDLE                 0x01U
#define SD_R1_ILLEGAL_COMMAND      0x04U
#define SD_R1_ERROR_MASK           0x7EU

#define SD_DATA_TOKEN_START_BLOCK  0xFEU
#define SD_DATA_TOKEN_WRITE        0xFEU
#define SD_TOKEN_MULTI_WRITE_START 0xFCU
#define SD_TOKEN_MULTI_WRITE_STOP  0xFDU
#define SD_DATA_RESPONSE_MASK      0x1FU
#define SD_DATA_RESPONSE_ACCEPTED  0x05U

#define SD_OCR_POWER_READY         (1UL << 31U)
#define SD_OCR_CCS                 (1UL << 30U)
#define SD_OCR_3V3_MASK            \
    ((1UL << 20U) | (1UL << 21U))

#define SD_CMD8_ARGUMENT           0x000001AAUL
#define SD_CMD8_EXPECTED_R7        0x000001AAUL
#define SD_ACMD41_HCS              0x40000000UL

#define SD_R1_TIMEOUT_BYTES        16U
#define SD_READY_TIMEOUT_MS        100U
#define SD_DATA_TIMEOUT_BYTES      65535U
#define SD_BUSY_TIMEOUT_MS         500U
#define SD_ACMD41_MAX_ATTEMPTS     1000U
#define SD_CMD0_MAX_ATTEMPTS       5U
#define SD_SPI_TIMEOUT_MS          100U

static SPI_HandleTypeDef sd_spi_handle;
static bool sd_transport_initialized;
static SD_CardInfo sd_card_info;

static HAL_StatusTypeDef SD_Transport_Init(void);

static HAL_StatusTypeDef SD_TransferByte(
    uint8_t transmit_byte,
    uint8_t *receive_byte);

static HAL_StatusTypeDef SD_SendIdleBytes(
    uint32_t count);

static HAL_StatusTypeDef SD_WaitReady(
    uint32_t timeout_ms);

static void SD_Select(void);
static void SD_Deselect(void);

static HAL_StatusTypeDef SD_SendCommand(
    uint8_t command,
    uint32_t argument,
    uint8_t crc,
    uint8_t *r1_response);

static HAL_StatusTypeDef SD_ReadBytes(
    uint8_t *buffer,
    uint32_t length);

static uint32_t SD_GetCommandAddress(
    uint32_t sector_number);

static void SD_ResetInfo(void);

static HAL_StatusTypeDef SD_Transport_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (sd_transport_initialized)
    {
        return HAL_OK;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    GPIOA->BSRR = GPIO_PIN_5;

    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PB10: SPI2 SCK. */
    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PC3: SPI2 MOSI. */
    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PC2: SPI2 MISO with an internal pull-up. */
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOC, &gpio);

    __HAL_RCC_SPI2_FORCE_RESET();
    __HAL_RCC_SPI2_RELEASE_RESET();

    memset(&sd_spi_handle, 0, sizeof(sd_spi_handle));

    sd_spi_handle.Instance = SPI2;
    sd_spi_handle.Init.Mode = SPI_MODE_MASTER;
    sd_spi_handle.Init.Direction = SPI_DIRECTION_2LINES;
    sd_spi_handle.Init.DataSize = SPI_DATASIZE_8BIT;
    sd_spi_handle.Init.CLKPolarity = SPI_POLARITY_LOW;
    sd_spi_handle.Init.CLKPhase = SPI_PHASE_1EDGE;
    sd_spi_handle.Init.NSS = SPI_NSS_SOFT;
    sd_spi_handle.Init.BaudRatePrescaler =
        SPI_BAUDRATEPRESCALER_256;
    sd_spi_handle.Init.FirstBit = SPI_FIRSTBIT_MSB;
    sd_spi_handle.Init.TIMode = SPI_TIMODE_DISABLE;
    sd_spi_handle.Init.CRCCalculation =
        SPI_CRCCALCULATION_DISABLE;
    sd_spi_handle.Init.CRCPolynomial = 7U;

    if (HAL_SPI_Init(&sd_spi_handle) != HAL_OK)
    {
        sd_transport_initialized = false;
        return HAL_ERROR;
    }

    sd_transport_initialized = true;
    SD_Deselect();

    return HAL_OK;
}

static HAL_StatusTypeDef SD_TransferByte(
    uint8_t transmit_byte,
    uint8_t *receive_byte)
{
    if (!sd_transport_initialized ||
        (receive_byte == NULL))
    {
        return HAL_ERROR;
    }

    return HAL_SPI_TransmitReceive(
        &sd_spi_handle,
        &transmit_byte,
        receive_byte,
        1U,
        SD_SPI_TIMEOUT_MS);
}

static HAL_StatusTypeDef SD_SendIdleBytes(
    uint32_t count)
{
    for (uint32_t index = 0U;
         index < count;
         index++)
    {
        uint8_t received;
        HAL_StatusTypeDef status;

        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &received);

        if (status != HAL_OK)
        {
            return status;
        }
    }

    return HAL_OK;
}

static HAL_StatusTypeDef SD_WaitReady(
    uint32_t timeout_ms)
{
    uint32_t start_tick;

    start_tick = HAL_GetTick();

    do
    {
        uint8_t received_byte = 0x00U;
        HAL_StatusTypeDef status;

        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &received_byte);

        if (status != HAL_OK)
        {
            return status;
        }

        if (received_byte == SD_IDLE_BYTE)
        {
            return HAL_OK;
        }
    }
    while ((HAL_GetTick() - start_tick) <
           timeout_ms);

    return HAL_TIMEOUT;
}

static void SD_Select(void)
{
    GPIOA->BSRR =
        (uint32_t)GPIO_PIN_5 << 16U;
}

static void SD_Deselect(void)
{
    uint8_t discarded;

    GPIOA->BSRR = GPIO_PIN_5;

    if (sd_transport_initialized)
    {
        (void)SD_TransferByte(
            SD_IDLE_BYTE,
            &discarded);
    }
}

static HAL_StatusTypeDef SD_SendCommand(
    uint8_t command,
    uint32_t argument,
    uint8_t crc,
    uint8_t *r1_response)
{
    uint8_t packet[6];
    HAL_StatusTypeDef status;

    if (r1_response == NULL)
    {
        return HAL_ERROR;
    }

    *r1_response = SD_IDLE_BYTE;

    packet[0] =
        (uint8_t)(SD_COMMAND_PREFIX |
                  (command & 0x3FU));
    packet[1] =
        (uint8_t)((argument >> 24U) & 0xFFU);
    packet[2] =
        (uint8_t)((argument >> 16U) & 0xFFU);
    packet[3] =
        (uint8_t)((argument >> 8U) & 0xFFU);
    packet[4] =
        (uint8_t)(argument & 0xFFU);
    packet[5] =
        (uint8_t)(crc | 0x01U);

    SD_Select();

    if (command != SD_CMD0_GO_IDLE_STATE)
    {
        status = SD_WaitReady(
            SD_READY_TIMEOUT_MS);

        if (status != HAL_OK)
        {
            SD_Deselect();
            return status;
        }
    }

    for (uint32_t index = 0U;
         index < (uint32_t)sizeof(packet);
         index++)
    {
        uint8_t discarded;

        status = SD_TransferByte(
            packet[index],
            &discarded);

        if (status != HAL_OK)
        {
            SD_Deselect();
            return status;
        }
    }

    for (uint32_t attempt = 0U;
         attempt < SD_R1_TIMEOUT_BYTES;
         attempt++)
    {
        uint8_t response = SD_IDLE_BYTE;

        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &response);

        if (status != HAL_OK)
        {
            SD_Deselect();
            return status;
        }

        if ((response & 0x80U) == 0U)
        {
            *r1_response = response;
            return HAL_OK;
        }
    }

    SD_Deselect();
    return HAL_TIMEOUT;
}

static HAL_StatusTypeDef SD_ReadBytes(
    uint8_t *buffer,
    uint32_t length)
{
    if ((buffer == NULL) ||
        (length == 0U))
    {
        return HAL_ERROR;
    }

    for (uint32_t index = 0U;
         index < length;
         index++)
    {
        HAL_StatusTypeDef status;

        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &buffer[index]);

        if (status != HAL_OK)
        {
            return status;
        }
    }

    return HAL_OK;
}

static uint32_t SD_GetCommandAddress(
    uint32_t sector_number)
{
    if (sd_card_info.type ==
        SD_CARD_TYPE_SDHC)
    {
        return sector_number;
    }

    return sector_number *
           SD_CARD_SECTOR_SIZE;
}

static void SD_ResetInfo(void)
{
    sd_card_info.initialized = false;
    sd_card_info.version_2 = false;
    sd_card_info.power_ready = false;
    sd_card_info.supports_3v3 = false;
    sd_card_info.type = SD_CARD_TYPE_UNKNOWN;
    sd_card_info.ocr = 0U;

    sd_card_info.cmd0_r1 = SD_IDLE_BYTE;
    sd_card_info.cmd8_r1 = SD_IDLE_BYTE;
    sd_card_info.cmd55_r1 = SD_IDLE_BYTE;
    sd_card_info.acmd41_r1 = SD_IDLE_BYTE;

    sd_card_info.cmd8_r7 = 0U;
    sd_card_info.cmd0_attempts = 0U;
    sd_card_info.acmd41_attempts = 0U;

    sd_card_info.cmd8_status = HAL_TIMEOUT;
    sd_card_info.cmd55_status = HAL_TIMEOUT;
    sd_card_info.acmd41_status = HAL_TIMEOUT;
}

HAL_StatusTypeDef SD_Card_Reset(
    uint8_t *r1_response)
{
    HAL_StatusTypeDef status;

    if (r1_response == NULL)
    {
        return HAL_ERROR;
    }

    status = SD_Transport_Init();

    if (status != HAL_OK)
    {
        return status;
    }

    HAL_Delay(100U);
    SD_Deselect();

    status = SD_SendIdleBytes(10U);

    if (status != HAL_OK)
    {
        return status;
    }

    status = SD_SendCommand(
        SD_CMD0_GO_IDLE_STATE,
        0U,
        0x95U,
        r1_response);

    SD_Deselect();

    if ((status == HAL_OK) &&
        (*r1_response == SD_R1_IDLE))
    {
        return HAL_OK;
    }

    return (status == HAL_OK) ?
           HAL_ERROR :
           status;
}

HAL_StatusTypeDef SD_Card_CheckInterface(
    uint8_t *r1_response,
    uint32_t *r7_response)
{
    HAL_StatusTypeDef status;
    uint8_t r7_bytes[4] = {0U};

    if ((r1_response == NULL) ||
        (r7_response == NULL))
    {
        return HAL_ERROR;
    }

    *r1_response = SD_IDLE_BYTE;
    *r7_response = 0U;

    status = SD_SendCommand(
        SD_CMD8_SEND_IF_COND,
        SD_CMD8_ARGUMENT,
        0x87U,
        r1_response);

    if ((status == HAL_OK) &&
        ((*r1_response &
          SD_R1_ILLEGAL_COMMAND) == 0U))
    {
        status = SD_ReadBytes(
            r7_bytes,
            (uint32_t)sizeof(r7_bytes));
    }

    SD_Deselect();

    if (status != HAL_OK)
    {
        return status;
    }

    if ((*r1_response &
         SD_R1_ILLEGAL_COMMAND) != 0U)
    {
        return HAL_ERROR;
    }

    *r7_response =
        ((uint32_t)r7_bytes[0] << 24U) |
        ((uint32_t)r7_bytes[1] << 16U) |
        ((uint32_t)r7_bytes[2] << 8U) |
        ((uint32_t)r7_bytes[3]);

    if ((*r1_response == SD_R1_IDLE) &&
        (*r7_response ==
         SD_CMD8_EXPECTED_R7))
    {
        return HAL_OK;
    }

    return HAL_ERROR;
}

HAL_StatusTypeDef SD_Card_ReadOcr(
    uint8_t *r1_response,
    uint32_t *ocr_response)
{
    HAL_StatusTypeDef status;
    uint8_t ocr_bytes[4] = {0U};

    if ((r1_response == NULL) ||
        (ocr_response == NULL))
    {
        return HAL_ERROR;
    }

    *r1_response = SD_IDLE_BYTE;
    *ocr_response = 0U;

    status = SD_SendCommand(
        SD_CMD58_READ_OCR,
        0U,
        0x01U,
        r1_response);

    if ((status == HAL_OK) &&
        ((*r1_response &
          SD_R1_ERROR_MASK) == 0U))
    {
        status = SD_ReadBytes(
            ocr_bytes,
            (uint32_t)sizeof(ocr_bytes));
    }
    else if (status == HAL_OK)
    {
        status = HAL_ERROR;
    }

    SD_Deselect();

    if (status != HAL_OK)
    {
        return status;
    }

    *ocr_response =
        ((uint32_t)ocr_bytes[0] << 24U) |
        ((uint32_t)ocr_bytes[1] << 16U) |
        ((uint32_t)ocr_bytes[2] << 8U) |
        ((uint32_t)ocr_bytes[3]);

    return HAL_OK;
}

HAL_StatusTypeDef SD_Card_Init(void)
{
    HAL_StatusTypeDef status = HAL_TIMEOUT;
    uint8_t r1_response = SD_IDLE_BYTE;
    uint32_t r7_response = 0U;
    bool ready = false;

    SD_ResetInfo();

    for (uint32_t attempt = 0U;
         attempt < SD_CMD0_MAX_ATTEMPTS;
         attempt++)
    {
        status = SD_Card_Reset(&r1_response);
        sd_card_info.cmd0_r1 = r1_response;
        sd_card_info.cmd0_attempts =
            attempt + 1U;

        if ((status == HAL_OK) &&
            (r1_response == SD_R1_IDLE))
        {
            break;
        }

        HAL_Delay(10U);
    }

    if ((status != HAL_OK) ||
        (r1_response != SD_R1_IDLE))
    {
        return (status == HAL_OK) ?
               HAL_ERROR :
               status;
    }

    status = SD_Card_CheckInterface(
        &r1_response,
        &r7_response);

    sd_card_info.cmd8_status = status;
    sd_card_info.cmd8_r1 = r1_response;
    sd_card_info.cmd8_r7 = r7_response;

    if (status != HAL_OK)
    {
        return status;
    }

    sd_card_info.version_2 = true;

    for (uint32_t attempt = 0U;
         attempt < SD_ACMD41_MAX_ATTEMPTS;
         attempt++)
    {
        status = SD_SendCommand(
            SD_CMD55_APP_CMD,
            0U,
            0x01U,
            &r1_response);

        sd_card_info.cmd55_status = status;
        sd_card_info.cmd55_r1 = r1_response;

        SD_Deselect();

        if (status != HAL_OK)
        {
            return status;
        }

        status = SD_SendCommand(
            SD_ACMD41_SEND_OP_COND,
            SD_ACMD41_HCS,
            0x01U,
            &r1_response);

        sd_card_info.acmd41_status = status;
        sd_card_info.acmd41_r1 = r1_response;
        sd_card_info.acmd41_attempts =
            attempt + 1U;

        SD_Deselect();

        if (status != HAL_OK)
        {
            return status;
        }

        if (r1_response == SD_R1_READY)
        {
            ready = true;
            break;
        }

        if (r1_response != SD_R1_IDLE)
        {
            return HAL_ERROR;
        }

        HAL_Delay(1U);
    }

    if (!ready)
    {
        return HAL_TIMEOUT;
    }

    status = SD_Card_ReadOcr(
        &r1_response,
        &sd_card_info.ocr);

    if ((status != HAL_OK) ||
        (r1_response != SD_R1_READY))
    {
        return (status == HAL_OK) ?
               HAL_ERROR :
               status;
    }

    sd_card_info.power_ready =
        (sd_card_info.ocr &
         SD_OCR_POWER_READY) != 0U;

    sd_card_info.supports_3v3 =
        (sd_card_info.ocr &
         SD_OCR_3V3_MASK) != 0U;

    sd_card_info.type =
        ((sd_card_info.ocr &
          SD_OCR_CCS) != 0U) ?
        SD_CARD_TYPE_SDHC :
        SD_CARD_TYPE_SDSC;

    if (!sd_card_info.power_ready ||
        !sd_card_info.supports_3v3)
    {
        return HAL_ERROR;
    }

    if (sd_card_info.type ==
        SD_CARD_TYPE_SDSC)
    {
        status = SD_SendCommand(
            SD_CMD16_SET_BLOCKLEN,
            SD_CARD_SECTOR_SIZE,
            0x01U,
            &r1_response);

        SD_Deselect();

        if ((status != HAL_OK) ||
            (r1_response != SD_R1_READY))
        {
            return (status == HAL_OK) ?
                   HAL_ERROR :
                   status;
        }
    }

    sd_card_info.initialized = true;

    status = SD_Card_SetTransferSpeed();

    if (status != HAL_OK)
    {
        sd_card_info.initialized = false;
        return status;
    }

    return HAL_OK;
}

const SD_CardInfo *SD_Card_GetInfo(void)
{
    return &sd_card_info;
}

HAL_StatusTypeDef SD_Card_ReadSector(
    uint32_t sector_number,
    uint8_t *buffer)
{
    HAL_StatusTypeDef status;
    uint8_t r1_response = SD_IDLE_BYTE;
    uint8_t token = SD_IDLE_BYTE;
    uint32_t argument;

    if (!sd_card_info.initialized ||
        (buffer == NULL))
    {
        return HAL_ERROR;
    }

    argument =
        SD_GetCommandAddress(sector_number);

    status = SD_SendCommand(
        SD_CMD17_READ_SINGLE_BLOCK,
        argument,
        0x01U,
        &r1_response);

    if ((status == HAL_OK) &&
        (r1_response == SD_R1_READY))
    {
        for (uint32_t attempt = 0U;
             attempt < SD_DATA_TIMEOUT_BYTES;
             attempt++)
        {
            status = SD_TransferByte(
                SD_IDLE_BYTE,
                &token);

            if (status != HAL_OK)
            {
                break;
            }

            if (token != SD_IDLE_BYTE)
            {
                break;
            }
        }

        if ((status == HAL_OK) &&
            (token !=
             SD_DATA_TOKEN_START_BLOCK))
        {
            status = HAL_TIMEOUT;
        }
    }

    if (status == HAL_OK)
    {
        status = SD_ReadBytes(
            buffer,
            SD_CARD_SECTOR_SIZE);
    }

    if (status == HAL_OK)
    {
        uint8_t discarded;

        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &discarded);

        if (status == HAL_OK)
        {
            status = SD_TransferByte(
                SD_IDLE_BYTE,
                &discarded);
        }
    }

    SD_Deselect();

    return status;
}

HAL_StatusTypeDef SD_Card_WriteSector(
    uint32_t sector_number,
    const uint8_t *buffer)
{
    HAL_StatusTypeDef status;
    uint8_t r1_response = SD_IDLE_BYTE;
    uint8_t received;
    uint32_t argument;

    if (!sd_card_info.initialized ||
        (buffer == NULL))
    {
        return HAL_ERROR;
    }

    argument =
        SD_GetCommandAddress(sector_number);

    status = SD_SendCommand(
        SD_CMD24_WRITE_SINGLE_BLOCK,
        argument,
        0x01U,
        &r1_response);

    if ((status == HAL_OK) &&
        (r1_response == SD_R1_READY))
    {
        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &received);
    }

    if (status == HAL_OK)
    {
        status = SD_TransferByte(
            SD_DATA_TOKEN_WRITE,
            &received);
    }

    for (uint32_t index = 0U;
         (status == HAL_OK) &&
         (index < SD_CARD_SECTOR_SIZE);
         index++)
    {
        status = SD_TransferByte(
            buffer[index],
            &received);
    }

    if (status == HAL_OK)
    {
        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &received);
    }

    if (status == HAL_OK)
    {
        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &received);
    }

    if (status == HAL_OK)
    {
        status = SD_TransferByte(
            SD_IDLE_BYTE,
            &received);
    }

    if ((status == HAL_OK) &&
        ((received &
          SD_DATA_RESPONSE_MASK) !=
         SD_DATA_RESPONSE_ACCEPTED))
    {
        status = HAL_ERROR;
    }

    if (status == HAL_OK)
    {
        status = SD_WaitReady(
            SD_BUSY_TIMEOUT_MS);
    }

    SD_Deselect();

    return status;
}

HAL_StatusTypeDef SD_Card_WriteSectors(
    uint32_t start_sector,
    const uint8_t *buffer,
    uint32_t sector_count)
{
    HAL_StatusTypeDef status;
    uint8_t r1_response = SD_IDLE_BYTE;
    uint8_t received = SD_IDLE_BYTE;
    uint32_t argument;
    bool command_started = false;

    if (!sd_card_info.initialized ||
        (buffer == NULL) ||
        (sector_count == 0U))
    {
        return HAL_ERROR;
    }

    if (sector_count == 1U)
    {
        return SD_Card_WriteSector(
            start_sector,
            buffer);
    }

    argument =
        SD_GetCommandAddress(start_sector);

    /*
     * SD_SendCommand() selects the card and leaves CS low
     * when the command succeeds. CMD25 requires CS to remain
     * low until the 0xFD stop-transmission token is sent.
     */
    status = SD_SendCommand(
        SD_CMD25_WRITE_MULTIPLE_BLOCK,
        argument,
        0x01U,
        &r1_response);

    if ((status != HAL_OK) ||
        (r1_response != SD_R1_READY))
    {
        SD_Deselect();
        return (status == HAL_OK) ?
               HAL_ERROR :
               status;
    }

    command_started = true;

    for (uint32_t sector_index = 0U;
         sector_index < sector_count;
         sector_index++)
    {
        const uint8_t *sector_buffer =
            &buffer[sector_index *
                    SD_CARD_SECTOR_SIZE];

        status = SD_WaitReady(
            SD_BUSY_TIMEOUT_MS);

        if (status != HAL_OK)
        {
            break;
        }

        /*
         * CMD25 uses 0xFC before each 512-byte block.
         */
        status = SD_TransferByte(
            SD_TOKEN_MULTI_WRITE_START,
            &received);

        for (uint32_t byte_index = 0U;
             (status == HAL_OK) &&
             (byte_index < SD_CARD_SECTOR_SIZE);
             byte_index++)
        {
            status = SD_TransferByte(
                sector_buffer[byte_index],
                &received);
        }

        /* Send two dummy CRC bytes. */
        if (status == HAL_OK)
        {
            status = SD_TransferByte(
                SD_IDLE_BYTE,
                &received);
        }

        if (status == HAL_OK)
        {
            status = SD_TransferByte(
                SD_IDLE_BYTE,
                &received);
        }

        /* Read and validate the data-response token. */
        if (status == HAL_OK)
        {
            status = SD_TransferByte(
                SD_IDLE_BYTE,
                &received);
        }

        if ((status == HAL_OK) &&
            ((received & SD_DATA_RESPONSE_MASK) !=
             SD_DATA_RESPONSE_ACCEPTED))
        {
            status = HAL_ERROR;
        }

        if (status != HAL_OK)
        {
            break;
        }

        /*
         * Wait for programming to complete before sending
         * the next block.
         */
        status = SD_WaitReady(
            SD_BUSY_TIMEOUT_MS);

        if (status != HAL_OK)
        {
            break;
        }
    }

    if (command_started)
    {
        HAL_StatusTypeDef stop_status;
        uint8_t ignored;

        /*
         * CMD25 is terminated with the 0xFD stop token.
         * CMD12 is not used for a multi-block write.
         */
        stop_status = SD_TransferByte(
            SD_TOKEN_MULTI_WRITE_STOP,
            &ignored);

        if ((status == HAL_OK) &&
            (stop_status != HAL_OK))
        {
            status = stop_status;
        }

        if (stop_status == HAL_OK)
        {
            stop_status = SD_WaitReady(
                SD_BUSY_TIMEOUT_MS);

            if ((status == HAL_OK) &&
                (stop_status != HAL_OK))
            {
                status = stop_status;
            }
        }
    }

    SD_Deselect();

    return status;
}

HAL_StatusTypeDef SD_Card_SetTransferSpeed(void)
{
    HAL_StatusTypeDef status;

    if (!sd_transport_initialized)
    {
        return HAL_ERROR;
    }

    /*
     * APB1 peripheral clock:
     *     42 MHz
     *
     * Normal transfer clock:
     *     42 MHz / 8 = 5.25 MHz
     */
    sd_spi_handle.Init.BaudRatePrescaler =
        SPI_BAUDRATEPRESCALER_8;

    status = HAL_SPI_Init(&sd_spi_handle);

    if (status != HAL_OK)
    {
        return status;
    }

    SD_Deselect();

    return HAL_OK;
}
