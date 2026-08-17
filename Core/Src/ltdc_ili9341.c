/*
 * ILI9341 control and LTDC Layer 0 support for STM32F429I-DISC1.
 *
 * SPI5 is used to configure the ILI9341 controller.
 * LTDC continuously streams RGB565 pixel data from external SDRAM.
 */

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_ltdc.h"
#include "ltdc_ili9341.h"

#include <stddef.h>
#include <stdint.h>

#ifndef ILI9341_MADCTL
#define ILI9341_MADCTL 0x48U
#endif

#ifndef LCD_DE_ACTIVE_HIGH
#define LCD_DE_ACTIVE_HIGH 0
#endif

#define LCD_SPI                 SPI5
#define LCD_SPI_CLK_ENABLE()    __HAL_RCC_SPI5_CLK_ENABLE()

#define LCD_SPI_SCK_PORT        GPIOF
#define LCD_SPI_SCK_PIN         GPIO_PIN_7
#define LCD_SPI_SCK_AF          GPIO_AF5_SPI5

#define LCD_SPI_MISO_PORT       GPIOF
#define LCD_SPI_MISO_PIN        GPIO_PIN_8
#define LCD_SPI_MISO_AF         GPIO_AF5_SPI5

#define LCD_SPI_MOSI_PORT       GPIOF
#define LCD_SPI_MOSI_PIN        GPIO_PIN_9
#define LCD_SPI_MOSI_AF         GPIO_AF5_SPI5

#define LCD_CS_PORT             GPIOC
#define LCD_CS_PIN              GPIO_PIN_2

#define LCD_DC_PORT             GPIOD
#define LCD_DC_PIN              GPIO_PIN_13

#define LCD_RESET_PORT          GPIOA
#define LCD_RESET_PIN           GPIO_PIN_1

#define LCD_WIDTH               240U
#define LCD_HEIGHT              320U

#define LCD_HSYNC               10U
#define LCD_HBP                 20U
#define LCD_HFP                 10U
#define LCD_VSYNC               2U
#define LCD_VBP                 2U
#define LCD_VFP                 4U

static LTDC_HandleTypeDef hltdc;
static SPI_HandleTypeDef hspi_lcd;
static uint8_t lcd_initialized;
static uint8_t layer0_configured;

static void gpio_init_output(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(port, &gpio);
}

static void gpio_init_af(GPIO_TypeDef *port,
                         uint16_t pin,
                         uint32_t alternate)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = alternate;

    HAL_GPIO_Init(port, &gpio);
}

static void gpio_write(GPIO_TypeDef *port,
                       uint16_t pin,
                       GPIO_PinState state)
{
    HAL_GPIO_WritePin(port, pin, state);
}

static HAL_StatusTypeDef ili9341_write(uint8_t command,
                                       const uint8_t *data,
                                       size_t length)
{
    if (ili9341_send_cmd(command) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if ((length > 0U) && (ili9341_send_data(data, length) != HAL_OK))
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef ili9341_init_spi(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    LCD_SPI_CLK_ENABLE();

    gpio_init_af(LCD_SPI_SCK_PORT, LCD_SPI_SCK_PIN, LCD_SPI_SCK_AF);
    gpio_init_af(LCD_SPI_MISO_PORT, LCD_SPI_MISO_PIN, LCD_SPI_MISO_AF);
    gpio_init_af(LCD_SPI_MOSI_PORT, LCD_SPI_MOSI_PIN, LCD_SPI_MOSI_AF);

    gpio_init_output(LCD_CS_PORT, LCD_CS_PIN);
    gpio_init_output(LCD_DC_PORT, LCD_DC_PIN);
    gpio_init_output(LCD_RESET_PORT, LCD_RESET_PIN);

    gpio_write(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);
    gpio_write(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET);
    gpio_write(LCD_RESET_PORT, LCD_RESET_PIN, GPIO_PIN_SET);

    hspi_lcd.Instance = LCD_SPI;
    hspi_lcd.Init.Mode = SPI_MODE_MASTER;
    hspi_lcd.Init.Direction = SPI_DIRECTION_1LINE;
    hspi_lcd.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi_lcd.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi_lcd.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi_lcd.Init.NSS = SPI_NSS_SOFT;
    hspi_lcd.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    hspi_lcd.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi_lcd.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi_lcd.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi_lcd.Init.CRCPolynomial = 7U;

    return HAL_SPI_Init(&hspi_lcd);
}

HAL_StatusTypeDef ili9341_hw_reset(void)
{
    gpio_write(LCD_RESET_PORT, LCD_RESET_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);

    gpio_write(LCD_RESET_PORT, LCD_RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(120U);

    return HAL_OK;
}

HAL_StatusTypeDef ili9341_send_cmd(uint8_t command)
{
    HAL_StatusTypeDef status;

    gpio_write(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET);
    gpio_write(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_RESET);

    status = HAL_SPI_Transmit(&hspi_lcd, &command, 1U, 20U);

    gpio_write(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);

    return status;
}

HAL_StatusTypeDef ili9341_send_data(const uint8_t *data, size_t length)
{
    HAL_StatusTypeDef status;

    if (length == 0U)
    {
        return HAL_OK;
    }

    if (data == NULL)
    {
        return HAL_ERROR;
    }

    gpio_write(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET);
    gpio_write(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET);

    status = HAL_SPI_Transmit(&hspi_lcd,
                              (uint8_t *)data,
                              (uint16_t)length,
                              100U);

    gpio_write(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);

    return status;
}

HAL_StatusTypeDef ili9341_init_sequence(void)
{
    static const uint8_t rgb_interface[] = {0xC2U};
    static const uint8_t pixel_format[] = {0x55U};
    static const uint8_t memory_access[] = {ILI9341_MADCTL};
    static const uint8_t porch_control[] =
        {0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U};
    static const uint8_t power_control_1[] = {0x23U};
    static const uint8_t power_control_2[] = {0x10U};
    static const uint8_t vcom_control_1[] = {0x3EU, 0x28U};
    static const uint8_t vcom_control_2[] = {0x86U};
    static const uint8_t display_function[] =
        {0x0AU, 0xA2U, 0x27U, 0x04U};
    static const uint8_t interface_control[] = {0x00U, 0x00U, 0x06U};
    static const uint8_t frame_rate[] = {0x00U, 0x18U};

    if (ili9341_write(0x01U, NULL, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }
    HAL_Delay(5U);

    if (ili9341_write(0x11U, NULL, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }
    HAL_Delay(120U);

    if (ili9341_write(0xB0U, rgb_interface, sizeof(rgb_interface)) != HAL_OK ||
        ili9341_write(0x3AU, pixel_format, sizeof(pixel_format)) != HAL_OK ||
        ili9341_write(0x36U, memory_access, sizeof(memory_access)) != HAL_OK ||
        ili9341_write(0xB2U, porch_control, sizeof(porch_control)) != HAL_OK ||
        ili9341_write(0xC0U, power_control_1, sizeof(power_control_1)) != HAL_OK ||
        ili9341_write(0xC1U, power_control_2, sizeof(power_control_2)) != HAL_OK ||
        ili9341_write(0xC5U, vcom_control_1, sizeof(vcom_control_1)) != HAL_OK ||
        ili9341_write(0xC7U, vcom_control_2, sizeof(vcom_control_2)) != HAL_OK ||
        ili9341_write(0xB6U, display_function, sizeof(display_function)) != HAL_OK ||
        ili9341_write(0xF6U, interface_control, sizeof(interface_control)) != HAL_OK ||
        ili9341_write(0xB1U, frame_rate, sizeof(frame_rate)) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (ili9341_write(0x29U, NULL, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }
    HAL_Delay(20U);

    return HAL_OK;
}

static HAL_StatusTypeDef ltdc_clock_init(void)
{
    RCC_PeriphCLKInitTypeDef clock = {0};

    clock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    clock.PLLSAI.PLLSAIN = 192U;
    clock.PLLSAI.PLLSAIR = 4U;
    clock.PLLSAIDivR = RCC_PLLSAIDIVR_8;

    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
    {
        return HAL_ERROR;
    }

    __HAL_RCC_LTDC_CLK_ENABLE();

    return HAL_OK;
}

static void ltdc_pins_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* LTDC clock and synchronization signals. */
    gpio_init_af(GPIOG, GPIO_PIN_7, GPIO_AF14_LTDC);
    gpio_init_af(GPIOC, GPIO_PIN_6, GPIO_AF14_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_4, GPIO_AF14_LTDC);
    gpio_init_af(GPIOF, GPIO_PIN_10, GPIO_AF14_LTDC);

    /* Red data lines R2 through R7. */
    gpio_init_af(GPIOC, GPIO_PIN_10, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_0, GPIO_AF9_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_11, GPIO_AF14_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_12, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_1, GPIO_AF9_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_6, GPIO_AF14_LTDC);

    /* Green data lines G2 through G7. */
    gpio_init_af(GPIOA, GPIO_PIN_6, GPIO_AF14_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_10, GPIO_AF9_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_10, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_11, GPIO_AF14_LTDC);
    gpio_init_af(GPIOC, GPIO_PIN_7, GPIO_AF14_LTDC);
    gpio_init_af(GPIOD, GPIO_PIN_3, GPIO_AF14_LTDC);

    /* Blue data lines B2 through B7. */
    gpio_init_af(GPIOD, GPIO_PIN_6, GPIO_AF14_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_11, GPIO_AF14_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_12, GPIO_AF9_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_3, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_8, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_9, GPIO_AF14_LTDC);
}

static HAL_StatusTypeDef ltdc_core_init(void)
{
    hltdc.Instance = LTDC;

    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
#if LCD_DE_ACTIVE_HIGH
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AH;
#else
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
#endif
    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IIPC;

    hltdc.Init.HorizontalSync = LCD_HSYNC - 1U;
    hltdc.Init.AccumulatedHBP = LCD_HSYNC + LCD_HBP - 1U;
    hltdc.Init.AccumulatedActiveW =
        LCD_HSYNC + LCD_HBP + LCD_WIDTH - 1U;
    hltdc.Init.TotalWidth =
        LCD_HSYNC + LCD_HBP + LCD_WIDTH + LCD_HFP - 1U;

    hltdc.Init.VerticalSync = LCD_VSYNC - 1U;
    hltdc.Init.AccumulatedVBP = LCD_VSYNC + LCD_VBP - 1U;
    hltdc.Init.AccumulatedActiveH =
        LCD_VSYNC + LCD_VBP + LCD_HEIGHT - 1U;
    hltdc.Init.TotalHeigh =
        LCD_VSYNC + LCD_VBP + LCD_HEIGHT + LCD_VFP - 1U;

    hltdc.Init.Backcolor.Red = 0U;
    hltdc.Init.Backcolor.Green = 0U;
    hltdc.Init.Backcolor.Blue = 0U;

    return HAL_LTDC_Init(&hltdc);
}

HAL_StatusTypeDef f429_ltdc_layer0_rgb565(uint32_t framebuffer_address,
                                          uint16_t width,
                                          uint16_t height)
{
    LTDC_LayerCfgTypeDef layer = {0};

    if ((framebuffer_address == 0U) || (width == 0U) || (height == 0U))
    {
        return HAL_ERROR;
    }

    if (!lcd_initialized && (f429_ltdc_init() != HAL_OK))
    {
        return HAL_ERROR;
    }

    layer.WindowX0 = 0U;
    layer.WindowX1 = width;
    layer.WindowY0 = 0U;
    layer.WindowY1 = height;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer.FBStartAdress = framebuffer_address;
    layer.Alpha = 255U;
    layer.Alpha0 = 0U;
    layer.Backcolor.Red = 0U;
    layer.Backcolor.Green = 0U;
    layer.Backcolor.Blue = 0U;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.ImageWidth = width;
    layer.ImageHeight = height;

    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    __HAL_LTDC_LAYER_ENABLE(&hltdc, 0U);
    __HAL_LTDC_RELOAD_CONFIG(&hltdc);

    layer0_configured = 1U;

    return HAL_OK;
}

HAL_StatusTypeDef f429_ltdc_layer0_enable(int enable)
{
    if (!layer0_configured)
    {
        return HAL_OK;
    }

    if (enable != 0)
    {
        __HAL_LTDC_LAYER_ENABLE(&hltdc, 0U);
    }
    else
    {
        __HAL_LTDC_LAYER_DISABLE(&hltdc, 0U);
    }

    __HAL_LTDC_RELOAD_CONFIG(&hltdc);

    return HAL_OK;
}

uint16_t f429_lcd_width(void)
{
    return LCD_WIDTH;
}

uint16_t f429_lcd_height(void)
{
    return LCD_HEIGHT;
}

HAL_StatusTypeDef f429_ltdc_init(void)
{
    if (lcd_initialized)
    {
        return HAL_OK;
    }

    if (ili9341_init_spi() != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (ili9341_hw_reset() != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (ltdc_clock_init() != HAL_OK)
    {
        return HAL_ERROR;
    }

    ltdc_pins_init();

    if (ltdc_core_init() != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (ili9341_init_sequence() != HAL_OK)
    {
        return HAL_ERROR;
    }

    lcd_initialized = 1U;

    return HAL_OK;
}
