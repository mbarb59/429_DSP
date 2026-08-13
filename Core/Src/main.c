#include "stm32f429xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_pwr_ex.h"

#include "uart.h"
#include "f429_sdram.h"
#include "ltdc_ili9341.h"
#include "adc.h"
#include "capture.h"

#include <stdint.h>
#include <string.h>

#define LCD_WIDTH              240U
#define LCD_HEIGHT             320U
#define FRAMEBUFFER_ADDR       0xD0000000U
#define COMMAND_BUFFER_SIZE    64U
#define DMA_TIMEOUT_MS         1000U

static void SystemClock_Config(void);
static void Error_Handler(void);
static void FillRgbBars(volatile uint16_t *framebuffer);
static void Console_Poll(void);
static void Console_ProcessCommand(char *command);
static void Console_PrintPrompt(void);
static void Console_PrintHelp(void);
static void Console_PrintStatus(void);
static void Console_RunDmaCapture(void);
static void Console_DumpSamples(uint32_t count);

static volatile uint16_t *const framebuffer =
    (volatile uint16_t *)FRAMEBUFFER_ADDR;

static char command_buffer[COMMAND_BUFFER_SIZE];
static uint32_t command_length;

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
    HAL_Init();
    SystemClock_Config();
    UART_Init();

    UART_SendString("\r\n");
    UART_SendString("================================\r\n");
    UART_SendString(" STM32F429 SPECTRUM ANALYZER\r\n");
    UART_SendString("================================\r\n");

    UART_SendString("SDRAM INIT...\r\n");

    if (f429_sdram_init() != HAL_OK || !f429_sdram_test())
    {
        UART_SendString("SDRAM FAIL\r\n");
        Error_Handler();
    }

    UART_SendString("SDRAM OK\r\n");
    UART_SendString("LTDC INIT...\r\n");

    if (f429_ltdc_init() != HAL_OK)
    {
        UART_SendString("LTDC FAIL\r\n");
        Error_Handler();
    }

    FillRgbBars(framebuffer);

    if (f429_ltdc_layer0_rgb565(
            (uint32_t)framebuffer,
            LCD_WIDTH,
            LCD_HEIGHT) != HAL_OK)
    {
        UART_SendString("LAYER FAIL\r\n");
        Error_Handler();
    }

    UART_SendString("DISPLAY OK\r\n");

    ADC_Init();
    DMA_Init();

    if (!Capture_SetSampleRate(CAPTURE_DEFAULT_RATE_HZ))
    {
        UART_SendString("CAPTURE RATE FAIL\r\n");
        Error_Handler();
    }

    UART_SendString("ADC/DMA OK\r\n");
    UART_SendString("Type help for commands.\r\n");
    Console_PrintPrompt();

    while (1)
    {
        Console_Poll();
    }
}

static void Console_Poll(void)
{
    while ((USART1->SR & USART_SR_RXNE) != 0U)
    {
        char character = (char)(USART1->DR & 0xFFU);

        if ((character == '\r') || (character == '\n'))
        {
            if (command_length != 0U)
            {
                command_buffer[command_length] = '\0';
                UART_SendString("\r\n");
                Console_ProcessCommand(command_buffer);
                command_length = 0U;
                Console_PrintPrompt();
            }
        }
        else if ((character == '\b') || ((uint8_t)character == 0x7FU))
        {
            if (command_length != 0U)
            {
                command_length--;
                UART_SendString("\b \b");
            }
        }
        else if (command_length < (COMMAND_BUFFER_SIZE - 1U))
        {
            command_buffer[command_length++] = character;
            UART_SendChar(character);
        }
    }
}

static void Console_ProcessCommand(char *command)
{
    if (strcmp(command, "help") == 0)
    {
        Console_PrintHelp();
    }
    else if (strcmp(command, "version") == 0)
    {
        UART_SendString("STM32F429 Spectrum Analyzer\r\n");
        UART_SendString("Version 0.1\r\n");
    }
    else if (strcmp(command, "status") == 0)
    {
        Console_PrintStatus();
    }
    else if (strcmp(command, "adc") == 0)
    {
        UART_SendString("ADC1 = ");
        UART_SendDec((uint32_t)ADC_Read());
        UART_SendString("\r\n");
    }
    else if (strcmp(command, "capture") == 0)
    {
        Capture_Samples();
        UART_SendString("CPU capture complete\r\n");
        UART_SendString("Samples = ");
        UART_SendDec(sampleCount);
        UART_SendString("\r\n");
    }
    else if (strcmp(command, "capturedma") == 0)
    {
        Console_RunDmaCapture();
    }
    else if (strcmp(command, "dump") == 0)
    {
        Console_DumpSamples(sampleCount);
    }
    else if (strcmp(command, "dump16") == 0)
    {
        Console_DumpSamples(16U);
    }
    else if (strcmp(command, "bars") == 0)
    {
        FillRgbBars(framebuffer);
        UART_SendString("RGB bars drawn\r\n");
    }
    else if (strcmp(command, "clear") == 0)
    {
        for (uint32_t i = 0U; i < (LCD_WIDTH * LCD_HEIGHT); i++)
        {
            framebuffer[i] = 0x0000U;
        }

        UART_SendString("Display cleared\r\n");
    }
    else if (command[0] != '\0')
    {
        UART_SendString("Unknown command\r\n");
    }
}

static void Console_PrintHelp(void)
{
    UART_SendString(
        "\r\n"
        "F429 Analyzer Commands\r\n"
        "----------------------\r\n"
        "help        - Show command list\r\n"
        "version     - Show firmware version\r\n"
        "status      - Show system status\r\n"
        "adc         - Read ADC1 channel 0 on PA0\r\n"
        "capture     - CPU-polled capture\r\n"
        "capturedma  - TIM2-triggered DMA capture\r\n"
        "dump        - Dump captured samples\r\n"
        "dump16      - Dump first 16 samples\r\n"
        "bars        - Draw RGB test bars\r\n"
        "clear       - Clear the display\r\n");
}

static void Console_PrintStatus(void)
{
    UART_SendString("Board: STM32F429I-DISC1\r\n");

    UART_SendString("Core Clock: ");
    UART_SendDec(SystemCoreClock);
    UART_SendString(" Hz\r\n");

    UART_SendString("Capture Rate: ");
    UART_SendDec(Capture_GetSampleRate());
    UART_SendString(" SPS\r\n");

    UART_SendString("Sample Count: ");
    UART_SendDec(sampleCount);
    UART_SendString("\r\n");

    UART_SendString("SDRAM: OK\r\n");
    UART_SendString("Display: OK\r\n");
    UART_SendString("ADC Input: ADC1 channel 0, PA0\r\n");
}

static void Console_RunDmaCapture(void)
{
    uint32_t deadline;

    UART_SendString("Starting DMA capture...\r\n");
    DMA_CaptureSamples();

    deadline = HAL_GetTick() + DMA_TIMEOUT_MS;

    while (captureComplete == 0)
    {
        if ((int32_t)(HAL_GetTick() - deadline) >= 0)
{
    UART_SendString("DMA capture timeout\r\n");

    UART_SendString("TIM2_CR1=");
    UART_SendHex32(TIM2->CR1);
    UART_SendString("\r\n");

    UART_SendString("TIM2_CR2=");
    UART_SendHex32(TIM2->CR2);
    UART_SendString("\r\n");

    UART_SendString("TIM2_CNT=");
    UART_SendHex32(TIM2->CNT);
    UART_SendString("\r\n");

    UART_SendString("TIM2_ARR=");
    UART_SendHex32(TIM2->ARR);
    UART_SendString("\r\n");

    UART_SendString("ADC_CR2=");
    UART_SendHex32(ADC1->CR2);
    UART_SendString("\r\n");

    UART_SendString("ADC_SR=");
    UART_SendHex32(ADC1->SR);
    UART_SendString("\r\n");

    UART_SendString("DMA_CR=");
    UART_SendHex32(DMA2_Stream0->CR);
    UART_SendString("\r\n");

    UART_SendString("DMA_NDTR=");
    UART_SendHex32(DMA2_Stream0->NDTR);
    UART_SendString("\r\n");

    UART_SendString("DMA_LISR=");
    UART_SendHex32(DMA2->LISR);
    UART_SendString("\r\n");

    return;
}
    }

    if (captureComplete < 0)
    {
        UART_SendString("DMA capture error\r\n");
        return;
    }

    UART_SendString("DMA capture complete\r\n");

    UART_SendString("Samples = ");
    UART_SendDec(sampleCount);
    UART_SendString("\r\n");

    UART_SendString("Sample Rate = ");
    UART_SendDec(sampleRate);
    UART_SendString(" SPS\r\n");

    UART_SendString("DMA Cycles = ");
    UART_SendDec(dmaCaptureCycles);
    UART_SendString("\r\n");

    UART_SendString("First Sample = ");
    UART_SendDec(sampleBuffer[0]);
    UART_SendString("\r\n");

    UART_SendString("Last Sample = ");
    UART_SendDec(sampleBuffer[SAMPLE_COUNT - 1U]);
    UART_SendString("\r\n");
}

static void Console_DumpSamples(uint32_t count)
{
    if (sampleCount == 0U)
    {
        UART_SendString("Buffer empty\r\n");
        return;
    }

    if (count > sampleCount)
    {
        count = sampleCount;
    }

    for (uint32_t i = 0U; i < count; i++)
    {
        UART_SendDec(sampleBuffer[i]);
        UART_SendString("\r\n");
    }
}

static void Console_PrintPrompt(void)
{
    UART_SendString("> ");
}

static void FillRgbBars(volatile uint16_t *target)
{
    for (uint32_t y = 0U; y < LCD_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < LCD_WIDTH; x++)
        {
            uint32_t pixel = (y * LCD_WIDTH) + x;

            if (x < 80U)
            {
                target[pixel] = 0xF800U;
            }
            else if (x < 160U)
            {
                target[pixel] = 0x07E0U;
            }
            else
            {
                target[pixel] = 0x001FU;
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
