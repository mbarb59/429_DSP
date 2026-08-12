#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_sdram.h"
#include "f429_sdram.h"
#include "uart.h"

#include <stdbool.h>
#include <stdint.h>

#define SDRAM_BASE_ADDR      ((uint32_t)0xD0000000U)
#define SDRAM_FB_TEST_ADDR   ((uint32_t)0xD07C0000U)
#define SDRAM_TIMEOUT        ((uint32_t)0xFFFFU)
#define SDRAM_REFRESH_COUNT  ((uint32_t)1292U)
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000U)
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000U)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030U)
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000U)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200U)
static SDRAM_HandleTypeDef hsdram;
static bool sdram_is_init = false;

static void sdram_gpio_init(void)
{
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = GPIO_AF12_FMC;

    /*
     * PB5  = SDCKE1
     * PB6  = SDNE1
     */
    gi.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOB, &gi);

    /*
     * PC0 = SDNWE
     */
    gi.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gi);

    /*
     * PD0  = D2
     * PD1  = D3
     * PD8  = D13
     * PD9  = D14
     * PD10 = D15
     * PD14 = D0
     * PD15 = D1
     */
    gi.Pin = GPIO_PIN_0  |
             GPIO_PIN_1  |
             GPIO_PIN_8  |
             GPIO_PIN_9  |
             GPIO_PIN_10 |
             GPIO_PIN_14 |
             GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gi);

    /*
     * PE0  = NBL0
     * PE1  = NBL1
     * PE7  = D4
     * PE8  = D5
     * PE9  = D6
     * PE10 = D7
     * PE11 = D8
     * PE12 = D9
     * PE13 = D10
     * PE14 = D11
     * PE15 = D12
     */
    gi.Pin = GPIO_PIN_0  |
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
    HAL_GPIO_Init(GPIOE, &gi);

    /*
     * PF0  = A0
     * PF1  = A1
     * PF2  = A2
     * PF3  = A3
     * PF4  = A4
     * PF5  = A5
     * PF11 = SDNRAS
     * PF12 = A6
     * PF13 = A7
     * PF14 = A8
     * PF15 = A9
     */
    gi.Pin = GPIO_PIN_0  |
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
    HAL_GPIO_Init(GPIOF, &gi);

    /*
     * PG0  = A10
     * PG1  = A11
     * PG4  = BA0
     * PG5  = BA1
     * PG8  = SDCLK
     * PG15 = SDNCAS
     */
    gi.Pin = GPIO_PIN_0  |
             GPIO_PIN_1  |
             GPIO_PIN_4  |
             GPIO_PIN_5  |
             GPIO_PIN_8  |
             GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gi);
}

static HAL_StatusTypeDef sdram_init_sequence(void)
{
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
    SDRAM_MODEREG_BURST_LENGTH_1 |
    SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL |
    SDRAM_MODEREG_CAS_LATENCY_3 |
    SDRAM_MODEREG_OPERATING_MODE_STANDARD |
    SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

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

HAL_StatusTypeDef f429_sdram_init(void)
{
    UART_SendString("SDRAM A\r\n");

    if (sdram_is_init)
    {
        UART_SendString("SDRAM A1\r\n");
        return HAL_OK;
    }

    UART_SendString("SDRAM B\r\n");

    sdram_gpio_init();

    UART_SendString("SDRAM C\r\n");

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

    FMC_SDRAM_TimingTypeDef timing = {0};

    timing.LoadToActiveDelay    = 2;
    timing.ExitSelfRefreshDelay = 7;
    timing.SelfRefreshTime      = 4;
    timing.RowCycleDelay        = 7;
    timing.WriteRecoveryTime    = 2;
    timing.RPDelay              = 2;
    timing.RCDDelay             = 2;

    UART_SendString("SDRAM D\r\n");

    if (HAL_SDRAM_Init(&hsdram, &timing) != HAL_OK)
    {
        UART_SendString("SDRAM ERR D\r\n");
        return HAL_ERROR;
    }

    UART_SendString("SDRAM E\r\n");

    if (sdram_init_sequence() != HAL_OK)
    {
        UART_SendString("SDRAM ERR E\r\n");
        return HAL_ERROR;
    }

    UART_SendString("SDRAM F\r\n");

    sdram_is_init = true;

    UART_SendString("SDRAM G\r\n");

    return HAL_OK;
}

bool f429_sdram_test(void)
{
    volatile uint32_t *p =
        (volatile uint32_t *)SDRAM_FB_TEST_ADDR;

    if (f429_sdram_init() != HAL_OK)
    {
        return false;
    }

    UART_SendString("SDRAM BASIC TEST\r\n");

    /*
      Write all locations before reading any of them.
      This detects adjacent-address aliasing.
     */
    p[0] = 0x55AA55AAU;
    p[1] = 0xAAAAAAAAU;
    p[2] = 0x55555555U;
    p[3] = 0x12345678U;

   if (p[0] != 0x55AA55AAU)
    {
       UART_SendString("FAIL P0\r\n");
        return false;
    }

   if (p[1] != 0xAAAAAAAAU)
    {
      UART_SendString("FAIL P1\r\n");
        return false;
    }

   if (p[2] != 0x55555555U)
    {
       UART_SendString("FAIL P2\r\n");
        return false;
    }

   if (p[3] != 0x12345678U)
    {
       UART_SendString("FAIL P3\r\n");
        return false;
    }

    UART_SendString("SDRAM BASIC TEST O*\r\n");

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