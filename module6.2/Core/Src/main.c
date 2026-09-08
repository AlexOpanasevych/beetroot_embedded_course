/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "cmsis_os.h"

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
UART_HandleTypeDef huart1;

/* Definitions for SensorReaderTask */
osThreadId_t SensorReaderTaskHandle;
const osThreadAttr_t SensorReaderTask_attributes = {
  .name = "SensorReader",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for EventTriggerTask */
osThreadId_t EventTriggerTaskHandle;
const osThreadAttr_t EventTriggerTask_attributes = {
  .name = "EventTrigger",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for WorkerTask (created dynamically by EventTriggerTask) */
const osThreadAttr_t WorkerTask_attributes = {
  .name = "Worker",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for MaintenanceTask */
osThreadId_t MaintenanceTaskHandle;
const osThreadAttr_t MaintenanceTask_attributes = {
  .name = "Maintenance",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityRealtime,
};
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
void StartSensorReaderTask(void *argument);
void StartEventTriggerTask(void *argument);
void StartWorkerTask(void *argument);
void StartMaintenanceTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
// Перенаправлення printf на USART1 (блокуючий режим, для простих навчальних задач цього достатньо).
int _write(int file, char *ptr, int len)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)ptr, len, HAL_MAX_DELAY);
  return len;
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
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of SensorReaderTask */
  SensorReaderTaskHandle = osThreadNew(StartSensorReaderTask, NULL, &SensorReaderTask_attributes);

  /* creation of EventTriggerTask */
  EventTriggerTaskHandle = osThreadNew(StartEventTriggerTask, NULL, &EventTriggerTask_attributes);

  /* creation of MaintenanceTask */
  MaintenanceTaskHandle = osThreadNew(StartMaintenanceTask, NULL, &MaintenanceTask_attributes);

  /* NOTE: WorkerTask is NOT created here. It is created dynamically at
     runtime by EventTriggerTask whenever the simulated event fires, and it
     deletes itself (osThreadExit) once its job is done. */

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 100;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
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
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartSensorReaderTask */
/**
  * @brief  Function implementing the SensorReaderTask thread.
  *         Imitates polling a sensor with a strictly periodic 500 ms rate.
  *         osDelayUntil (not plain osDelay) is used on purpose: it schedules
  *         the next wake-up relative to the *previous* wake-up time, so the
  *         period stays exactly 500 ms regardless of how long the task body
  *         itself takes to run - a plain osDelay(500) would instead measure
  *         500 ms from "when the task finished", letting jitter accumulate.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartSensorReaderTask */
void StartSensorReaderTask(void *argument)
{
  /* USER CODE BEGIN StartSensorReaderTask */
  uint32_t lastWakeTime = osKernelGetTickCount();
  const uint32_t periodTicks = 500;

  /* Infinite loop */
  for(;;)
  {
    printf("[Sensor] Data read...\r\n");

    lastWakeTime += periodTicks;
    osDelayUntil(lastWakeTime);
  }
  /* USER CODE END StartSensorReaderTask */
}

/* USER CODE BEGIN Header_StartEventTriggerTask */
/**
  * @brief Function implementing the EventTriggerTask thread.
  *        Imitates an unpredictable external event: sleeps for 3 s, then
  *        dynamically spawns a WorkerTask to handle the "event" and goes
  *        back to sleep.
  * @param argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartEventTriggerTask */
void StartEventTriggerTask(void *argument)
{
  /* USER CODE BEGIN StartEventTriggerTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(3000);

    osThreadNew(StartWorkerTask, NULL, &WorkerTask_attributes);
  }
  /* USER CODE END StartEventTriggerTask */
}

/* USER CODE BEGIN Header_StartWorkerTask */
/**
  * @brief Function implementing the WorkerTask thread.
  *        Created on demand by EventTriggerTask. Does one unit of work and
  *        then MUST terminate itself with osThreadExit() (CMSIS-RTOS2
  *        equivalent of vTaskDelete(NULL)) - simply returning/falling off
  *        the end of the function is undefined behaviour for a FreeRTOS
  *        task and is not allowed.
  * @param argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartWorkerTask */
void StartWorkerTask(void *argument)
{
  /* USER CODE BEGIN StartWorkerTask */
  printf("[Worker] Processing event...\r\n");

  osThreadExit();
  /* USER CODE END StartWorkerTask */
}

/* USER CODE BEGIN Header_StartMaintenanceTask */
/**
  * @brief Function implementing the MaintenanceTask thread.
  *        Highest (Realtime) priority. Once every 10 s it puts the device
  *        into a simulated service mode: pauses SensorReaderTask, waits 2 s,
  *        then resumes it.
  * @param argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartMaintenanceTask */
void StartMaintenanceTask(void *argument)
{
  /* USER CODE BEGIN StartMaintenanceTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(10000);

    osThreadSuspend(SensorReaderTaskHandle);

    printf("[Maintenance] System paused for 2 seconds.\r\n");
    osDelay(2000);

    osThreadResume(SensorReaderTaskHandle);
  }
  /* USER CODE END StartMaintenanceTask */
}

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
