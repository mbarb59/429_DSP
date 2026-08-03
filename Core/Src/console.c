#include <stdint.h>
#include <string.h>

#include "console.h"
#include "uart.h"
#include "adc.h"
#include "capture.h"
#include "dsp.h"
#include "stm32g431xx.h"
#include "fft.h"

extern volatile int led_enabled;

static void UART_SendUInt(uint32_t value)
{
    char buf[16];
    int i = 0;

    if (value == 0)
    {
        UART_SendChar('0');
        return;
    }

    while (value > 0)
    {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i > 0)
    {
        UART_SendChar(buf[--i]);
    }
}

void Console_ProcessCommand(char *cmd)
{
    if (strcmp(cmd, "help") == 0)
    {
        UART_SendString(
            "\r\n"
            "431 DSP Analyzer Commands\r\n"
            "-------------------------\r\n"
            "help         - Show command list\r\n"
            "version      - Show firmware version\r\n"
            "status       - Show system status\r\n"
            "adc          - Read ADC channel\r\n"
            "adcavg       - Average 64 ADC samples\r\n"
            "adcminmax    - Show min/max of 64 samples\r\n"
            "adcstream    - Stream 32 ADC samples\r\n"
            "capture      - CPU capture\r\n"
            "capturedma   - DMA capture\r\n"
            "dump         - Dump sample buffer\r\n"
            "bufferstats  - Analyze sample buffer\r\n"
            "capturetime  - Measure capture speed\r\n"
            "peak         - Largest sample\r\n"
            "dc           - Average sample value\r\n"
            "range        - Show min/max range\r\n"
            "fft          - Analyze sample buffer\r\n"
            "led on       - Enable heartbeat LED\r\n"
            "led off      - Disable heartbeat LED\r\n"
            "dmarate      - Measure DMA capture rate\r\n"
            "dcdump       - Dump DC-removed samples\r\n"
            "windowdump - Dump Hann-windowed samples\r\n");
    }
    else if (strcmp(cmd, "version") == 0)
    {
        UART_SendString(
            "431 DSP Analyzer\r\n"
            "Version 1.0\r\n");
    }
    else if (strcmp(cmd, "status") == 0)
    {
        UART_SendString(
            "Board: STM32G431KB\r\n"
            "ADC: Enabled\r\n"
            "DMA: Enabled\r\n"
            "Status: OK\r\n");
    }
    else if (strcmp(cmd, "adc") == 0)
    {
        UART_SendString("ADC1 = ");
        UART_SendUInt(ADC_Read());
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "adcavg") == 0)
    {
        uint32_t sum = 0;

        for (int i = 0; i < 64; i++)
        {
            sum += ADC_Read();
        }

        UART_SendString("ADC AVG = ");
        UART_SendUInt(sum / 64);
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "adcminmax") == 0)
    {
        uint16_t min = 4095;
        uint16_t max = 0;

        for (int i = 0; i < 64; i++)
        {
            uint16_t value = ADC_Read();

            if (value < min)
            {
                min = value;
            }

            if (value > max)
            {
                max = value;
            }
        }

        UART_SendString("MIN = ");
        UART_SendUInt(min);
        UART_SendString("\r\n");

        UART_SendString("MAX = ");
        UART_SendUInt(max);
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "adcstream") == 0)
    {
        UART_SendString("Streaming 32 samples:\r\n");

        for (int i = 0; i < 32; i++)
        {
            UART_SendUInt(ADC_Read());
            UART_SendString("\r\n");
        }
    }
    else if (strcmp(cmd, "capture") == 0)
    {
        Capture_Samples();

        UART_SendString("Capture complete\r\n");
        UART_SendString("Samples = ");
        UART_SendUInt(sampleCount);
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "capturedma") == 0)
    {
        ADC_EnableDMA();

        DMA_CaptureSamples();

        while (!captureComplete)
        {
        }

        ADC_DisableDMA();

        UART_SendString("DMA Capture Complete\r\n");
        UART_SendString("Samples = ");
        UART_SendUInt(sampleCount);
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "dump") == 0)
    {
        if (sampleCount == 0)
        {
            UART_SendString("Buffer Empty\r\n");
        }
        else
        {
            for (uint32_t i = 0; i < sampleCount; i++)
            {
                UART_SendUInt(sampleBuffer[i]);
                UART_SendString("\r\n");
            }
        }
    }
    else if (strcmp(cmd, "bufferstats") == 0)
    {
        if (sampleCount == 0)
        {
            UART_SendString("Buffer Empty\r\n");
        }
        else
        {
            uint32_t sum = 0;
            uint16_t min = 4095;
            uint16_t max = 0;

            for (uint32_t i = 0; i < sampleCount; i++)
            {
                uint16_t value = sampleBuffer[i];

                sum += value;

                if (value < min)
                {
                    min = value;
                }

                if (value > max)
                {
                    max = value;
                }
            }

            UART_SendString("Samples = ");
            UART_SendUInt(sampleCount);
            UART_SendString("\r\n");

            UART_SendString("Min = ");
            UART_SendUInt(min);
            UART_SendString("\r\n");

            UART_SendString("Max = ");
            UART_SendUInt(max);
            UART_SendString("\r\n");

            UART_SendString("Average = ");
            UART_SendUInt(sum / sampleCount);
            UART_SendString("\r\n");
        }
    }
    else if (strcmp(cmd, "peak") == 0)
    {
        UART_SendString("Peak = ");
        UART_SendUInt(DSP_Peak());
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "dc") == 0)
    {
        UART_SendString("DC = ");
        UART_SendUInt(DSP_Average());
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "range") == 0)
    {
        UART_SendString("Min = ");
        UART_SendUInt(DSP_Min());
        UART_SendString("\r\n");

        UART_SendString("Max = ");
        UART_SendUInt(DSP_Max());
        UART_SendString("\r\n");

        UART_SendString("Range = ");
        UART_SendUInt(DSP_Max() - DSP_Min());
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "capturetime") == 0)
    {
        uint32_t start;
        uint32_t stop;

        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        start = DWT->CYCCNT;

        Capture_Samples();

        stop = DWT->CYCCNT;

        UART_SendString("Samples = ");
        UART_SendUInt(sampleCount);
        UART_SendString("\r\n");

        UART_SendString("Cycles = ");
        UART_SendUInt(stop - start);
        UART_SendString("\r\n");
    }
    else if (strcmp(cmd, "led on") == 0)
    {
        led_enabled = 1;
        UART_SendString("LED Enabled\r\n");
    }
    else if (strcmp(cmd, "led off") == 0)
    {
        led_enabled = 0;
        UART_SendString("LED Disabled\r\n");
    }

   else if (strcmp(cmd, "fft") == 0)
{
    if (sampleCount == 0)
    {
        UART_SendString(
            "No Samples\r\n"
            "Run capture or capturedma first\r\n");
    }
    else
    {
        FFT_Process();

        UART_SendString("FFT Analysis\r\n");

        UART_SendString("Peak Sample = ");
        UART_SendUInt(FFT_GetPeak());
        UART_SendString("\r\n");

        UART_SendString("DC Level = ");
        UART_SendUInt(FFT_GetDC());
        UART_SendString("\r\n");
        UART_SendString("RMS = ");
	UART_SendUInt(FFT_GetRMS());
	UART_SendString("\r\n");
        UART_SendString("Bin Width = ");
        UART_SendUInt(FFT_GetBinWidth());
        UART_SendString(" Hz\r\n");
    }
}
   else if (strcmp(cmd, "dmarate") == 0)
{
    uint32_t start;
    uint32_t stop;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    captureComplete = 0;

    start = DWT->CYCCNT;

    ADC_EnableDMA();
    DMA_CaptureSamples();

   uint32_t timeout = 10000000;

   while (!captureComplete && timeout--)
   {
   }

    stop = DWT->CYCCNT;

    ADC_DisableDMA();

    dmaCaptureCycles = stop - start;
   

    sampleRate =
    (uint32_t)(
        ((uint64_t)170000000UL * SAMPLE_COUNT) /
        dmaCaptureCycles);

    UART_SendString("Samples = ");
    UART_SendUInt(sampleCount);
    UART_SendString("\r\n");

    UART_SendString("DMA Cycles = ");
    UART_SendUInt(dmaCaptureCycles);
    UART_SendString("\r\n");
    UART_SendString("Sample Rate = ");
    UART_SendUInt(sampleRate);
    UART_SendString(" SPS\r\n");
}
    else if (strcmp(cmd, "dcdump") == 0)
{
    FFT_Process();

    for (uint32_t i = 0; i < 16; i++)
    {
        UART_SendUInt(
            (uint32_t)(FFT_GetCenteredSample(i) + 32768));

        UART_SendString("\r\n");
    }
}
    else if (strcmp(cmd, "windowdump") == 0)
{
    FFT_Process();

    for (uint32_t i = 0; i < 16; i++)
    {
        UART_SendUInt(
            (uint32_t)(FFT_GetWindowedSample(i) + 32768));

        UART_SendString("\r\n");
    }
}
    else if (cmd[0] != '\0')
    {
        UART_SendString("Unknown Command\r\n");
    }

    UART_SendString("> ");
}