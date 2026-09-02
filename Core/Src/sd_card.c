/*
 * Software-SPI SD-card driver.
 *
 * Wiring:
 *
 *   PB13 -> Adafruit CLK
 *   PB14 <- Adafruit DO
 *   PB15 -> Adafruit DI
 *   PA5  -> Adafruit CS
 *
 * The Adafruit breakout is powered from 5 V and provides card-side
 * 3.3 V regulation and level translation.
 */

#include "sd_card.h"

#include "stm32f429xx.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * SD command numbers.
 */
#define SD_CMD0_GO_IDLE_STATE          0U
#define SD_CMD8_SEND_IF_COND           8U
#define SD_CMD16_SET_BLOCKLEN         16U
#define SD_CMD17_READ_SINGLE_BLOCK    17U
#define SD_CMD24_WRITE_SINGLE_BLOCK   24U
#define SD_CMD55_APP_CMD              55U
#define SD_CMD58_READ_OCR             58U
#define SD_ACMD41_SEND_OP_COND        41U
/*
 * Complete command byte is 0x40 OR command number.
 */
#define SD_COMMAND_PREFIX          0x40U

/*
 * Bus idle byte.
 */
#define SD_IDLE_BYTE               0xFFU

/*
 * R1 response values and masks.
 */
#define SD_R1_READY                0x00U
#define SD_R1_IDLE                 0x01U
#define SD_R1_ILLEGAL_COMMAND      0x04U

/*
 * Data tokens.
 */
#define SD_DATA_TOKEN_START_BLOCK  0xFEU
#define SD_DATA_TOKEN_WRITE        0xFEU

/*
 * Write-data response token mask and accepted value.
 */
#define SD_DATA_RESPONSE_MASK      0x1FU
#define SD_DATA_ACCEPTED           0x05U

/*
 * OCR bits.
 */
#define SD_OCR_POWER_READY         (1UL << 31U)
#define SD_OCR_CCS                 (1UL << 30U)
#define SD_OCR_3V3_MASK            \
    ((1UL << 20U) | (1UL << 21U))

/*
 * Timeouts are counted in software-SPI byte transfers.
 */
#define SD_R1_TIMEOUT_BYTES        16U
#define SD_DATA_TIMEOUT_BYTES      65535U
#define SD_BUSY_TIMEOUT_BYTES      65535U
#define SD_ACMD41_MAX_ATTEMPTS     1000U

/*
 * CMD8 check pattern and supported voltage value.
 */
#define SD_CMD8_ARGUMENT           0x000001AAUL
#define SD_CMD8_EXPECTED_R7        0x000001AAUL

/*
 * ACMD41 HCS argument for version 2 cards.
 */
#define SD_ACMD41_HCS              0x40000000UL

/*
 * Function prototypes for private driver operations.
 */
static void SD_Card_ConfigurePins(void);
static void SD_Card_Select(void);
static void SD_Card_Deselect(void);

static void SD_SoftwareDelay(void);

static uint8_t SD_SoftwareTransferByte(
    uint8_t transmit_byte);

static void SD_Card_SendStartupClocks(void);

static HAL_StatusTypeDef SD_Card_SendCommand(
    uint8_t command,
    uint32_t argument,
    uint8_t crc,
    uint8_t *r1_response);

static HAL_StatusTypeDef SD_Card_ReadExtendedResponse(
    uint8_t *buffer,
    uint32_t length);

static HAL_StatusTypeDef SD_Card_WaitUntilReady(void);

static uint32_t SD_Card_GetCommandAddress(
    uint32_t sector_number);

/*
 * Information collected during initialization.
 */
static SD_CardInfo sd_card_info;


/*
 * Configure GPIO pins for software SPI.
 */
static void SD_Card_ConfigurePins(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /*
     * Disable hardware SPI2 before controlling PB13, PB14, and PB15
     * as ordinary GPIO pins.
     */
    SPI2->CR1 &= ~SPI_CR1_SPE;

    /*
     * PA5 = active-low chip select.
     *
     * Preload high before configuring it as an output.
     */
    GPIOA->BSRR = GPIO_PIN_5;

    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    /*
     * PB13 = software SCK, initially low.
     * PB15 = software MOSI, initially high.
     */
    GPIOB->BSRR =
        (uint32_t)GPIO_PIN_13 << 16U;

    GPIOB->BSRR = GPIO_PIN_15;

    gpio.Pin = GPIO_PIN_13 |
               GPIO_PIN_15;

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    /*
     * PB14 = software MISO input.
     *
     * The pull-up defines the bus level while the card's DO output
     * is high-impedance.
     */
    gpio.Pin = GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);
}


/*
 * Assert card chip select.
 */
static void SD_Card_Select(void)
{
    GPIOA->BSRR =
        (uint32_t)GPIO_PIN_5 << 16U;
}


/*
 * Release card chip select and provide trailing clocks.
 */
static void SD_Card_Deselect(void)
{
    GPIOA->BSRR = GPIO_PIN_5;

    /*
     * Eight clocks after releasing CS allow the card to complete the
     * transaction and release DO.
     */
    (void)SD_SoftwareTransferByte(
        SD_IDLE_BYTE);
}


/*
 * Software timing delay.
 *
 * This intentionally keeps the initial software-SPI clock slow.
 */
static void SD_SoftwareDelay(void)
{
    for (volatile uint32_t index = 0U;
         index < 1200U;
         index++)
    {
        __NOP();
    }
}


/*
 * Transfer one full-duplex byte using software SPI Mode 0.
 *
 * Mode 0:
 *
 *   SCK idle low
 *   MOSI prepared while SCK is low
 *   Input sampled after SCK rises
 */
static uint8_t SD_SoftwareTransferByte(
    uint8_t transmit_byte)
{
    uint8_t receive_byte = 0U;

    for (uint32_t bit = 0U;
         bit < 8U;
         bit++)
    {
        GPIOB->BSRR =
            (uint32_t)GPIO_PIN_13 << 16U;

        if ((transmit_byte & 0x80U) != 0U)
        {
            GPIOB->BSRR = GPIO_PIN_15;
        }
        else
        {
            GPIOB->BSRR =
                (uint32_t)GPIO_PIN_15 << 16U;
        }

        transmit_byte <<= 1U;

        SD_SoftwareDelay();

        GPIOB->BSRR = GPIO_PIN_13;

        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();
        __NOP();

        receive_byte <<= 1U;

        if ((GPIOB->IDR & GPIO_PIN_14) != 0U)
        {
            receive_byte |= 0x01U;
        }

        SD_SoftwareDelay();

        GPIOB->BSRR =
            (uint32_t)GPIO_PIN_13 << 16U;

        SD_SoftwareDelay();
    }

    return receive_byte;
}


/*
 * Supply the required startup clocks with CS high and MOSI high.
 */
static void SD_Card_SendStartupClocks(void)
{
    GPIOA->BSRR = GPIO_PIN_5;
    GPIOB->BSRR = GPIO_PIN_15;

    for (uint32_t index = 0U;
         index < 10U;
         index++)
    {
        (void)SD_SoftwareTransferByte(
            SD_IDLE_BYTE);
    }
}

/*
 * Send one six-byte SD command and read its R1 response.
 *
 * Important:
 *
 * The command begins immediately after CS goes low. Do not insert an
 * idle 0xFF byte between SD_Card_Select() and the command packet.
 */
static HAL_StatusTypeDef SD_Card_SendCommand(
    uint8_t command,
    uint32_t argument,
    uint8_t crc,
    uint8_t *r1_response)
{
    uint8_t packet[6];
    uint8_t response = SD_IDLE_BYTE;

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

    /*
     * The command end bit, bit zero, must be one.
     */
    packet[5] = (uint8_t)(crc | 0x01U);
    /*
     * Provide eight idle clocks while the card is deselected.
     */
        
    SD_Card_Select();
    SD_SoftwareDelay();

    /*
     * Do not send 0xFF here.
     */
    for (uint32_t index = 0U;
         index < (uint32_t)sizeof(packet);
         index++)
    {
        (void)SD_SoftwareTransferByte(
            packet[index]);
    }

    for (uint32_t attempt = 0U;
         attempt < SD_R1_TIMEOUT_BYTES;
         attempt++)
    {
        response =
            SD_SoftwareTransferByte(
                SD_IDLE_BYTE);

        if ((response & 0x80U) == 0U)
        {
            *r1_response = response;
            return HAL_OK;
        }
    }

    *r1_response = response;

    return HAL_TIMEOUT;
}


/*
 * Read bytes that follow an R1 response, such as CMD8 R7 or CMD58 R3.
 *
 * CS must remain low while this function is called.
 */
static HAL_StatusTypeDef SD_Card_ReadExtendedResponse(
    uint8_t *buffer,
    uint32_t length)
{
    if ((buffer == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    for (uint32_t index = 0U;
         index < length;
         index++)
    {
        buffer[index] =
            SD_SoftwareTransferByte(
                SD_IDLE_BYTE);
    }

    return HAL_OK;
}


/*
 * Wait until the card stops driving DO low.
 */
static HAL_StatusTypeDef SD_Card_WaitUntilReady(void)
{
    for (uint32_t attempt = 0U;
         attempt < SD_BUSY_TIMEOUT_BYTES;
         attempt++)
    {
        if (SD_SoftwareTransferByte(
                SD_IDLE_BYTE) == SD_IDLE_BYTE)
        {
            return HAL_OK;
        }
    }

    return HAL_TIMEOUT;
}


/*
 * Convert a logical sector number into the card command argument.
 */
static uint32_t SD_Card_GetCommandAddress(
    uint32_t sector_number)
{
    if (sd_card_info.type == SD_CARD_TYPE_SDHC)
    {
        return sector_number;
    }

    return sector_number * SD_CARD_SECTOR_SIZE;
}


/*
 * Configure the card interface, supply startup clocks, and issue CMD0.
 */
HAL_StatusTypeDef SD_Card_Reset(
    uint8_t *r1_response)
{
    HAL_StatusTypeDef status;
    uint32_t primask;

    if (r1_response == NULL)
    {
        return HAL_ERROR;
    }

    sd_card_info.initialized = false;
    sd_card_info.version_2 = false;
    sd_card_info.power_ready = false;
    sd_card_info.supports_3v3 = false;
    sd_card_info.type = SD_CARD_TYPE_UNKNOWN;
    sd_card_info.ocr = 0U;

    SD_Card_ConfigurePins();

    /*
     * HAL_Delay requires SysTick, so delay before disabling IRQs.
     */
    HAL_Delay(100U);

    primask = __get_PRIMASK();
    __disable_irq();

    SD_Card_SendStartupClocks();

    status = SD_Card_SendCommand(
        SD_CMD0_GO_IDLE_STATE,
        0U,
        0x95U,
        r1_response);

    SD_Card_Deselect();

    if (primask == 0U)
    {
        __enable_irq();
    }

    if ((status == HAL_OK) &&
        (*r1_response == SD_R1_IDLE))
    {
        return HAL_OK;
    }

    return (status == HAL_OK) ?
        HAL_ERROR :
        status;
}


/*
 * Send CMD8 and read its four-byte R7 response.
 *
 * SD_Card_Reset() must be called successfully first.
 */
HAL_StatusTypeDef SD_Card_CheckInterface(
    uint8_t *r1_response,
    uint32_t *r7_response)
{
    HAL_StatusTypeDef status = HAL_TIMEOUT;
    uint8_t r7_bytes[4] = {0U};
    uint32_t primask;

    if ((r1_response == NULL) ||
        (r7_response == NULL))
    {
        return HAL_ERROR;
    }

    *r1_response = SD_IDLE_BYTE;
    *r7_response = 0U;

    /*
     * Try CMD8 several times. Each attempt is a complete,
     * independent CS transaction.
     */
    for (uint32_t attempt = 0U;
         attempt < 5U;
         attempt++)
    {
        primask = __get_PRIMASK();
        __disable_irq();

        status = SD_Card_SendCommand(
            SD_CMD8_SEND_IF_COND,
            SD_CMD8_ARGUMENT,
            0x87U,
            r1_response);

        if (status == HAL_OK)
        {
            if ((*r1_response &
                 SD_R1_ILLEGAL_COMMAND) == 0U)
            {
                status =
                    SD_Card_ReadExtendedResponse(
                        r7_bytes,
                        (uint32_t)sizeof(r7_bytes));
            }
        }

        SD_Card_Deselect();

        if (primask == 0U)
        {
            __enable_irq();
        }

        if (status == HAL_OK)
        {
            break;
        }

        /*
         * HAL_Delay requires interrupts, so it must occur after
         * restoring the previous interrupt state.
         */
        HAL_Delay(1U);
    }

    if (status != HAL_OK)
    {
        return status;
    }

    if ((*r1_response &
         SD_R1_ILLEGAL_COMMAND) != 0U)
    {
        /*
         * A valid R1 response with the illegal-command bit set
         * indicates that CMD8 is not supported.
         */
        sd_card_info.version_2 = false;
        return HAL_OK;
    }

    *r7_response =
        ((uint32_t)r7_bytes[0] << 24U) |
        ((uint32_t)r7_bytes[1] << 16U) |
        ((uint32_t)r7_bytes[2] << 8U)  |
        ((uint32_t)r7_bytes[3]);

    if ((*r1_response == SD_R1_IDLE) &&
    (*r7_response == SD_CMD8_EXPECTED_R7))
    {
        sd_card_info.version_2 = true;
        return HAL_OK;
    }

    return HAL_ERROR;
}


/*
 * Perform the complete SD-card initialization sequence.
 */
HAL_StatusTypeDef SD_Card_ReadOcr(
    uint8_t *r1_response,
    uint32_t *ocr_response)
{
    HAL_StatusTypeDef status;
    uint8_t ocr_bytes[4] = {0U};
    uint32_t primask;

    if ((r1_response == NULL) ||
        (ocr_response == NULL))
    {
        return HAL_ERROR;
    }

    *r1_response = SD_IDLE_BYTE;
    *ocr_response = 0U;

    primask = __get_PRIMASK();
    __disable_irq();

    status = SD_Card_SendCommand(
        SD_CMD58_READ_OCR,
        0U,
        0x01U,
        r1_response);

    if ((status == HAL_OK) &&
    ((*r1_response & 0x7EU) == 0U))
    {
        status = SD_Card_ReadExtendedResponse(
        ocr_bytes,
        (uint32_t)sizeof(ocr_bytes));
    }
    else if (status == HAL_OK)
    {
        status = HAL_ERROR;
    }
    SD_Card_Deselect();

    if (primask == 0U)
    {
        __enable_irq();
    }

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
    HAL_StatusTypeDef status;
    uint8_t r1_response = SD_IDLE_BYTE;
    uint32_t r7_response = 0U;
    uint8_t ocr_bytes[4] = {0U};
    uint32_t acmd41_argument;
    uint32_t primask;
    bool ready = false;

    status = SD_Card_Reset(
        &r1_response);

    if (status != HAL_OK)
    {
        return status;
    }

    status = SD_Card_CheckInterface(
        &r1_response,
        &r7_response);

    if (status != HAL_OK)
    {
        return status;
    }

    acmd41_argument =
        sd_card_info.version_2 ?
        SD_ACMD41_HCS :
        0U;

    /*
     * CMD55 followed by ACMD41 must be repeated until the card
     * leaves idle state.
     */
    for (uint32_t attempt = 0U;
         attempt < SD_ACMD41_MAX_ATTEMPTS;
         attempt++)
    {
        primask = __get_PRIMASK();
        __disable_irq();

        status = SD_Card_SendCommand(
            SD_CMD55_APP_CMD,
            0U,
            0x01U,
            &r1_response);

        SD_Card_Deselect();

        if (status == HAL_OK)
        {
            status = SD_Card_SendCommand(
                SD_ACMD41_SEND_OP_COND,
                acmd41_argument,
                0x01U,
                &r1_response);

            SD_Card_Deselect();
        }

        if (primask == 0U)
        {
            __enable_irq();
        }

        if (status != HAL_OK)
        {
            return status;
        }

        if (r1_response == SD_R1_READY)
        {
            ready = true;
            break;
        }

        /*
         * The card may need time for internal initialization.
         */
        HAL_Delay(1U);
    }

    if (!ready)
    {
        return HAL_TIMEOUT;
    }

    /*
     * CMD58 reads the OCR and capacity-status bit.
     */
    primask = __get_PRIMASK();
    __disable_irq();

    status = SD_Card_SendCommand(
        SD_CMD58_READ_OCR,
        0U,
        0x01U,
        &r1_response);

    if ((status == HAL_OK) &&
        (r1_response == SD_R1_READY))
    {
        status =
            SD_Card_ReadExtendedResponse(
                ocr_bytes,
                (uint32_t)sizeof(ocr_bytes));
    }

    SD_Card_Deselect();

    if (primask == 0U)
    {
        __enable_irq();
    }

    if ((status != HAL_OK) ||
        (r1_response != SD_R1_READY))
    {
        return (status == HAL_OK) ?
            HAL_ERROR :
            status;
    }

    sd_card_info.ocr =
        ((uint32_t)ocr_bytes[0] << 24U) |
        ((uint32_t)ocr_bytes[1] << 16U) |
        ((uint32_t)ocr_bytes[2] << 8U)  |
        ((uint32_t)ocr_bytes[3]);

    sd_card_info.power_ready =
        (sd_card_info.ocr &
         SD_OCR_POWER_READY) != 0U;

    sd_card_info.supports_3v3 =
        (sd_card_info.ocr &
         SD_OCR_3V3_MASK) != 0U;

    if ((sd_card_info.ocr &
         SD_OCR_CCS) != 0U)
    {
        sd_card_info.type =
            SD_CARD_TYPE_SDHC;
    }
    else
    {
        sd_card_info.type =
            SD_CARD_TYPE_SDSC;
    }

    if (!sd_card_info.power_ready ||
        !sd_card_info.supports_3v3)
    {
        return HAL_ERROR;
    }

    /*
     * SDSC cards require an explicit 512-byte block length.
     * SDHC cards always use 512-byte blocks and ignore CMD16.
     */
    if (sd_card_info.type ==
        SD_CARD_TYPE_SDSC)
    {
        primask = __get_PRIMASK();
        __disable_irq();

        status = SD_Card_SendCommand(
            SD_CMD16_SET_BLOCKLEN,
            SD_CARD_SECTOR_SIZE,
            0x01U,
            &r1_response);

        SD_Card_Deselect();

        if (primask == 0U)
        {
            __enable_irq();
        }

        if ((status != HAL_OK) ||
            (r1_response != SD_R1_READY))
        {
            return (status == HAL_OK) ?
                HAL_ERROR :
                status;
        }
    }

    sd_card_info.initialized = true;

    return HAL_OK;
}


/*
 * Return initialization information.
 */
const SD_CardInfo *SD_Card_GetInfo(void)
{
    return &sd_card_info;
}


/*
 * Read one 512-byte sector using CMD17.
 */
HAL_StatusTypeDef SD_Card_ReadSector(
    uint32_t sector_number,
    uint8_t *buffer)
{
    HAL_StatusTypeDef status;
    uint8_t r1_response = SD_IDLE_BYTE;
    uint8_t token = SD_IDLE_BYTE;
    uint32_t argument;
    uint32_t primask;

    if (!sd_card_info.initialized ||
        (buffer == NULL))
    {
        return HAL_ERROR;
    }

    argument =
        SD_Card_GetCommandAddress(
            sector_number);

    primask = __get_PRIMASK();
    __disable_irq();

    status = SD_Card_SendCommand(
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
            token =
                SD_SoftwareTransferByte(
                    SD_IDLE_BYTE);

            if (token != SD_IDLE_BYTE)
            {
                break;
            }
        }

        if (token != SD_DATA_TOKEN_START_BLOCK)
        {
            status = HAL_TIMEOUT;
        }
    }

    if (status == HAL_OK)
    {
        for (uint32_t index = 0U;
             index < SD_CARD_SECTOR_SIZE;
             index++)
        {
            buffer[index] =
                SD_SoftwareTransferByte(
                    SD_IDLE_BYTE);
        }

        /*
         * Discard the two-byte data CRC.
         */
        (void)SD_SoftwareTransferByte(
            SD_IDLE_BYTE);

        (void)SD_SoftwareTransferByte(
            SD_IDLE_BYTE);
    }

    SD_Card_Deselect();

    if (primask == 0U)
    {
        __enable_irq();
    }

    return status;
}


/*
 * Write one 512-byte sector using CMD24.
 *
 * This function should eventually be called only through FatFs or
 * another layer that knows the target sector is safe to modify.
 */
HAL_StatusTypeDef SD_Card_WriteSector(
    uint32_t sector_number,
    const uint8_t *buffer)
{
    HAL_StatusTypeDef status;
    uint8_t r1_response = SD_IDLE_BYTE;
    uint8_t data_response;
    uint32_t argument;
    uint32_t primask;

    if (!sd_card_info.initialized ||
        (buffer == NULL))
    {
        return HAL_ERROR;
    }

    argument =
        SD_Card_GetCommandAddress(
            sector_number);

    primask = __get_PRIMASK();
    __disable_irq();

    status = SD_Card_SendCommand(
        SD_CMD24_WRITE_SINGLE_BLOCK,
        argument,
        0x01U,
        &r1_response);

    if ((status == HAL_OK) &&
        (r1_response == SD_R1_READY))
    {
        /*
         * One idle byte separates the command response from the data
         * packet. CS remains asserted throughout the transaction.
         */
        (void)SD_SoftwareTransferByte(
            SD_IDLE_BYTE);

        (void)SD_SoftwareTransferByte(
            SD_DATA_TOKEN_WRITE);

        for (uint32_t index = 0U;
             index < SD_CARD_SECTOR_SIZE;
             index++)
        {
            (void)SD_SoftwareTransferByte(
                buffer[index]);
        }

        /*
         * Send a dummy two-byte data CRC.
         */
        (void)SD_SoftwareTransferByte(
            SD_IDLE_BYTE);

        (void)SD_SoftwareTransferByte(
            SD_IDLE_BYTE);

        data_response =
            SD_SoftwareTransferByte(
                SD_IDLE_BYTE);

        if ((data_response &
             SD_DATA_RESPONSE_MASK) !=
            SD_DATA_ACCEPTED)
        {
            status = HAL_ERROR;
        }
    }

    if (status == HAL_OK)
    {
        status = SD_Card_WaitUntilReady();
    }

    SD_Card_Deselect();

    if (primask == 0U)
    {
        __enable_irq();
    }

    return status;
}
