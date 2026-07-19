/**
  ******************************************************************************
  * @file    ask_rx_debug.c
  * @brief   Diagnostic version of ASK receiver - adds printf output for debug
  ******************************************************************************
  */

#include "main.h"
#include <stdio.h>

// Debug flags - set these to narrow down the problem
#define DEBUG_ENVELOPE     0   // 1 = print envelope values (LOTS of output)
#define DEBUG_BIT_DECISION 1   // 1 = print each decoded bit
#define DEBUG_STATE        1   // 1 = print state transitions
#define DEBUG_FRAME        1   // 1 = print frame reception events
#define DEBUG_CRC_FAIL     1   // 1 = print CRC failures
#define DEBUG_TIMEOUT      1   // 1 = print timeout events

/* Add these includes in your actual ask_rx.c file */
// #include "usart.h"  // For printf (via fputc)

/* ============================================================================
   DIAGNOSTIC FUNCTIONS - Add to ask_rx.c
   ============================================================================*/

/* Print envelope value statistics every 100 envelopes */
static uint32_t s_env_count = 0;
static uint32_t s_env_sum = 0;
static uint32_t s_env_max = 0;
static uint32_t s_env_min = 0xFFFFFFFF;
static uint32_t s_env_ones = 0;

/* Print envelope statistics - call after processing envelope batch */
void ASK_RX_DumpEnvelopeStats(void)
{
    if (s_env_count >= 100)
    {
        uint32_t avg = (s_env_count > 0) ? (s_env_sum / s_env_count) : 0;
        printf("ENV STATS: avg=%lu, min=%lu, max=%lu, ones=%lu/100\r\n",
               avg, s_env_min, s_env_max, s_env_ones);

        // Check if envelope values are in expected range
        if (avg < 2000)
            printf("  WARN: Low envelope - check signal strength\r\n");
        else if (avg > 15000)
            printf("  WARN: High envelope - possible saturation\r\n");

        // Reset
        s_env_count = 0;
        s_env_sum = 0;
        s_env_max = 0;
        s_env_min = 0xFFFFFFFF;
        s_env_ones = 0;
    }
}

/* Modified ASK_RX_PushEnvelope with debug output */
/* Replace your ASK_RX_PushEnvelope with this diagnostic version */
void ASK_RX_PushEnvelope_Diag(uint32_t env_sum_40)
{
    /* Statistics collection */
    s_env_count++;
    s_env_sum += env_sum_40;
    if (env_sum_40 > s_env_max) s_env_max = env_sum_40;
    if (env_sum_40 < s_env_min) s_env_min = env_sum_40;

#if DEBUG_ENVELOPE
    static uint8_t env_idx = 0;
    if (env_idx < 20)  // Only print first 20 envelopes to avoid flood
    {
        printf("ENV[%d]=%lu\r\n", env_idx, env_sum_40);
        env_idx++;
    }
#endif

    uint8_t vote = (env_sum_40 > ASK_RX_ENV_THRESHOLD) ? 1u : 0u;
    if (vote) s_env_ones++;

    uint16_t prev_phase = s_bit_phase_acc;
    s_bit_vote_ones  += vote;
    s_bit_vote_total += 1u;

    s_bit_phase_acc += ASK_RX_SAMPLES_PER_ENV;

    /* Check if we accumulated enough for a bit decision */
    while (s_bit_phase_acc >= ASK_RX_SAMPLES_PER_BIT)
    {
        uint8_t bit;
        s_bit_phase_acc = (uint16_t)(s_bit_phase_acc - ASK_RX_SAMPLES_PER_BIT);

        /* Majority vote */
        bit = (s_bit_vote_ones * 2u >= s_bit_vote_total) ? 1u : 0u;

#if DEBUG_BIT_DECISION
        printf("BIT: %d (votes=%d/%d, phase=%d->%d)\r\n",
               bit, s_bit_vote_ones, s_bit_vote_total,
               prev_phase, s_bit_phase_acc);
        prev_phase = s_bit_phase_acc;
#endif

        s_bit_vote_ones  = 0;
        s_bit_vote_total = 0;
        s_OnNewBit(bit);
    }

    /* Periodically dump envelope statistics */
    ASK_RX_DumpEnvelopeStats();
}

/* Modified state machine with debug output */
/* Add these debug prints to your s_OnNewBit function in each case */
static void s_OnNewBit_Diag(uint8_t bit)
{
    const char *prev_state = ASK_RX_StateName();

    s_last_bit_tick = HAL_GetTick();
    s_shift_reg = (uint16_t)((s_shift_reg << 1) | (bit & 1u));
    if (s_bits_in_reg < 16) s_bits_in_reg++;

    switch (s_state)
    {
    case RX_HUNT:
        if (s_bits_in_reg == 16)
        {
            uint16_t word = s_shift_reg & 0xFFFFu;
            uint8_t  errs = s_PopCount16((uint16_t)(word ^ ASK_PREAMBLE_WORD));
#if DEBUG_STATE
            printf("HUNT: word=0x%04X, errs=%d, threshold=%d\r\n",
                   word, errs, ASK_RX_PREAMBLE_MAX_ERRORS);
#endif
            if (errs <= ASK_RX_PREAMBLE_MAX_ERRORS)
            {
#if DEBUG_STATE
                printf("  -> PREAMBLE FOUND, entering TYPE state\r\n");
#endif
                s_state = RX_TYPE;
                s_bits_in_reg = 0;
                s_shift_reg = 0;
            }
            else
            {
                s_shift_reg &= 0x7FFFu;
                s_bits_in_reg = 15;
            }
        }
        break;

    case RX_TYPE:
        if (s_bits_in_reg == 8)
        {
            s_frame_type = (uint8_t)(s_shift_reg & 0xFFu);
#if DEBUG_STATE
            printf("TYPE: 0x%02X\r\n", s_frame_type);
#endif
            s_bits_in_reg = 0;
            s_shift_reg = 0;
            s_state = RX_LEN;
        }
        break;

    case RX_LEN:
        if (s_bits_in_reg == 8)
        {
            s_frame_len = (uint8_t)(s_shift_reg & 0xFFu);
#if DEBUG_STATE
            printf("LEN: %d bytes\r\n", s_frame_len);
#endif
            s_bits_in_reg = 0;
            s_shift_reg = 0;
            s_payload_idx = 0;
            if (s_frame_len > ASK_MAX_PAYLOAD)
            {
#if DEBUG_STATE
                printf("  -> Invalid length, returning to HUNT\r\n");
#endif
                s_ResetToHunt();
            }
            else if (s_frame_len == 0)
            {
                s_state = RX_CRC_HI;
            }
            else
            {
                s_state = RX_PAYLOAD;
            }
        }
        break;

    case RX_PAYLOAD:
        if (s_bits_in_reg == 8)
        {
            s_payload_buf[s_payload_idx++] = (uint8_t)(s_shift_reg & 0xFFu);
#if DEBUG_STATE
            printf("PAYLOAD[%d]: 0x%02X\r\n", s_payload_idx-1, s_payload_buf[s_payload_idx-1]);
#endif
            s_bits_in_reg = 0;
            s_shift_reg = 0;
            if (s_payload_idx == s_frame_len)
            {
#if DEBUG_STATE
                printf("  -> Payload complete, entering CRC_HI\r\n");
#endif
                s_state = RX_CRC_HI;
            }
        }
        break;

    case RX_CRC_HI:
        if (s_bits_in_reg == 8)
        {
            s_crc_hi = (uint8_t)(s_shift_reg & 0xFFu);
#if DEBUG_STATE
            printf("CRC_HI: 0x%02X\r\n", s_crc_hi);
#endif
            s_bits_in_reg = 0;
            s_shift_reg = 0;
            s_state = RX_CRC_LO;
        }
        break;

    case RX_CRC_LO:
        if (s_bits_in_reg == 8)
        {
            uint8_t  crc_buf[2 + ASK_MAX_PAYLOAD];
            uint16_t crc_calc, crc_recv;

            s_crc_lo = (uint8_t)(s_shift_reg & 0xFFu);
            crc_recv = (uint16_t)(((uint16_t)s_crc_hi << 8) | s_crc_lo);

            /* Compute CRC */
            crc_buf[0] = s_frame_type;
            crc_buf[1] = s_frame_len;
            if (s_frame_len)
                memcpy(&crc_buf[2], s_payload_buf, s_frame_len);
            crc_calc = ASK_CRC16_Calc(crc_buf, (uint16_t)(2u + s_frame_len));

#if DEBUG_CRC_FAIL || DEBUG_FRAME
            printf("CRC: recv=0x%04X, calc=0x%04X", crc_recv, crc_calc);
#endif

            if (crc_calc == crc_recv)
            {
#if DEBUG_FRAME
                printf("  -> MATCH! Frame valid, type=%d, len=%d\r\n",
                       s_frame_type, s_frame_len);
#endif
                s_last_type = s_frame_type;
                s_last_len = s_frame_len;
                if (s_frame_len)
                    memcpy(s_last_payload, s_payload_buf, s_frame_len);
                s_frame_ready_flag = 1;

                ASK_RX_OnFrameReceived(s_frame_type, s_payload_buf, s_frame_len);
            }
            else
            {
#if DEBUG_CRC_FAIL
                printf("  -> FAIL, returning to HUNT\r\n");
#endif
            }

            s_bits_in_reg = 0;
            s_shift_reg = 0;
            s_ResetToHunt();
        }
        break;
    }
}

/* Modified timeout handling with debug */
void ASK_RX_Process_Diag(void)
{
    static const char *last_state = "HUNT";
    const char *curr_state = ASK_RX_StateName();

    /* Print state changes */
    if (curr_state != last_state)
    {
        printf("STATE: %s -> %s\r\n", last_state, curr_state);
        last_state = curr_state;
    }

    if (s_state != RX_HUNT)
    {
        uint32_t now = HAL_GetTick();
        if ((now - s_last_bit_tick) > ASK_RX_BIT_TIMEOUT_MS)
        {
#if DEBUG_TIMEOUT
            printf("TIMEOUT: %d ms without bits, returning to HUNT\r\n",
                   (now - s_last_bit_tick));
#endif
            s_ResetToHunt();
        }
    }
}

/* ============================================================================
   QUICK DIAGNOSTIC - Add this to main() to test signal path
   ============================================================================*/

/* Call this from main() in a loop to see what's happening */
void ASK_RX_DiagnosticLoop(void)
{
    /* Print RX state every 500ms */
    static uint32_t last_print = 0;
    uint32_t now = HAL_GetTick();

    if (now - last_print > 500)
    {
        printf("RX State: %s, bits_in_reg=%d, shift_reg=0x%04X\r\n",
               ASK_RX_StateName(), s_bits_in_reg, s_shift_reg);
        last_print = now;
    }

    ASK_RX_Process_Diag();
}

/* ============================================================================
   MANUAL CALIBRATION HELPER - Call to find correct threshold
   ============================================================================*/

/* Sample envelope values with LED on and off to find optimal threshold */
void ASK_RX_Calibrate(void)
{
    uint32_t env_on_sum = 0, env_off_sum = 0;
    uint16_t i, samples = 100;

    printf("\r\n=== CALIBRATION MODE ===\r\n");
    printf("1. Turn LED OFF and press key...\r\n");
    // Wait for key press
    HAL_Delay(2000);

    for (i = 0; i < samples; i++)
    {
        HAL_Delay(10);
        // env_off_sum += read_current_envelope();  // You'll need to expose this
    }
    uint32_t env_off_avg = env_off_sum / samples;
    printf("LED OFF: avg envelope = %lu\r\n", env_off_avg);

    printf("2. Turn LED ON and press key...\r\n");
    HAL_Delay(2000);

    for (i = 0; i < samples; i++)
    {
        HAL_Delay(10);
        // env_on_sum += read_current_envelope();
    }
    uint32_t env_on_avg = env_on_sum / samples;
    printf("LED ON: avg envelope = %lu\r\n", env_on_avg);

    /* Recommended threshold = midpoint between ON and OFF averages */
    uint32_t recommended_threshold = (env_off_avg + env_on_avg) / 2;
    printf("RECOMMENDED THRESHOLD: %lu\r\n", recommended_threshold);
    printf("Update ASK_RX_ENV_THRESHOLD in ask_rx.h\r\n");
    printf("========================\r\n\r\n");
}

/* ============================================================================
   TROUBLESHOOTING GUIDE
   ============================================================================*/

/*
Problem solving based on debug output:

1. NO ENVELOPE VALUES (env always ~0 or constant):
   -> Check ADC is sampling correctly
   -> Check TIM3 is triggering ADC
   -> Check PB11 is connected correctly

2. ENVELOPE VALUES TOO LOW (< 2000):
   -> Signal too weak
   -> Check LED is actually on
   -> Check photodiode alignment
   -> Check amplifier gain

3. ENVELOPE VALUES TOO HIGH (> 20000):
   -> Possible ADC saturation
   -> Reduce amplifier gain
   -> Check DC offset is correct

4. NO BIT DECISIONS (BIT output never appears):
   -> Check ASK_RX_SAMPLES_PER_BIT matches TX baud rate
   -> TX: 1kHz = 1000 bits/sec
   -> RX ADC: ~180kHz = 180 samples/bit
   -> Verify TIM3 configuration

5. NEVER FINDS PREAMBLE (always in HUNT state):
   -> Check ASK_RX_DC_OFFSET calibration
   -> Check ASK_RX_ENV_THRESHOLD (see calibration helper)
   -> Check TX is actually sending correct preamble 0xAAE4

6. FINDS PREAMBLE BUT CRC FAILS:
   -> Good! Signal path works
   -> Check bit timing alignment (phase drift)
   -> Try increasing ASK_RX_PREAMBLE_MAX_ERRORS

7. STATE MACHINE STUCK IN PAYLOAD:
   -> Payload length might be wrong
   -> Check LEN byte is received correctly

8. TIMEOUTS FREQUENT:
   -> Signal intermittent
   -> Reduce distance/alignment
   -> Increase ASK_RX_BIT_TIMEOUT_MS
*/