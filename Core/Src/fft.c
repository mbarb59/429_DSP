#include <stdint.h>
#include <math.h>

#include "fft.h"
#include "capture.h"
#include "arm_math.h"

#define FFT_SIZE SAMPLE_COUNT

static uint16_t fftPeak = 0;
static uint32_t fftDC = 0;
static uint32_t fftRMS = 0;

float centeredBuffer[FFT_SIZE];
float windowedBuffer[FFT_SIZE];




void FFT_Process(void)
{
    uint32_t sum = 0;
    uint64_t sumSquares = 0;

    fftPeak = 0;
    fftDC = 0;
    fftRMS = 0;

    if (sampleCount == 0)
    {
        return;
    }

    /* Statistics */
    for (uint32_t i = 0; i < sampleCount; i++)
    {
        uint32_t sample = sampleBuffer[i];

        sum += sample;
        sumSquares += ((uint64_t)sample * sample);

        if (sample > fftPeak)
        {
            fftPeak = sample;
        }
    }

    fftDC = sum / sampleCount;

    fftRMS = (uint32_t)sqrt(
        (double)sumSquares / sampleCount);

    /* Remove DC */
    for (uint32_t i = 0; i < sampleCount; i++)
    {
        centeredBuffer[i] =
            (float)sampleBuffer[i] -
            (float)fftDC;
    }

    /* Hann Window */
    for (uint32_t i = 0; i < sampleCount; i++)
    {
        float window =
            0.5f *
            (1.0f -
             cosf(
                 (2.0f * PI * i) /
                 (sampleCount - 1)));

        windowedBuffer[i] =
            centeredBuffer[i] * window;
    }
}
uint16_t FFT_GetPeak(void)
{
    return fftPeak;
}

uint32_t FFT_GetDC(void)
{
    return fftDC;
}

uint32_t FFT_GetRMS(void)
{
    return fftRMS;
}

uint32_t FFT_GetBinWidth(void)
{
    if (sampleRate == 0)
    {
        return 0;
    }

    return sampleRate / FFT_SIZE;
}

float FFT_GetCenteredSample(uint32_t index)
{
    if (index >= sampleCount)
    {
        return 0.0f;
    }

    return centeredBuffer[index];
}

float FFT_GetWindowedSample(uint32_t index)
{
    if (index >= sampleCount)
    {
        return 0.0f;
    }

    return windowedBuffer[index];
}