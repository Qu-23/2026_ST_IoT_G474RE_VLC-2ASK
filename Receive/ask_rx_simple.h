/**
  ******************************************************************************
  * @file    ask_rx_simple.c
  * @brief   Simplified RX for link verification - bypasses full protocol
  ******************************************************************************
  *
  * This is a stripped-down version that:
  *   1. Only detects carrier presence (0 vs 1)
  *   2. Displays received bits directly
  *   3. No CRC, no frame parsing, just raw bit display
  *
  * Use this FIRST to verify the optical link works.
  *
  * Once you see bits matching TX (e.g., TX sends 01010101, RX displays same),
  * then switch back to full ask_rx.c for frame decoding.
  ******************************************************************************
  */

#ifndef __ASK_RX_SIMPLE_H
#define __ASK_RX_SIMPLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* Simple bit statistics */
typedef struct {
    uint32_t total_bits;       // Total bits received
    uint32_t ones_count;       // Number of '1' bits
    uint32_t last_16_bits;     // Last 16 bits (for pattern matching)
    uint32_t pattern_0101;     // Count of 0x55 (01010101) patterns
    uint32_t pattern_1010;     // Count of 0xAA (10101010) patterns
    uint32_t envelope_sum;     // Running envelope sum (for calibration)
    uint32_t envelope_count;   // Envelope count
} SimpleRX_Stats_t;

/* Initialize simple RX */
void SimpleRX_Init(void);

/* Push envelope value (call from ADC DMA callback) */
void SimpleRX_PushEnvelope(uint32_t env_sum_40);

/* Get current statistics */
void SimpleRX_GetStats(SimpleRX_Stats_t *stats);

/* Test: check if we're receiving a specific 8-bit pattern */
uint8_t SimpleRX_TestPattern(uint8_t pattern, uint16_t samples);

/* Calibration: get current envelope level */
uint32_t SimpleRX_GetEnvelopeLevel(void);

#ifdef __cplusplus
}
#endif

#endif /* __ASK_RX_SIMPLE_H */