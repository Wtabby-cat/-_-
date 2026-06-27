/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

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

void HAL_HRTIM_MspPostInit(HRTIM_HandleTypeDef *hhrtim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define TANK_CURR_Pin GPIO_PIN_0
#define TANK_CURR_GPIO_Port GPIOA
#define TANK_VOLT_Pin GPIO_PIN_1
#define TANK_VOLT_GPIO_Port GPIOA
#define RELAY_CTRL_Pin GPIO_PIN_0
#define RELAY_CTRL_GPIO_Port GPIOB
#define PWM_EN_N_Pin GPIO_PIN_1
#define PWM_EN_N_GPIO_Port GPIOB
#define LEFT_HIGH_Pin GPIO_PIN_8
#define LEFT_HIGH_GPIO_Port GPIOA
#define LEFT_LOW_Pin GPIO_PIN_9
#define LEFT_LOW_GPIO_Port GPIOA
#define RIGHT_HIGH_Pin GPIO_PIN_10
#define RIGHT_HIGH_GPIO_Port GPIOA
#define RIGHT_LOW_Pin GPIO_PIN_11
#define RIGHT_LOW_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
