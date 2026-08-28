/*
 * Timer-triggered sample capture for STM32F429I-DISC1.
 *
 * Capture path:
 *
 *   TIM2 update event / TRGO
 *       -> ADC1 regular conversion
 *       -> DMA2 Stream 0, Channel 0
 *       -> sampleBuffer[]
 */

#include "stm32f4xx_hal.h"
#include "adc.h"
#include "capture.h"

#include <stdbool.h>
#include <stdint.h>

uint16_t sampleBuffer[CAPTURE_SAMPLE_COUNT_MAX];
uint32_t sampleCount;
volatile int captureComplete;
uint32_t dmaCaptureCycles;
uint32_t sampleRate;

static uint32_t capture_start_cycles;
static uint32_t configured_rate_hz = CAPTURE_DEFAULT_RATE_HZ;
static uint32_t configured_sample_count = CAPTURE_SAMPLE_COUNT_DEFAULT;
static uint8_t capture_initialized;

static void dwt_cycle_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static uint32_t tim2_input_clock_hz(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();

    /* STM32F4 timer clock doubles when the APB1 prescaler is not 1. */
    if ((RCC->CFGR & RCC_CFGR_PPRE1) == 0U)
    {
        return pclk1;
    }

    return pclk1 * 2U;
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

static void tim2_stop(void)
{
    TIM2->CR1 &= ~TIM_CR1_CEN;
}

static bool capture_is_idle(void)
{
    if ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U)
    {
        return false;
    }

    if ((TIM2->CR1 & TIM_CR1_CEN) != 0U)
    {
        return false;
    }

    return true;
}

static bool tim2_configure_sample_rate(uint32_t requested_rate_hz)
{
    uint32_t timer_clock_hz;
    uint32_t period;

    if (requested_rate_hz == 0U)
    {
        return false;
    }

    __HAL_RCC_TIM2_CLK_ENABLE();

    timer_clock_hz = tim2_input_clock_hz();
    period = (timer_clock_hz + (requested_rate_hz / 2U)) /
             requested_rate_hz;

    if (period == 0U)
    {
        return false;
    }

    tim2_stop();

    TIM2->CR1 = 0U;
    TIM2->CR2 = 0U;
    TIM2->SMCR = 0U;
    TIM2->DIER = 0U;
    TIM2->PSC = 0U;
    TIM2->ARR = period - 1U;
    TIM2->CNT = 0U;

    /* Load PSC and ARR before enabling TRGO. */
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;

    /* MMS = 010: TIM2 update event is used as TRGO. */
    TIM2->CR2 = TIM_CR2_MMS_1;

    configured_rate_hz = timer_clock_hz / period;
    sampleRate = configured_rate_hz;

    return true;
}

bool Capture_IsValidSampleCount(uint32_t count)
{
    return (count == 256U) ||
           (count == 512U) ||
           (count == 1024U) ||
           (count == 2048U);
}

bool Capture_SetSampleCount(uint32_t count)
{
    if (!Capture_IsValidSampleCount(count))
    {
        return false;
    }

    if (!capture_is_idle())
    {
        return false;
    }

    configured_sample_count = count;
    sampleCount = 0U;
    captureComplete = 1;

    return true;
}

uint32_t Capture_GetSampleCount(void)
{
    return configured_sample_count;
}

bool Capture_SetSampleRate(uint32_t rate_hz)
{
    if (!capture_is_idle())
    {
        return false;
    }

    return tim2_configure_sample_rate(rate_hz);
}

uint32_t Capture_GetSampleRate(void)
{
    return configured_rate_hz;
}

void Capture_Samples(void)
{
    uint32_t requested_count = configured_sample_count;

    sampleCount = 0U;
    captureComplete = 0;

    for (uint32_t i = 0U; i < requested_count; i++)
    {
        sampleBuffer[i] = ADC_Read();
    }

    sampleCount = requested_count;
    captureComplete = 1;
}

void DMA_Init(void)
{
    if (capture_initialized != 0U)
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
     * CHSEL = 000: channel 0
     * DIR   = 00: peripheral to memory
     * PSIZE = 01: 16-bit peripheral data
     * MSIZE = 01: 16-bit memory data
     * MINC  = 1 : increment sampleBuffer address
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

    configured_sample_count = CAPTURE_SAMPLE_COUNT_DEFAULT;

    if (!tim2_configure_sample_rate(CAPTURE_DEFAULT_RATE_HZ))
    {
        captureComplete = -1;
        return;
    }

    sampleCount = 0U;
    captureComplete = 1;
    capture_initialized = 1U;
}

void DMA_CaptureSamples(void)
{
    uint32_t requested_count;

    DMA_Init();

    if (capture_initialized == 0U)
    {
        captureComplete = -1;
        return;
    }

    requested_count = configured_sample_count;

    if (!Capture_IsValidSampleCount(requested_count))
    {
        captureComplete = -1;
        return;
    }

    tim2_stop();
    ADC_DisableDMA();
    ADC_UseSoftwareTrigger();
    dma2_stream0_disable();
    dma2_stream0_clear_flags();

    captureComplete = 0;
    sampleCount = 0U;
    dmaCaptureCycles = 0U;
    sampleRate = configured_rate_hz;

    /* Clear stale ADC status, including a possible overrun. */
    ADC1->SR = 0U;

    DMA2_Stream0->PAR = (uint32_t)&ADC1->DR;
    DMA2_Stream0->M0AR = (uint32_t)sampleBuffer;
    DMA2_Stream0->NDTR = requested_count;

    ADC_UseTimer2Trigger();
    ADC_EnableDMA();

    capture_start_cycles = DWT->CYCCNT;

    DMA2_Stream0->CR |= DMA_SxCR_EN;

    TIM2->CNT = 0U;
    TIM2->SR = 0U;
    TIM2->CR1 |= TIM_CR1_CEN;
}

void DMA2_Stream0_IRQHandler(void)
{
    uint32_t status = DMA2->LISR;

    if ((status & DMA_LISR_TCIF0) != 0U)
    {
        uint32_t stop_cycles = DWT->CYCCNT;

        tim2_stop();
        dma2_stream0_clear_flags();
        dma2_stream0_disable();
        ADC_DisableDMA();
        ADC_UseSoftwareTrigger();

        dmaCaptureCycles = stop_cycles - capture_start_cycles;
        sampleCount = configured_sample_count;
        sampleRate = configured_rate_hz;
        captureComplete = 1;

        return;
    }

    if ((status & (DMA_LISR_TEIF0 |
                   DMA_LISR_DMEIF0 |
                   DMA_LISR_FEIF0)) != 0U)
    {
        tim2_stop();
        dma2_stream0_clear_flags();
        dma2_stream0_disable();
        ADC_DisableDMA();
        ADC_UseSoftwareTrigger();

        sampleCount = 0U;
        dmaCaptureCycles = 0U;
        captureComplete = -1;
    }
}
