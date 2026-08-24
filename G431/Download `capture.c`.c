/*
 * Sample capture support for STM32F429I-DISC1.
 *
 * ADC1 regular conversions are transferred to sampleBuffer by:
 *
 *   ADC1 -> DMA2, Stream 0, Channel 0
 *
 * This first version supports:
 *   - CPU-polled sample capture
 *   - One-shot DMA sample capture
 *   - DWT cycle timing
 *   - Calculated average sample rate for the completed DMA capture
 *
 * The ADC currently runs from software-triggered continuous conversion.
 * A later real-time analyzer revision should use a timer-triggered ADC so the
 * sample interval is exact and independent of ADC conversion timing.
 */

#include "stm32f4xx_hal.h"
#include "adc.h"
#include "capture.h"

#include <stdint.h>

uint16_t sampleBuffer[SAMPLE_COUNT];
uint32_t sampleCount;
volatile int captureComplete;
uint32_t dmaCaptureCycles;
uint32_t sampleRate;

static uint32_t capture_start_cycles;
static uint8_t dma_initialized;

static void dwt_cycle_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dma2_stream0_clear_flags(void)
{
    DMA2->LIFCR = DMA_LIFCR_CFEIF0 |
                  DMA_LIFCR_CDMEIF0 |
                  DMA_LIFCR_CTEIF0 |
                  DMA_LIFCR_CHTIF0 |
                  DMA_LIFCR_CTCIF0;
}

static void dma2_stream0_disable(void)
{
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;

    while ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U)
    {
    }
}

void Capture_Samples(void)
{
    sampleCount = 0U;
    captureComplete = 0;

    for (uint32_t i = 0U; i < SAMPLE_COUNT; i++)
    {
        sampleBuffer[i] = ADC_Read();
    }

    sampleCount = SAMPLE_COUNT;
    captureComplete = 1;
}

void DMA_Init(void)
{
    if (dma_initialized != 0U)
    {
        return;
    }

    ADC_Init();
    __HAL_RCC_DMA2_CLK_ENABLE();

    dma2_stream0_disable();
    dma2_stream0_clear_flags();

    DMA2_Stream0->CR = 0U;
    DMA2_Stream0->NDTR = 0U;
    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)sampleBuffer;
    DMA2_Stream0->FCR = 0U;

    /*
     * Channel 0 is selected because CHSEL = 000 after reset.
     * Peripheral-to-memory is selected because DIR = 00.
     * ADC data and sampleBuffer entries are 16 bits wide.
     */
    DMA2_Stream0->CR = DMA_SxCR_PL_1 |
                       DMA_SxCR_MSIZE_0 |
                       DMA_SxCR_PSIZE_0 |
                       DMA_SxCR_MINC |
                       DMA_SxCR_TCIE |
                       DMA_SxCR_TEIE |
                       DMA_SxCR_DMEIE;

    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

    dwt_cycle_counter_init();
    dma_initialized = 1U;
}

void DMA_CaptureSamples(void)
{
    DMA_Init();

    captureComplete = 0;
    sampleCount = 0U;
    dmaCaptureCycles = 0U;
    sampleRate = 0U;

    /* Stop any previous ADC conversion and DMA transfer. */
    ADC1->CR2 &= ~(ADC_CR2_CONT | ADC_CR2_SWSTART);
    ADC_DisableDMA();
    dma2_stream0_disable();
    dma2_stream0_clear_flags();

    /* Clear stale ADC status, especially an overrun from an earlier capture. */
    ADC1->SR = 0U;

    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)sampleBuffer;
    DMA2_Stream0->NDTR = SAMPLE_COUNT;

    ADC_EnableDMA();

    /* Continuous software-triggered ADC conversions for this one-shot buffer. */
    ADC1->CR2 |= ADC_CR2_CONT;

    capture_start_cycles = DWT->CYCCNT;

    DMA2_Stream0->CR |= DMA_SxCR_EN;
    ADC1->CR2 |= ADC_CR2_SWSTART;
}

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t status = DMA2->LISR;

    if ((status & DMA_LISR_TCIF0) != 0U)
    {
        uint32_t stop_cycles = DWT->CYCCNT;

        dma2_stream0_clear_flags();
        dma2_stream0_disable();

        ADC1->CR2 &= ~ADC_CR2_CONT;
        ADC_DisableDMA();

        dmaCaptureCycles = stop_cycles - capture_start_cycles;
        sampleCount = SAMPLE_COUNT;

        if (dmaCaptureCycles != 0U)
        {
            sampleRate = (uint32_t)(
                ((uint64_t)SystemCoreClock * (uint64_t)SAMPLE_COUNT) /
                (uint64_t)dmaCaptureCycles);
        }

        captureComplete = 1;
    }

    if ((status & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)) != 0U)
    {
        dma2_stream0_clear_flags();
        dma2_stream0_disable();

        ADC1->CR2 &= ~ADC_CR2_CONT;
        ADC_DisableDMA();

        sampleCount = 0U;
        dmaCaptureCycles = 0U;
        sampleRate = 0U;
        captureComplete = -1;
    }
}
