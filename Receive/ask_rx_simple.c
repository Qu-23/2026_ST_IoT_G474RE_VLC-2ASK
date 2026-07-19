/**
  ******************************************************************************
  * @file    ask_rx_simple.c
  * @brief   Simplified RX implementation for link verification
  ******************************************************************************
  */

#include "ask_rx_simple.h"
#include <stdio.h>

/* Simple RX state */
static SimpleRX_Stats_t s_stats = {0};

/* Bit decision parameters - copy from ask_rx.h or adjust */
#define SIMPLE_ENV_THRESHOLD     6000u
#define SIMPLE_SAMPLES_PER_BIT   180u
#define SIMPLE_SAMPLES_PER_ENV   40u

/* Private state */
static uint16_t s_bit_phase_acc = 0;
static uint8_t  s_bit_vote_ones = 0;
static uint8_t  s_bit_vote_total = 0;

/* Initialize */
void SimpleRX_Init(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    s_bit_phase_acc = 0;
    s_bit_vote_ones = 0;
    s_bit_vote_total = 0;
}

/* Push envelope - simplified bit slicing */
void SimpleRX_PushEnvelope(uint32_t env_sum_40)
{
    /* Update envelope statistics */
    s_stats.envelope_sum += env_sum_40;
    s_stats.envelope_count++;

    /* Vote for this envelope */
    uint8_t vote = (env_sum_40 > SIMPLE_ENV_THRESHOLD) ? 1u : 0u;
    s_bit_vote_ones += vote;
    s_bit_vote_total++;

    /* Advance phase accumulator */
    s_bit_phase_acc += SIMPLE_SAMPLES_PER_ENV;

    /* Check if we have a complete bit */
    while (s_bit_phase_acc >= SIMPLE_SAMPLES_PER_BIT)
    {
        uint8_t bit;
        s_bit_phase_acc -= SIMPLE_SAMPLES_PER_BIT;

        /* Majority vote */
        bit = (s_bit_vote_ones * 2u >= s_bit_vote_total) ? 1u : 0u;

        /* Update statistics */
        s_stats.total_bits++;
        if (bit) s_stats.ones_count++;

        /* Shift into 16-bit register */
        s_stats.last_16_bits = (s_stats.last_16_bits << 1) | bit;

        /* Check for common test patterns */
        if ((s_stats.last_16_bits & 0xFF) == 0x55)  // 01010101
            s_stats.pattern_0101++;
        if ((s_stats.last_16_bits & 0xFF) == 0xAA)  // 10101010
            s_stats.pattern_1010++;

        /* Reset for next bit */
        s_bit_vote_ones = 0;
        s_bit_vote_total = 0;
    }
}

/* Get statistics */
void SimpleRX_GetStats(SimpleRX_Stats_t *stats)
{
    if (stats)
        memcpy(stats, &s_stats, sizeof(SimpleRX_Stats_t));
}

/* Test: receive samples and check for pattern match */
uint8_t SimpleRX_TestPattern(uint8_t pattern, uint16_t samples)
{
    /* Reset for test */
    SimpleRX_Init();

    /* Wait for specified number of bits */
    while (s_stats.total_bits < samples)
    {
        /* Processing happens in ISR */
        HAL_Delay(10);
    }

    /* Check if last 8 bits match pattern */
    return ((s_stats.last_16_bits & 0xFF) == pattern) ? 1 : 0;
}

/* Get current envelope level (for calibration) */
uint32_t SimpleRX_GetEnvelopeLevel(void)
{
    if (s_stats.envelope_count == 0)
        return 0;
    return s_stats.envelope_sum / s_stats.envelope_count;
}

/* ============================================================================
   HOW TO USE - Add this to your main.c for quick test
   ============================================================================*/

/*
In main.c, replace the full RX initialization with:

    SimpleRX_Init();

And in the ADC DMA callback, replace ASK_RX_PushEnvelope() with:

    SimpleRX_PushEnvelope(env);

Then add this to your main loop to see what's happening:

    static uint32_t last_stat = 0;
    if (HAL_GetTick() - last_stat > 1000)
    {
        SimpleRX_Stats_t stats;
        SimpleRX_GetStats(&stats);

        printf("=== SIMPLE RX ===\r\n");
        printf("Bits: %lu, Ones: %lu (%lu%%)\r\n",
               stats.total_bits, stats.ones_count,
               (stats.ones_count * 100) / (stats.total_bits ? stats.total_bits : 1));
        printf("Last 16 bits: 0x%04lX\r\n", stats.last_16_bits);
        printf("0x55 patterns: %lu, 0xAA patterns: %lu\r\n",
               stats.pattern_0101, stats.pattern_1010);
        printf("Envelope avg: %lu\r\n",
               stats.envelope_count ? (stats.envelope_sum / stats.envelope_count) : 0);
        printf("==================\r\n");

        last_stat = HAL_GetTick();
    }

TEST PROCEDURE:
1. Compile with SimpleRX instead of full ASK_RX
2. On TX side, send alternating bits (modify milestone2 code to send 0x55 repeatedly)
3. Check if RX displays "0x55 patterns: >0"
4. If yes -> optical link works! The problem is in frame sync/timing.
5. If no -> check envelope level, adjust threshold, check alignment.
*/