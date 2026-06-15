/*
 * Clean Bare-Metal Driver for ES8388 Audio Codec
 * Completely removed RT-Thread dependencies. Compatible with STM32CubeMX HAL.
 */

#include "drv_es8388.h"
#include <string.h> // For NULL definition

/* HAL library shifts the 7-bit address 0x10 left by 1 to make 0x20 */
#define ES8388_ADDR 0x20

struct es8388_device
{
    I2C_HandleTypeDef *i2c_handle;
    GPIO_TypeDef* pa_gpio_port;
    uint16_t pa_gpio_pin;
};

static struct es8388_device es_dev = {0};

static uint8_t reg_read(uint8_t addr)
{
    uint8_t val = 0xff;

    if (es_dev.i2c_handle == NULL) return 0xff;

    if (HAL_I2C_Mem_Read(es_dev.i2c_handle, ES8388_ADDR, addr, I2C_MEMADD_SIZE_8BIT, &val, 1, 100) != HAL_OK)
    {
        return 0xff;
    }

    return val;
}

static void reg_write(uint8_t addr, uint8_t val)
{
    if (es_dev.i2c_handle == NULL) return;

    HAL_I2C_Mem_Write(es_dev.i2c_handle, ES8388_ADDR, addr, I2C_MEMADD_SIZE_8BIT, &val, 1, 100);
}

static int es8388_set_adc_dac_volume(int mode, int volume, int dot)
{
    int res = 0;
    if (volume < -96 || volume > 0)
    {
        if (volume < -96)
            volume = -96;
        else
            volume = 0;
    }
    dot = (dot >= 5 ? 1 : 0);
    volume = (-volume << 1) + dot;
    if (mode == ES_MODE_ADC || mode == ES_MODE_DAC_ADC)
    {
        reg_write(ES8388_ADCCONTROL8, volume);
        reg_write(ES8388_ADCCONTROL9, volume);
    }
    if (mode == ES_MODE_DAC || mode == ES_MODE_DAC_ADC)
    {
        reg_write(ES8388_DACCONTROL5, volume);
        reg_write(ES8388_DACCONTROL4, volume);
    }
    return res;
}

void es8388_set_voice_mute(uint8_t enable)
{
    uint8_t reg = 0;

    reg = reg_read(ES8388_DACCONTROL3);
    reg = reg & 0xFB;
    reg_write(ES8388_DACCONTROL3, reg | (((int)enable) << 2));
}

HAL_StatusTypeDef es8388_init(I2C_HandleTypeDef *hi2c, GPIO_TypeDef* PA_Port, uint16_t PA_Pin)
{
    if (hi2c == NULL)
    {
        return HAL_ERROR;
    }

    es_dev.i2c_handle = hi2c;
    es_dev.pa_gpio_port = PA_Port;
    es_dev.pa_gpio_pin = PA_Pin;

    // Run a quick verification check to make sure the chip responds before writing configurations
    if (HAL_I2C_IsDeviceReady(es_dev.i2c_handle, ES8388_ADDR, 5, 100) != HAL_OK)
    {
        return HAL_ERROR;
    }

    reg_write(ES8388_DACCONTROL3, 0x04);
    reg_write(ES8388_CONTROL2, 0x50);
    reg_write(ES8388_CHIPPOWER, 0x00);
    reg_write(ES8388_MASTERMODE, 0x00);

    /* dac */
    reg_write(ES8388_DACPOWER, 0xC0);
    reg_write(ES8388_CONTROL1, 0x12);
    reg_write(ES8388_DACCONTROL1, 0x18);
    reg_write(ES8388_DACCONTROL2, 0x02);
    reg_write(ES8388_DACCONTROL16, 0x00);
    reg_write(ES8388_DACCONTROL17, 0x9C);
    reg_write(ES8388_DACCONTROL20, 0x9C);
    reg_write(ES8388_DACCONTROL21, 0x80);
    reg_write(ES8388_DACCONTROL23, 0x00);
    es8388_set_adc_dac_volume(ES_MODE_DAC, 0, 0);

    reg_write(ES8388_DACPOWER, 0x3c);
    /* adc */
    reg_write(ES8388_ADCPOWER, 0xFF);
    reg_write(ES8388_ADCCONTROL1, 0xbb);
    reg_write(ES8388_ADCCONTROL2, 0x00);
    reg_write(ES8388_ADCCONTROL3, 0x02);
    reg_write(ES8388_ADCCONTROL4, 0x0d);
    reg_write(ES8388_ADCCONTROL5, 0x02);
    es8388_set_adc_dac_volume(ES_MODE_ADC, 0, 0);
    reg_write(ES8388_ADCPOWER, 0x09);

    /* enable es8388 PA external power gate */
    es8388_pa_power(1);

    reg_write(ES8388_DACCONTROL24, 0x1E);
    reg_write(ES8388_DACCONTROL25, 0x1E);

    return HAL_OK;
}

HAL_StatusTypeDef es8388_start(enum es8388_mode mode)
{
    uint8_t prev_data = 0, data = 0;

    prev_data = reg_read(ES8388_DACCONTROL21);
    if (mode == ES_MODE_LINE)
    {
        reg_write(ES8388_DACCONTROL16, 0x09);
        reg_write(ES8388_DACCONTROL17, 0x50);
        reg_write(ES8388_DACCONTROL20, 0x50);
        reg_write(ES8388_DACCONTROL21, 0xC0);
    }
    else
    {
        reg_write(ES8388_DACCONTROL21, 0x80);
    }
    data = reg_read(ES8388_DACCONTROL21);

    if (prev_data != data)
    {
        reg_write(ES8388_CHIPPOWER, 0xF0);
        reg_write(ES8388_CHIPPOWER, 0x00);
    }
    if (mode == ES_MODE_ADC || mode == ES_MODE_DAC_ADC || mode == ES_MODE_LINE)
    {
        reg_write(ES8388_ADCPOWER, 0x00);
    }
    if (mode == ES_MODE_DAC || mode == ES_MODE_DAC_ADC || mode == ES_MODE_LINE)
    {
        reg_write(ES8388_DACPOWER, 0x3c);
        es8388_set_voice_mute(0);
    }

    return HAL_OK;
}

HAL_StatusTypeDef es8388_stop(enum es8388_mode mode)
{
    if (mode == ES_MODE_LINE)
    {
        reg_write(ES8388_DACCONTROL21, 0x80);
        reg_write(ES8388_DACCONTROL16, 0x00);
        reg_write(ES8388_DACCONTROL17, 0x90);
        reg_write(ES8388_DACCONTROL20, 0x90);
        return HAL_OK;
    }
    if (mode == ES_MODE_DAC || mode == ES_MODE_DAC_ADC)
    {
        reg_write(ES8388_DACPOWER, 0x00);
        es8388_set_voice_mute(1);
    }
    if (mode == ES_MODE_ADC || mode == ES_MODE_DAC_ADC)
    {
        reg_write(ES8388_ADCPOWER, 0xFF);
    }
    if (mode == ES_MODE_DAC_ADC)
    {
        reg_write(ES8388_DACCONTROL21, 0x9C);
    }

    return HAL_OK;
}

HAL_StatusTypeDef es8388_fmt_set(enum es8388_mode mode, enum es8388_format fmt)
{
    uint8_t reg = 0;

    if (mode == ES_MODE_ADC || mode == ES_MODE_DAC_ADC)
    {
        reg = reg_read(ES8388_ADCCONTROL4);
        reg = reg & 0xfc;
        reg_write(ES8388_ADCCONTROL4, reg | fmt);
    }
    if (mode == ES_MODE_DAC || mode == ES_MODE_DAC_ADC)
    {
        reg = reg_read(ES8388_DACCONTROL1);
        reg = reg & 0xf9;
        reg_write(ES8388_DACCONTROL1, reg | (fmt << 1));
    }

    return HAL_OK;
}

void es8388_volume_set(uint8_t volume)
{
    uint32_t real_vol = 0;
    volume = 100 - volume;
    if (volume > 100)
        volume = 100;

    real_vol = 192 * volume / 100;

    reg_write(ES8388_DACCONTROL4, (uint8_t)real_vol);
    reg_write(ES8388_DACCONTROL5, (uint8_t)real_vol);
}

uint8_t es8388_volume_get(void)
{
    uint8_t volume;

    volume = reg_read(ES8388_DACCONTROL24);
    if (volume == 0xff)
    {
        volume = 0;
    }
    else
    {
        volume *= 3;
        if (volume == 99)
            volume = 100;
    }

    return volume;
}

void es8388_pa_power(uint8_t enable)
{
    if (es_dev.pa_gpio_port != NULL)
    {
        if (enable)
        {
            HAL_GPIO_WritePin(es_dev.pa_gpio_port, es_dev.pa_gpio_pin, GPIO_PIN_SET);
        }
        else
        {
            HAL_GPIO_WritePin(es_dev.pa_gpio_port, es_dev.pa_gpio_pin, GPIO_PIN_RESET);
        }
    }
}

void estest(void)
{
	// 1. Microphone Input Setup
	    reg_write(ES8388_ADCCONTROL1, 0xAA);   // PGA Gain (Sensitivity)
	    reg_write(ES8388_ADCCONTROL2, 0x10);   // Use Onboard Mic
	    reg_write(ES8388_ADCCONTROL3, 0x00);   // Disable HPF for better vocal response

	    // 2. ADC Digital Volume (0x00 is max gain)
	    reg_write(ES8388_ADCCONTROL8, 0x00);
	    reg_write(ES8388_ADCCONTROL9, 0x00);

	    // 3. Signal Path Routing (Bypass mode)
	    reg_write(ES8388_DACCONTROL16, 0x1B);  // LMIXSEL RMIXSEL
	    reg_write(ES8388_DACCONTROL17, 0x40);  // LI2LOVOL

	    // 4. Final Output Volume
	    // Lower number = Louder, Higher number = Quieter
	    uint8_t master_volume = 36;
	    reg_write(ES8388_DACCONTROL24, master_volume);
	    reg_write(ES8388_DACCONTROL25, master_volume);
}
