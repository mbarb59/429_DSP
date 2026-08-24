#ifndef ADC_H
#define ADC_H

#include <stdint.h>

/*
 * Default analog input for the first STM32F429 analyzer build:
 *
 *   ADC1 channel 0
 *   PA0
 *
 * If the analyzer input is connected to another pin, update the channel and
 * GPIO definitions together in adc.c before connecting the signal source.
 */

void ADC_Init(void);
uint16_t ADC_Read(void);
void ADC_EnableDMA(void);
void ADC_DisableDMA(void);

#endif /* ADC_H */
