#ifndef STMPE811_H
#define STMPE811_H

#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define STMPE811_I2C_ADDRESS  0x41U
#define STMPE811_EXPECTED_ID  0x0811U

typedef struct
{
    bool pressed;
    uint16_t raw_x;
    uint16_t raw_y;
    uint8_t pressure;
} STMPE811_TouchData;

HAL_StatusTypeDef STMPE811_Init(void);
HAL_StatusTypeDef STMPE811_ReadChipId(uint16_t *chip_id);
bool STMPE811_IsInitialized(void);
bool STMPE811_IsTouched(void);
HAL_StatusTypeDef STMPE811_ReadRaw(STMPE811_TouchData *touch);
void STMPE811_ClearFIFO(void);

#endif /* STMPE811_H */
