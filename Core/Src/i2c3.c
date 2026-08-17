/*
 * I2C3 driver for STM32F429I-DISC1.
 *
 * Board connections:
 *   PA8 = I2C3_SCL, alternate function 4
 *   PC9 = I2C3_SDA, alternate function 4
 *
 * The board supplies the required external I2C pull-up resistors.
 */

#include "i2c3.h"

#define I2C3_TIMEOUT_MS  100U

static I2C_HandleTypeDef hi2c3;
static uint8_t i2c3_initialized;

static uint16_t i2c_address(uint8_t address_7bit)
{
    return (uint16_t)address_7bit << 1U;
}

static void i2c3_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C3;

    gpio.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOC, &gpio);
}

HAL_StatusTypeDef I2C3_Init(void)
{
    if (i2c3_initialized != 0U)
    {
        return HAL_OK;
    }

    i2c3_gpio_init();
    __HAL_RCC_I2C3_CLK_ENABLE();

    /* Start from a known peripheral state. */
    __HAL_RCC_I2C3_FORCE_RESET();
    __NOP();
    __NOP();
    __HAL_RCC_I2C3_RELEASE_RESET();

    hi2c3.Instance = I2C3;
    hi2c3.Init.ClockSpeed = 100000U;
    hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c3.Init.OwnAddress1 = 0U;
    hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c3.Init.OwnAddress2 = 0U;
    hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c3) != HAL_OK)
    {
        return HAL_ERROR;
    }

    i2c3_initialized = 1U;
    return HAL_OK;
}

bool I2C3_IsReady(uint8_t address_7bit)
{
    if (I2C3_Init() != HAL_OK)
    {
        return false;
    }

    return HAL_I2C_IsDeviceReady(&hi2c3,
                                 i2c_address(address_7bit),
                                 3U,
                                 I2C3_TIMEOUT_MS) == HAL_OK;
}

HAL_StatusTypeDef I2C3_WriteRegister(uint8_t address_7bit,
                                     uint8_t register_address,
                                     uint8_t value)
{
    return I2C3_Write(address_7bit,
                      register_address,
                      &value,
                      1U);
}

HAL_StatusTypeDef I2C3_Write(uint8_t address_7bit,
                             uint8_t register_address,
                             const uint8_t *data,
                             uint16_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    if (I2C3_Init() != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Write(&hi2c3,
                             i2c_address(address_7bit),
                             register_address,
                             I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)data,
                             length,
                             I2C3_TIMEOUT_MS);
}

HAL_StatusTypeDef I2C3_ReadRegister(uint8_t address_7bit,
                                    uint8_t register_address,
                                    uint8_t *value)
{
    return I2C3_Read(address_7bit,
                     register_address,
                     value,
                     1U);
}

HAL_StatusTypeDef I2C3_Read(uint8_t address_7bit,
                            uint8_t register_address,
                            uint8_t *data,
                            uint16_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    if (I2C3_Init() != HAL_OK)
    {
        return HAL_ERROR;
    }

    return HAL_I2C_Mem_Read(&hi2c3,
                            i2c_address(address_7bit),
                            register_address,
                            I2C_MEMADD_SIZE_8BIT,
                            data,
                            length,
                            I2C3_TIMEOUT_MS);
}
