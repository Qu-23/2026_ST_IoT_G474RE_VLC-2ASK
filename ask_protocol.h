/**
  ******************************************************************************
  * @file    ask_protocol.h
  * @brief   2ASK optical communication - shared frame protocol (TX & RX)
  *
  * Frame format (MSB first, two-stage sync):
  *   [PREAMBLE1 16b=0xAAE4][FILL 304b=0xAA][PREAMBLE2 16b=0xAAE4]
  *   [TYPE 8b][LEN 8b][PAYLOAD: LEN bytes][CRC16 16b]
  *
  * Two-stage sync (per teacher's suggestion):
  *   - 1st 0xAAE4 match: enter CONFIRM state (don't trust yet)
  *   - 2nd 0xAAE4 match at ~320 bits after 1st: CONFIRMED
  *   - False alarm probability: P(single)^2, extremely low
  *
  * Preamble design:
  *   - 0xAA (10101010): clock recovery / AGC (alternating 1/0)
  *   - 0xE4 (11100100): frame sync marker
  *   - 0xE4 NEVER appears in idle 0x55/0xAA stream -> zero false alarm
  *
  * CRC-16-CCITT: poly 0x1021, init 0xFFFF, over [TYPE, LEN, PAYLOAD]
  ******************************************************************************
  */
#ifndef __ASK_PROTOCOL_H
#define __ASK_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*============================================================================*
 *                              Frame format                                  *
 *============================================================================*/
#define ASK_PREAMBLE_WORD     0xAAE4u
#define ASK_PREAMBLE_BITS     16
#define ASK_TYPE_BITS         8
#define ASK_LEN_BITS          8
#define ASK_CRC_BITS          16

/* Two-stage preamble confirmation */
#define ASK_PREAMBLE_CONFIRM_DISTANCE  48   /* Bits between 1st and 2nd preamble */
#define ASK_PREAMBLE_FILL_BITS        (ASK_PREAMBLE_CONFIRM_DISTANCE - ASK_PREAMBLE_BITS)  /* 32 */

/* Sync bytes before first preamble (for bit sync establishment) */
#define ASK_SYNC_BYTES        4      /* 4 x 0xAA = 32 bits of alternating pattern */

#define ASK_HEADER_BITS       (2 * ASK_PREAMBLE_BITS + ASK_PREAMBLE_FILL_BITS + ASK_TYPE_BITS + ASK_LEN_BITS)  /* 336 */
#define ASK_TRAILER_BITS      ASK_CRC_BITS                                      /* 16 */

#define ASK_MAX_PAYLOAD       200

/*============================================================================*
 *                              Frame types                                   *
 *============================================================================*/
typedef enum {
    ASK_TYPE_RAW     = 0x01,
    ASK_TYPE_TEXT    = 0x02,
    ASK_TYPE_GRAPHIC = 0x03,
    ASK_TYPE_AUDIO   = 0x04,
} ASK_FrameType_t;

/*============================================================================*
 *                              CRC-16-CCITT                                  *
 *============================================================================*/
/**
  * @brief  Compute CRC-16-CCITT (poly 0x1021, init 0xFFFF).
  * @param  data  pointer to data bytes
  * @param  len   number of bytes
  * @retval 16-bit CRC
  */
uint16_t ASK_CRC16_Calc(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __ASK_PROTOCOL_H */