// LCD bring-up + LTDC Layer 0 support for STM32F429DISC / STM32F429I-DISC1

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_ltdc.h"
#include "ltdc_ili9341.h"
#include "uart.h"

#include <stdint.h>
#include <stddef.h>
//#include <stdio.h>


//#include "py/misc.h"
//#include "py/obj.h"

#ifndef LCD_DBG
#define LCD_DBG(...)
#endif

#define LCD_MARK(msg) UART_SendString(msg "\r\n")

#ifndef ILI9341_MADCTL
#define ILI9341_MADCTL (0x48)
#endif

#ifndef LCD_DE_ACTIVE_HIGH
#define LCD_DE_ACTIVE_HIGH (0)
#endif

#define LCD_SPIx                SPI5
#define LCD_SPIx_CLK_ENABLE()   __HAL_RCC_SPI5_CLK_ENABLE()

#define LCD_SPI_SCK_GPIO_PORT   GPIOF
#define LCD_SPI_SCK_PIN         GPIO_PIN_7
#define LCD_SPI_SCK_AF          GPIO_AF5_SPI5

#define LCD_SPI_MISO_GPIO_PORT  GPIOF
#define LCD_SPI_MISO_PIN        GPIO_PIN_8
#define LCD_SPI_MISO_AF         GPIO_AF5_SPI5

#define LCD_SPI_MOSI_GPIO_PORT  GPIOF
#define LCD_SPI_MOSI_PIN        GPIO_PIN_9
#define LCD_SPI_MOSI_AF         GPIO_AF5_SPI5

#define LCD_CS_GPIO_PORT        GPIOC
#define LCD_CS_PIN              GPIO_PIN_2

#define LCD_DC_GPIO_PORT        GPIOD
#define LCD_DC_PIN              GPIO_PIN_13

#define LCD_RST_GPIO_PORT       GPIOA
#define LCD_RST_PIN             GPIO_PIN_1

#define LCD_BL_GPIO_PORT        GPIOA
#define LCD_BL_PIN              GPIO_PIN_8

#define LCD_WIDTH   (240)
#define LCD_HEIGHT  (320)

#define HSYNC   (9)
#define HBP     (29)
#define HFP     (2)
#define VSYNC   (2)
#define VBP     (2)
#define VFP     (4)

static LTDC_HandleTypeDef hltdc;
static SPI_HandleTypeDef hspi_lcd;
static uint8_t lcd_inited = 0;
static uint8_t layer0_configured = 0;

static void gpio_init_output(GPIO_TypeDef *port, uint16_t pin) {
    GPIO_InitTypeDef gi = {0};
    gi.Pin = pin;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &gi);
}

static void gpio_init_af(GPIO_TypeDef *port, uint16_t pin, uint32_t af) {
    GPIO_InitTypeDef gi = {0};
    gi.Pin = pin;
    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = af;
    HAL_GPIO_Init(port, &gi);
}

static inline void gpio_out(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState v) {
    HAL_GPIO_WritePin(port, pin, v);
}

HAL_StatusTypeDef ili9341_init_spi(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    LCD_SPIx_CLK_ENABLE();

    gpio_init_af(LCD_SPI_SCK_GPIO_PORT, LCD_SPI_SCK_PIN, LCD_SPI_SCK_AF);
    gpio_init_af(LCD_SPI_MISO_GPIO_PORT, LCD_SPI_MISO_PIN, LCD_SPI_MISO_AF);
    gpio_init_af(LCD_SPI_MOSI_GPIO_PORT, LCD_SPI_MOSI_PIN, LCD_SPI_MOSI_AF);

    gpio_init_output(LCD_CS_GPIO_PORT, LCD_CS_PIN);
    gpio_init_output(LCD_DC_GPIO_PORT, LCD_DC_PIN);
    gpio_init_output(LCD_RST_GPIO_PORT, LCD_RST_PIN);
    gpio_init_output(LCD_BL_GPIO_PORT, LCD_BL_PIN);

    gpio_out(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_SET);
    gpio_out(LCD_DC_GPIO_PORT, LCD_DC_PIN, GPIO_PIN_SET);
    gpio_out(LCD_RST_GPIO_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    gpio_out(LCD_BL_GPIO_PORT, LCD_BL_PIN, GPIO_PIN_RESET);

    hspi_lcd.Instance = LCD_SPIx;
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
    hspi_lcd.Init.CRCPolynomial = 7;

    if (HAL_SPI_Init(&hspi_lcd) != HAL_OK) {
        LCD_DBG("SPI5 init failed\n");
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef ili9341_hw_reset(void) {
    gpio_out(LCD_RST_GPIO_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    gpio_out(LCD_RST_GPIO_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(120);
    return HAL_OK;
}

HAL_StatusTypeDef ili9341_send_cmd(uint8_t cmd) {
    gpio_out(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_RESET);
    gpio_out(LCD_DC_GPIO_PORT, LCD_DC_PIN, GPIO_PIN_RESET);

    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi_lcd, &cmd, 1, 20);

    gpio_out(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_SET);
    return st;
}

HAL_StatusTypeDef ili9341_send_data(const uint8_t *data, size_t len) {
    if (len == 0) {
        return HAL_OK;
    }

    gpio_out(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_RESET);
    gpio_out(LCD_DC_GPIO_PORT, LCD_DC_PIN, GPIO_PIN_SET);

    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi_lcd, (uint8_t *)data, len, 100);

    gpio_out(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_SET);
    return st;
}

HAL_StatusTypeDef ili9341_init_sequence(void)
{
    if (ili9341_send_cmd(0x01) != HAL_OK)
    {
        return HAL_ERROR;
    }
    HAL_Delay(5);

    if (ili9341_send_cmd(0x11) != HAL_OK)
    {
        return HAL_ERROR;
    }
    HAL_Delay(120);

    if (ili9341_send_cmd(0xB0) != HAL_OK)
    {
        return HAL_ERROR;
    }
    {
        uint8_t d = 0xC2;
        if (ili9341_send_data(&d, 1) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    if (ili9341_send_cmd(0x3A) != HAL_OK)
    {
        return HAL_ERROR;
    }
    {
        uint8_t d = 0x55;
        if (ili9341_send_data(&d, 1) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    if (ili9341_send_cmd(0x36) != HAL_OK)
    {
        return HAL_ERROR;
    }
    {
        uint8_t d = ILI9341_MADCTL;
        if (ili9341_send_data(&d, 1) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    if (ili9341_send_cmd(0xB2) != HAL_OK)
    {
        return HAL_ERROR;
    }
    {
        uint8_t d[5] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
        if (ili9341_send_data(d, 5) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

if (ili9341_send_cmd(0xC0) != HAL_OK)
{
    return HAL_ERROR;
}
{
    uint8_t d = 0x23;
    if (ili9341_send_data(&d, 1) != HAL_OK)
    {
        return HAL_ERROR;
    }
}

if (ili9341_send_cmd(0xC1) != HAL_OK)
{
    return HAL_ERROR;
}
{
    uint8_t d = 0x10;
    if (ili9341_send_data(&d, 1) != HAL_OK)
    {
        return HAL_ERROR;
    }
if (ili9341_send_cmd(0xC5) != HAL_OK)
{
    return HAL_ERROR;
}
if (ili9341_send_cmd(0xC7) != HAL_OK)
{
    return HAL_ERROR;
}
{
    uint8_t d = 0x86;

    if (ili9341_send_data(&d, 1) != HAL_OK)
    {
        return HAL_ERROR;
    }
}
{
    uint8_t d[2] = {0x3E, 0x28};
    if (ili9341_send_data(d, 2) != HAL_OK)
    {
        return HAL_ERROR;
    }
}
}

if (ili9341_send_cmd(0xB6) != HAL_OK)
{
    return HAL_ERROR;
}
{
    uint8_t d[4] = {0x0A, 0xA2, 0x27, 0x04};
    if (ili9341_send_data(d, 4) != HAL_OK)
    {
        return HAL_ERROR;
    }
}

if (ili9341_send_cmd(0xF6) != HAL_OK)
{
    return HAL_ERROR;
}
{
    uint8_t d[3] = {0x00, 0x00, 0x06};
    if (ili9341_send_data(d, 3) != HAL_OK)
    {
        return HAL_ERROR;
    }
}

if (ili9341_send_cmd(0xB1) != HAL_OK)
{
    return HAL_ERROR;
}
{
    uint8_t d[2] = {0x00, 0x18};
    if (ili9341_send_data(d, 2) != HAL_OK)
    {
        return HAL_ERROR;
    }
}

if (ili9341_send_cmd(0x29) != HAL_OK)
{
    return HAL_ERROR;
}

HAL_Delay(20);

gpio_out(LCD_BL_GPIO_PORT, LCD_BL_PIN, GPIO_PIN_SET);
HAL_Delay(10);

return HAL_OK;
}

static HAL_StatusTypeDef ltdc_clock_init(void)
{
    RCC_PeriphCLKInitTypeDef p = {0};

    LCD_MARK("CLK A");

    p.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    p.PLLSAI.PLLSAIN = 192;
    p.PLLSAI.PLLSAIR = 4;
    p.PLLSAIDivR = RCC_PLLSAIDIVR_8;

    if (HAL_RCCEx_PeriphCLKConfig(&p) != HAL_OK)
    {
        LCD_MARK("CLK FAIL");
        return HAL_ERROR;
    }

    __HAL_RCC_LTDC_CLK_ENABLE();

    LCD_MARK("CLK OK");

    return HAL_OK;
}
static void ltdc_pins_init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    gpio_init_af(GPIOG, GPIO_PIN_7, GPIO_AF14_LTDC);
    gpio_init_af(GPIOC, GPIO_PIN_6, GPIO_AF14_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_4, GPIO_AF14_LTDC);
    gpio_init_af(GPIOF, GPIO_PIN_10, GPIO_AF14_LTDC);

    gpio_init_af(GPIOC, GPIO_PIN_10, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_0, GPIO_AF9_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_11, GPIO_AF14_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_12, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_1, GPIO_AF9_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_6, GPIO_AF14_LTDC);

    gpio_init_af(GPIOA, GPIO_PIN_6, GPIO_AF14_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_10, GPIO_AF9_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_10, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_11, GPIO_AF14_LTDC);
    gpio_init_af(GPIOC, GPIO_PIN_7, GPIO_AF14_LTDC);
    gpio_init_af(GPIOD, GPIO_PIN_3, GPIO_AF14_LTDC);

    gpio_init_af(GPIOD, GPIO_PIN_6, GPIO_AF14_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_11, GPIO_AF14_LTDC);
    gpio_init_af(GPIOG, GPIO_PIN_12, GPIO_AF9_LTDC);
    gpio_init_af(GPIOA, GPIO_PIN_3, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_8, GPIO_AF14_LTDC);
    gpio_init_af(GPIOB, GPIO_PIN_9, GPIO_AF14_LTDC);
}

static HAL_StatusTypeDef ltdc_core_init_background_only(void) {
    hltdc.Instance = LTDC;

    hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;

#if LCD_DE_ACTIVE_HIGH
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AH;
#else
    hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
#endif

    hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IIPC;

    hltdc.Init.HorizontalSync = HSYNC - 1;
    hltdc.Init.AccumulatedHBP = HSYNC + HBP - 1;
    hltdc.Init.AccumulatedActiveW = HSYNC + HBP + LCD_WIDTH - 1;
    hltdc.Init.TotalWidth = HSYNC + HBP + LCD_WIDTH + HFP - 1;

    hltdc.Init.VerticalSync = VSYNC - 1;
    hltdc.Init.AccumulatedVBP = VSYNC + VBP - 1;
    hltdc.Init.AccumulatedActiveH = VSYNC + VBP + LCD_HEIGHT - 1;
    hltdc.Init.TotalHeigh = VSYNC + VBP + LCD_HEIGHT + VFP - 1;

    hltdc.Init.Backcolor.Red = 0;
    hltdc.Init.Backcolor.Green = 255;
    hltdc.Init.Backcolor.Blue = 0;

   LCD_MARK("CORE A");

if (HAL_LTDC_Init(&hltdc) != HAL_OK)
{
    LCD_MARK("CORE FAIL");
    return HAL_ERROR;
}
UART_SendString("LTDC_GCR=");
UART_SendHex32(LTDC->GCR);
UART_SendString("\r\n");

UART_SendString("LTDC_AWCR=");
UART_SendHex32(LTDC->AWCR);
UART_SendString("\r\n");

UART_SendString("LTDC_TWCR=");
UART_SendHex32(LTDC->TWCR);
UART_SendString("\r\n");
LCD_MARK("CORE B");

__HAL_LTDC_RELOAD_CONFIG(&hltdc);

LCD_MARK("CORE C");

return HAL_OK;
}

HAL_StatusTypeDef f429_ltdc_layer0_rgb565(uint32_t fb_addr,
                                          uint16_t width,
                                          uint16_t height)
{
    LTDC_LayerCfgTypeDef layer = {0};

    /* Validate the arguments. */
    if ((fb_addr == 0U) || (width == 0U) || (height == 0U))
    {
        return HAL_ERROR;
    }

    /* Initialize the LCD and LTDC if needed. */
    if (!lcd_inited)
    {
        if (f429_ltdc_init() != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    /* Configure the visible Layer 0 window. */
    layer.WindowX0 = 0U;
    layer.WindowX1 = width;
    layer.WindowY0 = 0U;
    layer.WindowY1 = height;

    /* Configure the RGB565 framebuffer. */
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer.FBStartAdress = fb_addr;
    layer.ImageWidth = width;
    layer.ImageHeight = height;

    /* Make Layer 0 fully opaque. */
    layer.Alpha = 255U;
    layer.Alpha0 = 0U;

    /* Default layer background color. */
    layer.Backcolor.Red = 0U;
    layer.Backcolor.Green = 0U;
    layer.Backcolor.Blue = 0U;

    /* Configure alpha blending. */
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;

    /* HAL layer 0 corresponds to the LTDC Layer 1 register block. */
    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Ensure Layer 0 uses the requested framebuffer address. */
    if (HAL_LTDC_SetAddress(&hltdc, fb_addr, 0U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Enable the layer and load the new register values. */
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

    if (enable)
    {
        __HAL_LTDC_LAYER_ENABLE(&hltdc, 0);
    }
    else
    {
        __HAL_LTDC_LAYER_DISABLE(&hltdc, 0);
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
    if (lcd_inited)
    {
        LCD_MARK("LTDC A0");
        return HAL_OK;
    }

    LCD_MARK("LTDC A");

    if (ili9341_init_spi() != HAL_OK)
    {
        LCD_MARK("LTDC FAIL A");
        return HAL_ERROR;
    }

    LCD_MARK("LTDC B");

    if (ili9341_hw_reset() != HAL_OK)
    {
        LCD_MARK("LTDC FAIL B");
        return HAL_ERROR;
    }

    LCD_MARK("LTDC C");

    if (ltdc_clock_init() != HAL_OK)
    {
        LCD_MARK("LTDC FAIL C");
        return HAL_ERROR;
    }

    LCD_MARK("LTDC D");

    ltdc_pins_init();

    LCD_MARK("LTDC E");

    if (ltdc_core_init_background_only() != HAL_OK)
    {
        LCD_MARK("LTDC FAIL E");
        return HAL_ERROR;
    }

    LCD_MARK("LTDC F");

    if (ili9341_init_sequence() != HAL_OK)
    {
        LCD_MARK("LTDC FAIL F");
        return HAL_ERROR;
    }

    LCD_MARK("LTDC G");

    lcd_inited = 1;

    LCD_MARK("LTDC DONE");

    return HAL_OK;
}

    
       