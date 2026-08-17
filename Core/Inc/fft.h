#ifndef FFT_H
#define FFT_H

#include <stdbool.h>
#include <stdint.h>

#define FFT_SIZE       256U
#define FFT_BIN_COUNT  (FFT_SIZE / 2U)

/* Process sampleBuffer[] using the current sampleRate. */
bool FFT_Process(void);

/* Fill sampleBuffer[] with a synthetic ADC-style sine wave. */
bool FFT_GenerateSine(uint32_t frequency_hz,
                      uint16_t amplitude_counts,
                      uint16_t dc_offset_counts);

bool FFT_IsValid(void);

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
