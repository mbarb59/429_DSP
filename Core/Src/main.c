#include "stm32f429xx.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_pwr_ex.h"

#include "uart.h"
#include "f429_sdram.h"
#include "ltdc_ili9341.h"
#include "adc.h"
#include "capture.h"
#include "fft.h"
#include "display.h"
#include "i2c3.h"
#include "stmpe811.h"
#include "ui.h"

#include <stdint.h>
#include <string.h>

#define LCD_WIDTH              240U
#define LCD_HEIGHT             320U
#define FRAMEBUFFER_ADDR       0xD0000000U
#define COMMAND_BUFFER_SIZE    64U
#define DMA_TIMEOUT_MS         1000U

#define SPECTRUM_TOP           20
#define SPECTRUM_BOTTOM        220
#define SPECTRUM_HEIGHT        (SPECTRUM_BOTTOM - SPECTRUM_TOP)
#define SPECTRUM_MAX_MAG       1600U

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
static int32_t Spectrum_BinToX(uint32_t bin);
static int32_t Spectrum_MagnitudeToY(uint32_t magnitude);
static uint32_t Spectrum_StringWidth(const char *text);
static void Spectrum_FormatFrequency(uint32_t frequency_hz,
                                     char *buffer,
                                     uint32_t buffer_size);
static void Spectrum_DrawPeakLabel(int32_t peak_x,
                                   int32_t peak_y,
                                   uint32_t frequency_hz);
static void Spectrum_Draw(void);

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

if (f429_sdram_init() != HAL_OK)
{
    UART_SendString("SDRAM INIT FAIL\r\n");
    Error_Handler();
}

UART_SendString("SDRAM BASIC TEST...\r\n");

if (!f429_sdram_test())
{
    UART_SendString("SDRAM BASIC TEST FAIL\r\n");
    Error_Handler();
}

UART_SendString("SDRAM BASIC TEST OK\r\n");

UART_SendString("SDRAM STRESS TEST...\r\n");

if (!f429_sdram_stress())
{
    UART_SendString("SDRAM STRESS TEST FAIL\r\n");
    Error_Handler();
}

UART_SendString("SDRAM STRESS TEST OK\r\n");
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

    Display_Init(FRAMEBUFFER_ADDR);

    UART_SendString("DISPLAY OK\r\n");

    UART_SendString("TOUCH INIT...\r\n");

    if (STMPE811_Init() != HAL_OK)
    {
        UART_SendString("TOUCH INIT FAIL\r\n");
        Error_Handler();
    }

    UART_SendString("TOUCH OK\r\n");

    ADC_Init();
    DMA_Init();

    if (!Capture_SetSampleRate(CAPTURE_DEFAULT_RATE_HZ))
    {
        UART_SendString("CAPTURE RATE FAIL\r\n");
        Error_Handler();
    }

    UART_SendString("ADC/DMA OK\r\n");
    UI_Init();
    UART_SendString("UI OK\r\n");
    
    UART_SendString("Type help for commands.\r\n");
    Console_PrintPrompt();
    
    while (1)
    {
        Console_Poll();
        UI_ProcessTouch();
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
    else if (strcmp(command, "signal") == 0)
    {
        if (FFT_GenerateSine(1500U, 1500U, 2048U))
        {
            UART_SendString("Synthetic signal generated\r\n");
            UART_SendString("Frequency = 1500 Hz\r\n");
            UART_SendString("Samples = 256\r\n");
        }
        else
        {
            UART_SendString("Signal generation failed\r\n");
        }
    }
    else if (strcmp(command, "fft") == 0)
    {
        if (!FFT_Process())
        {
            UART_SendString("FFT failed\r\n");
            UART_SendString("Run signal or capturedma first\r\n");
        }
        else
        {
            uint32_t bin_width_millihz = FFT_GetBinWidthMilliHz();
            uint32_t peak_bin = FFT_GetPeakBin();

            UART_SendString("FFT complete\r\n");
            UART_SendString("FFT size = ");
            UART_SendDec(FFT_SIZE);
            UART_SendString("\r\n");
            UART_SendString("Sample rate = ");
            UART_SendDec(FFT_GetSampleRate());
            UART_SendString(" SPS\r\n");
            UART_SendString("Bin width = ");
            UART_SendDec(bin_width_millihz / 1000U);
            UART_SendChar('.');
            UART_SendDec(bin_width_millihz % 1000U);
            UART_SendString(" Hz\r\n");
            UART_SendString("DC level = ");
            UART_SendDec(FFT_GetDC());
            UART_SendString("\r\n");
            UART_SendString("RMS = ");
            UART_SendDec(FFT_GetRMS());
            UART_SendString("\r\n");
            UART_SendString("Peak bin = ");
            UART_SendDec(peak_bin);
            UART_SendString("\r\n");
            UART_SendString("Peak frequency = ");
            UART_SendDec(FFT_GetPeakFrequency());
            UART_SendString(" Hz\r\n");
            UART_SendString("Peak magnitude = ");
            UART_SendDec(FFT_GetMagnitude(peak_bin));
            UART_SendString("\r\n");

            Spectrum_Draw();
            UART_SendString("Spectrum drawn\r\n");
        }
    }
    else if (strcmp(command, "magdump") == 0)
    {
        if (!FFT_IsValid())
        {
            UART_SendString("No valid FFT result\r\n");
            UART_SendString("Run fft first\r\n");
        }
        else
        {
            for (uint32_t bin = 0U; bin < 16U; bin++)
            {
                UART_SendString("Bin ");
                UART_SendDec(bin);
                UART_SendString(" = ");
                UART_SendDec(FFT_GetMagnitude(bin));
                UART_SendString("\r\n");
            }
        }
    }
    else if (strcmp(command, "texttest") == 0)
    {
    Display_Clear(COLOR_BLACK);

    Display_DrawString(
        10,
        20,
        "0123456789",
        COLOR_WHITE,
        COLOR_BLACK);

    Display_DrawString(
        10,
        40,
        "938 Hz",
        COLOR_YELLOW,
        COLOR_BLACK);

    Display_DrawString(
        10,
        60,
        "1500 Hz",
        COLOR_GREEN,
        COLOR_BLACK);

    Display_DrawString(
        10,
        80,
        "2063 Hz",
        COLOR_CYAN,
        COLOR_BLACK);

    UART_SendString("LCD text test drawn\r\n");
    }
    else if (strcmp(command, "bars") == 0)
    {
        FillRgbBars(framebuffer);
        UART_SendString("RGB bars drawn\r\n");
    }
    else if (strcmp(command, "clear") == 0)
    {
        Display_Clear(COLOR_BLACK);
        UART_SendString("Display cleared\r\n");
    }
    else if (strcmp(command, "i2c") == 0)
    {
    UART_SendString("Testing I2C3...\r\n");

    if (I2C3_Init() != HAL_OK)
    {
        UART_SendString("I2C3 initialization failed\r\n");
    }
    else if (I2C3_IsReady(0x41U))
    {
        UART_SendString("STMPE811 detected at address 0x41\r\n");
    }
    else
    {
        UART_SendString("STMPE811 not detected\r\n");
    }
  } else if (strcmp(command, "touchinit") == 0)
    {
    uint16_t chip_id = 0U;

    UART_SendString("Initializing STMPE811...\r\n");
    
    if (STMPE811_Init() != HAL_OK)
    {
        UART_SendString("STMPE811 initialization failed\r\n");
    }
    else if (STMPE811_ReadChipId(&chip_id) != HAL_OK)
    {
        UART_SendString("STMPE811 chip ID read failed\r\n");
    }
    else
    {
        UART_SendString("STMPE811 initialization OK\r\n");

        UART_SendString("Chip ID = ");
        UART_SendHex32((uint32_t)chip_id);
        UART_SendString("\r\n");

        if (chip_id == STMPE811_EXPECTED_ID)
        {
            UART_SendString("Chip ID verified\r\n");
        }
        else
        {
            UART_SendString("Unexpected chip ID\r\n");
        }
    }
}
      else if (strcmp(command, "touchraw") == 0)
{
    STMPE811_TouchData touch = {0};

    if (!STMPE811_IsInitialized())
    {
        UART_SendString("Touch controller is not initialized\r\n");
        UART_SendString("Run touchinit first\r\n");
    }
    else if (STMPE811_ReadRaw(&touch) != HAL_OK)
    {
        UART_SendString("Touch read failed\r\n");
    }
    else if (!touch.pressed)
    {
        UART_SendString("No touch detected\r\n");
    }
    else
    {
        UART_SendString("Touch detected\r\n");

        UART_SendString("Raw X = ");
        UART_SendDec((uint32_t)touch.raw_x);
        UART_SendString("\r\n");

        UART_SendString("Raw Y = ");
        UART_SendDec((uint32_t)touch.raw_y);
        UART_SendString("\r\n");

        UART_SendString("Pressure = ");
        UART_SendDec((uint32_t)touch.pressure);
        UART_SendString("\r\n");
    }
}     else if (strcmp(command, "corners") == 0)
{
    Display_Clear(COLOR_BLACK);

    /* Top-left: red. */
    Display_FillRect(
        0,  0,
        30,
        30,
        COLOR_RED);

    /* Top-right: green. */
    Display_FillRect(
        DISPLAY_WIDTH - 30,
        0,
        30,
        30,
        COLOR_GREEN);

    /* Bottom-left: blue. */
    Display_FillRect(
        0, DISPLAY_HEIGHT - 30,
        30,
        30,
        COLOR_BLUE);

    /* Bottom-right: yellow. */
    Display_FillRect(
        DISPLAY_WIDTH - 30,
        DISPLAY_HEIGHT - 30,
        30,
        30,
        COLOR_YELLOW);

    UART_SendString("Corner markers drawn\r\n");
    UART_SendString("Top-left = red\r\n");
    UART_SendString("Top-right = green\r\n");
    UART_SendString("Bottom-left = blue\r\n");
    UART_SendString("Bottom-right = yellow\r\n");
}
else if (strcmp(command, "gridtest") == 0)
{
    Display_Clear(COLOR_BLACK);

    /* Draw a white border around the logical 320 x 240 screen. */
    Display_DrawRect(
        0,
        0,
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
        COLOR_WHITE);

    /* Draw vertical grid lines every 40 pixels. */
    for (int32_t x = 40;
         x < (int32_t)DISPLAY_WIDTH;
         x += 40)
    {
        Display_DrawLine(
            x,
            0,
            x,
            (int32_t)DISPLAY_HEIGHT - 1,
            COLOR_DARK_GRAY);
    }

    /* Draw horizontal grid lines every 40 pixels. */
    for (int32_t y = 40;
         y < (int32_t)DISPLAY_HEIGHT;
         y += 40)
    {
        Display_DrawLine(
            0,
            y,
            (int32_t)DISPLAY_WIDTH - 1,
            y,
            COLOR_DARK_GRAY);
    }

    /* Top-left marker. */
    Display_FillRect(
        0,
        0,
        20,
        20,
        COLOR_RED);

    /* Top-right marker. */
    Display_FillRect(
        (int32_t)DISPLAY_WIDTH - 20,
        0,
        20,
        20,
        COLOR_GREEN);

    /* Bottom-left marker. */
    Display_FillRect(
        0,
        (int32_t)DISPLAY_HEIGHT - 20,
        20,
        20,
        COLOR_BLUE);

    /* Bottom-right marker. */
    Display_FillRect(
        (int32_t)DISPLAY_WIDTH - 20,
        (int32_t)DISPLAY_HEIGHT - 20,
        20,
        20,
        COLOR_YELLOW);

    UART_SendString("Display grid drawn\r\n");
}
    else if (strcmp(command, "ltdcstat") == 0)
{
    uint32_t status = LTDC->ISR;

    UART_SendString("LTDC_ISR=");
    UART_SendHex32(status);
    UART_SendString("\r\n");

    if ((status & LTDC_ISR_FUIF) != 0U)
    {
        UART_SendString("FIFO UNDERRUN\r\n");
    }
    else
    {
        UART_SendString("FIFO OK\r\n");
    }

    if ((status & LTDC_ISR_TERRIF) != 0U)
    {
        UART_SendString("TRANSFER ERROR\r\n");
    }
    else
    {
        UART_SendString("TRANSFER OK\r\n");
    }

    LTDC->ICR = LTDC_ICR_CFUIF |
                LTDC_ICR_CTERRIF;
}
    else if (strcmp(command, "touch") == 0)
{
    STMPE811_TouchData touch = {0};

    if (!STMPE811_IsInitialized())
    {
        UART_SendString("Touch controller not initialized\r\n");
        UART_SendString("Run touchinit first\r\n");
    }
    else if (STMPE811_ReadTouch(&touch) != HAL_OK)
    {
        UART_SendString("Touch read failed\r\n");
    }
    else if (!touch.pressed)
    {
        UART_SendString("No touch detected\r\n");
    }
    else
    {
        UART_SendString("Touch detected\r\n");

        UART_SendString("Raw X = ");
        UART_SendDec(touch.raw_x);
        UART_SendString("\r\n");

        UART_SendString("Raw Y = ");
        UART_SendDec(touch.raw_y);
        UART_SendString("\r\n");

        UART_SendString("Screen X = ");
        UART_SendDec(touch.x);
        UART_SendString("\r\n");

        UART_SendString("Screen Y = ");
        UART_SendDec(touch.y);
        UART_SendString("\r\n");
    }
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
        "signal      - Generate synthetic 1500 Hz signal\r\n"
        "fft         - Process current sample buffer\r\n"
        "magdump     - Dump FFT bins 0 through 15\r\n"
        "texttest    - Draw LCD font test\r\n"
        "bars        - Draw RGB test bars\r\n"
        "clear       - Clear the display\r\n"
        "i2c         - Test STMPE811 I2C connection\r\n"
        "touchinit   - Initialize STMPE811 and verify ID\r\n"
        "touchraw    - Read one raw touch sample\r\n"
        "corners     - Draw touch calibration corners\r\n"
	"ltdcstat    - Check LTDC error flags\r\n"
        "touch       - Read calibrated touch coordinates\r\n"
        "gridtest    - Draw display geometry test\r\n");
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

static int32_t Spectrum_BinToX(uint32_t bin)
{
    /* Bin 0 is DC. Map useful bins 1 through 127 across columns 0 through 239. */
    if (bin < 1U)
    {
        bin = 1U;
    }

    if (bin >= FFT_BIN_COUNT)
    {
        bin = FFT_BIN_COUNT - 1U;
    }

    return (int32_t)(((bin - 1U) * (DISPLAY_WIDTH - 1U)) /
                     (FFT_BIN_COUNT - 2U));
}

static int32_t Spectrum_MagnitudeToY(uint32_t magnitude)
{
    if (magnitude > SPECTRUM_MAX_MAG)
    {
        magnitude = SPECTRUM_MAX_MAG;
    }

    return SPECTRUM_BOTTOM -
           (int32_t)((magnitude * SPECTRUM_HEIGHT) /
                     SPECTRUM_MAX_MAG);
}

static uint32_t Spectrum_StringWidth(const char *text)
{
    uint32_t character_count = 0U;

    if (text == NULL)
    {
        return 0U;
    }

    while (*text != '\0')
    {
        character_count++;
        text++;
    }

    if (character_count == 0U)
    {
        return 0U;
    }

    return (character_count * (DISPLAY_FONT_WIDTH + 1U)) - 1U;
}

static void Spectrum_FormatFrequency(uint32_t frequency_hz,
                                     char *buffer,
                                     uint32_t buffer_size)
{
    char reversed_digits[10];
    uint32_t digit_count = 0U;
    uint32_t output_index = 0U;

    if ((buffer == NULL) || (buffer_size < 5U))
    {
        return;
    }

    if (frequency_hz == 0U)
    {
        reversed_digits[digit_count++] = '0';
    }
    else
    {
        while ((frequency_hz != 0U) &&
               (digit_count < (uint32_t)sizeof(reversed_digits)))
        {
            reversed_digits[digit_count++] =
                (char)('0' + (frequency_hz % 10U));
            frequency_hz /= 10U;
        }
    }

    while ((digit_count != 0U) &&
           (output_index < (buffer_size - 4U)))
    {
        digit_count--;
        buffer[output_index++] = reversed_digits[digit_count];
    }

    buffer[output_index++] = ' ';
    buffer[output_index++] = 'H';
    buffer[output_index++] = 'z';
    buffer[output_index] = '\0';
}

static void Spectrum_DrawPeakLabel(int32_t peak_x,
                                   int32_t peak_y,
                                   uint32_t frequency_hz)
{
    char frequency_text[12];
    uint32_t text_width;
    int32_t label_x;
    int32_t label_y;

    Spectrum_FormatFrequency(frequency_hz,
                             frequency_text,
                             sizeof(frequency_text));

    text_width = Spectrum_StringWidth(frequency_text);

    if (text_width == 0U)
    {
        return;
    }

    label_x = peak_x - ((int32_t)text_width / 2);
    label_y = peak_y - 10 - (int32_t)DISPLAY_FONT_HEIGHT - 3;

    if (label_x < 2)
    {
        label_x = 2;
    }

    if ((label_x + (int32_t)text_width) >
        ((int32_t)DISPLAY_WIDTH - 2))
    {
        label_x =
            (int32_t)DISPLAY_WIDTH - (int32_t)text_width - 2;
    }

    if (label_y < (SPECTRUM_TOP + 2))
    {
        label_y = SPECTRUM_TOP + 2;
    }

    Display_FillRect(label_x - 2,
                     label_y - 1,
                     (int32_t)text_width + 4,
                     (int32_t)DISPLAY_FONT_HEIGHT + 2,
                     COLOR_BLACK);

    Display_DrawString(label_x,
                       label_y,
                       frequency_text,
                       COLOR_YELLOW,
                       COLOR_BLACK);
}

static void Spectrum_Draw(void)
{
    if (!FFT_IsValid())
    {
        return;
    }

    Display_Clear(COLOR_BLACK);

    /* Graph outline and three horizontal reference guides. */
    Display_DrawRect(0,
                     SPECTRUM_TOP,
                     (int32_t)DISPLAY_WIDTH,
                     SPECTRUM_HEIGHT + 1,
                     COLOR_GRAY);

    for (int32_t y = SPECTRUM_TOP + (SPECTRUM_HEIGHT / 4);
         y < SPECTRUM_BOTTOM;
         y += (SPECTRUM_HEIGHT / 4))
    {
        Display_DrawLine(0,
                         y,
                         (int32_t)DISPLAY_WIDTH - 1,
                         y,
                         COLOR_DARK_GRAY);
    }

    /* Connect adjacent useful FFT bins to form a continuous spectrum trace. */
    for (uint32_t bin = 2U; bin < FFT_BIN_COUNT; bin++)
    {
        int32_t x0 = Spectrum_BinToX(bin - 1U);
        int32_t x1 = Spectrum_BinToX(bin);
        int32_t y0 = Spectrum_MagnitudeToY(
            FFT_GetMagnitude(bin - 1U));
        int32_t y1 = Spectrum_MagnitudeToY(
            FFT_GetMagnitude(bin));

        Display_DrawLine(x0, y0, x1, y1, COLOR_GREEN);
    }

/* Draw a short red marker above the strongest FFT bin. */
{
    uint32_t peak_bin = FFT_GetPeakBin();

    int32_t peak_x =
        Spectrum_BinToX(peak_bin);

    int32_t peak_y =
        Spectrum_MagnitudeToY(
            FFT_GetMagnitude(peak_bin));

    int32_t marker_top = peak_y - 10;

    if (marker_top < SPECTRUM_TOP)
    {
        marker_top = SPECTRUM_TOP;
    }

    Display_DrawLine(
        peak_x,
        marker_top,
        peak_x,
        peak_y,
        COLOR_RED);

    Display_DrawLine(
        peak_x - 3,
        marker_top,
        peak_x + 3,
        marker_top,
        COLOR_RED);

    Spectrum_DrawPeakLabel(
        peak_x,
        peak_y,
        FFT_GetPeakFrequency());
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
