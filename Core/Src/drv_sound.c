#include "drv_sound.h"
#include "drv_es8388.h"
#include "stm32f4xx_hal.h"

static I2S_HandleTypeDef I2S3_Handler = {0};
static DMA_HandleTypeDef I2S3_TXDMA_Handler = {0};
volatile uint8_t dma_tx_complete_flag = 0;

/*
 * I2S Prescaler Calculation Table:
 * Format: { SampleRate/10, PLLI2SN, PLLI2SR, I2SDIV, ODD }
 */
const uint16_t I2S_PSC_TBL[][5] =
{
    {   800, 256, 5, 12, 1 },   /* 8Khz */
    {  1102, 429, 4, 19, 0 },   /* 11.025Khz */
    {  1600, 213, 2, 13, 0 },   /* 16Khz */
    {  2205, 429, 4,  9, 1 },   /* 22.05Khz */
    {  3200, 213, 2,  6, 1 },   /* 32Khz */
    {  4410, 271, 2,  6, 0 },   /* 44.1Khz */
    {  4800, 258, 3,  3, 1 },   /* 48Khz */
    {  8820, 316, 2,  3, 1 },   /* 88.2Khz */
    {  9600, 344, 2,  3, 1 },   /* 96Khz */
    { 17640, 361, 2,  2, 0 },   /* 176.4Khz */
    { 19200, 393, 2,  2, 0 },   /* 192Khz */
};

/**
 * Bare-Metal Hardware MSP Init 
 * Configures physical pin-routing and alternate functions for SPI3/I2S3 signals.
 * Assumes standard pins: PC7 (MCK), PC10 (CK), PC12 (SD), and PA15 (WS).
 */
void HAL_I2S_MspInit(I2S_HandleTypeDef *hi2s)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    if(hi2s->Instance == SPI3)
    {
        /* Enable Peripheral Clocks */
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE(); 
        __HAL_RCC_SPI3_CLK_ENABLE();

        /* Route Audio Clock, Bit Clock, and Data Lines (PC7, PC10, PC12) */
        GPIO_InitStruct.Pin = GPIO_PIN_7 | GPIO_PIN_10 | GPIO_PIN_12;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        /* Route Word Select / LRCK Line (PA15) */
        GPIO_InitStruct.Pin = GPIO_PIN_15; 
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

void BareMetal_Audio_Init(void)
{
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /* 1. Setup Audio Engine Peripheral Core Clocks */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2S;
    PeriphClkInitStruct.PLLI2S.PLLI2SN = 192;
    PeriphClkInitStruct.PLLI2S.PLLI2SR = 2;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);

    HAL_I2S_DeInit(&I2S3_Handler);

    /* 2. Configure Local STM32 I2S3 Peripheral Parameters */
    I2S3_Handler.Instance = SPI3;
    I2S3_Handler.Init.Mode = I2S_MODE_MASTER_TX;
    I2S3_Handler.Init.Standard = I2S_STANDARD_PHILIPS;
    I2S3_Handler.Init.DataFormat = I2S_DATAFORMAT_16B;
    I2S3_Handler.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    I2S3_Handler.Init.AudioFreq = I2S_AUDIOFREQ_44K;
    I2S3_Handler.Init.CPOL = I2S_CPOL_LOW;
    I2S3_Handler.Init.ClockSource = I2S_CLOCK_PLL;
    I2S3_Handler.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_ENABLE;
    HAL_I2S_Init(&I2S3_Handler); // Calls HAL_I2S_MspInit() automatically internally

    SET_BIT(I2S3_Handler.Instance->CR2, SPI_CR2_TXDMAEN);
    __HAL_I2S_ENABLE(&I2S3_Handler);

    /* 3. Configure DMA1 Stream 7 Pipeline mapped to I2S Transmission */
    __HAL_RCC_DMA1_CLK_ENABLE();
    I2S3_TXDMA_Handler.Instance = DMA1_Stream7;
    I2S3_TXDMA_Handler.Init.Channel = DMA_CHANNEL_0;
    I2S3_TXDMA_Handler.Init.Direction = DMA_MEMORY_TO_PERIPH;
    I2S3_TXDMA_Handler.Init.PeriphInc = DMA_PINC_DISABLE;
    I2S3_TXDMA_Handler.Init.MemInc = DMA_MINC_ENABLE;
    I2S3_TXDMA_Handler.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    I2S3_TXDMA_Handler.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    I2S3_TXDMA_Handler.Init.Mode = DMA_CIRCULAR; 
    I2S3_TXDMA_Handler.Init.Priority = DMA_PRIORITY_HIGH;
    I2S3_TXDMA_Handler.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    __HAL_LINKDMA(&I2S3_Handler, hdmatx, I2S3_TXDMA_Handler);
    HAL_DMA_DeInit(&I2S3_TXDMA_Handler);
    HAL_DMA_Init(&I2S3_TXDMA_Handler);

    __HAL_DMA_DISABLE(&I2S3_TXDMA_Handler);  
    __HAL_DMA_ENABLE_IT(&I2S3_TXDMA_Handler, DMA_IT_TC); 
    
    /* Fixed macro compilation mapping for cleaning current DMA Stream flags */
    __HAL_DMA_CLEAR_FLAG(&I2S3_TXDMA_Handler, __HAL_DMA_GET_TC_FLAG_INDEX(&I2S3_TXDMA_Handler)); 

    /* 4. Enable Vector Interrupt Nested Controllers */
    HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 1, 0); 
    HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);

    /* 5. Trigger Target Component Setup across Control Pipeline (I2C) */
    BareMetal_ES8388_Init();
    es8388_volume_set(65); 
}

void BareMetal_Audio_SetFrequency(uint32_t samplerate)
{
    uint8_t i = 0; 
    uint32_t tempreg = 0;
    RCC_PeriphCLKInitTypeDef rcc_i2s_clkinit_struct;

    for (i = 0; i < (sizeof(I2S_PSC_TBL) / sizeof(I2S_PSC_TBL[0])); i++)
    {
        if ((samplerate / 10) == I2S_PSC_TBL[i][0])
        {
            break;
        }
    }
    
    if (i < (sizeof(I2S_PSC_TBL) / sizeof(I2S_PSC_TBL[0])))
    {
        rcc_i2s_clkinit_struct.PeriphClockSelection = RCC_PERIPHCLK_I2S;
        rcc_i2s_clkinit_struct.PLLI2S.PLLI2SN = (uint32_t)I2S_PSC_TBL[i][1];
        rcc_i2s_clkinit_struct.PLLI2S.PLLI2SR = (uint32_t)I2S_PSC_TBL[i][2];
        HAL_RCCEx_PeriphCLKConfig(&rcc_i2s_clkinit_struct);

        RCC->CR |= (1 << 26); 
        while((RCC->CR & (1 << 27)) == 0); 

        tempreg = I2S_PSC_TBL[i][3] << 0;   
        tempreg |= I2S_PSC_TBL[i][4] << 8;  
        tempreg |= 1 << 9;                  
        I2S3_Handler.Instance->I2SPR = tempreg;
    }
}

void BareMetal_Audio_Start(uint16_t *buffer_ptr, uint16_t size)
{
    es8388_start(ES_MODE_DAC);
    HAL_I2S_Transmit_DMA(&I2S3_Handler, buffer_ptr, size);
}

void BareMetal_Audio_Stop(void)
{
    HAL_I2S_DMAStop(&I2S3_Handler);
    es8388_stop(ES_MODE_DAC);
}

/**
 * High-Speed Minimal Latency Vector Interrupt Vector Handle
 */
void DMA1_Stream7_IRQHandler(void)
{
    if(__HAL_DMA_GET_FLAG(&I2S3_TXDMA_Handler, __HAL_DMA_GET_TC_FLAG_INDEX(&I2S3_TXDMA_Handler)) != RESET)
    {
        __HAL_DMA_CLEAR_FLAG(&I2S3_TXDMA_Handler, __HAL_DMA_GET_TC_FLAG_INDEX(&I2S3_TXDMA_Handler));
        dma_tx_complete_flag = 1;
    }
}