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
#include "sd_spi.h"
#include "sd_card.h"
#include "storage_framework.h"
#include "recorder_framework.h"
#include "file_manager_framework.h"
#include "sd_raw_test.h"

#include <stdint.h>
#include <string.h>

#define LCD_WIDTH              240U
#define LCD_HEIGHT             320U
#define FRAMEBUFFER_ADDR       0xD0000000U
#define COMMAND_BUFFER_SIZE    64U
#define DMA_TIMEOUT_MS         1000U
#define ANALYZER_DISPLAY_PERIOD_MS  50U

#define SPECTRUM_TOP           20
#define SPECTRUM_BOTTOM        220
#define SPECTRUM_HEIGHT        (SPECTRUM_BOTTOM - SPECTRUM_TOP)

#define SPECTRUM_SCALE_DEFAULT 1600U
#define SPECTRUM_SCALE_MIN     100U
#define SPECTRUM_SCALE_MAX     65535U

static void SystemClock_Config(void);
static void Error_Handler(void);
static bool Analyzer_RequireStopped(void);
static bool Analyzer_SetFFTSize(uint32_t fft_size);
static void Analyzer_Process(void);
static void FillRgbBars(volatile uint16_t *framebuffer);
static void Console_Poll(void);
static void Console_ProcessCommand(char *command);
static void Console_PrintPrompt(void);
static void Console_PrintHelp(void);
static void Console_PrintStatus(void);
static void Console_PrintAnalyzerConfig(void);
static void Console_PrintSdSpiStatus(void);
static void Console_RunSdSoftwareCmd0(void);
static void Console_RunSdSoftwareCmd8(void);
static void Console_RunSdCmd0(void);
static void Console_RunSdSoftwareCmd58(void);
static void Console_RunSdCsTest(void);
static void Console_RunSdInit(void);
static void Console_RunSdReadZero(void);
static void Console_RunDmaCapture(void);
static void Console_RunMicStats(void);
static void Console_DumpSamples(uint32_t count);
static void Console_RunSdRawTest(void);
static void Console_RunSdLoopback(void);
static void Console_RunSdDiTest(void);
static int32_t Spectrum_BinToX(uint32_t bin);
static int32_t Spectrum_MagnitudeToY(uint32_t magnitude);
static void Spectrum_SetScale(uint32_t new_scale);
static void Spectrum_AutoScale(void);
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

typedef enum
{
    ANALYZER_STOPPED = 0,
    ANALYZER_START_CAPTURE,
    ANALYZER_WAIT_CAPTURE
} AnalyzerState;

static AnalyzerState analyzer_state = ANALYZER_STOPPED;
static uint32_t analyzer_frame_count;
static uint32_t analyzer_error_count;
static uint32_t analyzer_last_display_ms;

static uint32_t spectrum_max_magnitude =
    SPECTRUM_SCALE_DEFAULT;

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
    Recorder_FrameworkInit();
    FileManager_FrameworkInit();

    UART_SendString("Type help for commands.\r\n");
    Console_PrintPrompt();

    while (1)
    {
        Console_Poll();
        UI_ProcessTouch();
        Analyzer_Process();
        Recorder_Process();
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
    else if (strcmp(command, "config") == 0)
    {
        Console_PrintAnalyzerConfig();
    }
    else if (strcmp(command, "sdspi") == 0)
    {
    Console_PrintSdSpiStatus();
    }
    else if (strcmp(command, "sdcmd0") == 0)
    {
        if (Analyzer_RequireStopped())
        {
            Console_RunSdCmd0();
        }
    }
else if (strcmp(command, "sddi") == 0)
{
    if (Analyzer_RequireStopped())
    {
        Console_RunSdDiTest();
    }
}
else if (strcmp(command, "sdraw") == 0)
{
    if (Analyzer_RequireStopped())
    {
        Console_RunSdRawTest();
    }
}
else if (strcmp(command, "sdloop") == 0)
{
    if (Analyzer_RequireStopped())
    {
        Console_RunSdLoopback();
    }
}
else if (strcmp(command, "sdbitcmd0") == 0)
{
    if (Analyzer_RequireStopped())
    {
        Console_RunSdSoftwareCmd0();
    }
}
else if (strcmp(command, "sdbitcmd8") == 0)
{
    if (Analyzer_RequireStopped())
    {
        Console_RunSdSoftwareCmd8();
    }
}
else if (strcmp(command, "sdbitcmd58") == 0)
{
    if (Analyzer_RequireStopped())
    {
        Console_RunSdSoftwareCmd58();
    }
}
else if (strcmp(command, "sdcs") == 0)
{
    if (Analyzer_RequireStopped())
    {
        Console_RunSdCsTest();
    }
}
else if (strcmp(command, "sdinit") == 0)
{
    if (Analyzer_RequireStopped())
    {
        Console_RunSdInit();
    }
}
else if (strcmp(command, "sdread0") == 0)
    {
        if (Analyzer_RequireStopped())
        {
            Console_RunSdReadZero();
        }
    }
    else if (strcmp(command, "fft256") == 0)
{
    if (Analyzer_SetFFTSize(256U))
    {
        Console_PrintAnalyzerConfig();
    }
}
    else if (strcmp(command, "fft512") == 0)
    {
        if (Analyzer_SetFFTSize(512U))
        {
            Console_PrintAnalyzerConfig();
        }
    }
    else if (strcmp(command, "fft1024") == 0)
    {
        if (Analyzer_SetFFTSize(1024U))
        {
            Console_PrintAnalyzerConfig();
        }
    }
    else if (strcmp(command, "fft2048") == 0)
    {
        if (Analyzer_SetFFTSize(2048U))
        {
            Console_PrintAnalyzerConfig();
        }
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
    else if (strcmp(command, "micstats") == 0)
    {
        Console_RunMicStats();
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
            UART_SendString("Samples = ");
            UART_SendDec(FFT_GetSize());
            UART_SendString("\r\n");
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
            UART_SendDec(FFT_GetSize());
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
    else if (strcmp(command, "scale100") == 0)
    {
        Spectrum_SetScale(100U);
    }
    else if (strcmp(command, "scale200") == 0)
    {
        Spectrum_SetScale(200U);
    }
    else if (strcmp(command, "scale500") == 0)
    {
        Spectrum_SetScale(500U);
    }
    else if (strcmp(command, "scale1000") == 0)
    {
        Spectrum_SetScale(1000U);
    }
    else if (strcmp(command, "scale1600") == 0)
    {
        Spectrum_SetScale(SPECTRUM_SCALE_DEFAULT);
    }
    else if (strcmp(command, "autoscale") == 0)
    {
        Spectrum_AutoScale();
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
        "config      - Show analyzer configuration\r\n"
        "sdspi       - Initialize and test SD SPI transport\r\n"
        "sdcmd0      - Reset SD card into SPI idle state\r\n"
	"sdbitcmd0  - Test CMD0 using software SPI\r\n"
	"sdbitcmd8  - Send CMD0 then CMD8 using software SPI\r\n"
	"sdbitcmd58 - Send CMD0 then CMD58 using software SPI\r\n"
	"sdcs        - Hold SD chip select low and high\r\n"
        "sdraw       - Minimal hardware SPI2 CMD0/CMD8 test\r\n"
	"sddi        - Hold SD DI high, low, then high\r\n"
	"sdloop      - Test SPI2 with PB15 connected to PB14\r\n"
        "sdinit      - Initialize SD card and show card information\r\n"
        "fft256      - Set capture and FFT size to 256\r\n"
        "fft512      - Set capture and FFT size to 512\r\n"
        "fft1024     - Set capture and FFT size to 1024\r\n"
        "fft2048     - Set capture and FFT size to 2048\r\n"
        "adc         - Read ADC1 channel 0 on PA0\r\n"
        "capture     - CPU-polled capture\r\n"
        "capturedma  - TIM2-triggered DMA capture\r\n"
        "micstats    - Capture and show microphone statistics\r\n"
        "dump        - Dump captured samples\r\n"
        "dump16      - Dump first 16 samples\r\n"
        "signal      - Generate synthetic 1500 Hz signal\r\n"
        "fft         - Process current sample buffer\r\n"
        "magdump     - Dump FFT bins 0 through 15\r\n"
        "scale100    - Set graph magnitude scale to 100\r\n"
        "scale200    - Set graph magnitude scale to 200\r\n"
        "scale500    - Set graph magnitude scale to 500\r\n"
        "scale1000   - Set graph magnitude scale to 1000\r\n"
        "scale1600   - Restore default magnitude scale\r\n"
        "autoscale   - Scale graph to current FFT result\r\n"
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

static void Console_PrintAnalyzerConfig(void)
{
    uint32_t rate_hz = Capture_GetSampleRate();
    uint32_t capture_count = Capture_GetSampleCount();
    uint32_t fft_size = FFT_GetSize();
    uint32_t bin_count = FFT_GetBinCount();
    uint64_t bin_width_millihz = 0U;
    uint64_t capture_time_microseconds = 0U;

    if (fft_size != 0U)
    {
        bin_width_millihz =
            ((uint64_t)rate_hz * 1000U) / fft_size;
    }

    if (rate_hz != 0U)
    {
        capture_time_microseconds =
            ((uint64_t)capture_count * 1000000U) / rate_hz;
    }

    UART_SendString("Analyzer Configuration\r\n");
    UART_SendString("----------------------\r\n");

    UART_SendString("Sample Rate     = ");
    UART_SendDec(rate_hz);
    UART_SendString(" SPS\r\n");

    UART_SendString("Capture Samples = ");
    UART_SendDec(capture_count);
    UART_SendString("\r\n");

    UART_SendString("FFT Size        = ");
    UART_SendDec(fft_size);
    UART_SendString("\r\n");

    UART_SendString("FFT Bins        = ");
    UART_SendDec(bin_count);
    UART_SendString("\r\n");

    UART_SendString("Bin Width       = ");
    UART_SendDec((uint32_t)(bin_width_millihz / 1000U));
    UART_SendChar('.');
    UART_SendDec((uint32_t)(bin_width_millihz % 1000U));
    UART_SendString(" Hz\r\n");

    UART_SendString("Capture Time    = ");
    UART_SendDec((uint32_t)(capture_time_microseconds / 1000U));
    UART_SendChar('.');
    UART_SendDec((uint32_t)(capture_time_microseconds % 1000U));
    UART_SendString(" ms\r\n");

    UART_SendString("Maximum Freq    = ");
    UART_SendDec(rate_hz / 2U);
    UART_SendString(" Hz\r\n");

    UART_SendString("Continuous Run  = ");
    UART_SendString(UI_IsRunning() ? "YES\r\n" : "NO\r\n");

    UART_SendString("Configuration   = ");

    if (capture_count == fft_size)
    {
        UART_SendString("MATCHED\r\n");
    }
    else
    {
        UART_SendString("ERROR: SIZE MISMATCH\r\n");
    }
}
static void Console_PrintSdSpiStatus(void)
{
    HAL_StatusTypeDef status;
    uint8_t idle_byte = 0U;

    UART_SendString("SD SPI Status\r\n");
    UART_SendString("-------------\r\n");

    UART_SendString("Peripheral    = SPI2\r\n");
    UART_SendString("SCK           = PB13\r\n");
    UART_SendString("MISO          = PB14\r\n");
    UART_SendString("MOSI          = PB15\r\n");
    UART_SendString("CS            = PA5\r\n");
    UART_SendString("SPI Mode      = 0\r\n");

    status = SD_SPI_Init();

    UART_SendString("Initialized   = ");

    if (status != HAL_OK)
    {
        UART_SendString("NO\r\n");
        UART_SendString("Init Status   = ");
        UART_SendDec((uint32_t)status);
        UART_SendString("\r\n");
        return;
    }

    UART_SendString("YES\r\n");
    UART_SendString("PB14 Mode     = ");
    UART_SendHex32(
         (GPIOB->MODER >> (14U * 2U)) & 0x3U);
    UART_SendString("\r\n");

    UART_SendString("PB14 Pull     = ");
    UART_SendHex32(
        (GPIOB->PUPDR >> (14U * 2U)) & 0x3U);
    UART_SendString("\r\n");

    UART_SendString("PB14 AF       = ");
    UART_SendHex32(
        (GPIOB->AFR[1] >> ((14U - 8U) * 4U)) & 0xFU);
    UART_SendString("\r\n");

    UART_SendString("PB14 IDR      = ");
    UART_SendDec(
        ((GPIOB->IDR & GPIO_PIN_14) != 0U) ? 1U : 0U);
    UART_SendString("\r\n");
    UART_SendString("Clock         = ");
    UART_SendDec(SD_SPI_GetClockHz());
    UART_SendString(" Hz\r\n");

    UART_SendString("CS State      = ");
    UART_SendString(
        SD_SPI_IsDeselected() ? "HIGH\r\n" : "LOW\r\n");

    status = SD_SPI_SendIdleClocks();

    UART_SendString("Idle Clocks   = ");

    if (status != HAL_OK)
    {
        UART_SendString("FAILED\r\n");
        UART_SendString("Idle Status   = ");
        UART_SendDec((uint32_t)status);
        UART_SendString("\r\n");
        return;
    }

    UART_SendString("OK\r\n");

    /*
     * Keep the card deselected. This generates another eight clocks
     * while observing the idle state of the MISO line.
     */
    SD_SPI_Deselect();
    status = SD_SPI_ReadByte(&idle_byte);

    UART_SendString("Idle Read     = ");

    if (status != HAL_OK)
    {
        UART_SendString("FAILED\r\n");
        UART_SendString("Read Status   = ");
        UART_SendDec((uint32_t)status);
        UART_SendString("\r\n");
        return;
    }

    UART_SendString("OK\r\n");

    UART_SendString("Idle RX Byte  = ");
    UART_SendHex32((uint32_t)idle_byte);
    UART_SendString("\r\n");

    UART_SendString("CS Final      = ");
    UART_SendString(
        SD_SPI_IsDeselected() ? "HIGH\r\n" : "LOW\r\n");
}
static void Console_RunSdCmd0(void)
{
    HAL_StatusTypeDef status;
    uint8_t r1_response = 0xFFU;
    bool idle_response_received = false;

    UART_SendString("SD CMD0 Test\r\n");
    UART_SendString("------------\r\n");

    status = SD_SPI_Init();

    UART_SendString("SPI Init      = ");

    if (status != HAL_OK)
    {
        UART_SendString("FAILED\r\n");
        UART_SendString("HAL Status    = ");
        UART_SendDec((uint32_t)status);
        UART_SendString("\r\n");
        return;
    }

    UART_SendString("OK\r\n");

    /*
     * Let the card supply and internal controller stabilize before
     * sending the required startup clocks.
     */
    HAL_Delay(20U);

    status = SD_SPI_SendIdleClocks();

    UART_SendString("Idle Clocks   = ");

    if (status != HAL_OK)
    {
        UART_SendString("FAILED\r\n");
        UART_SendString("HAL Status    = ");
        UART_SendDec((uint32_t)status);
        UART_SendString("\r\n");
        return;
    }

    UART_SendString("OK\r\n");

    /*
     * CMD0 attempts continue here.
     */
    for (uint32_t attempt = 1U; attempt <= 10U; attempt++)
    {
        UART_SendString("CMD0 Attempt  = ");
        UART_SendDec(attempt);
        UART_SendString("\r\n");

        status = SD_SPI_SendCommand(
            0U,
            0U,
            0x95U,
            &r1_response);

        if ((status == HAL_OK) &&
            (r1_response == 0x01U))
        {
            idle_response_received = true;
            break;
        }

        HAL_Delay(10U);
    }

    UART_SendString("R1 Response   = ");
    UART_SendHex32((uint32_t)r1_response);
    UART_SendString("\r\n");

    UART_SendString("Card State    = ");

    if (idle_response_received)
    {
        UART_SendString("IDLE\r\n");
    }
    else
    {
        UART_SendString("NO RESPONSE\r\n");
    }

    UART_SendString("CS Final      = ");
    UART_SendString(
        SD_SPI_IsDeselected() ? "HIGH\r\n" : "LOW\r\n");
}
static void Console_RunSdSoftwareCmd0(void)
{
    HAL_StatusTypeDef status;
    uint8_t r1_response = 0xFFU;

    UART_SendString("Software SPI CMD0 Test\r\n");
    UART_SendString("----------------------\r\n");

    status = SD_Card_Reset(&r1_response);

    UART_SendString("HAL Status    = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    UART_SendString("R1 Response   = ");
    UART_SendHex32((uint32_t)r1_response);
    UART_SendString("\r\n");

    UART_SendString("Card State    = ");

    if ((status == HAL_OK) &&
        (r1_response == 0x01U))
    {
        UART_SendString("IDLE\r\n");
    }
    else
    {
        UART_SendString("NOT IDLE\r\n");
    }
}
static void Console_RunSdDiTest(void)
{
    GPIO_InitTypeDef gpio = {0};

    UART_SendString("PB15 to PB14 Static Loopback\r\n");
    UART_SendString("----------------------------\r\n");

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*
     * PB15 is the test output.
     */
    gpio.Pin = GPIO_PIN_15;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    /*
     * PB14 is the test input.
     */
    gpio.Pin = GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    /*
     * Drive PB15 high.
     */
    GPIOB->BSRR = GPIO_PIN_15;
    HAL_Delay(100U);

    UART_SendString("PB15 Output High\r\n");

    UART_SendString("PB15 ODR      = ");
    UART_SendDec(
        ((GPIOB->ODR & GPIO_PIN_15) != 0U) ?
        1U :
        0U);
    UART_SendString("\r\n");

    UART_SendString("PB15 IDR      = ");
    UART_SendDec(
        ((GPIOB->IDR & GPIO_PIN_15) != 0U) ?
        1U :
        0U);
    UART_SendString("\r\n");

    UART_SendString("PB14 IDR      = ");
    UART_SendDec(
        ((GPIOB->IDR & GPIO_PIN_14) != 0U) ?
        1U :
        0U);
    UART_SendString("\r\n");

    UART_SendString("Measure PB14 now: HIGH for 5 seconds\r\n");
    HAL_Delay(5000U);

    /*
     * Drive PB15 low.
     */
    GPIOB->BSRR =
        (uint32_t)GPIO_PIN_15 << 16U;

    HAL_Delay(100U);

    UART_SendString("PB15 Output Low\r\n");

    UART_SendString("PB15 ODR      = ");
    UART_SendDec(
        ((GPIOB->ODR & GPIO_PIN_15) != 0U) ?
        1U :
        0U);
    UART_SendString("\r\n");

    UART_SendString("PB15 IDR      = ");
    UART_SendDec(
        ((GPIOB->IDR & GPIO_PIN_15) != 0U) ?
        1U :
        0U);
    UART_SendString("\r\n");

    UART_SendString("PB14 IDR      = ");
    UART_SendDec(
        ((GPIOB->IDR & GPIO_PIN_14) != 0U) ?
        1U :
        0U);
    UART_SendString("\r\n");

    UART_SendString("Measure PB14 now: LOW for 5 seconds\r\n");
    HAL_Delay(5000U);

    /*
     * Return PB15 high.
     */
    GPIOB->BSRR = GPIO_PIN_15;
    HAL_Delay(100U);

    UART_SendString("PB15 Output High Again\r\n");

    UART_SendString("PB14 IDR      = ");
    UART_SendDec(
        ((GPIOB->IDR & GPIO_PIN_14) != 0U) ?
        1U :
        0U);
    UART_SendString("\r\n");

    UART_SendString(
        "Power-cycle before another SPI test\r\n");
}
static void Console_RunSdCsTest(void)
{
    GPIO_InitTypeDef gpio = {0};

    UART_SendString("SD CS Test\r\n");
    UART_SendString("----------\r\n");

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * Preload CS high before configuring PA5 as an output.
     */
    GPIOA->BSRR = GPIO_PIN_5;

    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    UART_SendString("CS HIGH for 5 seconds\r\n");
    GPIOA->BSRR = GPIO_PIN_5;
    HAL_Delay(5000U);

    UART_SendString("CS LOW for 5 seconds\r\n");
    GPIOA->BSRR = (uint32_t)GPIO_PIN_5 << 16U;
    HAL_Delay(5000U);

    UART_SendString("CS HIGH for 5 seconds\r\n");
    GPIOA->BSRR = GPIO_PIN_5;
    HAL_Delay(5000U);

    UART_SendString("CS test complete\r\n");
}
static void Console_RunSdSoftwareCmd8(void)
{
    HAL_StatusTypeDef status;
    uint8_t cmd0_r1 = 0xFFU;
    uint8_t cmd8_r1 = 0xFFU;
    uint32_t r7_response = 0U;

    UART_SendString("Software SPI CMD8 Test\r\n");
    UART_SendString("----------------------\r\n");

    /*
     * Enter SPI mode and place the card in idle state.
     */
    status = SD_Card_Reset(&cmd0_r1);

    UART_SendString("CMD0 Status   = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    UART_SendString("CMD0 R1       = ");
    UART_SendHex32((uint32_t)cmd0_r1);
    UART_SendString("\r\n");

    if ((status != HAL_OK) ||
        (cmd0_r1 != 0x01U))
    {
        UART_SendString(
            "CMD8 skipped because CMD0 failed\r\n");

        return;
    }

    /*
     * Send CMD8 as a separate CS transaction.
     *
     * Packet:
     *     48 00 00 01 AA 87
     */
    status = SD_Card_CheckInterface(
        &cmd8_r1,
        &r7_response);

    UART_SendString("CMD8 Status   = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    UART_SendString("CMD8 R1       = ");
    UART_SendHex32((uint32_t)cmd8_r1);
    UART_SendString("\r\n");

    UART_SendString("CMD8 R7       = ");
    UART_SendHex32(r7_response);
    UART_SendString("\r\n");

    UART_SendString("CMD8 Result   = ");

    if (status == HAL_TIMEOUT)
    {
        UART_SendString("NO RESPONSE\r\n");
    }
    else if (status != HAL_OK)
    {
        UART_SendString("TRANSFER ERROR\r\n");
    }
    else if ((cmd8_r1 == 0x01U) &&
             (r7_response == 0x000001AAU))
    {
        UART_SendString("SD VERSION 2 CARD\r\n");
    }
    else if ((cmd8_r1 & 0x04U) != 0U)
    {
        UART_SendString(
            "CMD8 NOT SUPPORTED, POSSIBLE VERSION 1 CARD\r\n");
    }
    else
    {
        UART_SendString("UNEXPECTED RESPONSE\r\n");
    }
}
static void Console_RunSdLoopback(void)
{
    static const uint8_t transmit_buffer[] =
    {
        0x00U,
        0xFFU,
        0x55U,
        0xAAU,
        0x3CU,
        0xC3U,
        0x40U,
        0x95U
    };

    uint8_t receive_buffer[
        sizeof(transmit_buffer)] = {0U};

    HAL_StatusTypeDef status;
    bool all_passed = true;

    UART_SendString("SPI2 Hardware Loopback Test\r\n");
    UART_SendString("---------------------------\r\n");

    status = SD_RawTest_LoopbackBuffer(
        transmit_buffer,
        receive_buffer,
        (uint16_t)sizeof(transmit_buffer));

    UART_SendString("Transfer Status = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    for (uint32_t index = 0U;
         index < (uint32_t)sizeof(transmit_buffer);
         index++)
    {
        UART_SendString("TX = ");
        UART_SendHex32(
            (uint32_t)transmit_buffer[index]);

        UART_SendString("  RX = ");
        UART_SendHex32(
            (uint32_t)receive_buffer[index]);

        if ((status == HAL_OK) &&
            (receive_buffer[index] ==
             transmit_buffer[index]))
        {
            UART_SendString("  PASS\r\n");
        }
        else
        {
            UART_SendString("  FAIL\r\n");
            all_passed = false;
        }
    }

    UART_SendString("SPI2 CR1      = ");
    UART_SendHex32(SPI2->CR1);
    UART_SendString("\r\n");

    UART_SendString("SPI2 CR2      = ");
    UART_SendHex32(SPI2->CR2);
    UART_SendString("\r\n");

    UART_SendString("SPI2 SR       = ");
    UART_SendHex32(SPI2->SR);
    UART_SendString("\r\n");

    UART_SendString("PB13 Mode     = ");
    UART_SendHex32(
        (GPIOB->MODER >> (13U * 2U)) &
        0x03U);
    UART_SendString("\r\n");

    UART_SendString("PB14 Mode     = ");
    UART_SendHex32(
        (GPIOB->MODER >> (14U * 2U)) &
        0x03U);
    UART_SendString("\r\n");

    UART_SendString("PB15 Mode     = ");
    UART_SendHex32(
        (GPIOB->MODER >> (15U * 2U)) &
        0x03U);
    UART_SendString("\r\n");

    UART_SendString("PB13 AF       = ");
    UART_SendHex32(
        (GPIOB->AFR[1] >>
         ((13U - 8U) * 4U)) &
        0x0FU);
    UART_SendString("\r\n");

    UART_SendString("PB14 AF       = ");
    UART_SendHex32(
        (GPIOB->AFR[1] >>
         ((14U - 8U) * 4U)) &
        0x0FU);
    UART_SendString("\r\n");

    UART_SendString("PB15 AF       = ");
    UART_SendHex32(
        (GPIOB->AFR[1] >>
         ((15U - 8U) * 4U)) &
        0x0FU);
    UART_SendString("\r\n");

    UART_SendString("Result        = ");

    if (all_passed)
    {
        UART_SendString("PASS\r\n");
    }
    else
    {
        UART_SendString("FAIL\r\n");
    }
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

    if (sampleCount != 0U)
    {
        UART_SendString("First Sample = ");
        UART_SendDec(sampleBuffer[0]);
        UART_SendString("\r\n");

        UART_SendString("Last Sample = ");
        UART_SendDec(sampleBuffer[sampleCount - 1U]);
        UART_SendString("\r\n");
    }
    else
    {
        UART_SendString("Capture buffer is empty\r\n");
    }
}
static void Console_RunSdSoftwareCmd58(void)
{
    HAL_StatusTypeDef status;
    uint8_t cmd0_r1 = 0xFFU;
    uint8_t cmd58_r1 = 0xFFU;
    uint32_t ocr = 0U;

    UART_SendString("Software SPI CMD58 Test\r\n");
    UART_SendString("-----------------------\r\n");

    status = SD_Card_Reset(&cmd0_r1);

    UART_SendString("CMD0 Status   = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    UART_SendString("CMD0 R1       = ");
    UART_SendHex32((uint32_t)cmd0_r1);
    UART_SendString("\r\n");

    if ((status != HAL_OK) ||
        (cmd0_r1 != 0x01U))
    {
        UART_SendString(
            "CMD58 skipped because CMD0 failed\r\n");

        return;
    }

    status = SD_Card_ReadOcr(
        &cmd58_r1,
        &ocr);

    UART_SendString("CMD58 Status  = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    UART_SendString("CMD58 R1      = ");
    UART_SendHex32((uint32_t)cmd58_r1);
    UART_SendString("\r\n");

    UART_SendString("OCR           = ");
    UART_SendHex32(ocr);
    UART_SendString("\r\n");

    if (status == HAL_TIMEOUT)
    {
        UART_SendString("CMD58 Result  = NO RESPONSE\r\n");
    }
    else if (status != HAL_OK)
    {
        UART_SendString("CMD58 Result  = TRANSFER ERROR\r\n");
    }
    else
    {
        UART_SendString("CMD58 Result  = RESPONSE RECEIVED\r\n");
    }
}
static void Console_RunSdReadZero(void)
{
    uint8_t preview[STORAGE_SECTOR0_PREVIEW_BYTES];
    HAL_StatusTypeDef status;

    UART_SendString("SD Sector 0 Read Test\r\n");
    UART_SendString("---------------------\r\n");

    status = Storage_ReadSectorZeroPreview(
        preview,
        sizeof(preview));

    UART_SendString("HAL Status = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    if (status != HAL_OK)
    {
        UART_SendString("Result     = FAIL\r\n");
        return;
    }

    for (uint32_t offset = 0U;
         offset < sizeof(preview);
         offset += 16U)
    {
        UART_SendHex32(offset);
        UART_SendString(": ");

        for (uint32_t index = 0U; index < 16U; index++)
        {
            uint8_t value = preview[offset + index];

            if (value < 0x10U)
            {
                UART_SendChar('0');
            }

            UART_SendHex32((uint32_t)value);
            UART_SendChar(' ');
        }

        UART_SendString("\r\n");
    }

    UART_SendString("Result     = PASS\r\n");
}

static void Console_RunSdInit(void)
{
    HAL_StatusTypeDef status;
    const SD_CardInfo *card_info;

    UART_SendString("SD Card Initialization\r\n");
    UART_SendString("----------------------\r\n");
    UART_SendString("Starting hardware SPI2 SD test\r\n");

    status = SD_Card_Init();
    card_info = SD_Card_GetInfo();
    UART_SendString("PB13 Mode     = ");
    UART_SendHex32(
                  (GPIOB->MODER >> (13U * 2U)) & 0x03U);
    UART_SendString("\r\n");

    UART_SendString("PB14 Mode     = ");
    UART_SendHex32(
                 (GPIOB->MODER >> (14U * 2U)) & 0x03U);
    UART_SendString("\r\n");

    UART_SendString("PB15 Mode     = ");
    UART_SendHex32(
                  (GPIOB->MODER >> (15U * 2U)) & 0x03U);
    UART_SendString("\r\n");

    UART_SendString("PB13 AF       = ");
    UART_SendHex32(
                  (GPIOB->AFR[1] >> ((13U - 8U) * 4U)) &
                  0x0FU);
    UART_SendString("\r\n");

    UART_SendString("PB14 AF       = ");
    UART_SendHex32(
                  (GPIOB->AFR[1] >> ((14U - 8U) * 4U)) &
                  0x0FU);
    UART_SendString("\r\n");

    UART_SendString("PB15 AF       = ");
    UART_SendHex32(
                  (GPIOB->AFR[1] >> ((15U - 8U) * 4U)) &
                  0x0FU);
    UART_SendString("\r\n");

    UART_SendString("SPI2 CR1      = ");
    UART_SendHex32(SPI2->CR1);
    UART_SendString("\r\n");

    UART_SendString("SPI2 CR2      = ");
    UART_SendHex32(SPI2->CR2);
    UART_SendString("\r\n");

    UART_SendString("SPI2 SR       = ");
    UART_SendHex32(SPI2->SR);
    UART_SendString("\r\n");
    UART_SendString("HAL Status    = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    UART_SendString("Initialized   = ");
    UART_SendString(
        card_info->initialized ?
        "YES\r\n" :
        "NO\r\n");

    UART_SendString("Version 2     = ");
    UART_SendString(
        card_info->version_2 ?
        "YES\r\n" :
        "NO\r\n");

    UART_SendString("CMD0 R1       = ");
    UART_SendHex32(
        (uint32_t)card_info->cmd0_r1);
    UART_SendString("\r\n");

    UART_SendString("CMD0 Tries    = ");
    UART_SendDec(
        card_info->cmd0_attempts);
    UART_SendString("\r\n");

    UART_SendString("CMD8 Status   = ");
    UART_SendDec(
        (uint32_t)card_info->cmd8_status);
    UART_SendString("\r\n");

    UART_SendString("CMD8 R1       = ");
    UART_SendHex32(
        (uint32_t)card_info->cmd8_r1);
    UART_SendString("\r\n");

    UART_SendString("CMD8 R7       = ");
    UART_SendHex32(
        card_info->cmd8_r7);
    UART_SendString("\r\n");

    UART_SendString("CMD55 Status  = ");
    UART_SendDec(
        (uint32_t)card_info->cmd55_status);
    UART_SendString("\r\n");

    UART_SendString("CMD55 R1      = ");
    UART_SendHex32(
        (uint32_t)card_info->cmd55_r1);
    UART_SendString("\r\n");

    UART_SendString("ACMD41 Status = ");
    UART_SendDec(
        (uint32_t)card_info->acmd41_status);
    UART_SendString("\r\n");

    UART_SendString("ACMD41 R1     = ");
    UART_SendHex32(
        (uint32_t)card_info->acmd41_r1);
    UART_SendString("\r\n");

    UART_SendString("ACMD41 Tries  = ");
    UART_SendDec(
        card_info->acmd41_attempts);
    UART_SendString("\r\n");

    UART_SendString("OCR           = ");
    UART_SendHex32(card_info->ocr);
    UART_SendString("\r\n");

    UART_SendString("Power Ready   = ");
    UART_SendString(
        card_info->power_ready ?
        "YES\r\n" :
        "NO\r\n");

    UART_SendString("3.3V Support  = ");
    UART_SendString(
        card_info->supports_3v3 ?
        "YES\r\n" :
        "NO\r\n");

    UART_SendString("Card Type     = ");

    switch (card_info->type)
    {
        case SD_CARD_TYPE_SDSC:
            UART_SendString("SDSC\r\n");
            break;

        case SD_CARD_TYPE_SDHC:
            UART_SendString("SDHC/SDXC\r\n");
            break;

        case SD_CARD_TYPE_UNKNOWN:
        default:
            UART_SendString("UNKNOWN\r\n");
            break;
    }

    UART_SendString("Addressing    = ");

    if (card_info->type == SD_CARD_TYPE_SDHC)
    {
        UART_SendString("BLOCK\r\n");
    }
    else if (card_info->type == SD_CARD_TYPE_SDSC)
    {
        UART_SendString("BYTE\r\n");
    }
    else
    {
        UART_SendString("UNKNOWN\r\n");
    }

    UART_SendString("Failure Stage = ");

    if ((status == HAL_OK) &&
        card_info->initialized)
    {
        UART_SendString("NONE\r\n");
    }
    else if (card_info->cmd0_attempts == 0U)
    {
        UART_SendString(
            "CMD0 NOT ATTEMPTED\r\n");
    }
    else if ((card_info->cmd0_r1 != 0x01U) &&
             (card_info->cmd0_r1 != 0x03U))
    {
        UART_SendString("CMD0 RESPONSE\r\n");
    }
    else if (card_info->cmd8_status == HAL_TIMEOUT)
    {
        UART_SendString("CMD8 TIMEOUT\r\n");
    }
    else if (card_info->cmd8_status != HAL_OK)
    {
        UART_SendString("CMD8 ERROR\r\n");
    }
    else if ((card_info->cmd8_r1 != 0x01U) &&
             (card_info->cmd8_r1 != 0x03U))
    {
        UART_SendString("CMD8 R1\r\n");
    }
    else if (card_info->cmd8_r7 != 0x000001AAU)
    {
        UART_SendString("CMD8 R7\r\n");
    }
    else if (card_info->cmd55_status == HAL_TIMEOUT)
    {
        UART_SendString("CMD55 TIMEOUT\r\n");
    }
    else if (card_info->cmd55_status != HAL_OK)
    {
        UART_SendString("CMD55 ERROR\r\n");
    }
    else if (card_info->acmd41_attempts == 0U)
    {
        UART_SendString(
            "ACMD41 NOT ATTEMPTED\r\n");
    }
    else if (card_info->acmd41_status == HAL_TIMEOUT)
    {
        UART_SendString("ACMD41 TIMEOUT\r\n");
    }
    else if (card_info->acmd41_status != HAL_OK)
    {
        UART_SendString("ACMD41 ERROR\r\n");
    }
    else if (card_info->acmd41_r1 != 0x00U)
    {
        UART_SendString(
            "ACMD41 NOT READY\r\n");
    }
    else if (card_info->ocr == 0U)
    {
        UART_SendString("CMD58 OR OCR\r\n");
    }
    else if (!card_info->power_ready)
    {
        UART_SendString(
            "OCR POWER READY\r\n");
    }
    else if (!card_info->supports_3v3)
    {
        UART_SendString("OCR VOLTAGE\r\n");
    }
    else
    {
        UART_SendString(
            "FINAL CARD CHECK\r\n");
    }

    UART_SendString("Result        = ");

    if ((status == HAL_OK) &&
        card_info->initialized)
    {
        UART_SendString("SUCCESS\r\n");
    }
    else if (status == HAL_TIMEOUT)
    {
        UART_SendString("TIMEOUT\r\n");
    }
    else
    {
        UART_SendString("FAILED\r\n");
    }
}
static void Console_RunSdRawTest(void)
{
    SD_RawTestResult result;
    HAL_StatusTypeDef status;

    UART_SendString(
        "Minimal Hardware SPI2 CMD0/CMD8 Test\r\n");

    UART_SendString(
        "-----------------------------------\r\n");

    status = SD_RawTest_Run(&result);

    UART_SendString("SPI Init      = ");
    UART_SendDec(
        (uint32_t)result.spi_init_status);
    UART_SendString("\r\n");

    UART_SendString("CMD0 Status   = ");
    UART_SendDec(
        (uint32_t)result.cmd0_status);
    UART_SendString("\r\n");

    UART_SendString("CMD0 R1       = ");
    UART_SendHex32(
        (uint32_t)result.cmd0_r1);
    UART_SendString("\r\n");

    UART_SendString("CMD8 Status   = ");
    UART_SendDec(
        (uint32_t)result.cmd8_status);
    UART_SendString("\r\n");

    UART_SendString("CMD8 R1       = ");
    UART_SendHex32(
        (uint32_t)result.cmd8_r1);
    UART_SendString("\r\n");

    UART_SendString("CMD8 R7       = ");
    UART_SendHex32(result.cmd8_r7);
    UART_SendString("\r\n");

    UART_SendString("Final Status  = ");
    UART_SendDec((uint32_t)status);
    UART_SendString("\r\n");

    UART_SendString("Result        = ");

    if ((status == HAL_OK) &&
        (result.cmd0_r1 == 0x01U) &&
        (result.cmd8_r1 == 0x01U) &&
        (result.cmd8_r7 == 0x000001AAUL))
    {
        UART_SendString("PASS\r\n");
    }
    else
    {
        UART_SendString("FAIL\r\n");
    }
}
static void Console_RunMicStats(void)
{
    uint32_t deadline;
    uint16_t minimum;
    uint16_t maximum;
    uint64_t sum = 0U;
    uint32_t average;
    uint32_t peak_to_peak;
    uint32_t peak_bin;
    bool near_clip_low = false;
    bool near_clip_high = false;

    UART_SendString("Starting microphone capture...\r\n");
    DMA_CaptureSamples();

    deadline = HAL_GetTick() + DMA_TIMEOUT_MS;

    while (captureComplete == 0)
    {
        if ((int32_t)(HAL_GetTick() - deadline) >= 0)
        {
            UART_SendString("Microphone capture timeout\r\n");
            return;
        }
    }

    if (captureComplete < 0)
    {
        UART_SendString("Microphone capture error\r\n");
        return;
    }

    if (sampleCount == 0U)
    {
        UART_SendString("Microphone buffer empty\r\n");
        return;
    }

    minimum = sampleBuffer[0];
    maximum = sampleBuffer[0];

    for (uint32_t i = 0U; i < sampleCount; i++)
    {
        uint16_t sample = sampleBuffer[i];

        if (sample < minimum)
        {
            minimum = sample;
        }

        if (sample > maximum)
        {
            maximum = sample;
        }

        if (sample <= 50U)
        {
            near_clip_low = true;
        }

        if (sample >= 4045U)
        {
            near_clip_high = true;
        }

        sum += sample;
    }

    average = (uint32_t)(sum / sampleCount);
    peak_to_peak = (uint32_t)maximum - (uint32_t)minimum;

    if (!FFT_Process())
    {
        UART_SendString("FFT processing failed\r\n");
        return;
    }

    peak_bin = FFT_GetPeakBin();

    UART_SendString("Microphone Statistics\r\n");
    UART_SendString("---------------------\r\n");
    UART_SendString("Samples         = ");
    UART_SendDec(sampleCount);
    UART_SendString("\r\n");
    UART_SendString("Sample Rate     = ");
    UART_SendDec(FFT_GetSampleRate());
    UART_SendString(" SPS\r\n");
    UART_SendString("Minimum         = ");
    UART_SendDec(minimum);
    UART_SendString("\r\n");
    UART_SendString("Maximum         = ");
    UART_SendDec(maximum);
    UART_SendString("\r\n");
    UART_SendString("DC Average      = ");
    UART_SendDec(average);
    UART_SendString("\r\n");
    UART_SendString("Peak-to-Peak    = ");
    UART_SendDec(peak_to_peak);
    UART_SendString("\r\n");
    UART_SendString("AC RMS          = ");
    UART_SendDec(FFT_GetRMS());
    UART_SendString("\r\n");
    UART_SendString("Near Clip Low   = ");
    UART_SendString(near_clip_low ? "YES\r\n" : "NO\r\n");
    UART_SendString("Near Clip High  = ");
    UART_SendString(near_clip_high ? "YES\r\n" : "NO\r\n");
    UART_SendString("Peak Bin        = ");
    UART_SendDec(peak_bin);
    UART_SendString("\r\n");
    UART_SendString("Peak Frequency  = ");
    UART_SendDec(FFT_GetPeakFrequency());
    UART_SendString(" Hz\r\n");
    UART_SendString("Peak Magnitude  = ");
    UART_SendDec(FFT_GetMagnitude(peak_bin));
    UART_SendString("\r\n");

    Spectrum_Draw();
    UART_SendString("Spectrum drawn\r\n");
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
    uint32_t bin_count = FFT_GetBinCount();

    if (bin_count <= 2U)
    {
        return 0;
    }

    if (bin < 1U)
    {
        bin = 1U;
    }

    if (bin >= bin_count)
    {
        bin = bin_count - 1U;
    }

    return (int32_t)(((bin - 1U) * (DISPLAY_WIDTH - 1U)) /
                     (bin_count - 2U));
}

static void Spectrum_SetScale(uint32_t new_scale)
{
    if (new_scale < SPECTRUM_SCALE_MIN)
    {
        new_scale = SPECTRUM_SCALE_MIN;
    }

    if (new_scale > SPECTRUM_SCALE_MAX)
    {
        new_scale = SPECTRUM_SCALE_MAX;
    }

    spectrum_max_magnitude = new_scale;

    UART_SendString("Spectrum scale = ");
    UART_SendDec(spectrum_max_magnitude);
    UART_SendString("\r\n");

    if (FFT_IsValid())
    {
        Spectrum_Draw();
        UART_SendString("Spectrum redrawn\r\n");
    }
    else
    {
        UART_SendString("No valid FFT result to draw\r\n");
    }
}

static void Spectrum_AutoScale(void)
{
    uint32_t maximum_magnitude = 0U;
    uint32_t new_scale;

    if (!FFT_IsValid())
    {
        UART_SendString("No valid FFT result\r\n");
        UART_SendString("Run fft or micstats first\r\n");
        return;
    }

    for (uint32_t bin = 1U;
         bin < FFT_GetBinCount();
         bin++)
    {
        uint32_t magnitude = FFT_GetMagnitude(bin);

        if (magnitude > maximum_magnitude)
        {
            maximum_magnitude = magnitude;
        }
    }

    new_scale = maximum_magnitude + (maximum_magnitude / 4U);

    if (new_scale < SPECTRUM_SCALE_MIN)
    {
        new_scale = SPECTRUM_SCALE_MIN;
    }

    if (new_scale > SPECTRUM_SCALE_MAX)
    {
        new_scale = SPECTRUM_SCALE_MAX;
    }

    UART_SendString("Peak magnitude = ");
    UART_SendDec(maximum_magnitude);
    UART_SendString("\r\n");

    UART_SendString("Old scale = ");
    UART_SendDec(spectrum_max_magnitude);
    UART_SendString("\r\n");

    spectrum_max_magnitude = new_scale;

    UART_SendString("New scale = ");
    UART_SendDec(spectrum_max_magnitude);
    UART_SendString("\r\n");

    Spectrum_Draw();
    UART_SendString("Spectrum redrawn\r\n");
}

static int32_t Spectrum_MagnitudeToY(uint32_t magnitude)
{
    uint64_t scaled_height;

    if (spectrum_max_magnitude < SPECTRUM_SCALE_MIN)
    {
        spectrum_max_magnitude = SPECTRUM_SCALE_MIN;
    }

    if (magnitude > spectrum_max_magnitude)
    {
        magnitude = spectrum_max_magnitude;
    }

    scaled_height =
        ((uint64_t)magnitude * (uint64_t)SPECTRUM_HEIGHT) /
        (uint64_t)spectrum_max_magnitude;

    return SPECTRUM_BOTTOM - (int32_t)scaled_height;
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
    for (uint32_t bin = 2U;
        bin < FFT_GetBinCount();
        bin++)
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

static bool Analyzer_RequireStopped(void)
{
    if (UI_IsRunning())
    {
        UART_SendString("Stop continuous analyzer first\r\n");
        return false;
    }

    if (analyzer_state != ANALYZER_STOPPED)
    {
        UART_SendString("Analyzer capture is still active\r\n");
        return false;
    }

    return true;
}
static bool Analyzer_SetFFTSize(uint32_t fft_size)
{
    uint32_t previous_capture_count;
    uint32_t previous_fft_size;

    if (!Analyzer_RequireStopped())
    {
        return false;
    }

    if (!Capture_IsValidSampleCount(fft_size) ||
        !FFT_IsValidSize(fft_size))
    {
        UART_SendString("Unsupported FFT size\r\n");
        return false;
    }

    previous_capture_count = Capture_GetSampleCount();
    previous_fft_size = FFT_GetSize();

    if (!Capture_SetSampleCount(fft_size))
    {
        UART_SendString("Capture size change failed\r\n");
        return false;
    }

    if (!FFT_SetSize(fft_size))
    {
        (void)Capture_SetSampleCount(previous_capture_count);
        (void)FFT_SetSize(previous_fft_size);
        UART_SendString("FFT size change failed\r\n");
        return false;
    }

    analyzer_state = ANALYZER_STOPPED;
    analyzer_last_display_ms = HAL_GetTick();

    sampleCount = 0U;
    captureComplete = 1;
    FFT_Invalidate();
    UI_OnFFTConfigurationChanged();

    UART_SendString("Capture and FFT size set to ");
    UART_SendDec(fft_size);
    UART_SendString("\r\n");

    return true;
}

static void Analyzer_Process(void)
{
    if (!UI_IsRunning())
    {
        analyzer_state = ANALYZER_STOPPED;
        return;
    }

    switch (analyzer_state)
    {
        case ANALYZER_STOPPED:
            analyzer_state = ANALYZER_START_CAPTURE;
            break;

        case ANALYZER_START_CAPTURE:
            if (Capture_GetSampleCount() != FFT_GetSize())
            {
                analyzer_error_count++;
                UI_SetRunning(false);
                analyzer_state = ANALYZER_STOPPED;
                UART_SendString(
                    "Continuous analyzer size mismatch\r\n");
                break;
            }

            DMA_CaptureSamples();
            analyzer_state = ANALYZER_WAIT_CAPTURE;
            break;

        case ANALYZER_WAIT_CAPTURE:
            if (captureComplete == 0)
            {
                break;
            }

            if (captureComplete < 0)
            {
                analyzer_error_count++;
                UI_SetRunning(false);
                analyzer_state = ANALYZER_STOPPED;
                UART_SendString("Continuous capture error\r\n");
                break;
            }

            if (!FFT_Process())
            {
                analyzer_error_count++;
                UI_SetRunning(false);
                analyzer_state = ANALYZER_STOPPED;
                UART_SendString("Continuous FFT error\r\n");
                break;
            }

            analyzer_frame_count++;

            if ((HAL_GetTick() - analyzer_last_display_ms) >=
                ANALYZER_DISPLAY_PERIOD_MS)
            {
                analyzer_last_display_ms = HAL_GetTick();
                UI_NotifySpectrumChanged();
            }

            analyzer_state = ANALYZER_START_CAPTURE;
            break;

        default:
            analyzer_error_count++;
            UI_SetRunning(false);
            analyzer_state = ANALYZER_STOPPED;
            UART_SendString("Continuous analyzer state error\r\n");
            break;
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
