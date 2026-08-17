#ifndef I2C3_H
#define I2C3_H

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_i2c.h"

#include <stdbool.h>
#include <stdint.h>

HAL_StatusTypeDef I2C3_Init(void);
bool I2C3_IsReady(uint8_t address_7bit);

HAL_StatusTypeDef I2C3_WriteRegister(uint8_t address_7bit,
                                     uint8_t register_address,
                                     uint8_t value);

HAL_StatusTypeDef I2C3_Write(uint8_t address_7bit,
                             uint8_t register_address,
                             const uint8_t *data,
                             uint16_t length);

HAL_StatusTypeDef I2C3_ReadRegister(uint8_t address_7bit,
                                    uint8_t register_address,
                                    uint8_t *value);

HAL_StatusTypeDef I2C3_Read(uint8_t address_7bit,
                            uint8_t register_address,
                            uint8_t *data,
                            uint16_t length);

#endif /* I2C3_H */
