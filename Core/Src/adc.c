#include "stm32g431xx.h"
#include "adc.h"

void ADC_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;

    /* ADC kernel clock = SYSCLK */
    RCC->CCIPR &= ~(3UL << 28);
    RCC->CCIPR |=  (1UL << 28);

    /* ADC common clock = PCLK / 4 */
    ADC12_COMMON->CCR &= ~(3UL << 16);
    ADC12_COMMON->CCR |=  (1UL << 16);

    /* PA0 = ADC1_IN1 */
    GPIOA->MODER |= (3UL << 0);

    /* ADC setup */
    ADC1->CR &= ~ADC_CR_DEEPPWD;
    ADC1->CR |= ADC_CR_ADVREGEN;

    for (volatile uint32_t i = 0; i < 50000; i++)
    {
        __asm__("nop");
    }

    /* Calibration */
    ADC1->CR |= ADC_CR_ADCAL;

    uint32_t timeout = 1000000;

    while (ADC1->CR & ADC_CR_ADCAL)
    {
        if (--timeout == 0)
        {
            return;
        }
    }

    /* Delay after calibration */
    for (volatile uint32_t i = 0; i < 10000; i++)
    {
        __asm__("nop");
    }

    /* Enable ADC */
    ADC1->ISR |= ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;

    timeout = 1000000;

    while (!(ADC1->ISR & ADC_ISR_ADRDY))
    {
        if (--timeout == 0)
        {
            return;
        }
    }

    /* Single conversion on channel 1 (PA0) */
    ADC1->SQR1 = (1UL << 6);

    /* Long sample time */
    ADC1->SMPR1 |= (7UL << 3);
}

uint16_t ADC_Read(void)
{
    uint32_t timeout = 1000000;

    ADC1->ISR |= ADC_ISR_EOC;

    ADC1->CR |= ADC_CR_ADSTART;

    while (!(ADC1->ISR & ADC_ISR_EOC))
    {
        if (--timeout == 0)
        {
            return 65535;
        }
    }

    return (uint16_t)ADC1->DR;
}