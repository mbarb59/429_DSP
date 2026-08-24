/*
 * External SDRAM support for STM32F429I-DISC1.
 *
 * The onboard 8 MB SDRAM is connected to FMC SDRAM Bank 2 and is mapped at
 * 0xD0000000. The framebuffer may be stored anywhere inside this region.
 */

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_sdram.h"
#include "f429_sdram.h"

#include <stdbool.h>
#include <stdint.h>

#define SDRAM_BASE_ADDR          0xD0000000U
#define SDRAM_SIZE_BYTES         0x00800000U
#define SDRAM_TEST_ADDR          0xD07C0000U
#define SDRAM_TIMEOUT            0x0000FFFFU
#define SDRAM_REFRESH_COUNT      1292U

#define SDRAM_MODE_BURST_LENGTH_1          0x0000U
#define SDRAM_MODE_BURST_TYPE_SEQUENTIAL  0x0000U
#define SDRAM_MODE_CAS_LATENCY_3           0x0030U
#define SDRAM_MODE_STANDARD                0x0000U
#define SDRAM_MODE_WRITEBURST_SINGLE       0x0200U

#define SDRAM_FRAMEBUFFER_WORDS  ((240U * 320U * 2U) / 4U)

static SDRAM_HandleTypeDef hsdram;
static bool sdram_initialized;

static void sdram_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;

    /* PB5 = SDCKE1, PB6 = SDNE1. */
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* PC0 = SDNWE. */
    gpio.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PD0/1 = D2/D3, PD8/9/10 = D13/D14/D15, PD14/15 = D0/D1. */
    gpio.Pin = GPIO_PIN_0  |
               GPIO_PIN_1  |
               GPIO_PIN_8  |
               GPIO_PIN_9  |
               GPIO_PIN_10 |
               GPIO_PIN_14 |
               GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* PE0/1 = NBL0/NBL1, PE7 through PE15 = D4 through D12. */
    gpio.Pin = GPIO_PIN_0  |
               GPIO_PIN_1  |
               GPIO_PIN_7  |
               GPIO_PIN_8  |
               GPIO_PIN_9  |
               GPIO_PIN_10 |
               GPIO_PIN_11 |
               GPIO_PIN_12 |
               GPIO_PIN_13 |
               GPIO_PIN_14 |
               GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio);

    /* PF0 through PF5 = A0 through A5, PF11 = SDNRAS, PF12 through PF15 = A6 through A9. */
    gpio.Pin = GPIO_PIN_0  |
               GPIO_PIN_1  |
               GPIO_PIN_2  |
               GPIO_PIN_3  |
               GPIO_PIN_4  |
               GPIO_PIN_5  |
               GPIO_PIN_11 |
               GPIO_PIN_12 |
               GPIO_PIN_13 |
               GPIO_PIN_14 |
               GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio);

    /* PG0/1 = A10/A11, PG4/5 = BA0/BA1, PG8 = SDCLK, PG15 = SDNCAS. */
    gpio.Pin = GPIO_PIN_0  |
               GPIO_PIN_1  |
               GPIO_PIN_4  |
               GPIO_PIN_5  |
               GPIO_PIN_8  |
               GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gpio);
}

static HAL_StatusTypeDef sdram_send_command(uint32_t mode,
                                             uint32_t auto_refresh_count,
                                             uint32_t mode_register)
{
    FMC_SDRAM_CommandTypeDef command = {0};

    command.CommandMode = mode;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = auto_refresh_count;
    command.ModeRegisterDefinition = mode_register;

    return HAL_SDRAM_SendCommand(&hsdram, &command, SDRAM_TIMEOUT);
}

static HAL_StatusTypeDef sdram_init_sequence(void)
{
    const uint32_t mode_register =
        SDRAM_MODE_BURST_LENGTH_1 |
        SDRAM_MODE_BURST_TYPE_SEQUENTIAL |
        SDRAM_MODE_CAS_LATENCY_3 |
        SDRAM_MODE_STANDARD |
        SDRAM_MODE_WRITEBURST_SINGLE;

    if (sdram_send_command(FMC_SDRAM_CMD_CLK_ENABLE, 1U, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    HAL_Delay(1U);

    if (sdram_send_command(FMC_SDRAM_CMD_PALL, 1U, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (sdram_send_command(FMC_SDRAM_CMD_AUTOREFRESH_MODE, 8U, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (sdram_send_command(FMC_SDRAM_CMD_LOAD_MODE, 1U, mode_register) != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_SDRAM_ProgramRefreshRate(&hsdram, SDRAM_REFRESH_COUNT);
}

HAL_StatusTypeDef f429_sdram_init(void)
{
    FMC_SDRAM_TimingTypeDef timing = {0};

    if (sdram_initialized)
    {
        return HAL_OK;
    }

    sdram_gpio_init();

    hsdram.Instance = FMC_SDRAM_DEVICE;
    hsdram.Init.SDBank = FMC_SDRAM_BANK2;
    hsdram.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
    hsdram.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    hsdram.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    hsdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    hsdram.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    hsdram.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    hsdram.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

    timing.LoadToActiveDelay = 2U;
    timing.ExitSelfRefreshDelay = 7U;
    timing.SelfRefreshTime = 4U;
    timing.RowCycleDelay = 7U;
    timing.WriteRecoveryTime = 2U;
    timing.RPDelay = 2U;
    timing.RCDDelay = 2U;

    if (HAL_SDRAM_Init(&hsdram, &timing) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (sdram_init_sequence() != HAL_OK)
    {
        return HAL_ERROR;
    }

    sdram_initialized = true;

    return HAL_OK;
}

bool f429_sdram_test(void)
{
    volatile uint32_t *memory = (volatile uint32_t *)SDRAM_TEST_ADDR;

    if (f429_sdram_init() != HAL_OK)
    {
        return false;
    }

    /* Write all values first so adjacent-address aliasing is detected. */
    memory[0] = 0x55AA55AAU;
    memory[1] = 0xAAAAAAAAU;
    memory[2] = 0x55555555U;
    memory[3] = 0x12345678U;

    return (memory[0] == 0x55AA55AAU) &&
           (memory[1] == 0xAAAAAAAAU) &&
           (memory[2] == 0x55555555U) &&
           (memory[3] == 0x12345678U);
}

bool f429_sdram_stress(void)
{
    volatile uint32_t *memory = (volatile uint32_t *)SDRAM_TEST_ADDR;

    if (f429_sdram_init() != HAL_OK)
    {
        return false;
    }

    for (uint32_t i = 0U; i < SDRAM_FRAMEBUFFER_WORDS; i++)
    {
        memory[i] = 0xA5A50000U ^ i;
    }

    for (uint32_t i = 0U; i < SDRAM_FRAMEBUFFER_WORDS; i++)
    {
        if (memory[i] != (0xA5A50000U ^ i))
        {
            return false;
        }
    }

    for (uint32_t i = 0U; i < SDRAM_FRAMEBUFFER_WORDS; i++)
    {
        memory[i] = 0x5A5A0000U ^ (i << 1U);
    }

    for (uint32_t i = 0U; i < SDRAM_FRAMEBUFFER_WORDS; i++)
    {
        if (memory[i] != (0x5A5A0000U ^ (i << 1U)))
        {
            return false;
        }
    }

    return true;
}

uint32_t f429_sdram_base(void)
{
    return SDRAM_BASE_ADDR;
}

uint32_t f429_sdram_size(void)
{
    return SDRAM_SIZE_BYTES;
}
