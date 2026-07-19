/**
  ******************************************************************************
  * @file    ask_rx.c
  * @brief   2ASK receiver - envelope detection + bit slicer + frame decoder
  ******************************************************************************
  */
#include "ask_rx.h"
#include <string.h>

/*============================================================================*
 *                              State machine                                 *
 *============================================================================*/
typedef enum {
    RX_HUNT = 0,    /* looking for preamble 0xAAE4           */
    RX_TYPE,        /* reading 8-bit type                    */
    RX_LEN,         /* reading 8-bit length                  */
    RX_PAYLOAD,     /* reading LEN*8 payload bits            */
    RX_CRC_HI,      /* reading CRC high byte                 */
    RX_CRC_LO       /* reading CRC low byte                  */
} RX_State_t;

static RX_State_t s_state = RX_HUNT;

/* Bit slicer state */
static uint16_t s_bit_phase_acc = 0;    /* in ADC-sample units, [0..179]   */
static uint8_t  s_bit_vote_ones = 0;    /* count of "1" envelope votes     */
static uint8_t  s_bit_vote_total = 0;   /* count of envelope votes          */

/* Bit-stream state */
static uint16_t s_shift_reg = 0;        /* incoming bits, MSB first         */
static uint8_t  s_bits_in_reg = 0;      /* number of valid bits in shift_reg*/
static uint32_t s_last_bit_tick = 0;    /* HAL_GetTick() of last bit decision*/

/* Frame assembly state */
static uint8_t  s_frame_type = 0;
static uint8_t  s_frame_len  = 0;
static uint8_t  s_payload_idx = 0;
static uint8_t  s_payload_buf[ASK_MAX_PAYLOAD];
static uint8_t  s_crc_hi = 0;

/* Last decoded frame (for polling API) */
static volatile uint8_t  s_frame_ready_flag = 0;
static uint8_t  s_last_type = 0;
static uint8_t  s_last_len  = 0;
static uint8_t  s_last_payload[ASK_MAX_PAYLOAD];

/*============================================================================*
 *                              Helpers                                       *
 *============================================================================*/
static uint8_t s_PopCount16(uint16_t x)
{
    uint8_t n = 0;
    while (x) { n += (uint8_t)(x & 1u); x >>= 1; }
    return n;
}

static void s_ResetToHunt(void)
{
    s_state         = RX_HUNT;
    s_shift_reg     = 0;
    s_bits_in_reg   = 0;
    s_bit_phase_acc = 0;
    s_bit_vote_ones = 0;
    s_bit_vote_total = 0;
    s_payload_idx   = 0;
}

/*============================================================================*
 *                        Bit decision (one new bit)                         *
 *============================================================================*/
static void s_OnNewBit(uint8_t bit)
{
    s_last_bit_tick = HAL_GetTick();

    /* Shift into 16-bit register (MSB first) */
    s_shift_reg = (uint16_t)((s_shift_reg << 1) | (bit & 1u));
    if (s_bits_in_reg < 16) s_bits_in_reg++;

    switch (s_state)
    {
    case RX_HUNT:
        /* Need at least 16 bits to test the preamble */
        if (s_bits_in_reg == 16)
        {
            uint16_t word = s_shift_reg & 0xFFFFu;
            uint8_t  errs = s_PopCount16((uint16_t)(word ^ ASK_PREAMBLE_WORD));
            if (errs <= ASK_RX_PREAMBLE_MAX_ERRORS)
            {
                /* Preamble found - advance to TYPE */
                s_state       = RX_TYPE;
                s_bits_in_reg = 0;
                s_shift_reg   = 0;
            }
            else
            {
                /* Slide 16-bit window: keep only the most recent 15 bits */
                s_shift_reg   &= 0x7FFFu;
                s_bits_in_reg  = 15;
            }
        }
        break;

    case RX_TYPE:
        if (s_bits_in_reg == 8)
        {
            s_frame_type  = (uint8_t)(s_shift_reg & 0xFFu);
            s_bits_in_reg = 0;
            s_shift_reg   = 0;
            s_state       = RX_LEN;
        }
        break;

    case RX_LEN:
        if (s_bits_in_reg == 8)
        {
            s_frame_len   = (uint8_t)(s_shift_reg & 0xFFu);
            s_bits_in_reg = 0;
            s_shift_reg   = 0;
            s_payload_idx = 0;
            if (s_frame_len > ASK_MAX_PAYLOAD)
            {
                s_ResetToHunt();        /* invalid length -> re-hunt */
            }
            else if (s_frame_len == 0)
            {
                s_state = RX_CRC_HI;    /* no payload */
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
            s_bits_in_reg = 0;
            s_shift_reg   = 0;
            if (s_payload_idx == s_frame_len)
                s_state = RX_CRC_HI;
        }
        break;

    case RX_CRC_HI:
        if (s_bits_in_reg == 8)
        {
            s_crc_hi      = (uint8_t)(s_shift_reg & 0xFFu);
            s_bits_in_reg = 0;
            s_shift_reg   = 0;
            s_state       = RX_CRC_LO;
        }
        break;

    case RX_CRC_LO:
        if (s_bits_in_reg == 8)
        {
            uint8_t  crc_buf[2 + ASK_MAX_PAYLOAD];
            uint16_t crc_calc, crc_recv;

            s_crc_lo = (uint8_t)(s_shift_reg & 0xFFu);
            s_bits_in_reg = 0;
            s_shift_reg   = 0;

            /* Verify CRC over [type, len, payload] */
            crc_buf[0] = s_frame_type;
            crc_buf[1] = s_frame_len;
            if (s_frame_len)
                memcpy(&crc_buf[2], s_payload_buf, s_frame_len);
            crc_calc = ASK_CRC16_Calc(crc_buf, (uint16_t)(2u + s_frame_len));
            crc_recv = (uint16_t)(((uint16_t)s_crc_hi << 8) | s_crc_lo);

            if (crc_calc == crc_recv)
            {
                /* Frame valid - cache it for the polling API and notify app */
                s_last_type = s_frame_type;
                s_last_len  = s_frame_len;
                if (s_frame_len)
                    memcpy(s_last_payload, s_payload_buf, s_frame_len);
                s_frame_ready_flag = 1;

                ASK_RX_OnFrameReceived(s_frame_type, s_payload_buf, s_frame_len);
            }
            /* Either way, go back to hunting for the next preamble */
            s_ResetToHunt();
        }
        break;

    default:
        s_ResetToHunt();
        break;
    }
}

/*============================================================================*
 *                              Public API                                    *
 *============================================================================*/
void ASK_RX_Init(void)
{
    s_ResetToHunt();
    s_frame_ready_flag = 0;
    s_last_type = 0;
    s_last_len  = 0;
    s_last_bit_tick = HAL_GetTick();
}

void ASK_RX_PushEnvelope(uint32_t env_sum_40)
{
    uint8_t vote;

    /* One envelope = one binary vote (carrier present / absent) */
    vote = (env_sum_40 > ASK_RX_ENV_THRESHOLD) ? 1u : 0u;
    s_bit_vote_ones  += vote;
    s_bit_vote_total += 1u;

    /* Advance bit-phase accumulator by 40 ADC samples */
    s_bit_phase_acc += ASK_RX_SAMPLES_PER_ENV;

    /* If we have accumulated >= 1 bit's worth of samples, decide a bit */
    while (s_bit_phase_acc >= ASK_RX_SAMPLES_PER_BIT)
    {
        uint8_t bit;
        s_bit_phase_acc = (uint16_t)(s_bit_phase_acc - ASK_RX_SAMPLES_PER_BIT);

        /* Majority vote across all envelopes that fell in this bit window */
        bit = (s_bit_vote_ones * 2u >= s_bit_vote_total) ? 1u : 0u;

        /* Reset vote accumulators for the next bit (the current envelope's
           remainder, if any, is reused as the first vote of the next bit) */
        s_bit_vote_ones  = 0;
        s_bit_vote_total = 0;

        s_OnNewBit(bit);

        /* NOTE: at most one bit is decided per envelope in practice (since
           40 < 180), so the while-loop normally executes once. */
    }
}

void ASK_RX_Process(void)
{
    /* Watchdog: if we are mid-frame and no bit has arrived for too long,
       declare loss of sync and return to HUNT. */
    if (s_state != RX_HUNT)
    {
        uint32_t now = HAL_GetTick();
        if ((now - s_last_bit_tick) > ASK_RX_BIT_TIMEOUT_MS)
            s_ResetToHunt();
    }
}

const char *ASK_RX_StateName(void)
{
    static const char *names[] = {
        "HUNT", "TYPE", "LEN", "DATA", "CRC1", "CRC2"
    };
    return names[(int)s_state];
}

uint8_t ASK_RX_FrameReady(void)
{
    uint8_t f = s_frame_ready_flag;
    s_frame_ready_flag = 0;
    return f;
}

void ASK_RX_GetFrame(uint8_t *type, uint8_t *len, uint8_t *payload_buf)
{
    *type = s_last_type;
    *len  = s_last_len;
    if (s_last_len && payload_buf)
        memcpy(payload_buf, s_last_payload, s_last_len);
}

/* Weak default implementation - the application should override this. */
__weak void ASK_RX_OnFrameReceived(uint8_t type, const uint8_t *payload, uint16_t len)
{
    (void)type; (void)payload; (void)len;
}
