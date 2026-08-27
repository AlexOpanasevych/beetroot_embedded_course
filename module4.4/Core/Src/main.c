/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * Module 4.4/4.5 mini-project.
  *
  * This board is still the SPI1 hardware-NSS slave from the module 4.3
  * mini-project (see beetroot_embedded_course/miniproject_module4/stm32slave),
  * now extended with:
  *   - BME280 over I2C1 (temperature, humidity, pressure) -- see bme280.c/h
  *   - a light sensor (LDR voltage divider) on ADC1_IN1 (PA1), reported as 0-100 %
  *   - the RTC peripheral (LSI-clocked) for date/time
  *
  * Every sensor snapshot is packed into a custom fixed-length SPI frame and
  * shipped to the ESP32 master (module 4.5) on each SPI transaction. See the
  * frame layout comment above Process_SPI_Request() below -- the ESP32 side
  * unpacks the exact same layout.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bme280.h"
#include <string.h>
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* ----------------------------------------------------------------------------
 * Custom SPI telemetry protocol (STM32 slave -> ESP32 master), module 4.4/4.5.
 *
 * Byte 0 of every frame in both directions is a throwaway dummy: STM32
 * hardware-NSS SPI slave mode is known to unreliably capture/shift out the
 * very first byte right after NSS asserts, so real data starts at byte 1
 * (same quirk documented in the module 4.3 mini-project).
 *
 * REQUEST (ESP32 -> STM32), bytes 1..4 meaningful, rest padding:
 *   [0] dummy
 *   [1] SYNC_BYTE   (0xA5)
 *   [2] CMD_GET_ENV (0x30)
 *   [3] arg         (unused, 0x00)
 *   [4] checksum = frame[1] ^ frame[2] ^ frame[3]
 *   [5..19] reserved (0x00)
 *
 * RESPONSE (STM32 -> ESP32), preloaded by the slave *before* the transaction
 * that carries it (classic SPI-slave one-cycle-behind pattern: the frame you
 * receive during transaction N was prepared while servicing transaction N-1):
 *   [0]  dummy
 *   [1]  ACK_BYTE (0x5A if the request validated, 0x00 otherwise)
 *   [2]  year   (uint8, offset from 2000, e.g. 26 -> 2026)
 *   [3]  month  (1-12)
 *   [4]  day    (1-31)
 *   [5]  hour   (0-23)
 *   [6]  minute (0-59)
 *   [7]  second (0-59)
 *   [8]  temperature_x100 hi byte   (int16, big-endian, degC * 100)
 *   [9]  temperature_x100 lo byte
 *   [10] humidity_x100 hi byte      (uint16, big-endian, %RH * 100)
 *   [11] humidity_x100 lo byte
 *   [12] pressure_x10 hi byte       (uint16, big-endian, hPa * 10)
 *   [13] pressure_x10 lo byte
 *   [14] light_percent (uint8, 0-100)
 *   [15] status bit0=BME280 read ok, bit1=RTC read ok
 *   [16] checksum = XOR of frame[1..15]
 *   [17..19] reserved (0x00)
 * -------------------------------------------------------------------------*/
#define SPI_FRAME_LEN     20U
#define SPI_SYNC_BYTE     0xA5U
#define SPI_ACK_BYTE      0x5AU
#define CMD_GET_ENV       0x30U

#define RTC_BACKUP_MAGIC  0x32F2U /* written to BKP0R once the RTC has a sane default date/time */

/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c1;
ADC_HandleTypeDef hadc1;
RTC_HandleTypeDef hrtc;

/* USER CODE BEGIN PV */
static uint8_t spi_tx_buf[SPI_FRAME_LEN];
static uint8_t spi_rx_buf[SPI_FRAME_LEN];
static BME280_Handle bme280;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_I2C1_Init(void);
static void MX_ADC1_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */
static uint8_t Read_Light_Percent(void);
static void Build_Env_Response(const uint8_t *request, uint8_t *response);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint8_t Read_Light_Percent(void)
{
    /* Assumes an LDR-on-top voltage divider (VDD -> LDR -> PA1 -> fixed R -> GND):
     * more ambient light -> lower LDR resistance -> higher voltage at PA1 -> higher %. */
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK) {
        HAL_ADC_Stop(&hadc1);
        return 0U;
    }
    uint32_t raw = HAL_ADC_GetValue(&hadc1); /* 12-bit, 0..4095 */
    HAL_ADC_Stop(&hadc1);

    uint32_t pct = (raw * 100U) / 4095U;
    return (uint8_t)(pct > 100U ? 100U : pct);
}

/* Builds the 20-byte telemetry response described in the frame-layout comment above.
 * `request` is only used to decide the ACK byte; the sensor snapshot itself is taken
 * unconditionally so the master always gets fresh data even after a corrupted request. */
static void Build_Env_Response(const uint8_t *request, uint8_t *response)
{
    memset(response, 0, SPI_FRAME_LEN);

    uint8_t req_checksum = request[1] ^ request[2] ^ request[3];
    bool request_valid = (request[1] == SPI_SYNC_BYTE) && (request[2] == CMD_GET_ENV) &&
                          (request[4] == req_checksum);

    float temperature_c = 0.0f, humidity_pct = 0.0f, pressure_hpa = 0.0f;
    bool bme_ok = BME280_Read(&bme280, &temperature_c, &humidity_pct, &pressure_hpa);

    RTC_TimeTypeDef time = {0};
    RTC_DateTypeDef date = {0};
    HAL_StatusTypeDef time_status = HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN);
    /* HAL_RTC_GetDate must immediately follow HAL_RTC_GetTime: reading TIME locks the
     * shadow registers, and DATE is what actually unlocks them again for the next read. */
    HAL_StatusTypeDef date_status = HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN);
    bool rtc_ok = (time_status == HAL_OK) && (date_status == HAL_OK);

    uint8_t light_percent = Read_Light_Percent();

    int16_t temp_x100 = (int16_t)(temperature_c * 100.0f);
    uint16_t hum_x100 = (uint16_t)(humidity_pct * 100.0f);
    uint16_t press_x10 = (uint16_t)(pressure_hpa * 10.0f);

    response[1] = request_valid ? SPI_ACK_BYTE : 0x00U;
    response[2] = date.Year;
    response[3] = date.Month;
    response[4] = date.Date;
    response[5] = time.Hours;
    response[6] = time.Minutes;
    response[7] = time.Seconds;
    response[8] = (uint8_t)((uint16_t)temp_x100 >> 8);
    response[9] = (uint8_t)((uint16_t)temp_x100 & 0xFFU);
    response[10] = (uint8_t)(hum_x100 >> 8);
    response[11] = (uint8_t)(hum_x100 & 0xFFU);
    response[12] = (uint8_t)(press_x10 >> 8);
    response[13] = (uint8_t)(press_x10 & 0xFFU);
    response[14] = light_percent;
    response[15] = (uint8_t)((bme_ok ? 0x01U : 0x00U) | (rtc_ok ? 0x02U : 0x00U));

    uint8_t checksum = 0U;
    for (uint8_t i = 1U; i <= 15U; i++) {
        checksum ^= response[i];
    }
    response[16] = checksum;
}

/* Sets a compile-time default date/time the first time the firmware runs on a given
 * board (or after a full power loss with no backup domain battery), so the RTC never
 * reports 00:00:00 on 2000-01-01 forever. Adjust STARTUP_* below before flashing if you
 * want the clock to start closer to the real time -- there's no host-side time sync here. */
static void RTC_SetDefaultIfUnconfigured(void)
{
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == RTC_BACKUP_MAGIC) {
        return; /* already configured on a previous boot */
    }

#define STARTUP_YEAR   26U /* 2026 */
#define STARTUP_MONTH  8U
#define STARTUP_DAY    20U
#define STARTUP_HOUR   12U
#define STARTUP_MIN    0U
#define STARTUP_SEC    0U

    RTC_TimeTypeDef time = {0};
    time.Hours = STARTUP_HOUR;
    time.Minutes = STARTUP_MIN;
    time.Seconds = STARTUP_SEC;
    HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN);

    RTC_DateTypeDef date = {0};
    date.WeekDay = RTC_WEEKDAY_THURSDAY;
    date.Month = STARTUP_MONTH;
    date.Date = STARTUP_DAY;
    date.Year = STARTUP_YEAR;
    HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN);

    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_BACKUP_MAGIC);
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

  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_I2C1_Init();
  MX_ADC1_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */
  RTC_SetDefaultIfUnconfigured();

  if (!BME280_Init(&bme280, &hi2c1)) {
    /* Sensor missing/miswired: keep running so SPI telemetry still flows (status
     * byte in the response frame will report bit0=0, and readings will be 0). */
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (HAL_SPI_TransmitReceive(&hspi1, spi_tx_buf, spi_rx_buf, SPI_FRAME_LEN, 1500) == HAL_OK)
    {
      Build_Env_Response(spi_rx_buf, spi_tx_buf);
      HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_0); /* heartbeat: toggles on every serviced transaction */
    }

    /* Unconditionally reinit every cycle: hardware-NSS slave mode can hit not just
     * OVR but also a mode fault (MODF), which auto-clears the peripheral's enable
     * bit and disables it until something re-enables it -- HAL_SPI_Init() does that
     * unconditionally, so this recovers from either fault regardless of which one
     * hit (see module 4.3 mini-project for where this workaround came from). */
    HAL_SPI_DeInit(&hspi1);
    MX_SPI1_Init();
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
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure. HSI for the system clock (unchanged
  * from module 4.3), LSI added on top to clock the RTC -- no external 32.768 kHz
  * crystal is assumed to be wired up on this board.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
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

  /** RTC clock: LSI (~32 kHz nominal, imprecise but fine for a lab demo -- swap to
  * RCC_RTCCLKSOURCE_LSE if a 32.768 kHz crystal is fitted for real timekeeping accuracy).
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function (BME280 bus)
  * @retval None
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000; /* 100 kHz standard mode -- plenty for a single BME280 */
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
}

/**
  * @brief ADC1 Initialization Function (light sensor on PA1 / ADC1_IN1)
  * @retval None
  */
static void MX_ADC1_Init(void)
{
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
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  sConfig.Channel = LIGHT_SENSOR_ADC_CHANNEL;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RTC Initialization Function
  * @retval None
  */
static void MX_RTC_Init(void)
{
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  /* AsynchPrediv/SynchPrediv chosen for a ~32 kHz LSI: (124+1)*(249+1) = 31250, giving a
   * 1 Hz RTC clock close enough for a lab demo (LSI is not trimmed, so expect some drift). */
  hrtc.Init.AsynchPrediv = 124;
  hrtc.Init.SynchPrediv = 249;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @retval None
  */
static void MX_SPI1_Init(void)
{
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
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
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
