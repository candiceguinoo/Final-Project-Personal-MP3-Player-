#include "drv_i2c2.h"

static I2C_HandleTypeDef I2C2_Handler = {0};

/**
 * Configure I2C2 Hardware Peripheral and Pins
 */
void BareMetal_I2C2_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 1. Enable Clocks for the I2C Engine and Port B Pins */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();

    /* 2. Configure Physical GPIO Pins
     * PB10 -> SCL (Serial Clock)
     * PB11 -> SDA (Serial Data)
     */
    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;          // I2C requires Open-Drain mode
    GPIO_InitStruct.Pull = GPIO_PULLUP;             // Active internal pull-ups
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;       // Alternate Function 4 maps pins to I2C2
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* 3. Initialize I2C2 Hardware Engine Parameters */
    I2C2_Handler.Instance = I2C2;
    I2C2_Handler.Init.ClockSpeed = 100000;          // Standard 100kHz communication speed
    I2C2_Handler.Init.DutyCycle = I2C_DUTYCYCLE_2;
    I2C2_Handler.Init.OwnAddress1 = 0x00;
    I2C2_Handler.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    I2C2_Handler.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    I2C2_Handler.Init.OwnAddress2 = 0x00;
    I2C2_Handler.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    I2C2_Handler.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    HAL_I2C_Init(&I2C2_Handler);
}

/**
 * Transmit a setting byte to a specific register inside the ES8388
 */
void BareMetal_I2C2_WriteReg(uint8_t dev_addr, uint8_t reg_addr, uint8_t value)
{
    uint8_t data[2];
    data[0] = reg_addr; // First byte is the register address inside the chip
    data[1] = value;    // Second byte is the configuration value we want to set

    /* The 7-bit ES8388 address (0x10) must be shifted left by 1 bit 
     * because the STM32 HAL expects an 8-bit format containing the R/W bit.
     */
    HAL_I2C_Master_Transmit(&I2C2_Handler, (dev_addr << 1), data, 2, HAL_MAX_DELAY);
}

/**
 * Read the current configuration value from a specific register inside the ES8388
 */
uint8_t BareMetal_I2C2_ReadReg(uint8_t dev_addr, uint8_t reg_addr)
{
    uint8_t value = 0;

    /* Step 1: Tell the ES8388 which register address we want to read from */
    HAL_I2C_Master_Transmit(&I2C2_Handler, (dev_addr << 1), &reg_addr, 1, HAL_MAX_DELAY);
    
    /* Step 2: Read the single-byte response back from that register */
    HAL_I2C_Master_Receive(&I2C2_Handler, (dev_addr << 1), &value, 1, HAL_MAX_DELAY);

    return value;
}