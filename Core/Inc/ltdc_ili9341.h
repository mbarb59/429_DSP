#ifndef MICROPY_INCLUDED_STM32F429DISC_LTDC_ILI9341_H
#define MICROPY_INCLUDED_STM32F429DISC_LTDC_ILI9341_H

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stddef.h>

HAL_StatusTypeDef f429_ltdc_init(void);

HAL_StatusTypeDef ili9341_init_spi(void);
HAL_StatusTypeDef ili9341_hw_reset(void);
HAL_StatusTypeDef ili9341_send_cmd(uint8_t cmd);
HAL_StatusTypeDef ili9341_send_data(const uint8_t *data, size_t len);
HAL_StatusTypeDef ili9341_init_sequence(void);

HAL_StatusTypeDef f429_ltdc_layer0_rgb565(uint32_t fb_addr, uint16_t width, uint16_t height);
HAL_StatusTypeDef f429_ltdc_layer0_enable(int enable);

uint16_t f429_lcd_width(void);
uint16_t f429_lcd_height(void);

#endif