#include "dsp.h"
#include "capture.h"

uint16_t DSP_Peak(void)
{
    uint16_t max = 0;

    for (uint32_t i = 0; i < sampleCount; i++)
    {
        if (sampleBuffer[i] > max)
        {
            max = sampleBuffer[i];
        }
    }

    return max;
}

uint16_t DSP_Min(void)
{
    uint16_t min = 4095;

    for (uint32_t i = 0; i < sampleCount; i++)
    {
        if (sampleBuffer[i] < min)
        {
            min = sampleBuffer[i];
        }
    }

    return min;
}

uint16_t DSP_Max(void)
{
    return DSP_Peak();
}

uint32_t DSP_Average(void)
{
    uint32_t sum = 0;

    if (sampleCount == 0)
    {
        return 0;
    }

    for (uint32_t i = 0; i < sampleCount; i++)
    {
        sum += sampleBuffer[i];
    }

    return sum / sampleCount;
}