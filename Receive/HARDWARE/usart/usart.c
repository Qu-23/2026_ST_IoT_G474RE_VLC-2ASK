/*============================================================================
 * usart.c - USART1 Driver & ADC Print Module
 * 
 * Platform: STM32F407 / CubeMX HAL
 * 
 * Hardware init (CubeMX generated in main.c):
 *   MX_USART1_UART_Init() - GPIO, clock, default baud via HAL
 *   MX_ADC1_Init()        - ADC1, 2 channels, DMA2_Stream0, TIM2 trigger
 * 
 * Software init (call after CubeMX init):
 *   USART1_Init(bound)    - set baud rate, enable RX interrupt
 * 
 * Usage:
 *   printf("Hello\r\n");              // redirected to USART1
 *   USART1_SendString("data\r\n");   // direct send
 *   
 *   // In main loop:
 *   if (adc_data_ready) {
 *       adc_data_ready = 0;
 *       USART_PrintADC();
 *   }
 *============================================================================*/

#include "sys.h"
#include "usart.h"
#include <string.h>

#if SYSTEM_SUPPORT_OS
#include "includes.h"
#endif

/*============================================================================
 * printf() Redirect
 * fputc -> HAL_UART_Transmit, no MicroLIB required
 * Timeout: HAL_MAX_DELAY (blocking)
 *============================================================================*/
#if 1
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
};

FILE __stdout;

void _sys_exit(int x)
{
    x = x;
}

int fputc(int ch, FILE *f)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
#endif

/*============================================================================
 * RX Buffer & Status
 *============================================================================*/
#if EN_USART1_RX
u8  USART_RX_BUF[USART_REC_LEN];
u16 USART_RX_STA = 0;

static u8 rx_byte;  /* single-byte RX buffer for HAL_UART_Receive_IT */

/*---- USART1_Init ------------------------------------------------------------
  Initialize USART1 with given baud rate.
  Must be called after MX_USART1_UART_Init() in main.c.
  Enables RXNE interrupt and starts first HAL_UART_Receive_IT.
----------------------------------------------------------------------------*/
void USART1_Init(u32 bound)
{
    huart1.Init.BaudRate = bound;
    HAL_UART_Init(&huart1);     /* apply new baud rate (MSP re-inits GPIO) */

    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART1_IRQn, 3, 3);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

/*---- HAL_UART_RxCpltCallback ------------------------------------------------
  RX byte complete callback (called from USART1_IRQHandler via HAL).
  Frame protocol: \r\n (0x0d 0x0a) terminated.
----------------------------------------------------------------------------*/
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
#if SYSTEM_SUPPORT_OS
    OSIntEnter();
#endif
    if (huart->Instance == USART1)
    {
        if ((USART_RX_STA & 0x8000) == 0)
        {
            if (USART_RX_STA & 0x4000)
            {
                if (rx_byte != 0x0a) USART_RX_STA = 0;
                else USART_RX_STA |= 0x8000;
            }
            else
            {
                if (rx_byte == 0x0d) USART_RX_STA |= 0x4000;
                else
                {
                    USART_RX_BUF[USART_RX_STA & 0x3FFF] = rx_byte;
                    USART_RX_STA++;
                    if (USART_RX_STA > (USART_REC_LEN - 1)) USART_RX_STA = 0;
                }
            }
        }
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
#if SYSTEM_SUPPORT_OS
    OSIntExit();
#endif
}

/*---- USART1_SendByte --------------------------------------------------------
  Blocking single-byte send.
----------------------------------------------------------------------------*/
void USART1_SendByte(uint8_t data)
{
    HAL_UART_Transmit(&huart1, &data, 1, HAL_MAX_DELAY);
}

/*---- USART1_SendString ------------------------------------------------------
  Blocking string send (null-terminated).
----------------------------------------------------------------------------*/
void USART1_SendString(char *str)
{
    while (*str)
    {
        USART1_SendByte((uint8_t)(*str++));
    }
}

#endif /* EN_USART1_RX */

/*============================================================================
 * ADC DMA Module
 * 
 * ADC1: 2-ch scan (PA0=CH0 ext input, PA1=CH1 feedback), TIM2_TRGO, DMA2_Stream0
 * Buffer: adc_buf[ADC_BUF_SIZE] = 2048 (circular DMA, 1024 CH0 + 1024 CH1)
 * Flag:   adc_data_ready (set by HAL_ADC_ConvCpltCallback)
 *============================================================================*/
uint16_t adc_buf[ADC_BUF_SIZE];
//uint16_t adc_buf_snap[ADC_BUF_SIZE];     /* snapshot: double-buffer for safe FFT reads */
volatile uint8_t adc_data_ready = 0;
//volatile uint8_t snap_busy = 0;           /* lock: ISR skips copy when main reads */

/*---- HAL_ADC_ConvHalfCpltCallback -------------------------------------------
  DMA half-transfer complete: first half [0..SIZE/2-1] is safe to read
  because DMA is currently writing the second half.
----------------------------------------------------------------------------*/
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1/* && !snap_busy*/)
    {
//        memcpy(adc_buf_snap,
//               adc_buf,
//               sizeof(adc_buf_snap)/2);
//        adc_data_ready = 1;
    }
}

/*---- HAL_ADC_ConvCpltCallback -----------------------------------------------
  DMA full-transfer complete: second half [SIZE/2..SIZE-1] is safe to read
  because DMA just wrapped and is now writing the first half again.
  Set flag after both halves are copied → FFT gets a complete, consistent window.
----------------------------------------------------------------------------*/
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1/* && !snap_busy*/)
    {
//        memcpy(adc_buf_snap + ADC_BUF_SIZE / 2,
//               adc_buf + ADC_BUF_SIZE / 2,
//               sizeof(adc_buf_snap) / 2);
        adc_data_ready = 1;
    }
}

/*---- USART_PrintADC ---------------------------------------------------------
  Print ADC buffer in VOFA+ Firewater format: "CH0,CH1\n" per sample pair.
  Buffer layout: interleaved CH0/CH1, ADC_CH_NUM=2 channels per scan.
----------------------------------------------------------------------------*/
void USART_PrintADC(void)
{
    char buf[64];
    int i;

    for (i = 0; i < ADC_BUF_SIZE; i += ADC_CH_NUM)
    {
        int len = snprintf(buf, sizeof(buf), "%u,%u\n",
                           adc_buf[i], adc_buf[i + 1]);
        HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, 100);
    }
}


