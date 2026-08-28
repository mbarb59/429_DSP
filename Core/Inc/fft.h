#ifndef FFT_H
#define FFT_H

#include <stdbool.h>
#include <stdint.h>

#define FFT_SIZE_DEFAULT   256U
#define FFT_SIZE_MAX       2048U
#define FFT_BIN_COUNT_MAX  (FFT_SIZE_MAX / 2U)

/*
 * Temporary compatibility aliases for main.c and ui.c.
 * Keep the active FFT size at 256 until those modules are runtime-sized.
 */
#define FFT_SIZE       FFT_SIZE_DEFAULT
#define FFT_BIN_COUNT  (FFT_SIZE_DEFAULT / 2U)

bool FFT_Process(void);

bool FFT_GenerateSine(uint32_t frequency_hz,
                      uint16_t amplitude_counts,
                      uint16_t dc_offset_counts);

bool FFT_SetSize(uint32_t fft_size);
bool FFT_IsValidSize(uint32_t fft_size);
uint32_t FFT_GetSize(void);
uint32_t FFT_GetBinCount(void);

bool FFT_IsValid(void);
void FFT_Invalidate(void);

uint32_t FFT_GetPeakBin(void);
uint32_t FFT_GetPeakFrequency(void);
uint32_t FFT_GetBinWidthMilliHz(void);
uint32_t FFT_GetSampleRate(void);

uint32_t FFT_GetDC(void);
uint32_t FFT_GetRMS(void);
uint32_t FFT_GetMagnitude(uint32_t bin);

int32_t FFT_GetCenteredSample(uint32_t index);
int32_t FFT_GetWindowedSample(uint32_t index);

const float *FFT_GetMagnitudes(void);

#endif /* FFT_H */
