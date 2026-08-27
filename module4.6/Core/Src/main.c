/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * Module 4.6 -- mini oscilloscope.
  *
  * ADC1_IN1 (PA1) is sampled continuously through circular DMA into
  * scope_buffer[]. The two DMA half/full-transfer callbacks (same
  * double-buffering trick as the module 4.6 light-sensor exercise this builds
  * on) fold each half of the buffer into running min/max/sum accumulators
  * while DMA keeps filling the other half, so the CPU never stalls waiting on
  * the ADC. Every REPORT_PERIOD_MS the main loop snapshots those accumulators,
  * converts raw 12-bit codes to millivolts (VDDA = 3.3 V full scale) and
  * prints Vmin / Vmax / Vpp / Vavg over USART1 (115200 8N1).
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
#define SCOPE_BUFFER_SIZE   256U   /* circular DMA capture buffer, in samples */
#define ADC_MAX_VALUE       4095U  /* 12-bit resolution */
#define ADC_VREF_MV         3300U  /* VDDA, in millivolts -- adjust if VDDA != 3.3V */
#define REPORT_PERIOD_MS    200U   /* console refresh rate */

static uint16_t scope_buffer[SCOPE_BUFFER_SIZE];

/* Running statistics, folded in by the DMA half/full-complete callbacks below
 * and drained by the main loop every REPORT_PERIOD_MS. volatile because they
 * are written from interrupt context and read from main(). */
static volatile uint16_t adc_min = 0xFFFFU;
static volatile uint16_t adc_max = 0U;
static volatile uint32_t adc_sum = 0U;
static volatile uint32_t adc_count = 0U;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC1_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
static void Update_Stats(const uint16_t *samples, uint16_t count);
static void Report_Measurements(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Scans one half of scope_buffer and folds it into the running accumulators.
 * Called from DMA ISR context (see HAL_ADC_ConvHalfCpltCallback/ConvCpltCallback
 * below) -- keep it cheap, it must finish well before DMA wraps back into the
 * half it just scanned. */
static void Update_Stats(const uint16_t *samples, uint16_t count)
{
  uint16_t local_min = 0xFFFFU;
  uint16_t local_max = 0U;
  uint32_t local_sum = 0U;

  for (uint16_t i = 0U; i < count; i++)
  {
    uint16_t v = samples[i];
    if (v < local_min) { local_min = v; }
    if (v > local_max) { local_max = v; }
    local_sum += v;
  }

  if (local_min < adc_min) { adc_min = local_min; }
  if (local_max > adc_max) { adc_max = local_max; }
  adc_sum += local_sum;
  adc_count += count;
}

/* Snapshots + resets the running accumulators and prints Vmin/Vmax/Vpp/Vavg.
 * IRQs are briefly disabled around the snapshot so a DMA callback can't tear
 * the read (e.g. update adc_sum but not yet adc_count) while this runs. */
static void Report_Measurements(void)
{
  __disable_irq();
  uint16_t vmin_raw = adc_min;
  uint16_t vmax_raw = adc_max;
  uint32_t sum_raw = adc_sum;
  uint32_t count = adc_count;
  adc_min = 0xFFFFU;
  adc_max = 0U;
  adc_sum = 0U;
  adc_count = 0U;
  __enable_irq();

  if (count == 0U)
  {
    return; /* DMA hasn't completed a half-buffer yet this period */
  }

  /* Millivolts, computed with integer math throughout: newlib-nano (the default
   * STM32CubeIDE libc spec) doesn't link float support into printf/snprintf
   * unless you add -u _printf_float, so %f silently prints garbage otherwise.
   * Splitting into whole/fractional millivolt digits sidesteps that entirely. */
  uint32_t vmin_mv = (vmin_raw * ADC_VREF_MV) / ADC_MAX_VALUE;
  uint32_t vmax_mv = (vmax_raw * ADC_VREF_MV) / ADC_MAX_VALUE;
  uint32_t vavg_mv = ((sum_raw / count) * ADC_VREF_MV) / ADC_MAX_VALUE;
  uint32_t vpp_mv  = vmax_mv - vmin_mv;

  char line[96];
  int len = snprintf(line, sizeof(line),
      "Vmin=%lu.%03luV  Vmax=%lu.%03luV  Vpp=%lu.%03luV  Vavg=%lu.%03luV  (n=%lu)\r\n",
      (unsigned long)(vmin_mv / 1000U), (unsigned long)(vmin_mv % 1000U),
      (unsigned long)(vmax_mv / 1000U), (unsigned long)(vmax_mv % 1000U),
      (unsigned long)(vpp_mv  / 1000U), (unsigned long)(vpp_mv  % 1000U),
      (unsigned long)(vavg_mv / 1000U), (unsigned long)(vavg_mv % 1000U),
      (unsigned long)count);

  if (len > 0)
  {
    HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)len, HAL_MAX_DELAY);
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ADC1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  static const char banner[] =
      "\r\n--- STM32 mini oscilloscope: ADC1_IN1 (PA1), 0-3.3V ---\r\n";
  HAL_UART_Transmit(&huart1, (uint8_t *)banner, sizeof(banner) - 1U, HAL_MAX_DELAY);

  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)scope_buffer, SCOPE_BUFFER_SIZE);

  uint32_t last_report_tick = HAL_GetTick();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    if ((HAL_GetTick() - last_report_tick) >= REPORT_PERIOD_MS)
    {
      last_report_tick = HAL_GetTick();
      Report_Measurements();
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function (scope probe on PA1 / ADC1_IN1)
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment
  * and number of conversion): continuous conversion + continuous DMA requests
  * so the ADC free-runs into scope_buffer without any software restart.
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in
  * the sequencer and its sample time. 3 cycles is the shortest available --
  * chosen for the fastest possible capture rate (oscilloscope-style), at the
  * cost of some accuracy on high-impedance sources.
  */
  sConfig.Channel = SCOPE_ADC_CHANNEL;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief USART1 Initialization Function (console output)
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* DMA half-transfer complete: first half of scope_buffer (indices 0..N/2-1)
 * just finished filling, DMA is now writing the second half -- fold the
 * finished half into the running stats while that happens. */
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    Update_Stats(&scope_buffer[0], SCOPE_BUFFER_SIZE / 2U);
  }
}

/* DMA transfer complete: second half just finished, DMA wraps back to the
 * start of scope_buffer -- fold the second half in while that happens. */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  if (hadc->Instance == ADC1)
  {
    Update_Stats(&scope_buffer[SCOPE_BUFFER_SIZE / 2U], SCOPE_BUFFER_SIZE / 2U);
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
