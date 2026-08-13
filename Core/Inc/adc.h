#ifndef ADC_H
#define ADC_H

#include <stdint.h>

void ADC_Init(void);

uint16_t ADC_Read(void);

void ADC_EnableDMA(void);
void ADC_DisableDMA(void);

void ADC_UseSoftwareTrigger(void);
void ADC_UseTimer2Trigger(void);

#endif /* ADC_H */