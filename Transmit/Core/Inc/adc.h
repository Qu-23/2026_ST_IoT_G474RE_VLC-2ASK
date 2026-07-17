/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.h
  * @brief   This file contains all the function prototypes for
  *          the adc.c file
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
#ifndef __ADC_H__
#define __ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern ADC_HandleTypeDef hadc1;

/* USER CODE BEGIN Private defines */

/* Audio DEMO: ADC1 on PA2 (channel 3) samples microphone signal at ~10 kHz.
   DMA buffer of ADC_BUF_SIZE samples; the ConvCplt callback computes the
   audio envelope (8-bit level) for the AUDIO frame transmitter. */
#define ADC_BUF_SIZE          100u     /* 100 samples * 12-bit = 200 bytes     */
#define ADC_CH_NUM            1u

extern uint16_t adc_buf[ADC_CH_NUM * ADC_BUF_SIZE];
extern volatile uint8_t  adc_data_ready;   /* set by DMA-complete callback   */
extern volatile uint8_t  audio_level;      /* 0..255 envelope for AUDIO frame*/

/* USER CODE END Private defines */

void MX_ADC1_Init(void);

/* USER CODE BEGIN Prototypes */
void ADC_Audio_Start(void);
void ADC_Audio_Stop(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H__ */

