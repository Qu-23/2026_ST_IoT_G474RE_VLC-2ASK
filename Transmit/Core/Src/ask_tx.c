/**
  ******************************************************************************
  * @file    ask_tx.c
  * @brief   2ASK transmitter - frame encoder + bit FIFO implementation
  ******************************************************************************
  */
#include "ask_tx.h"
#include <string.h>
#include <stdio.h>  /* for printf in debug functions */

/*============================================================================*
 *                        Bit FIFO (circular, MSB first)                      *
 *============================================================================*
 * Bits are packed MSB-first into bytes. Two indices track the bit stream:
 *   head_bit  : next free bit position (write side, encoder)
 *   tail_bit  : next bit to send       (read side, TIM2 ISR)
 * Both wrap modulo (ASK_TX_FIFO_BITS).
 *---------------------------------------------------------------------------*/
static uint8_t  s_fifo[ASK_TX_FIFO_BITS / 8 + 1];
static volatile uint16_t s_head_bit = 0;
static volatile uint16_t s_tail_bit = 0;

/* Push one bit into the FIFO (MSB of byte at head position). */
static void s_PushBit(uint8_t bit)
{
    uint16_t free_bits = (uint16_t)((s_tail_bit - s_head_bit - 1 + ASK_TX_FIFO_BITS)
                                    % ASK_TX_FIFO_BITS);
    if (free_bits == 0)
        return;                         /* FIFO full - drop (should not happen) */

    uint16_t byte_idx = (uint16_t)(s_head_bit >> 3);
    uint8_t  bit_mask = (uint8_t)(0x80 >> (s_head_bit & 7));

    if (bit)
        s_fifo[byte_idx] |= bit_mask;
    else
        s_fifo[byte_idx] &= (uint8_t)~bit_mask;

    s_head_bit = (uint16_t)((s_head_bit + 1) % ASK_TX_FIFO_BITS);
}

/* Push the 8 bits of a byte, MSB first (matches the preamble convention). */
static void s_PushByteMSB(uint8_t byte)
{
    int8_t i;
    for (i = 7; i >= 0; i--)
        s_PushBit((uint8_t)((byte >> i) & 0x01));
}

/*============================================================================*
 *                              Public API                                    *
 *============================================================================*/
void ASK_TX_Init(void)
{
    s_head_bit = 0;
    s_tail_bit = 0;
    memset(s_fifo, 0, sizeof(s_fifo));
}

/*============================================================================*
 * Debug API: Push raw byte (no frame header, no CRC)                         *
 * For link-layer testing only.                                               *
 *---------------------------------------------------------------------------*/
int ASK_TX_PushByte(uint8_t byte)
{
    uint16_t free_bits = (uint16_t)((s_tail_bit - s_head_bit - 1 + ASK_TX_FIFO_BITS)
                                    % ASK_TX_FIFO_BITS);
    if (free_bits < 8)
        return -1;                      /* not enough room */

    s_PushByteMSB(byte);
    return 0;
}

int ASK_TX_SendFrame(uint8_t type, const uint8_t *payload, uint16_t len)
{
    uint16_t crc;
    uint16_t bits_needed;
    uint16_t free_bits;
    uint16_t i;

    if (len > ASK_MAX_PAYLOAD)
        return -1;

    /* Check FIFO has room: preamble(16) + type(8) + len(8) + payload(len*8) + crc(16) */
    bits_needed = (uint16_t)(ASK_HEADER_BITS + ASK_TRAILER_BITS + 8u * len);
    free_bits = (uint16_t)((s_tail_bit - s_head_bit - 1 + ASK_TX_FIFO_BITS)
                           % ASK_TX_FIFO_BITS);
    if (free_bits < bits_needed)
        return -1;                      /* not enough room, try again later */

    /* Compute CRC over [type, len, payload] */
    {
        uint8_t crc_buf[2 + ASK_MAX_PAYLOAD];
        crc_buf[0] = type;
        crc_buf[1] = (uint8_t)len;
        if (len && payload)
            memcpy(&crc_buf[2], payload, len);
        crc = ASK_CRC16_Calc(crc_buf, (uint16_t)(2 + len));
    }

    /* Push frame: PREAMBLE(16, MSB first) + TYPE + LEN + PAYLOAD + CRC */
    s_PushByteMSB((uint8_t)(ASK_PREAMBLE_WORD >> 8));   /* 0xAA */
    s_PushByteMSB((uint8_t)(ASK_PREAMBLE_WORD & 0xFF)); /* 0xE4 */
    s_PushByteMSB(type);
    s_PushByteMSB((uint8_t)len);
    for (i = 0; i < len; i++)
        s_PushByteMSB(payload[i]);
    s_PushByteMSB((uint8_t)(crc >> 8));     /* CRC high byte first */
    s_PushByteMSB((uint8_t)(crc & 0xFF));

    return 0;
}

/*============================================================================*
 * Debug API: Print frame encoding details                                     *
 *---------------------------------------------------------------------------*/
void ASK_TX_PrintFrameDebug(uint8_t type, const uint8_t *payload, uint16_t len)
{
    uint16_t crc;
    uint16_t total_bits;
    uint16_t i;

    if (len > ASK_MAX_PAYLOAD)
    {
        printf("Error: payload too large (%u > %u)\r\n", len, ASK_MAX_PAYLOAD);
        return;
    }

    /* Calculate CRC */
    {
        uint8_t crc_buf[2 + ASK_MAX_PAYLOAD];
        crc_buf[0] = type;
        crc_buf[1] = (uint8_t)len;
        if (len && payload)
            memcpy(&crc_buf[2], payload, len);
        crc = ASK_CRC16_Calc(crc_buf, (uint16_t)(2 + len));
    }

    /* Print frame summary */
    total_bits = (uint16_t)(ASK_HEADER_BITS + ASK_TRAILER_BITS + 8u * len);
    printf("\r\n=== Frame Encoding Debug ===\r\n");
    printf("Type:  0x%02X\r\n", type);
    printf("Len:   %u bytes\r\n", len);
    printf("Total: %u bits\r\n", total_bits);

    /* Print hex dump */
    printf("Frame (hex): AA E4 %02X %02X ", type, len);
    for (i = 0; i < len; i++)
        printf("%02X ", payload[i]);
    printf("%02X %02X\r\n", (uint8_t)(crc >> 8), (uint8_t)(crc & 0xFF));

    /* Print payload (if printable) */
    printf("Payload: ");
    for (i = 0; i < len; i++)
    {
        if (payload[i] >= 0x20 && payload[i] <= 0x7E)
            printf("%c", payload[i]);
        else
            printf(".");
    }
    printf("\r\n");

    /* Print bit pattern (first 3 bytes) */
    printf("Bit pattern (first 24 bits):\r\n");
    printf("  AA: "); s_PrintByteBits(0xAA);
    printf("  E4: "); s_PrintByteBits(0xE4);
    printf("  %02X: ", type); s_PrintByteBits(type);
    printf("\r\n");
}

/* Helper: Print byte in binary (MSB first) */
static void s_PrintByteBits(uint8_t byte)
{
    for (int i = 7; i >= 0; i--)
        printf("%d", (byte >> i) & 1);
    printf("\r\n");
}

uint16_t ASK_TX_FIFO_Count(void)
{
    return (uint16_t)((s_head_bit - s_tail_bit + ASK_TX_FIFO_BITS)
                      % ASK_TX_FIFO_BITS);
}

uint8_t ASK_TX_FIFO_PopBit(void)
{
    uint8_t  bit;
    uint16_t byte_idx;
    uint8_t  bit_mask;

    if (s_head_bit == s_tail_bit)
        return 0;                                   /* FIFO empty -> idle (no carrier) */

    byte_idx = (uint16_t)(s_tail_bit >> 3);
    bit_mask = (uint8_t)(0x80 >> (s_tail_bit & 7));
    bit = (uint8_t)((s_fifo[byte_idx] & bit_mask) ? 1 : 0);

    s_tail_bit = (uint16_t)((s_tail_bit + 1) % ASK_TX_FIFO_BITS);
    return bit;
}
