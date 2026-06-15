#ifndef __DRV_I2C2_H
#define __DRV_I2C2_H

#include "stm32f4xx_hal.h"

/* Initializes the I2C2 peripheral registers and physical GPIO pins */
void BareMetal_I2C2_Init(void);

/* Low-level register read/write hooks used directly by es8388.c */
uint8_t BareMetal_I2C2_ReadReg(uint8_t dev_addr, uint8_t reg_addr);
void BareMetal_I2C2_WriteReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t value);

#endif /* __DRV_I2C2_H */