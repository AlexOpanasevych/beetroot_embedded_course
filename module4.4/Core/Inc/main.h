/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * Module 4.4 -- BME280 (I2C) + light sensor (ADC) + RTC date/time, packed into
  * a custom frame and shipped to the ESP32 over SPI (STM32 = SPI1 slave).
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN Private defines */

/* SPI1: PA4=NSS, PA5=SCK, PA6=MISO, PA7=MOSI (unchanged from module 4.3 miniproject) */
/* PB0: on-board LED, kept from module 4.3 for the SET_LED demo command       */

/* I2C1 (BME280): PB6=SCL, PB7=SDA */
#define BME280_I2C_SCL_PIN         GPIO_PIN_6
#define BME280_I2C_SDA_PIN         GPIO_PIN_7
#define BME280_I2C_PORT            GPIOB

/* ADC1_IN1 (light sensor / LDR voltage divider): PA1 */
#define LIGHT_SENSOR_ADC_PIN        GPIO_PIN_1
#define LIGHT_SENSOR_ADC_PORT       GPIOA
#define LIGHT_SENSOR_ADC_CHANNEL    ADC_CHANNEL_1

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
