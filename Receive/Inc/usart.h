/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  * @brief   This file contains all the function prototypes for
  *          the usart.c file
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
  /*============================================================================
  
 * USART1  : PA9(TX), PA10(RX), 921600-8N1 (default)
 * Protocol: \n terminated frames
 * 
 * Features:
 *   - Configurable baud rate via USART1_Init(bound)
 *   - printf() redirect via fputc -> HAL_UART_Transmit
 *   - Interrupt-based RX with frame detection
 *   - ADC real-time print via DMA callback
 
 *============================================================================*/

/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#include "adc.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

extern UART_HandleTypeDef huart1;

/* USER CODE BEGIN Private defines */
extern ADC_HandleTypeDef hadc1;

/*---- USART RX Configuration ------------------------------------------------*/
#define USART_REC_LEN   200             /* RX buffer size (max frame bytes) */
#define EN_USART1_RECOV    1            /* 1 = enable , 0 = disable */
/*---- RX Buffer & Status ----------------------------------------------------
   USART_RX_STA bit definitions:
     bit15   - frame complete (0x8000)
     bit14   - received 0x0d (0x4000)
     bit13~0 - valid byte count in USART_RX_BUF
----------------------------------------------------------------------------*/
extern uint8_t  USART_RX_BUF[USART_REC_LEN];
extern uint16_t  USART_RX_STA;
/*---- TX Buffer & Status --------------------------------------------------*/
extern char uart_tx_buf[];


/*---- ADC DMA Configuration -------------------------------------------------*/
/* ADC_BUF_SIZE now defined in signal_processor.h for unified configuration */
extern uint16_t adc_buf[];               /* circular DMA buffer (live) */
extern volatile uint8_t adc_data_ready;  /* flag set by DMA complete callback */
/* USER CODE END Private defines */

void MX_USART1_UART_Init(void);

/* USER CODE BEGIN Prototypes */
void USART1_ReInit(uint32_t bound);     /* init UART with baud rate 'bound' */
void USART1_SendByte(uint8_t data);     /* blocking single-byte TX */
void USART1_SendString(char *str);      /* blocking string TX */
//void USART_PrintADC(void);              /* print ADC buffer via UART */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

