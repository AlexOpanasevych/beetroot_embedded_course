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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PIN_SET(GPIO_PORT, PIN)    ((GPIO_PORT)->BSRR = (1U << (PIN)))
#define PIN_RESET(GPIO_PORT, PIN)  ((GPIO_PORT)->BSRR = (1U << ((PIN) + 16u)))
#define PIN_STATE(GPIO_PORT, PIN)  (((GPIO_PORT)->IDR & (1U << (PIN))) != 0)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define G_LED_PORT  GPIOC
const uint16_t G_LED_PIN = 13;

#define G_BUTTON_PORT GPIOA
const uint16_t G_BUTTON_PIN = 1;

void InitGPIOS (void) {
  RCC->AHB1ENR |= (uint32_t) (0b00000101); // Enable clock for port A and C

  ////////////////////////////////////////////////////
  G_LED_PORT->MODER &= ~(3 << (G_LED_PIN * 2));
  G_LED_PORT->MODER |= 1 << (G_LED_PIN * 2);  // Set PC13 as output

  G_LED_PORT->OTYPER &= ~(1 << G_LED_PIN);
  G_LED_PORT->OSPEEDR &= ~(3 << (G_LED_PIN * 2));     //Low speed;
  G_LED_PORT->PUPDR &= ~(3 << (G_LED_PIN * 2));     // Neither pull-up, nor pull-up;

  ////////////////////////////////////////////////////
  G_BUTTON_PORT->MODER &= ~(3 << (G_BUTTON_PIN * 2)); // Set PA1 as input
  G_BUTTON_PORT->PUPDR &= ~(3 << (G_BUTTON_PIN * 2));
  G_BUTTON_PORT->PUPDR |= 1 << (G_BUTTON_PIN * 2);     // Pull-up;
}

void Delay_Ticks (uint32_t ticks) {
  const uint32_t start = SysTick->VAL;
  const uint32_t load = SysTick->LOAD;
  uint32_t elapsed = 0;
  uint32_t last = start;

  while (elapsed < ticks) {
    uint32_t now = SysTick->VAL;
    if (now <= last) {
      elapsed += (last - now); // Normal down-counting
    }
    else {
      elapsed += (last + (load - now)); // Counter wrapped around
    }
    last = now;
  }
}

void Delay (uint32_t i_delay_ms) {
  Delay_Ticks (i_delay_ms * 16000);
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
  HAL_Delay (300);
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  //MX_GPIO_Init();
  /* USER CODE BEGIN 2 */
  InitGPIOS ();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t delay_ms = 500;
  while (1) {
    /* USER CODE END WHILE */
    uint8_t is_pressed = !PIN_STATE(G_BUTTON_PORT, G_BUTTON_PIN);
    delay_ms = is_pressed ? 150 : 500;

    PIN_SET(G_LED_PORT, G_LED_PIN);
    Delay (delay_ms);
    PIN_RESET(G_LED_PORT, G_LED_PIN);
    Delay (delay_ms);
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
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq ();
  while (1) {
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
