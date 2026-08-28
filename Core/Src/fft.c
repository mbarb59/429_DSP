/*
 * Runtime-selectable radix-2 FFT for the STM32F429 spectrum analyzer.
 *
 * Processing chain:
 *   unsigned ADC samples
 *       -> mean/DC removal
 *       -> Hann window
 *       -> complex radix-2 FFT
 *       -> positive-frequency magnitudes
 *       -> peak-bin and peak-frequency detection
 *
 * Supported active FFT sizes:
 *   256, 512, 1024, and 2048 samples
 */

#include "fft.h"
#include "capture.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define FFT_PI                 3.14159265358979323846f
#define FFT_TWO_PI             (2.0f * FFT_PI)
#define FFT_ADC_MAX            4095U
#define FFT_DEFAULT_RATE_HZ    48000U

#if CAPTURE_SAMPLE_COUNT_MAX < FFT_SIZE_MAX
#error "CAPTURE_SAMPLE_COUNT_MAX must be at least FFT_SIZE_MAX."
#endif

static float fft_real[FFT_SIZE_MAX];
static float fft_imag[FFT_SIZE_MAX];
static float magnitudes[FFT_BIN_COUNT_MAX];
static int32_t centered_samples[FFT_SIZE_MAX];
static int32_t windowed_samples[FFT_SIZE_MAX];

static uint32_t active_fft_size = FFT_SIZE_DEFAULT;
static uint32_t active_bin_count = FFT_SIZE_DEFAULT / 2U;
static uint32_t fft_sample_rate_hz;
static uint32_t fft_dc_counts;
static uint32_t fft_rms_counts;
static uint32_t peak_bin;
static uint32_t peak_frequency_hz;
static bool fft_valid;

static uint32_t fft_log2_size(uint32_t fft_size)
{
    uint32_t bit_count = 0U;

    while (fft_size > 1U)
    {
        fft_size >>= 1U;
        bit_count++;
    }

    return bit_count;
}

static uint32_t reverse_bits(uint32_t value, uint32_t bit_count)
{
    uint32_t reversed = 0U;

    for (uint32_t i = 0U; i < bit_count; i++)
    {
        reversed = (reversed << 1U) | (value & 1U);
        value >>= 1U;
    }

    return reversed;
}

static void bit_reverse_permute(void)
{
    uint32_t bit_count = fft_log2_size(active_fft_size);

    for (uint32_t i = 0U; i < active_fft_size; i++)
    {
        uint32_t j = reverse_bits(i, bit_count);

        if (j > i)
        {
            float temporary;

            temporary = fft_real[i];
            fft_real[i] = fft_real[j];
            fft_real[j] = temporary;

            temporary = fft_imag[i];
            fft_imag[i] = fft_imag[j];
            fft_imag[j] = temporary;
        }
    }
}

static void radix2_fft(void)
{
    bit_reverse_permute();

    for (uint32_t length = 2U;
         length <= active_fft_size;
         length <<= 1U)
    {
        uint32_t half_length = length >> 1U;
        float angle_step = -FFT_TWO_PI / (float)length;

        for (uint32_t start = 0U;
             start < active_fft_size;
             start += length)
        {
            for (uint32_t offset = 0U;
                 offset < half_length;
                 offset++)
            {
                float angle = angle_step * (float)offset;
                float twiddle_real = cosf(angle);
                float twiddle_imag = sinf(angle);
                uint32_t even_index = start + offset;
                uint32_t odd_index = even_index + half_length;

                float odd_real =
                    (twiddle_real * fft_real[odd_index]) -
                    (twiddle_imag * fft_imag[odd_index]);

                float odd_imag =
                    (twiddle_real * fft_imag[odd_index]) +
                    (twiddle_imag * fft_real[odd_index]);

                float even_real = fft_real[even_index];
                float even_imag = fft_imag[even_index];

                fft_real[even_index] = even_real + odd_real;
                fft_imag[even_index] = even_imag + odd_imag;
                fft_real[odd_index] = even_real - odd_real;
                fft_imag[odd_index] = even_imag - odd_imag;
            }
        }
    }
}

static void prepare_samples(void)
{
    uint64_t sum = 0U;
    double square_sum = 0.0;

    for (uint32_t i = 0U; i < active_fft_size; i++)
    {
        sum += sampleBuffer[i];
    }

    fft_dc_counts = (uint32_t)(sum / active_fft_size);

    for (uint32_t i = 0U; i < active_fft_size; i++)
    {
        float centered =
            (float)((int32_t)sampleBuffer[i] -
                    (int32_t)fft_dc_counts);

        float window = 0.5f -
            (0.5f * cosf((FFT_TWO_PI * (float)i) /
                         (float)(active_fft_size - 1U)));

        float windowed = centered * window;

        centered_samples[i] = (int32_t)centered;
        windowed_samples[i] = (int32_t)windowed;
        fft_real[i] = windowed;
        fft_imag[i] = 0.0f;

        square_sum += (double)centered * (double)centered;
    }

    fft_rms_counts =
        (uint32_t)sqrt(square_sum / (double)active_fft_size);
}

static void calculate_magnitudes(void)
{
    float largest = 0.0f;

    peak_bin = 0U;

    for (uint32_t bin = 0U; bin < active_bin_count; bin++)
    {
        float real = fft_real[bin];
        float imag = fft_imag[bin];
        float magnitude = sqrtf((real * real) + (imag * imag));

        /* Approximate single-sided amplitude correction for the Hann window. */
        magnitude *= (4.0f / (float)active_fft_size);
        magnitudes[bin] = magnitude;

        /* Ignore DC when locating the spectral peak. */
        if ((bin > 0U) && (magnitude > largest))
        {
            largest = magnitude;
            peak_bin = bin;
        }
    }

    peak_frequency_hz =
        (uint32_t)(((uint64_t)peak_bin * fft_sample_rate_hz +
                    (active_fft_size / 2U)) /
                   active_fft_size);
}

bool FFT_IsValidSize(uint32_t fft_size)
{
    return (fft_size == 256U) ||
           (fft_size == 512U) ||
           (fft_size == 1024U) ||
           (fft_size == 2048U);
}

bool FFT_SetSize(uint32_t fft_size)
{
    if (!FFT_IsValidSize(fft_size))
    {
        return false;
    }

    active_fft_size = fft_size;
    active_bin_count = fft_size / 2U;
    FFT_Invalidate();

    return true;
}

uint32_t FFT_GetSize(void)
{
    return active_fft_size;
}

uint32_t FFT_GetBinCount(void)
{
    return active_bin_count;
}

bool FFT_Process(void)
{
    if (sampleCount != active_fft_size)
    {
        fft_valid = false;
        return false;
    }

    fft_sample_rate_hz = sampleRate;

    if (fft_sample_rate_hz == 0U)
    {
        fft_sample_rate_hz = FFT_DEFAULT_RATE_HZ;
    }

    prepare_samples();
    radix2_fft();
    calculate_magnitudes();

    fft_valid = true;
    return true;
}

bool FFT_GenerateSine(uint32_t frequency_hz,
                      uint16_t amplitude_counts,
                      uint16_t dc_offset_counts)
{
    uint32_t rate_hz = Capture_GetSampleRate();

    if (rate_hz == 0U)
    {
        rate_hz = FFT_DEFAULT_RATE_HZ;
    }

    if ((Capture_GetSampleCount() != active_fft_size) ||
        (frequency_hz >= (rate_hz / 2U)) ||
        (dc_offset_counts > FFT_ADC_MAX) ||
        (amplitude_counts > FFT_ADC_MAX))
    {
        return false;
    }

    for (uint32_t i = 0U; i < active_fft_size; i++)
    {
        float phase = FFT_TWO_PI * (float)frequency_hz *
                      (float)i / (float)rate_hz;

        int32_t sample = (int32_t)dc_offset_counts +
                         (int32_t)((float)amplitude_counts * sinf(phase));

        if (sample < 0)
        {
            sample = 0;
        }
        else if (sample > (int32_t)FFT_ADC_MAX)
        {
            sample = (int32_t)FFT_ADC_MAX;
        }

        sampleBuffer[i] = (uint16_t)sample;
    }

    sampleCount = active_fft_size;
    sampleRate = rate_hz;
    captureComplete = 1;
    fft_valid = false;

    return true;
}

bool FFT_IsValid(void)
{
    return fft_valid;
}

void FFT_Invalidate(void)
{
    fft_valid = false;
    peak_bin = 0U;
    peak_frequency_hz = 0U;
}

uint32_t FFT_GetPeakBin(void)
{
    return peak_bin;
}

uint32_t FFT_GetPeakFrequency(void)
{
    return peak_frequency_hz;
}

uint32_t FFT_GetBinWidthMilliHz(void)
{
    if ((fft_sample_rate_hz == 0U) || (active_fft_size == 0U))
    {
        return 0U;
    }

    return (uint32_t)(((uint64_t)fft_sample_rate_hz * 1000U) /
                      active_fft_size);
}

uint32_t FFT_GetSampleRate(void)
{
    return fft_sample_rate_hz;
}

uint32_t FFT_GetDC(void)
{
    return fft_dc_counts;
}

uint32_t FFT_GetRMS(void)
{
    return fft_rms_counts;
}

uint32_t FFT_GetMagnitude(uint32_t bin)
{
    if (bin >= active_bin_count)
    {
        return 0U;
    }

    return (uint32_t)(magnitudes[bin] + 0.5f);
}

int32_t FFT_GetCenteredSample(uint32_t index)
{
    if (index >= active_fft_size)
    {
        return 0;
    }

    return centered_samples[index];
}

int32_t FFT_GetWindowedSample(uint32_t index)
{
    if (index >= active_fft_size)
    {
        return 0;
    }

    return windowed_samples[index];
}

const float *FFT_GetMagnitudes(void)
{
    return magnitudes;
}
