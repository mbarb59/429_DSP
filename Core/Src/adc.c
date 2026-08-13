#include "stm32f4xx_hal.h"
#include "adc.h"

#define ADC_INPUT_PORT          GPIOA
#define ADC_INPUT_PIN           GPIO_PIN_0
#define ADC_INPUT_CHANNEL       0U
#define ADC_POLL_TIMEOUT        1000000U

/* ADC1 CR2 DMA[1:0] = 01 selects ADC DMA mode 1. */
#define ADC_CR2_DMA_MODE_1      (1U << 14)

/* ADC1 CR2 EXTSEL[3:0] = 0110 selects TIM2 TRGO. */
#define ADC_EXTSEL_TIM2_TRGO    (6U << 24)

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

    /* PCLK2 is 84 MHz. Divide by 4 for a 21 MHz ADC clock. */
    ADC->CCR &= ~ADC_CCR_ADCPRE;
    ADC->CCR |= ADC_CCR_ADCPRE_0;

    ADC1->CR1 = 0U;
    ADC1->CR2 = 0U;

    /* One regular conversion: ADC1 channel 0 on PA0. */
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = ADC_INPUT_CHANNEL;

    /* Channel 0 sample time: 144 ADC clock cycles. */
    ADC1->SMPR2 &= ~ADC_SMPR2_SMP0;
    ADC1->SMPR2 |= ADC_SMPR2_SMP0_2 |
                   ADC_SMPR2_SMP0_1;

    /* EOC after each regular conversion. */
    ADC1->CR2 |= ADC_CR2_EOCS;

    /* Power on ADC1. */
    ADC1->CR2 |= ADC_CR2_ADON;

    ADC_UseSoftwareTrigger();
    adc_initialized = 1U;
}

void ADC_UseSoftwareTrigger(void)
{
    ADC1->CR2 &= ~(ADC_CR2_EXTSEL |
                   ADC_CR2_EXTEN |
                   ADC_CR2_CONT);
}

void ADC_UseTimer2Trigger(void)
{
    ADC1->CR2 &= ~(ADC_CR2_EXTSEL |
                   ADC_CR2_EXTEN |
                   ADC_CR2_CONT);

    ADC1->CR2 |= ADC_EXTSEL_TIM2_TRGO |
                 ADC_CR2_EXTEN_0;
}

uint16_t ADC_Read(void)
{
    uint32_t timeout = ADC_POLL_TIMEOUT;

    ADC_Init();
    ADC_DisableDMA();
    ADC_UseSoftwareTrigger();

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

    ADC1->CR2 |= ADC_CR2_DMA;
    ADC1->CR2 |= ADC_CR2_DDS;
}

void ADC_DisableDMA(void)
{
    ADC1->CR2 &= ~(ADC_CR2_DMA | ADC_CR2_DDS);
}