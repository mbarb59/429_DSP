/*
 * SPI2 transport for the Adafruit 5V-ready MicroSD breakout.
 *
 * Wiring:
 *   PB13 / SPI2_SCK   -> CLK
 *   PB14 / SPI2_MISO  <- DO
 *   PB15 / SPI2_MOSI  -> DI
 *   PA5  / GPIO       -> CS
 *
 * This module provides only the SPI transport. It does not yet implement
 * SD commands, card initialization, block access, or a filesystem.
 */

#include "sd_spi.h"

#include <stdbool.h>
#include <stdint.h>

#define SD_COMMAND_PREFIX       0x40U
#define SD_RESPONSE_TIMEOUT     32U
#define SD_IDLE_BYTE            0xFFU

static bool sd_spi_initialized;

static bool sd_spi_timeout_expired(uint32_t start_ms)
{
    return (HAL_GetTick() - start_ms) >= SD_SPI_TIMEOUT_MS;
}

static HAL_StatusTypeDef sd_spi_wait_for_flag(uint32_t flag,
                                               bool required_set)
{
    uint32_t start_ms = HAL_GetTick();

    while (true)
    {
        bool flag_is_set = (SD_SPI_INSTANCE->SR & flag) != 0U;

        if (flag_is_set == required_set)
        {
            return HAL_OK;
        }

        if (sd_spi_timeout_expired(start_ms))
        {
            return HAL_TIMEOUT;
        }
    }
}

static void sd_spi_clear_receive_state(void)
{
    volatile uint32_t discarded;

    while ((SD_SPI_INSTANCE->SR & SPI_SR_RXNE) != 0U)
    {
        discarded = SD_SPI_INSTANCE->DR;
        (void)discarded;
    }

    /* Clear a possible overrun by reading DR followed by SR. */
    if ((SD_SPI_INSTANCE->SR & SPI_SR_OVR) != 0U)
    {
        discarded = SD_SPI_INSTANCE->DR;
        discarded = SD_SPI_INSTANCE->SR;
        (void)discarded;
    }
}

HAL_StatusTypeDef SD_SPI_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * Always reinitialize during bring-up so diagnostic commands
     * cannot leave SPI2 or its GPIO pins in a stale configuration.
     */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /*
     * Preload CS high before configuring PA5 as an output.
     */
    SD_CS_GPIO_PORT->BSRR = SD_CS_PIN;

    gpio.Pin = SD_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SD_CS_GPIO_PORT, &gpio);

    SD_SPI_Deselect();

    /*
     * Configure SCK and MOSI as SPI2 alternate-function signals.
     */
    gpio.Pin = SD_SPI_SCK_PIN |
           SD_SPI_MOSI_PIN;
gpio.Mode = GPIO_MODE_AF_PP;
gpio.Pull = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
gpio.Alternate = SD_SPI_GPIO_AF;
HAL_GPIO_Init(SD_SPI_GPIO_PORT, &gpio);

gpio.Pin = SD_SPI_MISO_PIN;
gpio.Mode = GPIO_MODE_AF_PP;
gpio.Pull = GPIO_PULLUP;
gpio.Speed = GPIO_SPEED_FREQ_LOW;
gpio.Alternate = SD_SPI_GPIO_AF;
HAL_GPIO_Init(SD_SPI_GPIO_PORT, &gpio);

    /*
     * MISO is high-impedance while the card is deselected.
     * Use a pull-up so the idle level is defined.
     */
    gpio.Pin = SD_SPI_MISO_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = SD_SPI_GPIO_AF;
    HAL_GPIO_Init(SD_SPI_GPIO_PORT, &gpio);

    /*
     * Reset SPI2 before programming its control registers.
     */
    __HAL_RCC_SPI2_FORCE_RESET();
    __HAL_RCC_SPI2_RELEASE_RESET();

    SD_SPI_INSTANCE->CR1 = 0U;
    SD_SPI_INSTANCE->CR2 = 0U;

    /*
     * SPI2 shares hardware with I2S2. Explicitly select SPI mode.
     */
    SD_SPI_INSTANCE->I2SCFGR = 0U;

    /*
     * Configure SPI2:
     *
     * Master mode
     * Mode 0, CPOL = 0 and CPHA = 0
     * 8-bit data
     * MSB first
     * Software-controlled NSS
     * Divide-by-256 startup clock
     */
    SD_SPI_INSTANCE->CR1 =
    SPI_CR1_MSTR |
    SD_SPI_INIT_PRESCALER |
    SPI_CR1_SSM |
    SPI_CR1_SSI;



    sd_spi_clear_receive_state();

    SD_SPI_INSTANCE->CR1 |= SPI_CR1_SPE;
    sd_spi_initialized = true;

    return HAL_OK;
}
bool SD_SPI_IsInitialized(void)
{
    return sd_spi_initialized;
}

void SD_SPI_Select(void)
{
    SD_CS_GPIO_PORT->BSRR = (uint32_t)SD_CS_PIN << 16U;
}

void SD_SPI_Deselect(void)
{
    SD_CS_GPIO_PORT->BSRR = SD_CS_PIN;
}

bool SD_SPI_IsDeselected(void)
{
    return (SD_CS_GPIO_PORT->ODR & SD_CS_PIN) != 0U;
}

HAL_StatusTypeDef SD_SPI_TransferByte(uint8_t transmit_byte,
                                     uint8_t *receive_byte)
{
    HAL_StatusTypeDef status;
    uint8_t received;

    if (!sd_spi_initialized || (receive_byte == NULL))
    {
        return HAL_ERROR;
    }

    status = sd_spi_wait_for_flag(SPI_SR_TXE, true);

    if (status != HAL_OK)
    {
        return status;
    }

    *(__IO uint8_t *)&SD_SPI_INSTANCE->DR = transmit_byte;

    status = sd_spi_wait_for_flag(SPI_SR_RXNE, true);

    if (status != HAL_OK)
    {
        return status;
    }

    received = *(__IO uint8_t *)&SD_SPI_INSTANCE->DR;
    *receive_byte = received;

    return HAL_OK;
}

HAL_StatusTypeDef SD_SPI_Transfer(const uint8_t *transmit_data,
                                 uint8_t *receive_data,
                                 uint32_t length)
{
    for (uint32_t index = 0U; index < length; index++)
    {
        uint8_t transmit_byte = 0xFFU;
        uint8_t receive_byte;
        HAL_StatusTypeDef status;

        if (transmit_data != NULL)
        {
            transmit_byte = transmit_data[index];
        }

        status = SD_SPI_TransferByte(transmit_byte, &receive_byte);

        if (status != HAL_OK)
        {
            return status;
        }

        if (receive_data != NULL)
        {
            receive_data[index] = receive_byte;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef SD_SPI_WriteByte(uint8_t value)
{
    uint8_t discarded;

    return SD_SPI_TransferByte(value, &discarded);
}

HAL_StatusTypeDef SD_SPI_ReadByte(uint8_t *value)
{
    if (value == NULL)
    {
        return HAL_ERROR;
    }

    return SD_SPI_TransferByte(0xFFU, value);
}

HAL_StatusTypeDef SD_SPI_SendIdleClocks(void)
{
    HAL_StatusTypeDef status;

    if (!sd_spi_initialized)
    {
        return HAL_ERROR;
    }

    SD_SPI_Deselect();

    /* Ten bytes generate 80 clocks, exceeding the required 74 clocks. */
    for (uint32_t index = 0U; index < 10U; index++)
    {
        status = SD_SPI_WriteByte(0xFFU);

        if (status != HAL_OK)
        {
            return status;
        }
    }

    return sd_spi_wait_for_flag(SPI_SR_BSY, false);
}

uint32_t SD_SPI_GetClockHz(void)
{
    uint32_t peripheral_clock_hz = HAL_RCC_GetPCLK1Freq();
    uint32_t divider;
    uint32_t baud_bits =
        (SD_SPI_INSTANCE->CR1 & SPI_CR1_BR) >> SPI_CR1_BR_Pos;

    switch (baud_bits)
    {
        case 0U:
            divider = 2U;
            break;

        case 1U:
            divider = 4U;
            break;

        case 2U:
            divider = 8U;
            break;

        case 3U:
            divider = 16U;
            break;

        case 4U:
            divider = 32U;
            break;

        case 5U:
            divider = 64U;
            break;

        case 6U:
            divider = 128U;
            break;

        case 7U:
        default:
            divider = 256U;
            break;
    }

    return peripheral_clock_hz / divider;
}
HAL_StatusTypeDef SD_SPI_SendCommand(uint8_t command,
                                    uint32_t argument,
                                    uint8_t crc,
                                    uint8_t *r1_response)
{
    HAL_StatusTypeDef status;
    uint8_t command_packet[6];
    uint8_t response = SD_IDLE_BYTE;

    if (!sd_spi_initialized || (r1_response == NULL))
    {
        return HAL_ERROR;
    }

    /*
     * Default the caller's result to 0xFF. This makes the output
     * meaningful even if transmission or response polling fails.
     */
    *r1_response = SD_IDLE_BYTE;

    command_packet[0] =
        (uint8_t)(SD_COMMAND_PREFIX | (command & 0x3FU));

    command_packet[1] =
        (uint8_t)((argument >> 24U) & 0xFFU);

    command_packet[2] =
        (uint8_t)((argument >> 16U) & 0xFFU);

    command_packet[3] =
        (uint8_t)((argument >> 8U) & 0xFFU);

    command_packet[4] =
        (uint8_t)(argument & 0xFFU);

    command_packet[5] = crc;

    /*
     * Supply eight clocks while the card is deselected.
     */
    SD_SPI_Deselect();

    status = SD_SPI_WriteByte(SD_IDLE_BYTE);

    if (status != HAL_OK)
    {
        return status;
    }

    /*
     * Select the card and transmit the command packet immediately.
     */
    SD_SPI_Select();

    status = SD_SPI_Transfer(
        command_packet,
        NULL,
        (uint32_t)sizeof(command_packet));

    if (status != HAL_OK)
    {
        SD_SPI_Deselect();
        (void)SD_SPI_WriteByte(SD_IDLE_BYTE);
        return status;
    }

    /*
     * An R1 response has bit 7 cleared. The card may return one
     * or more 0xFF bytes before returning its response.
     */
    for (uint32_t attempt = 0U;
         attempt < SD_RESPONSE_TIMEOUT;
         attempt++)
    {
        status = SD_SPI_ReadByte(&response);

        if (status != HAL_OK)
        {
            SD_SPI_Deselect();
            (void)SD_SPI_WriteByte(SD_IDLE_BYTE);
            return status;
        }

        if ((response & 0x80U) == 0U)
        {
            *r1_response = response;

            SD_SPI_Deselect();

            /*
             * Supply eight trailing clocks after releasing CS.
             */
            status = SD_SPI_WriteByte(SD_IDLE_BYTE);

            if (status != HAL_OK)
            {
                return status;
            }

            return HAL_OK;
        }
    }

    /*
     * Preserve the final received byte for diagnostics.
     */
    *r1_response = response;

    SD_SPI_Deselect();
    (void)SD_SPI_WriteByte(SD_IDLE_BYTE);

    return HAL_TIMEOUT;
}
