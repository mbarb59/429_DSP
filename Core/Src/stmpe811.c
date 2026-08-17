/*
 * STMPE811 resistive-touch controller driver for STM32F429I-DISC1.
 *
 * This module initializes the touchscreen controller and returns raw
 * 12-bit X/Y coordinates. Calibration, landscape rotation, gestures,
 * zooming, and UI button handling belong in a higher-level UI module.
 */

#include "stmpe811.h"
#include "i2c3.h"

#include <stddef.h>
#include <stdint.h>

#define STMPE811_REG_CHIP_ID_MSB       0x00U
#define STMPE811_REG_CHIP_ID_LSB       0x01U
#define STMPE811_REG_SYS_CTRL1         0x03U
#define STMPE811_REG_SYS_CTRL2         0x04U
#define STMPE811_REG_INT_STA           0x0BU
#define STMPE811_REG_GPIO_AF           0x17U
#define STMPE811_REG_ADC_CTRL1         0x20U
#define STMPE811_REG_ADC_CTRL2         0x21U
#define STMPE811_REG_TSC_CTRL          0x40U
#define STMPE811_REG_TSC_CFG           0x41U
#define STMPE811_REG_FIFO_TH           0x4AU
#define STMPE811_REG_FIFO_STA          0x4BU
#define STMPE811_REG_FIFO_SIZE         0x4CU
#define STMPE811_REG_TSC_FRACT_XYZ     0x56U
#define STMPE811_REG_TSC_DATA_NON_INC  0xD7U
#define STMPE811_REG_TSC_I_DRIVE       0x58U

#define STMPE811_SYS_CTRL1_RESET       0x02U
#define STMPE811_TSC_CTRL_ENABLE       0x01U
#define STMPE811_TSC_CTRL_STATUS       0x80U
#define STMPE811_FIFO_RESET            0x01U

static bool stmpe811_initialized;

static HAL_StatusTypeDef stmpe811_read_u8(uint8_t register_address,
                                          uint8_t *value)
{
    if (value == NULL)
    {
        return HAL_ERROR;
    }

    return I2C3_ReadRegister(STMPE811_I2C_ADDRESS,
                             register_address,
                             value);
}

static HAL_StatusTypeDef stmpe811_write_u8(uint8_t register_address,
                                           uint8_t value)
{
    return I2C3_WriteRegister(STMPE811_I2C_ADDRESS,
                              register_address,
                              value);
}

HAL_StatusTypeDef STMPE811_ReadChipId(uint16_t *chip_id)
{
    uint8_t id_msb;
    uint8_t id_lsb;

    if (chip_id == NULL)
    {
        return HAL_ERROR;
    }

    if (I2C3_Init() != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (stmpe811_read_u8(STMPE811_REG_CHIP_ID_MSB, &id_msb) != HAL_OK ||
        stmpe811_read_u8(STMPE811_REG_CHIP_ID_LSB, &id_lsb) != HAL_OK)
    {
        return HAL_ERROR;
    }

    *chip_id = ((uint16_t)id_msb << 8U) | id_lsb;
    return HAL_OK;
}

void STMPE811_ClearFIFO(void)
{
    (void)stmpe811_write_u8(STMPE811_REG_FIFO_STA,
                            STMPE811_FIFO_RESET);
    (void)stmpe811_write_u8(STMPE811_REG_FIFO_STA, 0x00U);
}

HAL_StatusTypeDef STMPE811_Init(void)
{
    uint16_t chip_id;

    if (stmpe811_initialized)
    {
        return HAL_OK;
    }

    if (I2C3_Init() != HAL_OK ||
        !I2C3_IsReady(STMPE811_I2C_ADDRESS))
    {
        return HAL_ERROR;
    }

    if (STMPE811_ReadChipId(&chip_id) != HAL_OK ||
        chip_id != STMPE811_EXPECTED_ID)
    {
        return HAL_ERROR;
    }

    /* Software reset. */
    if (stmpe811_write_u8(STMPE811_REG_SYS_CTRL1,
                          STMPE811_SYS_CTRL1_RESET) != HAL_OK)
    {
        return HAL_ERROR;
    }

    HAL_Delay(10U);

    if (stmpe811_write_u8(STMPE811_REG_SYS_CTRL1, 0x00U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    HAL_Delay(2U);

    /* Enable the GPIO, touchscreen, and ADC blocks. */
    if (stmpe811_write_u8(STMPE811_REG_SYS_CTRL2, 0x00U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* ADC: 12-bit conversion and 3.25 MHz ADC clock. */
    if (stmpe811_write_u8(STMPE811_REG_ADC_CTRL1, 0x49U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    HAL_Delay(2U);

    if (stmpe811_write_u8(STMPE811_REG_ADC_CTRL2, 0x01U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Put the touchscreen pins under touchscreen-controller control. */
    if (stmpe811_write_u8(STMPE811_REG_GPIO_AF, 0x00U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* 4-sample averaging, 500 us touch delay, 500 us settling time. */
    if (stmpe811_write_u8(STMPE811_REG_TSC_CFG, 0x9AU) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if (stmpe811_write_u8(STMPE811_REG_FIFO_TH, 0x01U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    STMPE811_ClearFIFO();

    if (stmpe811_write_u8(STMPE811_REG_TSC_FRACT_XYZ, 0x01U) != HAL_OK ||
        stmpe811_write_u8(STMPE811_REG_TSC_I_DRIVE, 0x01U) != HAL_OK ||
        stmpe811_write_u8(STMPE811_REG_TSC_CTRL,
                          STMPE811_TSC_CTRL_ENABLE) != HAL_OK)
    {
        return HAL_ERROR;
    }

    /* Clear pending touchscreen and FIFO status bits. */
    if (stmpe811_write_u8(STMPE811_REG_INT_STA, 0xFFU) != HAL_OK)
    {
        return HAL_ERROR;
    }

    HAL_Delay(2U);
    stmpe811_initialized = true;
    return HAL_OK;
}

bool STMPE811_IsInitialized(void)
{
    return stmpe811_initialized;
}

bool STMPE811_IsTouched(void)
{
    uint8_t control;
    uint8_t fifo_size;

    if (!stmpe811_initialized && STMPE811_Init() != HAL_OK)
    {
        return false;
    }

    if (stmpe811_read_u8(STMPE811_REG_TSC_CTRL, &control) != HAL_OK ||
        stmpe811_read_u8(STMPE811_REG_FIFO_SIZE, &fifo_size) != HAL_OK)
    {
        return false;
    }

    return ((control & STMPE811_TSC_CTRL_STATUS) != 0U) &&
           (fifo_size != 0U);
}

HAL_StatusTypeDef STMPE811_ReadRaw(STMPE811_TouchData *touch)
{
    uint8_t data[4];

    if (touch == NULL)
    {
        return HAL_ERROR;
    }

    touch->pressed = false;
    touch->raw_x = 0U;
    touch->raw_y = 0U;
    touch->pressure = 0U;

    if (!STMPE811_IsTouched())
    {
        return HAL_OK;
    }

    if (I2C3_Read(STMPE811_I2C_ADDRESS,
                  STMPE811_REG_TSC_DATA_NON_INC,
                  data,
                  sizeof(data)) != HAL_OK)
    {
        return HAL_ERROR;
    }

    touch->raw_x = ((uint16_t)data[0] << 4U) |
                   ((uint16_t)data[1] >> 4U);

    touch->raw_y = (((uint16_t)data[1] & 0x0FU) << 8U) |
                   (uint16_t)data[2];

    touch->pressure = data[3];
    touch->pressed = true;

    /* Discard older queued samples so the next read is current. */
    STMPE811_ClearFIFO();

    return HAL_OK;
}
