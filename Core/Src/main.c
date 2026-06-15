/* USER CODE BEGIN Header */
/*
@file           : main.c
@brief          : Project-Based Final Exam - Personal MP3 Player
: Mindanao State University - IIT (CCS / CA)
*/
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "Song_def.h"
#include "drv_lcd.h"
#include <math.h>
#include "drv_es8388.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STOPPED,
    PLAYING
} PlaybackState;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NUM_SONGS 8
#define AUDIO_BUFFER_SIZE 512
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c2;
I2S_HandleTypeDef hi2s3;
DMA_HandleTypeDef hdma_spi3_tx;
TIM_HandleTypeDef htim3;
UART_HandleTypeDef huart1;
SRAM_HandleTypeDef hsram1;

/* Thread and Mutex Handles */
osThreadId lcdLedsTaskHandle;
osThreadId buttonsTaskHandle;
osThreadId volumeTaskHandle;
osMutexId lcdMutexHandle;

/* USER CODE BEGIN PV */
uint16_t audio_sample_buffer[AUDIO_BUFFER_SIZE];
volatile float active_tone_freq = 0.0f;
volatile float phase_accumulator = 0.0f;
const Song* track_list[NUM_SONGS] = {&song0, &song1, &song2, &song3, &song4, &song5, &song6, &song7};
volatile int active_track_idx = 0;
volatile int pending_track_idx = 0;
volatile int note_counter = 0;
volatile PlaybackState player_status = STOPPED;
volatile bool awaiting_confirm = false;
volatile uint32_t confirm_timeout_tick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_FSMC_Init(void);
static void MX_I2C2_Init(void);
static void MX_I2S3_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_ADC1_Init(void);

/* Project Specific Thread Functions */
void update_lcd_leds_thread(void const * argument);
void polling_buttons(void const * argument);
void adjust_volume(void const * argument);

/* USER CODE BEGIN PFP */
void Fill_Audio_Buffer(uint16_t offset, uint16_t length)
{
    float phase_increment = 0.0f;
    if (active_tone_freq > 0.0f) {
        phase_increment = (2.0f * 3.1415926535f * active_tone_freq) / 44100.0f;
    }

    for (uint16_t i = 0; i < length; i += 2)
    {
        int16_t audio_sample = 0;
        if (active_tone_freq > 0.0f) {
            audio_sample = (int16_t)(sinf(phase_accumulator) * 8192.0f);
            phase_accumulator += phase_increment;
            if (phase_accumulator >= 2.0f * 3.1415926535f) {
                phase_accumulator -= 2.0f * 3.1415926535f;
            }
        } else {
            phase_accumulator = 0.0f;
        }

        audio_sample_buffer[offset + i]     = (uint16_t)audio_sample;
        audio_sample_buffer[offset + i + 1] = (uint16_t)audio_sample;
    }
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
    Fill_Audio_Buffer(0, AUDIO_BUFFER_SIZE / 2);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
    Fill_Audio_Buffer(AUDIO_BUFFER_SIZE / 2, AUDIO_BUFFER_SIZE / 2);
}
/* USER CODE END PFP */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_FSMC_Init();
    MX_I2C2_Init();
    MX_I2S3_Init();
    MX_TIM3_Init();
    MX_USART1_UART_Init();
    MX_ADC1_Init();

    /* USER CODE BEGIN 2 */
    drv_lcd_init();
    lcd_clear(BLACK);
    lcd_set_color(BLACK, WHITE);
    es8388_init(&hi2c2, NULL, 0);
    es8388_start(ES_MODE_DAC_ADC);
    es8388_volume_set(100);

    hdma_spi3_tx.Init.Mode = DMA_CIRCULAR;
    hdma_spi3_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    hdma_spi3_tx.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_spi3_tx.Init.PeriphBurst = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_spi3_tx) != HAL_OK) {
        Error_Handler();
    }

    char* instructions = "\r\n=== Personal MP3 Player Instructions ===\r\n"
                         "1. Hold BTN2, BTN3, BTN4 to select song in binary.\r\n"
                         "2. Press BTN1 while holding to select.\r\n"
                         "3. Press BTN1 again within 5s to CONFIRM.\r\n"
                         "4. Press Play/Pause (PG0) anytime to Play/Stop.\r\n"
                         "=========================================\r\n";
    HAL_UART_Transmit(&huart1, (uint8_t*)instructions, strlen(instructions), 1000);
    /* USER CODE END 2 */

    osMutexDef(lcdMutex);
    lcdMutexHandle = osMutexCreate(osMutex(lcdMutex));

    osThreadDef(lcdLedsTask, update_lcd_leds_thread, osPriorityNormal, 0, 256);
    lcdLedsTaskHandle = osThreadCreate(osThread(lcdLedsTask), NULL);

    osThreadDef(buttonsTask, polling_buttons, osPriorityNormal, 0, 128);
    buttonsTaskHandle = osThreadCreate(osThread(buttonsTask), NULL);

    osThreadDef(volumeTask, adjust_volume, osPriorityNormal, 0, 128);
    volumeTaskHandle = osThreadCreate(osThread(volumeTask), NULL);

    osKernelStart();

    while (1)
    {
        HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    }
}

/**
  * @brief Thread 1: Updates LCD, RGB LEDs, and handles Music Playback timing
  */
void update_lcd_leds_thread(void const * argument)
{
    HAL_I2S_Transmit_DMA(&hi2s3, audio_sample_buffer, AUDIO_BUFFER_SIZE);

    for(;;)
    {
        /* 1. Sync RGB Diagnostic LEDs */
        if (awaiting_confirm) {
            HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_RESET);
        } else if (player_status == PLAYING) {
            HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_RESET);
        } else {
            HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_RESET);
        }

        /* 2. Update LCD UI Safely */
        if (osMutexWait(lcdMutexHandle, 10) == osOK)
        {
            char line1[32];
            char line2[32];

            if (awaiting_confirm) {
                snprintf(line1, sizeof(line1),  "CONFIRM TRACK?   ");
                snprintf(line2, sizeof(line2),  "Sel: %-12s ", track_list[pending_track_idx]->name1);
            } else {
                snprintf(line1, sizeof(line1),  "Status: %s   ", (player_status == PLAYING) ? "PLAYING " : "STOPPED ");
                snprintf(line2, sizeof(line2),  "Song %d: %-12s ", active_track_idx + 1, track_list[active_track_idx]->name1);
            }

            lcd_show_string(10, 20, 16, line1);
            lcd_show_string(10, 50, 16, line2);
            osMutexRelease(lcdMutexHandle);
        }

        /* 3. Playback Logic Engine */
        if (player_status == PLAYING && !awaiting_confirm)
        {
            const Song* current_track = track_list[active_track_idx];
            uint32_t milliseconds_per_beat = 60000 / current_track->tempo;

            if (note_counter < current_track->length)
            {
                active_tone_freq = current_track->note[note_counter];

                uint32_t note_duration_ms = (uint32_t)(current_track->beat[note_counter] * milliseconds_per_beat);
                uint32_t delay_remaining = (note_duration_ms > 30) ? (note_duration_ms - 30) : 1;

                while(delay_remaining > 0 && player_status == PLAYING && !awaiting_confirm) {
                    uint32_t delay_step = (delay_remaining > 15) ? 15 : delay_remaining;
                    osDelay(delay_step);
                    delay_remaining -= delay_step;
                }

                if (player_status == PLAYING && !awaiting_confirm) {
                    active_tone_freq = 0.0f;
                    osDelay(30);
                    note_counter++;
                } else {
                    active_tone_freq = 0.0f;
                }
            }
            else
            {
                player_status = STOPPED;
                active_tone_freq = 0.0f;
                note_counter = 0;
            }
        }
        else
        {
            active_tone_freq = 0.0f;
            osDelay(50);
        }
    }
}

/**
  * @brief Thread 2: Polls Input Channels and implements 5-second Confirmation Window
  */
void polling_buttons(void const * argument)
{
    bool last_btn1_state = false;
    bool last_user_btn_state = false;

    for(;;)
    {
        bool btn2_pressed = (HAL_GPIO_ReadPin(BTN2_GPIO_Port, BTN2_Pin) == GPIO_PIN_RESET);
        bool btn3_pressed = (HAL_GPIO_ReadPin(BTN3_GPIO_Port, BTN3_Pin) == GPIO_PIN_RESET);
        bool btn4_pressed = (HAL_GPIO_ReadPin(BTN4_GPIO_Port, BTN4_Pin) == GPIO_PIN_RESET);
        int binary_selection = (btn2_pressed ? 1 : 0) | (btn3_pressed ? 2 : 0) | (btn4_pressed ? 4 : 0);

        bool btn1_pressed = (HAL_GPIO_ReadPin(BTN1_GPIO_Port, BTN1_Pin) == GPIO_PIN_RESET);
        bool user_btn_pressed = (HAL_GPIO_ReadPin(USER_BTN_GPIO_Port, USER_BTN_Pin) == GPIO_PIN_RESET);

        if (btn1_pressed && !last_btn1_state)
        {
            if (!awaiting_confirm)
            {
                pending_track_idx = binary_selection % NUM_SONGS;
                awaiting_confirm = true;
                confirm_timeout_tick = osKernelSysTick() + 5000;
            }
            else
            {
                active_track_idx = pending_track_idx;
                note_counter = 0;
                player_status = PLAYING;
                awaiting_confirm = false;
            }
        }
        last_btn1_state = btn1_pressed;

        if (awaiting_confirm && (osKernelSysTick() > confirm_timeout_tick))
        {
            awaiting_confirm = false;
        }

        if (user_btn_pressed && !last_user_btn_state)
        {
            if (player_status == PLAYING) {
                player_status = STOPPED;
                active_tone_freq = 0.0f;
            } else {
                player_status = PLAYING;
                note_counter = 0;
            }
        }
        last_user_btn_state = user_btn_pressed;

        osDelay(40);
    }
}

/**
  * @brief Thread 3: Monitors ADC and protects from zero-volume muting
  */
void adjust_volume(void const * argument)
{
    for(;;)
    {
        // Start the ADC conversion for the Potentiometer on PA1
        HAL_ADC_Start(&hadc1);

        // Wait for the conversion to finish (10ms timeout)
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        {
            // Read the 12-bit analog value (0 to 4095)
            uint32_t potentiometer_raw = HAL_ADC_GetValue(&hadc1);

            // Scale the 0-4095 raw value directly to a 0-100 volume percentage
            uint8_t calculated_volume = (uint8_t)((potentiometer_raw * 100) / 4095);

            // Apply the volume to the audio codec
            es8388_volume_set(calculated_volume);
        }
        HAL_ADC_Stop(&hadc1);

        // Wait 150ms before polling again to prevent flooding the I2C bus
        osDelay(150);
    }
}

/* System Clock & Peripheral Initialization Functions */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 168;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_ADC1_Init(void) {
    ADC_ChannelConfTypeDef sConfig = {0};
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) { Error_Handler(); }
    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }
}

static void MX_I2C2_Init(void) {
    hi2c2.Instance = I2C2;
    hi2c2.Init.ClockSpeed = 100000;
    hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c2) != HAL_OK) { Error_Handler(); }
}

static void MX_I2S3_Init(void) {
    hi2s3.Instance = SPI3;
    hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
    hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
    hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_44K;
    hi2s3.Init.CPOL = I2S_CPOL_LOW;
    hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
    hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
    if (HAL_I2S_Init(&hi2s3) != HAL_OK) { Error_Handler(); }
}

static void MX_TIM3_Init(void) {
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 83;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 999;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) { Error_Handler(); }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK) { Error_Handler(); }
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK) { Error_Handler(); }
    HAL_TIM_MspPostInit(&htim3);
}

static void MX_USART1_UART_Init(void) {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK) { Error_Handler(); }
}

static void MX_DMA_Init(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    GPIO_InitStruct.Pin = LCD_BL_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LCD_RST_Pin;
    HAL_GPIO_Init(LCD_RST_GPIO_Port, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOF, LCD_BL_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_SET);

    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Pin = BTN1_Pin;   HAL_GPIO_Init(BTN1_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = BTN2_Pin;   HAL_GPIO_Init(BTN2_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = BTN3_Pin;   HAL_GPIO_Init(BTN3_GPIO_Port, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = BTN4_Pin;   HAL_GPIO_Init(BTN4_GPIO_Port, &GPIO_InitStruct);

    /* Reconfigured Play/Pause Button on PG0 */
    GPIO_InitStruct.Pin = USER_BTN_Pin;
    HAL_GPIO_Init(USER_BTN_GPIO_Port, &GPIO_InitStruct);

    /* Reconfigured Diagnostics Output LEDs grouped safely on Port G */
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Pin = LED_R_Pin | LED_G_Pin | LED_B_Pin;
    HAL_GPIO_Init(LED_R_GPIO_Port, &GPIO_InitStruct);
}

static void MX_FSMC_Init(void) {
    FSMC_NORSRAM_TimingTypeDef Timing = {0};
    hsram1.Instance = FSMC_NORSRAM_DEVICE;
    hsram1.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    hsram1.Init.NSBank = FSMC_NORSRAM_BANK1;
    hsram1.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    hsram1.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    hsram1.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    hsram1.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    hsram1.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    hsram1.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    hsram1.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    hsram1.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    hsram1.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    hsram1.Init.ExtendedMode = FSMC_EXTENDED_MODE_DISABLE;
    hsram1.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    hsram1.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
    hsram1.Init.PageSize = FSMC_PAGE_SIZE_NONE;
    Timing.AddressSetupTime = 15;
    Timing.AddressHoldTime = 15;
    Timing.DataSetupTime = 255;
    Timing.BusTurnAroundDuration = 15;
    Timing.CLKDivision = 16;
    Timing.DataLatency = 17;
    Timing.AccessMode = FSMC_ACCESS_MODE_A;
    if (HAL_SRAM_Init(&hsram1, &Timing, NULL) != HAL_OK) {
        Error_Handler();
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM1) {
        HAL_IncTick();
    }
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
