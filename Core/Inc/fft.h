#ifndef FFT_H
#define FFT_H

#include <stdint.h>

void FFT_Process(void);

uint16_t FFT_GetPeak(void);
uint32_t FFT_GetDC(void);
uint32_t FFT_GetRMS(void);

#endif