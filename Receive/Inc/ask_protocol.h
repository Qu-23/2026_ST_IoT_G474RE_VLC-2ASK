/**
  ******************************************************************************
  * @file    ask_protocol.h
  * @brief   2ASK digital optical communication - shared frame protocol
  *
  * Frame format (MSB first, two-stage sync):
  *   [PREAMBLE1 16b=0xAAE4][FILL 304b=0xAA][PREAMBLE2 16b=0xAAE4]
  *   [TYPE 8b][LEN 8b][PAYLOAD: LEN bytes][CRC16 16b]
  *
  * Two-stage sync:
  *   - 1st 0xAAE4: enter CONFIRM (don't trust)
  *   - 2nd 0xAAE4 at ~320 bits after: CONFIRMED
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
#define ASK_PREAMBLE_WORD     0xAAE4u      /* 16-bit preamble (MSB first)    */
#define ASK_PREAMBLE_BITS     16           /* preamble length in bits        */
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

#define ASK_MAX_PAYLOAD       200      /* bytes, keeps a frame < 1.7 s        */

/*============================================================================*
 *                              Frame types                                   *
 *============================================================================*/
typedef enum {
    ASK_TYPE_RAW     = 0x01,   /* raw bytes payload (e.g. binary patterns)   */
    ASK_TYPE_TEXT    = 0x02,   /* ANSI/GBK string, null-terminated optional  */
    ASK_TYPE_GRAPHIC = 0x03,   /* image index, 1-byte payload (0..255)        */
    ASK_TYPE_AUDIO   = 0x04,   /* audio samples (8-bit PCM envelope)         */
} ASK_FrameType_t;

/*============================================================================*
 *                              CRC-16-CCITT                                  *
 *============================================================================*/
/**
  * @brief  Compute CRC-16-CCITT (X^16 + X^12 + X^5 + 1, poly 0x1021, init 0xFFFF).
  * @param  data  pointer to data bytes
  * @param  len   number of bytes
  * @retval 16-bit CRC
  */
static __INLINE uint16_t ASK_CRC16_Calc(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    uint16_t i;
    uint8_t  j;
    for (i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000u)
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

#ifdef __cplusplus
}
#endif

#endif /* __ASK_PROTOCOL_H */
