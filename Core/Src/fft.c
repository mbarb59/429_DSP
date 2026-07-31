#include <stdint.h>
#include <math.h>

#include "fft.h"
#include "capture.h"

static uint16_t fftPeak = 0;
static uint32_t fftDC = 0;
static uint32_t fftRMS = 0;

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