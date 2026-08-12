#include "stm32f429xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_pwr_ex.h"

#include "uart.h"
#include "f429_sdram.h"
#include "ltdc_ili9341.h"

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
    HAL_Init();

    SystemClock_Config();

    UART_Init();

    UART_SendString("\r\n");
    UART_SendString("================================\r\n");
    UART_SendString(" LTDC RGB TEST\r\n");
    UART_SendString("================================\r\n");

    /*
 * Initialize and test external SDRAM.
 */
UART_SendString("SDRAM INIT...\r\n");

if (f429_sdram_init() != HAL_OK)
{
    UART_SendString("SDRAM INIT FAIL\r\n");

    while (1)
    {
    }
}

UART_SendString("SDRAM INIT OK\r\n");

if (!f429_sdram_test())
{
    UART_SendString("SDRAM TEST FAIL\r\n");

    while (1)
    {
    }
}

UART_SendString("SDRAM TEST OK\r\n");

/*
 * Test whether different SDRAM addresses retain different values.
 * This test must run before LTDC starts reading the framebuffer.
 */
volatile uint16_t *sdram =
    (volatile uint16_t *)0xD0000000U;

sdram[0]     = 0x1111U;
sdram[1]     = 0x2222U;
sdram[2]     = 0x3333U;
sdram[80]    = 0x4444U;
sdram[160]   = 0x5555U;
sdram[1024]  = 0x6666U;
sdram[32768] = 0x7777U;

UART_SendString("SDRAM ADDRESS TEST\r\n");

UART_SendString("S0=");
UART_SendHex32((uint32_t)sdram[0]);
UART_SendString("\r\n");

UART_SendString("S1=");
UART_SendHex32((uint32_t)sdram[1]);
UART_SendString("\r\n");

UART_SendString("S2=");
UART_SendHex32((uint32_t)sdram[2]);
UART_SendString("\r\n");

UART_SendString("S80=");
UART_SendHex32((uint32_t)sdram[80]);
UART_SendString("\r\n");

UART_SendString("S160=");
UART_SendHex32((uint32_t)sdram[160]);
UART_SendString("\r\n");

UART_SendString("S1024=");
UART_SendHex32((uint32_t)sdram[1024]);
UART_SendString("\r\n");

UART_SendString("S32768=");
UART_SendHex32((uint32_t)sdram[32768]);
UART_SendString("\r\n");

/*
 * Continue with LTDC initialization.
 */
UART_SendString("LTDC INIT...\r\n");

if (f429_ltdc_init() != HAL_OK)
{
    UART_SendString("LTDC FAIL\r\n");

    while (1)
    {
    }
}

UART_SendString("LTDC OK\r\n");

    /*
     * Initialize the ILI9341 and LTDC.
     */
    UART_SendString("LTDC INIT...\r\n");

    if (f429_ltdc_init() != HAL_OK)
    {
        UART_SendString("LTDC FAIL\r\n");

        while (1)
        {
        }
    }

    UART_SendString("LTDC OK\r\n");

    /*
     * Create the framebuffer in external SDRAM.
     */
    UART_SendString("FRAMEBUFFER...\r\n");

    volatile uint16_t *fb =
        (volatile uint16_t *)0xD0000000U;

    /*
     * Fill the framebuffer with three vertical RGB bars.
     *
     * Columns 0-79:    red
     * Columns 80-159:  green
     * Columns 160-239: blue
     */
    for (uint32_t y = 0U; y < 320U; y++)
    {
        for (uint32_t x = 0U; x < 240U; x++)
        {
            uint32_t pixel = (y * 240U) + x;

            if (x < 80U)
            {
                fb[pixel] = 0xF800U;
            }
            else if (x < 160U)
            {
                fb[pixel] = 0x07E0U;
            }
            else
            {
                fb[pixel] = 0x001FU;
            }
        }
    }

    /*
     * Verify three representative framebuffer pixels.
     */
    UART_SendString("FB_RED=");
    UART_SendHex32((uint32_t)fb[0]);
    UART_SendString("\r\n");

    UART_SendString("FB_GREEN=");
    UART_SendHex32((uint32_t)fb[80]);
    UART_SendString("\r\n");

    UART_SendString("FB_BLUE=");
    UART_SendHex32((uint32_t)fb[160]);
    UART_SendString("\r\n");

    /*
     * Configure and enable LTDC Layer 0.
     */
    UART_SendString("LAYER INIT...\r\n");

    if (f429_ltdc_layer0_rgb565(
            (uint32_t)fb,
            240U,
            320U) != HAL_OK)
    {
        UART_SendString("LAYER FAIL\r\n");

        while (1)
        {
        }
    }

    UART_SendString("LAYER OK\r\n");

    /*
     * Verify the important LTDC Layer 0 registers.
     */
    UART_SendString("LCR=");
    UART_SendHex32(LTDC_Layer1->CR);
    UART_SendString("\r\n");

    UART_SendString("CFBAR=");
    UART_SendHex32(LTDC_Layer1->CFBAR);
    UART_SendString("\r\n");

    UART_SendString("PFCR=");
    UART_SendHex32(LTDC_Layer1->PFCR);
    UART_SendString("\r\n");

    UART_SendString("CFBLR=");
    UART_SendHex32(LTDC_Layer1->CFBLR);
    UART_SendString("\r\n");

    UART_SendString("CFBLNR=");
    UART_SendHex32(LTDC_Layer1->CFBLNR);
    UART_SendString("\r\n");

    /*
     * Application loop.
     */
    while (1)
    {
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
