#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#define SAMPLE_COUNT             256U
#define CAPTURE_DEFAULT_RATE_HZ  48000U

extern uint16_t sampleBuffer[SAMPLE_COUNT];
extern uint32_t sampleCount;
extern volatile int captureComplete;
extern uint32_t dmaCaptureCycles;
extern uint32_t sampleRate;

void Capture_Samples(void);
void DMA_Init(void);
void DMA_CaptureSamples(void);

bool Capture_SetSampleRate(uint32_t rate_hz);
uint32_t Capture_GetSampleRate(void);

#endif /* CAPTURE_H */
