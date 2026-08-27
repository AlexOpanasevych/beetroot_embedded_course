/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
    uint8_t hour;
    uint8_t day;
    uint8_t month;
} DateTime_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define EEPROM_ADDR (0x50 << 1)  // Зсунута 7-бітна адреса для HAL
#define RTC_ADDR    (0x68 << 1)  // DS3231, зсунута 7-бітна адреса

// Розмір одного запису в EEPROM: [освітленість %][місяць][день][година] = 4 байти
#define RECORD_SIZE 4
#define LOG_START_ADDR 0x0002
#define LOG_END_ADDR   0x0FFF

// 1 година в мілісекундах (60 хвилин * 60 секунд * 1000)
// ⚠️ ПОРАДА: Для перевірки роботи на уроці змініть це значення на 5000 (5 секунд)
#define LOG_INTERVAL 3600000
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
// Змінні для керування логуванням
uint32_t last_log_time = 0;
uint16_t current_address = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);

/* USER CODE BEGIN PFP */
uint8_t EEPROM_ReadByte(uint16_t mem_address);
void EEPROM_WriteByte(uint16_t mem_address, uint8_t data);
void EEPROM_WriteRecord(uint16_t mem_address, const uint8_t *data, uint8_t len);
uint16_t Get_Next_Address(void);
void Save_Next_Address(uint16_t ptr);

static uint8_t BCD2DEC(uint8_t bcd);
static uint8_t DEC2BCD(uint8_t dec);
void RTC_GetDateTime(DateTime_t *dt);
void RTC_SetDateTime(uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);

void UART_Printf(const char *fmt, ...);
void EEPROM_DumpTable(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// Читання 1 байта
uint8_t EEPROM_ReadByte(uint16_t mem_address) {
    uint8_t data = 0;
    HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT, &data, 1, 100);
    return data;
}

// Запис 1 байта
void EEPROM_WriteByte(uint16_t mem_address, uint8_t data) {
    HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT, &data, 1, 100);
    HAL_Delay(5); // Затримка 5 мс для фізичного запису в EEPROM (обов'язково!)
}

// Запис декількох байтів одного запису (світло + timestamp)
void EEPROM_WriteRecord(uint16_t mem_address, const uint8_t *data, uint8_t len) {
    HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDR, mem_address, I2C_MEMADD_SIZE_16BIT, (uint8_t *)data, len, 100);
    HAL_Delay(5);
}

// Читання вказівника (адреси наступного запису) з перших двох байтів пам'яті
uint16_t Get_Next_Address(void) {
    uint8_t buf[2];
    HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDR, 0x0000, I2C_MEMADD_SIZE_16BIT, buf, 2, 100);

    // Об'єднання за стандартом Big-Endian
    uint16_t ptr = ((uint16_t)buf[0] << 8) | buf[1];

    // Якщо пам'ять чиста (0xFFFF) або адреса некоректна, починаємо з початку логів
    if (ptr < LOG_START_ADDR || ptr > LOG_END_ADDR) {
        return LOG_START_ADDR;
    }
    return ptr;
}

// Збереження нового вказівника
void Save_Next_Address(uint16_t ptr) {
    uint8_t buf[2];
    buf[0] = (ptr >> 8) & 0xFF; // MSB (старший байт)
    buf[1] = ptr & 0xFF;        // LSB (молодший байт)

    HAL_I2C_Mem_Write(&hi2c1, EEPROM_ADDR, 0x0000, I2C_MEMADD_SIZE_16BIT, buf, 2, 100);
    HAL_Delay(5);
}

// --- DS3231 RTC (та сама шина I2C1, що й EEPROM) ---

static uint8_t BCD2DEC(uint8_t bcd) {
    return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

static uint8_t DEC2BCD(uint8_t dec) {
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

// Зчитує поточний час/дату з DS3231 (формат мм:дд:гг = місяць:день:година)
void RTC_GetDateTime(DateTime_t *dt) {
    uint8_t buf[7];
    // Регістри DS3231: 0x00 сек, 0x01 хв, 0x02 год, 0x03 день тижня, 0x04 дата, 0x05 місяць, 0x06 рік
    HAL_I2C_Mem_Read(&hi2c1, RTC_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, buf, 7, 100);

    dt->hour  = BCD2DEC(buf[2] & 0x3F); // маскуємо біт режиму 12/24 год (працюємо в 24-год режимі)
    dt->day   = BCD2DEC(buf[4] & 0x3F);
    dt->month = BCD2DEC(buf[5] & 0x1F); // маскуємо біт століття
}

// Одноразове встановлення часу на DS3231 (викликати один раз, потім можна прибрати виклик —
// модуль тримає час на батарейці CR2032)
void RTC_SetDateTime(uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second) {
    uint8_t buf[7];
    buf[0] = DEC2BCD(second);
    buf[1] = DEC2BCD(minute);
    buf[2] = DEC2BCD(hour);   // 24-год режим (біт6 = 0)
    buf[3] = 1;               // день тижня, довільно (не використовується)
    buf[4] = DEC2BCD(day);
    buf[5] = DEC2BCD(month);
    buf[6] = 0;                // рік, довільно (не використовується у форматі мм:дд:гг)

    HAL_I2C_Mem_Write(&hi2c1, RTC_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, buf, 7, 100);
}

// --- Вивід у монітор порту через USB-TTL конвертер ---

void UART_Printf(const char *fmt, ...) {
    char buf[96];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, HAL_MAX_DELAY);
    }
}

// Зчитує всі збережені записи з EEPROM і виводить їх таблицею в монітор порту
void EEPROM_DumpTable(void) {
    UART_Printf("\r\n=== Log osvitlennya (EEPROM) ===\r\n");
    UART_Printf("MM:DD:HH  Svitlo,%%\r\n");

    uint8_t rec[RECORD_SIZE];
    for (uint16_t addr = LOG_START_ADDR; addr + RECORD_SIZE <= current_address; addr += RECORD_SIZE) {
        HAL_I2C_Mem_Read(&hi2c1, EEPROM_ADDR, addr, I2C_MEMADD_SIZE_16BIT, rec, RECORD_SIZE, 100);

        uint8_t light_percent = rec[0];
        uint8_t month = rec[1];
        uint8_t day   = rec[2];
        uint8_t hour  = rec[3];

        UART_Printf("%02u:%02u:%02u   %3u%%\r\n", month, day, hour, light_percent);
    }
    UART_Printf("=== Kinets logu ===\r\n\r\n");
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
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  // Одноразове встановлення часу на DS3231 (розкоментувати, прошити один раз,
  // потім знову закоментувати — час зберігається на батарейці модуля):
  // RTC_SetDateTime(8, 20, 14, 30, 0); // місяць=8, день=20, година=14, хв=30, сек=0

  // Зчитуємо адресу з пам'яті під час старту пристрою
  current_address = Get_Next_Address();

  // Домашнє завдання п.4: одразу при старті зчитуємо все, що вже накопичено
  // в EEPROM, і виводимо таблицею в монітор порту через USB-TTL конвертер
  EEPROM_DumpTable();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // Неблокуюча перевірка таймера
    if (HAL_GetTick() - last_log_time >= LOG_INTERVAL || last_log_time == 0) {

        // 1. Запуск вимірювання АЦП
        HAL_ADC_Start(&hadc1);

        // Чекаємо завершення конвертації (таймаут 10 мс)
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {

            // Отримуємо сире 12-бітне значення (0...4095)
            uint32_t adc_value = HAL_ADC_GetValue(&hadc1);

            // 2. Математика: переводимо у відсотки (0...100)
            uint8_t light_percent = (adc_value * 100) / 4095;

            // 2b. Зчитуємо поточний час/дату з RTC (формат мм:дд:гг)
            DateTime_t now;
            RTC_GetDateTime(&now);

            // 3. Записуємо в EEPROM (світло + timestamp), якщо є вільне місце
            if (current_address + RECORD_SIZE <= LOG_END_ADDR) {
                uint8_t record[RECORD_SIZE] = { light_percent, now.month, now.day, now.hour };
                EEPROM_WriteRecord(current_address, record, RECORD_SIZE);

                // Виводимо той самий запис одразу в монітор порту
                UART_Printf("[%02u:%02u:%02u] Svitlo: %u%%\r\n", now.month, now.day, now.hour, light_percent);

                // Зсуваємо вказівник і зберігаємо його
                current_address += RECORD_SIZE;
                Save_Next_Address(current_address);
            } else {
                UART_Printf("EEPROM povna, zapys prypyneno.\r\n");
            }
        }
        HAL_ADC_Stop(&hadc1); // Зупиняємо АЦП до наступного разу

        // Оновлюємо мітку часу
        last_log_time = HAL_GetTick();
    }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // Тут можна додати інший код, який має виконуватись постійно,
    // наприклад, перевірка кнопок або мигання світлодіодом.
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
  * @brief ADC1 Initialization Function
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

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
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
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
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
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART1 Initialization Function (для USB-TTL конвертера, PA9=TX, PA10=RX)
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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

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
