/*
 * ADC1 support for the STM32F429I-DISC1 spectrum-analyzer project.
 *
 * Initial implementation:
 *   - ADC1 channel 0 on PA0
 *   - 12-bit conversions
 *   - Software-triggered polling through ADC_Read()
 *   - DMA request enable/disable hooks for the later capture module
 *
 * The real-time analyzer will eventually use a timer trigger and DMA. Keeping
 * ADC DMA control here preserves the interface used by the earlier G431
 * diagnostic console while allowing the F429 DMA stream code to live in
 * capture.c.
 */

#include "stm32f4xx_hal.h"
#include "adc.h"

#define ADC_INPUT_PORT          GPIOA
#define ADC_INPUT_PIN           GPIO_PIN_0
#define ADC_INPUT_CHANNEL       0U
#define ADC_POLL_TIMEOUT        1000000U

static uint8_t adc_initialized;

static void adc_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = ADC_INPUT_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(ADC_INPUT_PORT, &gpio);
}

void ADC_Init(void)
{
    if (adc_initialized != 0U)
    {
        return;
    }

    adc_gpio_init();
    __HAL_RCC_ADC1_CLK_ENABLE();

    /*
     * APB2 is 84 MHz in the current F429 clock configuration.
     * ADCPRE = 01 divides PCLK2 by 4, producing a 21 MHz ADC clock.
     */
    ADC->CCR &= ~ADC_CCR_ADCPRE;
    ADC->CCR |= ADC_CCR_ADCPRE_0;

    ADC1->CR1 = 0U;
    ADC1->CR2 = 0U;

    /* 12-bit resolution, single conversion, one channel. */
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = ADC_INPUT_CHANNEL;

    /*
     * Channel 0 sample time = 144 ADC cycles. This is conservative for the
     * first hardware test and can be shortened later for higher sample rates.
     */
    ADC1->SMPR2 &= ~ADC_SMPR2_SMP0;
    ADC1->SMPR2 |= ADC_SMPR2_SMP0_2 |
                   ADC_SMPR2_SMP0_1;

    /* EOC is asserted after each conversion. */
    ADC1->CR2 |= ADC_CR2_EOCS;

    /* Power on ADC1. */
    ADC1->CR2 |= ADC_CR2_ADON;

    adc_initialized = 1U;
}

uint16_t ADC_Read(void)
{
    uint32_t timeout = ADC_POLL_TIMEOUT;

    ADC_Init();

    /* Clear stale status and start one software-triggered conversion. */
    ADC1->SR = 0U;
    ADC1->CR2 |= ADC_CR2_SWSTART;

    while ((ADC1->SR & ADC_SR_EOC) == 0U)
    {
        if (timeout == 0U)
        {
            return 0U;
        }

        timeout--;
    }

    return (uint16_t)(ADC1->DR & 0x0FFFU);
}

void ADC_EnableDMA(void)
{
    ADC_Init();

    /*
     * DMA mode 01 and DDS keep ADC DMA requests active for a complete capture.
     * The DMA stream, channel, destination address, and transfer count are
     * configured by the F429 capture module.
     */
    ADC1->CR2 &= ~ADC_CR2_DMA;
    ADC1->CR2 |= ADC_CR2_DMA_0 | ADC_CR2_DDS;
}

void ADC_DisableDMA(void)
{
    ADC1->CR2 &= ~(ADC_CR2_DMA | ADC_CR2_DDS);
}
