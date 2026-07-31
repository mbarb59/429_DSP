#include "stm32g431xx.h"
#include "capture.h"
#include "adc.h"

uint16_t sampleBuffer[SAMPLE_COUNT];
uint32_t sampleCount = 0;
volatile int captureComplete = 0;

void Capture_Samples(void)
{
    for (uint32_t i = 0; i < SAMPLE_COUNT; i++)
    {
        sampleBuffer[i] = ADC_Read();
    }

    sampleCount = SAMPLE_COUNT;
}

void DMA_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMAMUX1EN;

    DMA1_Channel1->CCR = 0;
    /* Route ADC1 requests to DMA1 Channel1 */    
    DMAMUX1_Channel0->CCR = 5;
    captureComplete = DMAMUX1_Channel0->CCR;
}

void DMA_CaptureSamples(void)
{
    captureComplete = 0;
    
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;

    DMA1_Channel1->CPAR =
        (uint32_t)&ADC1->DR;

    DMA1_Channel1->CMAR =
        (uint32_t)sampleBuffer;

    DMA1_Channel1->CNDTR =
        SAMPLE_COUNT;

    DMA1_Channel1->CCR =
        DMA_CCR_MINC    |
        DMA_CCR_PSIZE_0 |
        DMA_CCR_MSIZE_0 |
        DMA_CCR_TCIE;
    
    /* Clear any old transfer-complete flag */
    DMA1->IFCR = DMA_IFCR_CTCIF1;

    DMA1_Channel1->CCR |= DMA_CCR_EN;    // Enable

      /* Start ADC conversion */
    ADC1->CR |= ADC_CR_ADSTART;
}

void DMA1_Channel1_IRQHandler(void)
{
    DMA1->IFCR = DMA_IFCR_CTCIF1;

    sampleCount = SAMPLE_COUNT;

    captureComplete = 1;
}