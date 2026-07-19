/**
  ******************************************************************************
  * @file    ask_rx.h
  * @brief   2ASK receiver - envelope detection + bit slicer + frame decoder
  *
  * The RX side samples the photodiode signal with ADC1 + DMA at ~180 kHz
  * (40-sample DMA buffer, ~4.5 kHz buffer-ready rate). Each DMA-complete
  * callback computes one integrated envelope value (sum of |sample - DC| over
  * 40 samples) and pushes it to the decoder via ASK_RX_PushEnvelope().
  *
  * The decoder runs a small state machine:
  *
  *   HUNT  -> look for 16-bit preamble 0xAAE4 in the decoded bit stream
  *            (sliding 16-bit correlator, accepts up to 2 bit errors).
  *   TYPE  -> read 8-bit frame type.
  *   LEN   -> read 8-bit payload length.
  *   PAYLOAD -> read LEN * 8 payload bits.
  *   CRC   -> read 16-bit CRC and verify against CRC-16-CCITT.
  *
  * Bit slicing:
  *   - One envelope = 40 ADC samples.
  *   - One bit      = ~180 ADC samples = ~4.5 envelopes (1 kHz baud).
  *   - A fractional accumulator (units = ADC samples) decides bit boundaries
  *     precisely so that ~4.5 envelopes/bit doesn't drift over long frames.
  *   - Each envelope votes "1" (carrier present) or "0" (no carrier); a
  *     majority vote at each bit boundary produces the decoded bit.
  ******************************************************************************
  */
#ifndef __ASK_RX_H
#define __ASK_RX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "ask_protocol.h"

/*============================================================================*
 *                          Tunable parameters                                *
 *============================================================================*/

/* ADC DC offset (no-carrier level). 12-bit ADC, Vref=3.3 V.
   The TX side uses 1.2 V DC, so 1.2/3.3*4095 ~= 1490. The optical link may
   shift this; calibrate by reading the ADC with TX off. */
#ifndef ASK_RX_DC_OFFSET
#define ASK_RX_DC_OFFSET          1800u
#endif

/* Per-40-sample envelope threshold. If sum(|sample-DC|) over 40 samples
   exceeds this, the envelope votes "1" (carrier present).
   With carrier ON, peak |sample-DC| ~= 620 (0.5 V). Average ~= 310.
   Per-40-sample sum ~= 40 * 310 = 12400 when carrier is fully on.
   Noise floor when carrier is off is typically < 2000. */
#ifndef ASK_RX_ENV_THRESHOLD
#define ASK_RX_ENV_THRESHOLD      6000u
#endif

/* ADC samples per bit.  ADC sample rate ~ 179.7 kHz, baud rate 1 kHz,
   so 180 samples/bit. This must match the TX baud rate. */
#ifndef ASK_RX_SAMPLES_PER_BIT
#define ASK_RX_SAMPLES_PER_BIT    180u
#endif

/* ADC samples per envelope (one envelope = one DMA buffer). */
#ifndef ASK_RX_SAMPLES_PER_ENV
#define ASK_RX_SAMPLES_PER_ENV    40u
#endif

/* Number of bit errors tolerated when matching the 16-bit preamble. */
#ifndef ASK_RX_PREAMBLE_MAX_ERRORS
#define ASK_RX_PREAMBLE_MAX_ERRORS 2u
#endif

/* If no bit has been decoded for this many ms while in a non-HUNT state,
   the receiver declares loss of sync and returns to HUNT. */
#ifndef ASK_RX_BIT_TIMEOUT_MS
#define ASK_RX_BIT_TIMEOUT_MS     2000u
#endif

/*============================================================================*
 *                              Public API                                    *
 *============================================================================*/

/**
  * @brief  Initialise the receiver state machine (call once at boot).
  */
void  ASK_RX_Init(void);

/**
  * @brief  Push one envelope sample to the decoder.
  *         Call this from the ADC DMA-complete callback with the integrated
  *         envelope of the just-filled 40-sample buffer:
  *
  *            uint32_t env = 0;
  *            for (i=0; i<40; i++) {
  *                int32_t v = (int32_t)adc_buf[i] - ASK_RX_DC_OFFSET;
  *                if (v<0) v = -v;
  *                env += v;
  *            }
  *            ASK_RX_PushEnvelope(env);
  *
  * @param  env_sum_40  sum of |sample-DC| over the last 40 samples
  */
void  ASK_RX_PushEnvelope(uint32_t env_sum_40);

/**
  * @brief  Periodic housekeeping (call from main loop).
  *         Handles the bit-timeout watchdog that forces a return to HUNT
  *         if the link is lost mid-frame.
  */
void  ASK_RX_Process(void);

/**
  * @brief  Current state name (for debug / GUI display).
  */
const char *ASK_RX_StateName(void);

/**
  * @brief  Get the latest received-frame signal flag.
  * @retval 1 if a new frame has been decoded since the last call, else 0.
  *         The flag is cleared by this call.
  */
uint8_t ASK_RX_FrameReady(void);

/**
  * @brief  Copy out the most recent valid frame (type + payload + len).
  *         Call after ASK_RX_FrameReady() returns 1.
  */
void  ASK_RX_GetFrame(uint8_t *type, uint8_t *len, uint8_t *payload_buf);

/**
  * @brief  User-overrideable callback - called immediately when a valid frame
  *         is decoded. The default weak implementation does nothing.
  *         It is safe to call GUI/LCD functions from here, but keep it short.
  */
void  ASK_RX_OnFrameReceived(uint8_t type, const uint8_t *payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __ASK_RX_H */
