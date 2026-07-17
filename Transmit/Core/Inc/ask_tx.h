/**
  ******************************************************************************
  * @file    ask_tx.h
  * @brief   2ASK transmitter - frame encoder + bit FIFO
  *
  * The TX side feeds NRZ bits to the DAC2 carrier at 1 kHz (one bit per TIM2
  * period). Bits are pulled from a software FIFO inside the TIM2 ISR:
  *
  *   bit = 1  ->  DAC2 DMA buffer = sinx[] (carrier ON, 50 kHz sine + DC)
  *   bit = 0  ->  DAC2 DMA buffer = zero[] (carrier OFF, DC only)
  *
  * Frames are encoded with the format defined in ask_protocol.h and pushed
  * into the bit FIFO by the application via ASK_TX_SendFrame().
  ******************************************************************************
  */
#ifndef __ASK_TX_H
#define __ASK_TX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "ask_protocol.h"

/*============================================================================*
 *                              Bit FIFO                                      *
 *============================================================================*/
/* FIFO depth in bits. Must hold a complete frame + slack. */
#define ASK_TX_FIFO_BITS    (8 * (4 + ASK_MAX_PAYLOAD + 2) + 32)  /* ~1.7 kbit */

/**
  * @brief  Initialise the TX bit FIFO (call once at boot).
  */
void  ASK_TX_Init(void);

/**
  * @brief  Push a raw byte into the bit FIFO (debug, no frame header/CRC).
  * @param  byte     the byte to push (MSB first, 8 bits)
  * @retval 0 on success, -1 if FIFO full
  */
int   ASK_TX_PushByte(uint8_t byte);

/**
  * @brief  Push a complete frame into the bit FIFO.
  * @param  type     frame type (see ASK_FrameType_t)
  * @param  payload  pointer to payload bytes (may be NULL if len == 0)
  * @param  len      payload length in bytes (0 .. ASK_MAX_PAYLOAD)
  * @retval 0 on success, -1 if FIFO cannot hold the full frame
  */
int   ASK_TX_SendFrame(uint8_t type, const uint8_t *payload, uint16_t len);

/**
  * @brief  Number of bits currently waiting in the FIFO.
  */
uint16_t ASK_TX_FIFO_Count(void);

/**
  * @brief  Pop one bit from the FIFO (called from TIM2 ISR @ 10 kHz).
  * @retval 0 or 1 (the next bit), or 0 if FIFO empty (idle = no carrier).
  */
uint8_t  ASK_TX_FIFO_PopBit(void);

/**
  * @brief  Print frame encoding details for debugging (UART feedback).
  * @param  type     frame type (see ASK_FrameType_t)
  * @param  payload  pointer to payload bytes (may be NULL if len == 0)
  * @param  len      payload length in bytes
  */
void     ASK_TX_PrintFrameDebug(uint8_t type, const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __ASK_TX_H */
