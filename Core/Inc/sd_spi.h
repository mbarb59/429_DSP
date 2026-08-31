#ifndef SD_SPI_H
#define SD_SPI_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Adafruit 5V-ready MicroSD breakout connections:
 *
 *   STM32F429I-DISC1      MicroSD breakout
 *   ---------------------------------------
 *   PB13 / SPI2_SCK       CLK
 *   PB14 / SPI2_MISO      DO
 *   PB15 / SPI2_MOSI      DI
 *   PA5  / GPIO output    CS
 *   5V                    5V
 *   GND                   GND
 *
 * Breakout 3V and CD are currently left unconnected.
 */

#define SD_SPI_INSTANCE          SPI2

#define SD_SPI_GPIO_PORT         GPIOB
#define SD_SPI_SCK_PIN           GPIO_PIN_13
#define SD_SPI_MISO_PIN          GPIO_PIN_14
#define SD_SPI_MOSI_PIN          GPIO_PIN_15
#define SD_SPI_GPIO_AF           GPIO_AF5_SPI2

#define SD_CS_GPIO_PORT          GPIOA
#define SD_CS_PIN                GPIO_PIN_5

#define SD_SPI_TIMEOUT_MS        100U

/*
 * SPI2 runs from the APB1 peripheral clock.
 * With an 84 MHz SPI2 input clock and a divider of 256:
 *
 *   84,000,000 / 256 = 328,125 Hz
 *
 * This is suitable for initial SD-card communication.
 */
#define SD_SPI_INIT_PRESCALER    SPI_BAUDRATEPRESCALER_256

/*
 * Initialize GPIO and SPI2 at the low SD-card startup speed.
 *
 * CS is driven high before SPI2 is enabled.
 *
 * Returns HAL_OK on success.
 */
HAL_StatusTypeDef SD_SPI_Init(void);

/*
 * Return true after SD_SPI_Init() completes successfully.
 */
bool SD_SPI_IsInitialized(void);

/*
 * Assert the card's chip-select input.
 *
 * SD card selected:
 *   CS = LOW
 */
void SD_SPI_Select(void);

/*
 * Deassert the card's chip-select input.
 *
 * SD card deselected:
 *   CS = HIGH
 */
void SD_SPI_Deselect(void);

/*
 * Return true if the CS output is currently high.
 */
bool SD_SPI_IsDeselected(void);

/*
 * Transfer one byte through SPI2.
 *
 * SPI always transmits and receives simultaneously.
 * Transmit 0xFF when only a received byte is needed.
 */
HAL_StatusTypeDef SD_SPI_TransferByte(uint8_t transmit_byte,
                                     uint8_t *receive_byte);

/*
 * Transfer multiple bytes.
 *
 * Either buffer may be NULL:
 *
 *   transmit_data == NULL:
 *       Transmit 0xFF for every byte.
 *
 *   receive_data == NULL:
 *       Discard every received byte.
 */
HAL_StatusTypeDef SD_SPI_Transfer(const uint8_t *transmit_data,
                                 uint8_t *receive_data,
                                 uint32_t length);

/*
 * Send one byte and discard the received byte.
 */
HAL_StatusTypeDef SD_SPI_WriteByte(uint8_t value);

/*
 * Transmit 0xFF and return the received byte.
 */
HAL_StatusTypeDef SD_SPI_ReadByte(uint8_t *value);

/*
 * Send idle clocks while the card is deselected.
 *
 * SD-card startup requires at least 74 clock pulses with CS high.
 * Sending 10 bytes produces 80 clock pulses.
 */
HAL_StatusTypeDef SD_SPI_SendIdleClocks(void);

/*
 * Return the calculated SPI2 clock frequency in Hz.
 */
uint32_t SD_SPI_GetClockHz(void);

#endif /* SD_SPI_H */