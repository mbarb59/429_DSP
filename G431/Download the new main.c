#include "stm32f429xx.h"
#include "stm32f4xx_hal.h"
#include "uart.h"

static void SystemClock_Config(void);
static void Error_Handler(void);

/*
 * HAL_Init() configures SysTick. The startup file normally provides only a
 * weak SysTick handler, so this strong implementation is required for the HAL
 * millisecond time base and timeout functions.
 */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

void HardFault_Handler(void)
{
    /* PG13 toggles rapidly if a HardFault occurs. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;

    GPIOG->MODER &= ~(3U << (13U * 2U));
    GPIOG->MODER |=  (1U << (13U * 2U));

    while (1)
    {
        GPIOG->ODR ^= GPIO_PIN_13;

        for (volatile uint32_t i = 0; i < 1000000U; i++)
        {
            __NOP();
        }
    }
}

int main(void)
{
    /* Initialize HAL, Flash interface, NVIC grouping, and SysTick. */
    HAL_Init();

    /* Configure HSE and the main PLL before initializing the UART. */
    SystemClock_Config();

    /* UART_Init() must calculate BRR from the current PCLK2 frequency. */
    UART_Init();

    /* Discovery LEDs PG13 and PG14. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;

    GPIOG->MODER &= ~((3U << (13U * 2U)) |
                      (3U << (14U * 2U)));
    GPIOG->MODER |=  ((1U << (13U * 2U)) |
                      (1U << (14U * 2U)));

    UART_SendString("\r\n");
    UART_SendString("================================\r\n");
    UART_SendString(" STM32F429 CLOCK TEST\r\n");
    UART_SendString("================================\r\n");

    SystemCoreClockUpdate();

    UART_SendString("CORE=");
    UART_SendHex32(SystemCoreClock);
    UART_SendString("\r\n");

    UART_SendString("SYSCLK=");
    UART_SendHex32(HAL_RCC_GetSysClockFreq());
    UART_SendString("\r\n");

    UART_SendString("HCLK=");
    UART_SendHex32(HAL_RCC_GetHCLKFreq());
    UART_SendString("\r\n");

    UART_SendString("PCLK1=");
    UART_SendHex32(HAL_RCC_GetPCLK1Freq());
    UART_SendString("\r\n");

    UART_SendString("PCLK2=");
    UART_SendHex32(HAL_RCC_GetPCLK2Freq());
    UART_SendString("\r\n");

    UART_SendString("CLOCK TEST OK\r\n");

    while (1)
    {
        GPIOG->ODR ^= GPIO_PIN_13;
        UART_SendString("ALIVE\r\n");
        HAL_Delay(500U);
    }
}

/*
 * Conservative STM32F429I-DISC1 clock setup:
 *
 * HSE       = 8 MHz
 * PLLM      = 8
 * PLLN      = 336
 * PLLP      = 2
 * SYSCLK    = 168 MHz
 * HCLK      = 168 MHz
 * PCLK1     = 42 MHz
 * PCLK2     = 84 MHz
 *
 * This deliberately uses 168 MHz so HAL_PWREx_EnableOverDrive() and the
 * stm32f4xx_hal_pwr_ex.c linker dependency are not required for this first
 * clean clock test.
 */
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
    /* If clock configuration fails, PG14 remains on. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;

    GPIOG->MODER &= ~(3U << (14U * 2U));
    GPIOG->MODER |=  (1U << (14U * 2U));
    GPIOG->BSRR = GPIO_PIN_14;

    while (1)
    {
    }
}
