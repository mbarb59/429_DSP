#ifndef ADC_H
#define ADC_H

#include <stdbool.h>
#include <stdint.h>

/* ADC_PA1_INPUT_SELECTION_PATCH */
typedef enum
{
    ADC_INPUT_MICROPHONE_PA0 = 0,
    ADC_INPUT_ACCEL_X_PA1,
    ADC_INPUT_ACCEL_X_PC1,
    ADC_INPUT_ACCEL_Y_PC5,
    ADC_INPUT_ACCEL_Z_PC4
} ADC_Input;

void ADC_Init(void);
bool ADC_SelectInput(ADC_Input input);
ADC_Input ADC_GetSelectedInput(void);

uint16_t ADC_Read(void);

void ADC_EnableDMA(void);
void ADC_DisableDMA(void);

void ADC_UseSoftwareTrigger(void);
void ADC_UseTimer2Trigger(void);

/* ACCEL_XYZ_CAPTURE_PHASE1 */
bool ADC_ConfigureXYZScan(void);
void ADC_RestoreSingleInput(void);

#endif /* ADC_H */