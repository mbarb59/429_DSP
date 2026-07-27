#include "stm32g431xx.h"
#include "adc.h"

void ADC_Init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN;

    /* ADC kernel clock = SYSCLK */
    RCC->CCIPR &= ~(3UL << 28);
    RCC->CCIPR |=  (1UL << 28);

    /* PA0 analog */
    GPIOA->MODER |= (3UL << 0);

    /* Exit deep power down */
    ADC1->CR &= ~ADC_CR_DEEPPWD;

    /* Enable regulator */
    ADC1->CR |= ADC_CR_ADVREGEN;

    for (volatile uint32_t i = 0; i < 50000; i++)
    {
        __asm__("nop");
    }

    /* Start calibration */
    ADC1->CR |= ADC_CR_ADCAL;

    uint32_t timeout = 1000000;

    while (ADC1->CR & ADC_CR_ADCAL)
    {
        if (--timeout == 0)
        {
            return;
        }
    }
}

uint16_t ADC_Read(void)
{
    return 1234;
}