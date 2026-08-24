#include "stm32f429xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_pwr_ex.h"

#include "uart.h"
#include "f429_sdram.h"
#include "ltdc_ili9341.h"

#define LCD_WIDTH          240U
#define LCD_HEIGHT         320U
#define FRAMEBUFFER_ADDR   0xD0000000U

static void SystemClock_Config(void);
static void Error_Handler(void);
static void FillRgbBars(volatile uint16_t *framebuffer);

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void HardFault_Handler(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;

    GPIOG->MODER &= ~(3U << (13U * 2U));
    GPIOG->MODER |=  (1U << (13U * 2U));

    while (1)
    {
        GPIOG->ODR ^= GPIO_PIN_13;

        for (volatile uint32_t i = 0U; i < 1000000U; i++)
        {
            __NOP();
        }
    }
}

int main(void)
{
    volatile uint16_t *framebuffer =
        (volatile uint16_t *)FRAMEBUFFER_ADDR;

    HAL_Init();
    SystemClock_Config();
    UART_Init();

    UART_SendString("\r\n");
    UART_SendString("================================\r\n");
    UART_SendString(" LTDC RGB TEST\r\n");
    UART_SendString("================================\r\n");

    UART_SendString("SDRAM INIT...\r\n");

    if (f429_sdram_init() != HAL_OK)
    {
        UART_SendString("SDRAM INIT FAIL\r\n");
        Error_Handler();
    }

    if (!f429_sdram_test())
    {
        UART_SendString("SDRAM TEST FAIL\r\n");
        Error_Handler();
    }

    UART_SendString("SDRAM OK\r\n");

    UART_SendString("LTDC INIT...\r\n");

    if (f429_ltdc_init() != HAL_OK)
    {
        UART_SendString("LTDC INIT FAIL\r\n");
        Error_Handler();
    }

    UART_SendString("LTDC OK\r\n");

    FillRgbBars(framebuffer);

    UART_SendString("LAYER INIT...\r\n");

    if (f429_ltdc_layer0_rgb565(
            (uint32_t)framebuffer,
            LCD_WIDTH,
            LCD_HEIGHT) != HAL_OK)
    {
        UART_SendString("LAYER INIT FAIL\r\n");
        Error_Handler();
    }

    UART_SendString("LAYER OK\r\n");

    while (1)
    {
        HAL_Delay(500U);
    }
}

static void FillRgbBars(volatile uint16_t *framebuffer)
{
    for (uint32_t y = 0U; y < LCD_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < LCD_WIDTH; x++)
        {
            uint32_t pixel = (y * LCD_WIDTH) + x;

            if (x < 80U)
            {
                framebuffer[pixel] = 0xF800U;
            }
            else if (x < 160U)
            {
                framebuffer[pixel] = 0x07E0U;
            }
            else
            {
                framebuffer[pixel] = 0x001FU;
            }
        }
    }
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8U;
    osc.PLL.PLLN = 336U;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7U;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_HCLK   |
                    RCC_CLOCKTYPE_PCLK1  |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }

    SystemCoreClockUpdate();
}

static void Error_Handler(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;

    GPIOG->MODER &= ~(3U << (14U * 2U));
    GPIOG->MODER |=  (1U << (14U * 2U));
    GPIOG->BSRR = GPIO_PIN_14;

    while (1)
    {
    }
}
