#ifndef STM32F4XX_HAL_CONF_H
#define STM32F4XX_HAL_CONF_H

/* HAL core */
#define HAL_MODULE_ENABLED

/* Modules actually used */
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_SPI_MODULE_ENABLED
#define HAL_LTDC_MODULE_ENABLED
#define HAL_SDRAM_MODULE_ENABLED

/* Clock values */
#define HSE_VALUE               8000000U
#define HSI_VALUE              16000000U
#define LSE_VALUE                 32768U
#define LSI_VALUE                 32000U

/* HAL tick priority */
#define TICK_INT_PRIORITY           0U

/* Optional HAL settings */
#define DATA_CACHE_ENABLE           1
#define INSTRUCTION_CACHE_ENABLE    1
#define PREFETCH_ENABLE             1
#define USE_RTOS                    0

/* HAL includes */
#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_cortex.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_dma.h"
#include "stm32f4xx_hal_spi.h"
#include "stm32f4xx_hal_ltdc.h"
#include "stm32f4xx_hal_sdram.h"

/* Disable assertions */
#define assert_param(expr) ((void)0)

#endif /* STM32F4XX_HAL_CONF_H */