#ifndef MICROPY_INCLUDED_STM32F429DISC_F429_SDRAM_H
#define MICROPY_INCLUDED_STM32F429DISC_F429_SDRAM_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

HAL_StatusTypeDef f429_sdram_init(void);
bool f429_sdram_test(void);
bool f429_sdram_stress(void);
uint32_t f429_sdram_base(void);

#endif