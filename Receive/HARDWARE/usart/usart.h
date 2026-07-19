#ifndef __USART_H
#define __USART_H
#include "stdio.h"
#include "stm32f4xx_hal.h"
#include "sys.h"

/*============================================================================
 * usart.h - USART1 Driver & ADC Print Module
 * 
 * Platform : STM32F407 / CubeMX HAL
 * USART1  : PA9(TX), PA10(RX), 921600-8N1 (default)
 * Protocol: \r\n terminated frames
 * 
 * Features:
 *   - Configurable baud rate via USART1_Init(bound)
 *   - printf() redirect via fputc -> HAL_UART_Transmit
 *   - Interrupt-based RX with frame detection
 *   - ADC real-time print via DMA callback
 *============================================================================*/

/*---- USART RX Configuration ------------------------------------------------*/
#define USART_REC_LEN   200             /* RX buffer size (max frame bytes) */
#define EN_USART1_RX    1               /* 1 = enable RX, 0 = disable */

/*---- ADC DMA Configuration -------------------------------------------------*/
#define ADC_CH_NUM      2               /* Number of ADC channels in scan */
#define ADC_BUF_SIZE    2048            /* DMA buffer: 1024 CH0 + 1024 CH1 */

/*---- External Handles (from main.c CubeMX) ---------------------------------*/
extern UART_HandleTypeDef huart1;
extern ADC_HandleTypeDef hadc1;

/*---- RX Buffer & Status ----------------------------------------------------
   USART_RX_STA bit definitions:
     bit15   - frame complete (0x8000)
     bit14   - received 0x0d (0x4000)
     bit13~0 - valid byte count in USART_RX_BUF
----------------------------------------------------------------------------*/
extern u8  USART_RX_BUF[USART_REC_LEN];
extern u16 USART_RX_STA;

/*---- ADC DMA Buffer --------------------------------------------------------*/
extern uint16_t adc_buf[ADC_BUF_SIZE];       /* circular DMA buffer (live) */
extern uint16_t adc_buf_snap[ADC_BUF_SIZE];  /* snapshot: safe copy for FFT reads */
extern volatile uint8_t adc_data_ready;      /* flag set by DMA complete callback */
extern volatile uint8_t snap_busy;            /* lock: main loop reading snapshot */

/*---- API ------------------------------------------------------------------*/
void USART1_Init(u32 bound);            /* init UART with baud rate 'bound' */
void USART1_SendByte(uint8_t data);     /* blocking single-byte TX */
void USART1_SendString(char *str);      /* blocking string TX */
void USART_PrintADC(void);             /* print ADC buffer via UART */

#endif /* __USART_H */


