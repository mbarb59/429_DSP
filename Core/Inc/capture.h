#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>

#define SAMPLE_COUNT 256

extern uint16_t sampleBuffer[SAMPLE_COUNT];
extern uint32_t sampleCount;
extern volatile int captureComplete;
extern uint32_t dmaCaptureCycles;
extern uint32_t sampleRate;

void Capture_Samples(void);

void DMA_Init(void);
void DMA_CaptureSamples(void);

#endif