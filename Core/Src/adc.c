#include "stm32f4xx_hal.h"
#include "adc.h"

/* ADC_PA1_INPUT_SELECTION_PATCH */
#define ADC_INPUT_PORT              GPIOA
#define ADC_MICROPHONE_PIN          GPIO_PIN_0
#define ADC_ACCEL_X_PIN             GPIO_PIN_1
#define ADC_ACCEL_X_PC1_PIN             GPIO_PIN_1
#define ADC_ACCEL_Y_PC5_PIN         GPIO_PIN_5
#define ADC_ACCEL_Z_PC4_PIN         GPIO_PIN_4
#define ADC_MICROPHONE_CHANNEL      0U
#define ADC_ACCEL_X_CHANNEL         1U
#define ADC_ACCEL_X_PC1_CHANNEL         11U
#define ADC_ACCEL_Y_PC5_CHANNEL     15U
#define ADC_ACCEL_Z_PC4_CHANNEL     14U
#define ADXL335_ST_PORT              GPIOF
#define ADXL335_ST_PIN               GPIO_PIN_6
#define ADC_SQR3_FIRST_CHANNEL_MASK 0x1FU
#define ADC_POLL_TIMEOUT            1000000U

/* ADC1 CR2 DMA[1:0] = 01 selects ADC DMA mode 1. */
#define ADC_CR2_DMA_MODE_1      (1U << 14)

/* ADC1 CR2 EXTSEL[3:0] = 0110 selects TIM2 TRGO. */
#define ADC_EXTSEL_TIM2_TRGO    (6U << 24)

static uint8_t adc_initialized;
static ADC_Input selected_input = ADC_INPUT_MICROPHONE_PA0;
static uint8_t adc_xyz_scan_active;

static void adc_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    gpio.Pin = ADC_MICROPHONE_PIN | ADC_ACCEL_X_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(ADC_INPUT_PORT, &gpio);

    /* PC1/X and PC5/Y are high-impedance analog inputs. */
    gpio.Pin = ADC_ACCEL_X_PC1_PIN |
               ADC_ACCEL_Y_PC5_PIN |
               ADC_ACCEL_Z_PC4_PIN;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* Keep ADXL335 self-test inactive. Set latch LOW before output mode. */
    HAL_GPIO_WritePin(ADXL335_ST_PORT, ADXL335_ST_PIN, GPIO_PIN_RESET);
    gpio.Pin = ADXL335_ST_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ADXL335_ST_PORT, &gpio);
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

    /* One regular conversion. PA0/microphone is the safe default. */
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = ADC_MICROPHONE_CHANNEL;
    selected_input = ADC_INPUT_MICROPHONE_PA0;

    /* Channels 0 and 1 use a conservative 144-cycle sample time. */
    ADC1->SMPR2 &= ~(ADC_SMPR2_SMP0 | ADC_SMPR2_SMP1);
    ADC1->SMPR2 |= ADC_SMPR2_SMP0_2 |
                   ADC_SMPR2_SMP0_1 |
                   ADC_SMPR2_SMP1_2 |
                   ADC_SMPR2_SMP1_1;

    /* Channels 11, 14, and 15 use the conservative 144-cycle sample time. */
    ADC1->SMPR1 &= ~(ADC_SMPR1_SMP11 |
                     ADC_SMPR1_SMP14 |
                     ADC_SMPR1_SMP15);
    ADC1->SMPR1 |= ADC_SMPR1_SMP11_2 |
                   ADC_SMPR1_SMP11_1 |
                   ADC_SMPR1_SMP14_2 |
                   ADC_SMPR1_SMP14_1 |
                   ADC_SMPR1_SMP15_2 |
                   ADC_SMPR1_SMP15_1;

    /* EOC after each regular conversion. */
    ADC1->CR2 |= ADC_CR2_EOCS;

    /* Power on ADC1. */
    ADC1->CR2 |= ADC_CR2_ADON;

    ADC_UseSoftwareTrigger();
    adc_initialized = 1U;
}

bool ADC_SelectInput(ADC_Input input)
{
    uint32_t channel;

    ADC_Init();

    /* Never change the regular channel while timer/DMA capture is active. */
    if (((ADC1->CR2 & ADC_CR2_DMA) != 0U) ||
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U) ||
        ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U))
    {
        return false;
    }

    switch (input)
    {
        case ADC_INPUT_MICROPHONE_PA0:
            channel = ADC_MICROPHONE_CHANNEL;
            break;

        case ADC_INPUT_ACCEL_X_PA1:
            channel = ADC_ACCEL_X_CHANNEL;
            break;

        case ADC_INPUT_ACCEL_X_PC1:
            channel = ADC_ACCEL_X_PC1_CHANNEL;
            break;

        case ADC_INPUT_ACCEL_Y_PC5:
            channel = ADC_ACCEL_Y_PC5_CHANNEL;
            break;

        case ADC_INPUT_ACCEL_Z_PC4:
            channel = ADC_ACCEL_Z_PC4_CHANNEL;
            break;

        default:
            return false;
    }

    ADC1->CR1 &= ~ADC_CR1_SCAN;
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = channel;
    adc_xyz_scan_active = 0U;
    ADC1->SR = 0U;
    selected_input = input;
    return true;
}

ADC_Input ADC_GetSelectedInput(void)
{
    return selected_input;
}

void ADC_UseSoftwareTrigger(void)
{
    ADC1->CR2 &= ~(ADC_CR2_EXTSEL |
                   ADC_CR2_EXTEN |
                   ADC_CR2_CONT);
}

bool ADC_ConfigureXYZScan(void)
{
    ADC_Init();
    if (((ADC1->CR2 & ADC_CR2_DMA) != 0U) ||
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U) ||
        ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0U))
    {
        return false;
    }

    /* Three regular conversions: X/PC1 ch11, Y/PC5 ch15, Z/PC4 ch14. */
    ADC1->CR1 |= ADC_CR1_SCAN;
    /* ACCEL_XYZ_SEQUENCE_LENGTH_FIX: L=2 means three conversions. */
    ADC1->SQR1 = (ADC1->SQR1 & ~ADC_SQR1_L) | ADC_SQR1_L_1;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = (ADC_ACCEL_X_PC1_CHANNEL << 0) |
                 (ADC_ACCEL_Y_PC5_CHANNEL << 5) |
                 (ADC_ACCEL_Z_PC4_CHANNEL << 10);
    ADC1->SR = 0U;
    adc_xyz_scan_active = 1U;
    return true;
}

void ADC_RestoreSingleInput(void)
{
    uint32_t channel;

    switch (selected_input)
    {
        case ADC_INPUT_ACCEL_X_PA1: channel = ADC_ACCEL_X_CHANNEL; break;
        case ADC_INPUT_ACCEL_X_PC1: channel = ADC_ACCEL_X_PC1_CHANNEL; break;
        case ADC_INPUT_ACCEL_Y_PC5: channel = ADC_ACCEL_Y_PC5_CHANNEL; break;
        case ADC_INPUT_ACCEL_Z_PC4: channel = ADC_ACCEL_Z_PC4_CHANNEL; break;
        case ADC_INPUT_MICROPHONE_PA0:
        default: channel = ADC_MICROPHONE_CHANNEL; break;
    }

    ADC1->CR1 &= ~ADC_CR1_SCAN;
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = channel;
    ADC1->SR = 0U;
    adc_xyz_scan_active = 0U;
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