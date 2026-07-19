/**
  ******************************************************************************
  * @file    signal_processor.h
  * @brief   Signal processor - Edge detection + bit sync + frame parser
  ******************************************************************************
  */

#ifndef __SIGNAL_PROCESSOR_H
#define __SIGNAL_PROCESSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ask_protocol.h"

/* Configuration parameters */
#ifndef ADC_BUF_SIZE
#define ADC_BUF_SIZE           20      /* ADC DMA buffer (200kHz / 20 = 10kHz DMA) */
#endif

#ifndef ENVELOPE_THRESHOLD
#define ENVELOPE_THRESHOLD     1500u   /* ADC threshold */
#endif

#ifndef SAMPLES_PER_BIT
#define SAMPLES_PER_BIT        20u     /* 200kHz / 10kHz = 20 samples/bit */
#endif

/* Received frame structure */
typedef struct {
    uint8_t  type;                      /* Frame type (ASK_TYPE_*) */
    uint8_t  len;                       /* Payload length */
    uint8_t  payload[ASK_MAX_PAYLOAD];  /* Payload data */
    uint8_t  crc_ok;                    /* 1 = CRC valid */
} RxFrame_t;

/* Frame parser state codes (for LCD display) */
#define FRAME_STATE_SEARCH   0
#define FRAME_STATE_CONFIRM  1   /* Two-stage: waiting for 2nd preamble */
#define FRAME_STATE_TYPE     2
#define FRAME_STATE_LEN      3
#define FRAME_STATE_PAYLOAD  4
#define FRAME_STATE_CRC      5

/* Statistics structure */
typedef struct {
    uint32_t total_bits;       /* Total bits received */
    uint32_t last_16_bits;     /* Last 16 bits */
    uint32_t pattern_55;       /* 0x55 pattern count */
    uint32_t pattern_AA;       /* 0xAA pattern count */
    uint32_t env_avg;          /* Average ADC value */
    uint32_t frame_count;      /* Valid frames received */
    uint32_t frame_crc_err;    /* CRC error count */
    uint32_t break_count;      /* Break (gap) detected count */
    uint8_t  frame_ready;      /* 1 = new frame available */

    /* Debug: last decoded frame header (even on CRC fail) */
    uint8_t  last_frame_type;  /* TYPE byte from last frame attempt */
    uint8_t  last_frame_len;   /* LEN byte from last frame attempt */
    uint16_t last_crc_rx;      /* CRC received in frame */
    uint16_t last_crc_calc;    /* CRC calculated over data */

    /* Debug: live parser state (read at GetStats time) */
    uint8_t  frame_state;      /* Current parser state (FRAME_STATE_*) */
    uint16_t preamble_sr;      /* Preamble shift register value */
    uint16_t break_bit_count;  /* Bits processed in current break */
    uint8_t  in_break;         /* 1 = currently in break mode */
    uint16_t confirm_count;    /* Bits since 1st preamble (for two-stage) */

    /* Debug: bit stream capture around 2nd preamble match */
    uint8_t  capture_buf[16];  /* 128 bits: ~16 before match + ~112 after */

    /* Debug: adaptive threshold info */
    uint32_t baseline;         /* Measured 0-level during GAP */
    uint32_t eff_threshold;    /* Effective threshold being used */
} SignalStats_t;

/* Initialize */
void SignalProcessor_Init(void);

/* Process ADC data (called from DMA callback) */
void SignalProcessor_ProcessADC(uint16_t *adc_data, uint16_t len);

/* Get statistics */
void SignalProcessor_GetStats(SignalStats_t *stats);

/* Get last received frame (call after frame_ready == 1) */
void SignalProcessor_GetFrame(RxFrame_t *frame);

/* Reset statistics */
void SignalProcessor_ResetStats(void);

/* Set threshold */
void SignalProcessor_SetThreshold(uint32_t threshold);

#ifdef __cplusplus
}
#endif

#endif /* __SIGNAL_PROCESSOR_H */
