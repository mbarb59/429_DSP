#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_sdram.h"
#include "f429_sdram.h"

#include <stdbool.h>
#include <stdint.h>

#define SDRAM_BASE_ADDR      ((uint32_t)0xD0000000U)
#define SDRAM_FB_TEST_ADDR   ((uint32_t)0xD07C0000U)
#define SDRAM_TIMEOUT        ((uint32_t)0xFFFFU)
#define SDRAM_REFRESH_COUNT  ((uint32_t)1386U)

static SDRAM_HandleTypeDef hsdram;
static bool sdram_is_init = false;

static void sdram_gpio_init(void) {
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = GPIO_AF12_FMC;

    // GPIOD
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
             GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gi);

    // GPIOE
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
             GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
             GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gi);

    // GPIOF
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
             GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
             GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gi);

    // GPIOG
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
             GPIO_PIN_8 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gi);

    // GPIOH
    gi.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5;
    HAL_GPIO_Init(GPIOH, &gi);

    // GPIOI
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
             GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOI, &gi);

    // GPIOC
    gi.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gi);
}

static HAL_StatusTypeDef sdram_init_sequence(void) {
    FMC_SDRAM_CommandTypeDef cmd = {0};
    uint32_t mode_reg = 0;

    // Step 1: Enable SDRAM clock
    cmd.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber = 1;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(&hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
        return HAL_ERROR;
    }

    HAL_Delay(1);

    // Step 2: Precharge all
    cmd.CommandMode = FMC_SDRAM_CMD_PALL;
    cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber = 1;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(&hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
        return HAL_ERROR;
    }

    // Step 3: Auto-refresh
    cmd.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber = 8;
    cmd.ModeRegisterDefinition = 0;

    if (HAL_SDRAM_SendCommand(&hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
        return HAL_ERROR;
    }

    // Step 4: Load mode register
    mode_reg =
        0x0000 |   // Burst length = 1
        0x0000 |   // Burst type = sequential
        0x0030 |   // CAS latency = 3
        0x0000 |   // Operating mode = standard
        0x0200;    // Write burst = single

    cmd.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    cmd.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    cmd.AutoRefreshNumber = 1;
    cmd.ModeRegisterDefinition = mode_reg;

    if (HAL_SDRAM_SendCommand(&hsdram, &cmd, SDRAM_TIMEOUT) != HAL_OK) {
        return HAL_ERROR;
    }

    // Step 5: Program refresh rate
    if (HAL_SDRAM_ProgramRefreshRate(&hsdram, SDRAM_REFRESH_COUNT) != HAL_OK) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef f429_sdram_init(void) {
    if (sdram_is_init) {
        return HAL_OK;
    }

    sdram_gpio_init();

    hsdram.Instance = FMC_SDRAM_DEVICE;

    hsdram.Init.SDBank = FMC_SDRAM_BANK2;
    hsdram.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
    hsdram.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
    hsdram.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    hsdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    hsdram.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
    hsdram.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    hsdram.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
    hsdram.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;

    FMC_SDRAM_TimingTypeDef timing = {0};

    timing.LoadToActiveDelay    = 2;
    timing.ExitSelfRefreshDelay = 7;
    timing.SelfRefreshTime      = 4;
    timing.RowCycleDelay        = 7;
    timing.WriteRecoveryTime    = 2;
    timing.RPDelay              = 2;
    timing.RCDDelay             = 2;

    if (HAL_SDRAM_Init(&hsdram, &timing) != HAL_OK) {
        return HAL_ERROR;
    }

    if (sdram_init_sequence() != HAL_OK) {
        return HAL_ERROR;
    }

    sdram_is_init = true;
    return HAL_OK;
}

bool f429_sdram_test(void) {
    if (f429_sdram_init() != HAL_OK) {
        return false;
    }

    volatile uint32_t *p = (volatile uint32_t *)SDRAM_FB_TEST_ADDR;

    const uint32_t pattern0 = 0x00000000U;
    const uint32_t pattern1 = 0xFFFFFFFFU;
    const uint32_t pattern2 = 0xA5A55A5AU;
    const uint32_t pattern3 = 0x12345678U;

    p[0] = pattern0;
    p[1] = pattern1;
    p[2] = pattern2;
    p[3] = pattern3;

    if (p[0] != pattern0) {
        return false;
    }

    if (p[1] != pattern1) {
        return false;
    }

    if (p[2] != pattern2) {
        return false;
    }

    if (p[3] != pattern3) {
        return false;
    }

    p[256] = 0x55AA55AAU;

    if (p[256] != 0x55AA55AAU) {
        return false;
    }

    return true;
}

bool f429_sdram_stress(void) {
    if (f429_sdram_init() != HAL_OK) {
        return false;
    }

    volatile uint32_t *p = (volatile uint32_t *)SDRAM_FB_TEST_ADDR;

    /*
     * Full LCD framebuffer size:
     * 240 x 320 x 2 bytes = 153600 bytes
     * 153600 bytes / 4 = 38400 uint32_t words
     */
    const uint32_t words = (240U * 320U * 2U) / 4U;

    for (uint32_t i = 0; i < words; ++i) {
        p[i] = 0xA5A50000U ^ i;
    }

    for (uint32_t i = 0; i < words; ++i) {
        if (p[i] != (0xA5A50000U ^ i)) {
            return false;
        }
    }

    for (uint32_t i = 0; i < words; ++i) {
        p[i] = 0x5A5A0000U ^ (i << 1);
    }

    for (uint32_t i = 0; i < words; ++i) {
        if (p[i] != (0x5A5A0000U ^ (i << 1))) {
            return false;
        }
    }

    return true;
}

uint32_t f429_sdram_base(void) {
    return SDRAM_BASE_ADDR;
}