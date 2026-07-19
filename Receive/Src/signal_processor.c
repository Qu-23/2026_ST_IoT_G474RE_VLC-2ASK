/**
  ******************************************************************************
  * @file    signal_processor.c
  * @brief   Signal processor - Break detection + bit sync + frame parser
  *
  * Layer 0: ADC → threshold decision → bit stream
  * Layer 1: Break detection (continuous 0 = frame start marker)
  * Layer 2: Edge detection + bit synchronization (simple, no debounce)
  * Layer 3: Two-stage preamble confirmation + frame parser
  *
  * Two-stage sync (per teacher's suggestion):
  *   1. First 0xAAE4 match → enter CONFIRM state
  *   2. Continue scanning, look for second 0xAAE4
  *   3. Second match → confirmed, proceed to TYPE/LEN/PAYLOAD/CRC
  *   4. No second match within timeout → false alarm, back to SEARCH
  ******************************************************************************
  */

#include "main.h"
#include "signal_processor.h"
#include <string.h>

/*============================================================================*
 *                         Layer 1: Bit synchronization                       *
 *============================================================================*/
static SignalStats_t s_stats;
static uint32_t s_threshold = ENVELOPE_THRESHOLD;

static uint8_t  s_last_bit = 0;
static uint32_t s_phase = 0;
static uint8_t  s_sync_ok = 0;
static uint32_t s_no_edge_count = 0;

/* Over-sampling integration: accumulate all samples in a bit window,
 * decide by majority at window end. Robust to phase drift during
 * long runs of 1s or 0s where no edge is available to calibrate phase. */
static uint8_t  s_window_ones = 0;   /* count of 1-samples in current bit window */

/* Manchester decoding: 2 symbols → 1 data bit.
 * IEEE 802.3: (1,0)→1, (0,1)→0. Each bit has a transition, so bit sync
 * never drifts regardless of data content. */
static uint8_t  s_mcr_first = 0;       /* first symbol of pair */
static uint8_t  s_mcr_has_first = 0;   /* 1 = first symbol received, waiting for 2nd */

static uint32_t s_adc_avg = 0;

/* Sampling phase: sample earlier than midpoint to avoid narrow 0-bit boundary.
 * With >50% duty cycle, 0-bit is narrower than 1-bit; phase=10 may land on
 * the trailing edge of a 0-bit. phase=8 gives 2-sample margin. */
#define BIT_SAMPLE_PHASE    8u

/* Baseline tracking: measure 0-level during GAP, threshold = baseline + offset */
#define SIGNAL_HALF_SWING   700u   /* Half of signal swing (adjust after scope measurement) */
static uint32_t s_baseline = 0;        /* ADC 0-level measured during GAP */
static uint8_t  s_baseline_ready = 0;  /* 1 = baseline measured, use adaptive threshold */

/*============================================================================*
 *                    Layer 1b: Break detection (frame start)                  *
 *============================================================================*/
#define BREAK_ZERO_BITS     8
#define BREAK_TIMEOUT_BITS  2000   /* Must exceed: sync+2*preamble+confirm+header+payload+crc */

static uint8_t  s_in_break = 0;
static uint32_t s_zero_run = 0;
static uint16_t s_break_bit_count = 0;

/*============================================================================*
 *               Layer 2: Frame parser with two-stage sync                    *
 *============================================================================*/
typedef enum {
    FRAME_SEARCH,          /* Searching for first 0xAAE4 */
    FRAME_CONFIRM_PREAMBLE,/* Found 1st preamble, waiting for 2nd */
    FRAME_RECV_TYPE,
    FRAME_RECV_LEN,
    FRAME_RECV_PAYLOAD,
    FRAME_RECV_CRC
} FrameState_t;

/* Confirmation timeout: 2nd preamble at ~320 bits after 1st, plus margin */
#define PREAMBLE_CONFIRM_DISTANCE  48
#define PREAMBLE_CONFIRM_TIMEOUT   (PREAMBLE_CONFIRM_DISTANCE + 16)  /* 64 bits */

static FrameState_t s_frame_state = FRAME_SEARCH;
static uint16_t s_preamble_sr = 0;
static uint8_t  s_byte_buf = 0;
static uint8_t  s_bit_cnt = 0;
static uint8_t  s_frame_type = 0;
static uint8_t  s_frame_len = 0;
static uint8_t  s_frame_payload[ASK_MAX_PAYLOAD];
static uint16_t s_frame_crc = 0;
static uint16_t s_payload_idx = 0;
static uint16_t s_confirm_count = 0;  /* Bits since 1st preamble match */

/* Bit stream capture: 128 bits starting 16 bits before 2nd preamble match */
#define CAPTURE_BITS  128
static uint8_t  s_capture_buf[CAPTURE_BITS / 8];
static uint8_t  s_capture_idx = 0;
static uint8_t  s_capturing = 0;
#define CAPTURE_PRE_MATCH  16  /* start capturing this many bits before 2nd preamble */

static RxFrame_t s_rx_frame;

/*============================================================================*
 *                              Init                                          *
 *============================================================================*/
void SignalProcessor_Init(void)
{
    memset(&s_stats, 0, sizeof(SignalStats_t));
    s_last_bit = 0;
    s_phase = 0;
    s_sync_ok = 0;
    s_no_edge_count = 0;
    s_window_ones = 0;
    s_mcr_first = 0;
    s_mcr_has_first = 0;
    s_adc_avg = 0;
    s_baseline = 0;
    s_baseline_ready = 0;

    s_in_break = 0;
    s_zero_run = 0;
    s_break_bit_count = 0;

    s_frame_state = FRAME_SEARCH;
    s_preamble_sr = 0;
    s_bit_cnt = 0;
    s_payload_idx = 0;
    s_confirm_count = 0;
    s_capturing = 0;
    s_capture_idx = 0;
    memset(s_capture_buf, 0, sizeof(s_capture_buf));
    memset(&s_rx_frame, 0, sizeof(RxFrame_t));
}

/*============================================================================*
 *              Bit capture helper (records bits after frame sync)             *
 *============================================================================*/
static void s_CaptureBit(uint8_t bit)
{
    if (!s_capturing || s_capture_idx >= CAPTURE_BITS)
        return;

    uint8_t byte_idx = s_capture_idx >> 3;
    uint8_t bit_pos  = s_capture_idx & 7;

    if (bit)
        s_capture_buf[byte_idx] |= (uint8_t)(0x80 >> bit_pos);
    else
        s_capture_buf[byte_idx] &= (uint8_t)~(0x80 >> bit_pos);

    s_capture_idx++;
}

/*============================================================================*
 *              Layer 2: Frame parser - process one bit                       *
 *============================================================================*/
static void FrameParser_ProcessBit(uint8_t bit)
{
    switch (s_frame_state)
    {
    case FRAME_SEARCH:
        s_preamble_sr = (uint16_t)((s_preamble_sr << 1) | bit);
        if (s_preamble_sr == ASK_PREAMBLE_WORD)
        {
            /* First preamble match - enter confirmation stage */
            s_frame_state = FRAME_CONFIRM_PREAMBLE;
            s_confirm_count = 0;
            s_preamble_sr = 0;
        }
        break;

    case FRAME_CONFIRM_PREAMBLE:
        /* Continue scanning for 2nd preamble */
        s_preamble_sr = (uint16_t)((s_preamble_sr << 1) | bit);
        s_confirm_count++;

        /* Start capturing when close to expected 2nd preamble position.
         * PREAMBLE_CONFIRM_DISTANCE ≈ 48 bits. Start capture ~16 bits before. */
        if (s_confirm_count >= (PREAMBLE_CONFIRM_DISTANCE - CAPTURE_PRE_MATCH) && !s_capturing)
        {
            s_capturing = 1;
            s_capture_idx = 0;
            memset(s_capture_buf, 0, sizeof(s_capture_buf));
        }
        if (s_capturing)
            s_CaptureBit(bit);

        if (s_preamble_sr == ASK_PREAMBLE_WORD)
        {
            /* Second preamble match - proceed directly to TYPE.
             * Manchester encoding eliminates phase drift, no skip needed. */
            s_frame_state = FRAME_RECV_TYPE;
            s_bit_cnt = 0;
            s_capturing = 1;
            s_capture_idx = 0;
            memset(s_capture_buf, 0, sizeof(s_capture_buf));
        }
        else if (s_confirm_count >= PREAMBLE_CONFIRM_TIMEOUT)
        {
            /* Timeout - 1st preamble was false alarm */
            s_frame_state = FRAME_SEARCH;
            s_preamble_sr = 0;
            s_capturing = 0;
        }
        break;

    case FRAME_RECV_TYPE:
        s_byte_buf = (uint8_t)((s_byte_buf << 1) | bit);
        s_CaptureBit(bit);
        if (++s_bit_cnt >= 8)
        {
            s_frame_type = s_byte_buf;
            s_stats.last_frame_type = s_byte_buf;
            s_frame_state = FRAME_RECV_LEN;
            s_bit_cnt = 0;
        }
        break;

    case FRAME_RECV_LEN:
        s_byte_buf = (uint8_t)((s_byte_buf << 1) | bit);
        s_CaptureBit(bit);
        if (++s_bit_cnt >= 8)
        {
            s_frame_len = s_byte_buf;
            s_stats.last_frame_len = s_byte_buf;
            s_payload_idx = 0;
            s_bit_cnt = 0;
            if (s_frame_len == 0 || s_frame_len > ASK_MAX_PAYLOAD)
                s_frame_state = FRAME_RECV_CRC;
            else
                s_frame_state = FRAME_RECV_PAYLOAD;
        }
        break;

    case FRAME_RECV_PAYLOAD:
        s_byte_buf = (uint8_t)((s_byte_buf << 1) | bit);
        s_CaptureBit(bit);
        if (++s_bit_cnt >= 8)
        {
            s_frame_payload[s_payload_idx] = s_byte_buf;
            s_payload_idx++;
            s_bit_cnt = 0;
            if (s_payload_idx >= s_frame_len)
                s_frame_state = FRAME_RECV_CRC;
        }
        break;

    case FRAME_RECV_CRC:
        s_byte_buf = (uint8_t)((s_byte_buf << 1) | bit);
        s_CaptureBit(bit);
        if (++s_bit_cnt == 8)
        {
            s_frame_crc = (uint16_t)s_byte_buf << 8;
        }
        else if (s_bit_cnt >= 16)
        {
            s_frame_crc |= s_byte_buf;
            s_capturing = 0;  /* Stop capture */

            /* Validate CRC over [TYPE, LEN, PAYLOAD] */
            uint8_t crc_buf[2 + ASK_MAX_PAYLOAD];
            crc_buf[0] = s_frame_type;
            crc_buf[1] = s_frame_len;
            if (s_frame_len > 0)
                memcpy(&crc_buf[2], s_frame_payload, s_frame_len);
            uint16_t calc_crc = ASK_CRC16_Calc(crc_buf, 2 + s_frame_len);

            if (calc_crc == s_frame_crc)
            {
                s_rx_frame.type = s_frame_type;
                s_rx_frame.len = s_frame_len;
                memcpy(s_rx_frame.payload, s_frame_payload, s_frame_len);
                s_rx_frame.crc_ok = 1;
                s_stats.frame_count++;
                s_stats.frame_ready = 1;
            }
            else
            {
                s_stats.frame_crc_err++;
                s_stats.last_crc_rx = s_frame_crc;
                s_stats.last_crc_calc = calc_crc;
            }

            /* Back to search */
            s_frame_state = FRAME_SEARCH;
            s_preamble_sr = 0;
            s_bit_cnt = 0;
            s_in_break = 0;
            s_break_bit_count = 0;
        }
        break;
    }
}

/*============================================================================*
 *                    Layer 0/1: Process ADC data (DMA callback)               *
 *============================================================================*/
void SignalProcessor_ProcessADC(uint16_t *adc_data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        uint16_t sample = adc_data[i];

        /* Update ADC average */
        s_adc_avg = (s_adc_avg * 15 + sample) / 16;

        /*--- Baseline tracking during GAP ---*/
        /* While in break (GAP), ADC sees 0-level → track as baseline.
         * After enough samples, lock baseline and enable adaptive threshold. */
        if (s_in_break && !s_baseline_ready)
        {
            s_baseline = (s_baseline * 7 + sample) / 8;  /* smooth average */
            /* Lock baseline after ~160 samples (8 bit periods at 20 samples/bit) */
            static uint16_t s_baseline_samples = 0;
            s_baseline_samples++;
            if (s_baseline_samples >= 160)
            {
                s_baseline_ready = 1;
                s_baseline_samples = 0;
            }
        }

        /* Adaptive threshold: use baseline + half_swing if calibrated, else fixed */
        uint32_t effective_threshold = s_baseline_ready
            ? (s_baseline + SIGNAL_HALF_SWING)
            : s_threshold;

        /* Direct threshold decision */
        uint8_t bit = (sample > effective_threshold) ? 1 : 0;

        /*--- Layer 1b: Break detection (sample-level) ---*/
        if (bit == 0)
        {
            s_zero_run++;
            if (s_zero_run >= BREAK_ZERO_BITS * SAMPLES_PER_BIT)
            {
                if (!s_in_break)
                {
                    s_in_break = 1;
                    s_break_bit_count = 0;
                    s_stats.break_count++;
                    s_frame_state = FRAME_SEARCH;
                    s_preamble_sr = 0;
                    s_bit_cnt = 0;
                    s_mcr_has_first = 0;   /* Reset Manchester pair alignment */
                    s_baseline_ready = 0;  /* Will be set after baseline settles */
                }
            }
        }
        else
        {
            s_zero_run = 0;
        }

        /*--- Layer 1: Edge detection + over-sampling integration ---*/
        if (bit != s_last_bit)
        {
            /* Edge = bit boundary. Reset phase and integration window. */
            s_phase = 0;
            s_window_ones = 0;
            s_sync_ok = 1;
            s_no_edge_count = 0;
        }
        else
        {
            s_no_edge_count++;
            if (s_no_edge_count > SAMPLES_PER_BIT * 50)
                s_sync_ok = 0;
        }

        /* Accumulate bit into current integration window */
        s_window_ones += bit;

        /* Update phase */
        s_phase++;
        if (s_phase >= SAMPLES_PER_BIT)
        {
            /* Symbol window complete - integrate-decide all SAMPLES_PER_BIT
             * samples into one symbol. */
            if (s_sync_ok)
            {
                uint8_t symbol = (s_window_ones >= (SAMPLES_PER_BIT / 2)) ? 1 : 0;

                /* Manchester decode: collect 2 symbols → 1 data bit.
                 * IEEE 802.3: (1,0)→1, (0,1)→0.
                 * On illegal pair (00/11), resync: discard 1st symbol,
                 * keep 2nd as new 1st, skip this bit. This auto-recovers
                 * from pair misalignment after GAP. */
                if (!s_mcr_has_first)
                {
                    s_mcr_first = symbol;
                    s_mcr_has_first = 1;
                }
                else
                {
                    uint8_t bit;
                    uint8_t valid = 1;
                    if (s_mcr_first == 1 && symbol == 0)
                        bit = 1;                    /* 10 → 1 */
                    else if (s_mcr_first == 0 && symbol == 1)
                        bit = 0;                    /* 01 → 0 */
                    else
                    {
                        /* 00/11: pair misalignment. Resync by keeping this
                         * symbol as new 1st, skip this bit. */
                        s_mcr_first = symbol;
                        valid = 0;
                    }

                    if (valid)
                    {
                        s_mcr_has_first = 0;

                        s_stats.total_bits++;
                        s_stats.last_16_bits = (s_stats.last_16_bits << 1) | bit;

                        uint8_t last_byte = (uint8_t)(s_stats.last_16_bits & 0xFF);
                        if (last_byte == 0x55) s_stats.pattern_55++;
                        if (last_byte == 0xAA) s_stats.pattern_AA++;

                        /* Layer 2: feed bit to frame parser ONLY when in break mode */
                        if (s_in_break)
                        {
                            FrameParser_ProcessBit(bit);
                            s_break_bit_count++;

                            if (s_break_bit_count >= BREAK_TIMEOUT_BITS)
                            {
                                s_in_break = 0;
                                s_break_bit_count = 0;
                                s_frame_state = FRAME_SEARCH;
                                s_preamble_sr = 0;
                                s_bit_cnt = 0;
                                s_capturing = 0;
                                s_mcr_has_first = 0;
                            }
                        }
                    }
                }
            }
            s_phase = 0;
            s_window_ones = 0;
        }

        s_last_bit = bit;
    }

    s_stats.env_avg = s_adc_avg;
}

/*============================================================================*
 *                              Get stats / frame                             *
 *============================================================================*/
static uint8_t s_GetFrameStateCode(void)
{
    switch (s_frame_state)
    {
    case FRAME_SEARCH:           return FRAME_STATE_SEARCH;
    case FRAME_CONFIRM_PREAMBLE: return FRAME_STATE_CONFIRM;
    case FRAME_RECV_TYPE:        return FRAME_STATE_TYPE;
    case FRAME_RECV_LEN:         return FRAME_STATE_LEN;
    case FRAME_RECV_PAYLOAD:     return FRAME_STATE_PAYLOAD;
    case FRAME_RECV_CRC:         return FRAME_STATE_CRC;
    default:                     return FRAME_STATE_SEARCH;
    }
}

void SignalProcessor_GetStats(SignalStats_t *stats)
{
    if (stats)
    {
        memcpy(stats, &s_stats, sizeof(SignalStats_t));
        stats->frame_state     = s_GetFrameStateCode();
        stats->preamble_sr     = s_preamble_sr;
        stats->break_bit_count = s_break_bit_count;
        stats->in_break        = s_in_break;
        stats->confirm_count   = s_confirm_count;
        /* Copy bit capture buffer */
        memcpy(stats->capture_buf, s_capture_buf, CAPTURE_BITS / 8);
        /* Adaptive threshold info */
        stats->baseline      = s_baseline;
        stats->eff_threshold = s_baseline_ready ? (s_baseline + SIGNAL_HALF_SWING) : s_threshold;
    }
}

void SignalProcessor_GetFrame(RxFrame_t *frame)
{
    if (frame)
    {
        memcpy(frame, &s_rx_frame, sizeof(RxFrame_t));
        s_stats.frame_ready = 0;
    }
}

void SignalProcessor_ResetStats(void)
{
    memset(&s_stats, 0, sizeof(SignalStats_t));
}

void SignalProcessor_SetThreshold(uint32_t threshold)
{
    s_threshold = threshold;
}
