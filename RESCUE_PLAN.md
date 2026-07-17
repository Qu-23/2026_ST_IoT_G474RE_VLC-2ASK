# 5-Day Rescue Plan - Quick Reference Guide

## DAY 1-2: Verify Optical Link (CRITICAL)

### Step 1: Test with Simplified RX (1-2 hours)

**Goal:** Verify the optical link can distinguish between LED ON and OFF

**Procedure:**

1. **Replace ask_rx.c with simple version temporarily:**
   - In `Receive/Src/main.c`:
     - Comment out: `#include "ask_rx.h"`
     - Add: `#include "ask_rx_simple.h"`
     - Replace: `ASK_RX_Init()` with `SimpleRX_Init()`
   - In `Receive/Src/adc.c`, HAL_ADC_ConvCpltCallback():
     - Replace: `ASK_RX_PushEnvelope(env)` with `SimpleRX_PushEnvelope(env)`

2. **On TX side, send test pattern:**
   - Modify `Transmit/Core/Src/main.c`, ProcessCommand():
     ```c
     case 'Z':  // Test pattern - alternate bits
         {
             // Send 0x55 (01010101) 20 times = 160 bits
             for (int i = 0; i < 20; i++) {
                 ASK_TX_SendFrame(ASK_TYPE_RAW, (uint8_t*)"\x55", 1);
                 HAL_Delay(10);
             }
         }
         break;
     ```

3. **Add diagnostic output to RX main loop:**
   ```c
   static uint32_t last_stat = 0;
   if (HAL_GetTick() - last_stat > 1000)
   {
       SimpleRX_Stats_t stats;
       SimpleRX_GetStats(&stats);

       printf("Bits: %lu, Ones: %lu%%, Last: 0x%04lX\r\n",
              stats.total_bits,
              (stats.ones_count * 100) / (stats.total_bits ?: 1),
              stats.last_16_bits);

       last_stat = HAL_GetTick();
   }
   ```

4. **Test:**
   - Connect USART1 from RX to PC (for printf output)
   - Start RX
   - Send 'Z' command from laptop to TX
   - **EXPECTED:** RX prints "0x55 patterns: >0"
   - **IF NOT WORKING:** See troubleshooting below

### Step 2: Calibrate Threshold (30 min)

**If envelope values are wrong:**

1. Check envelope output with LED OFF:
   - `printf("LED OFF env: %lu\r\n", SimpleRX_GetEnvelopeLevel());`

2. Check envelope output with LED ON (continuous carrier):
   - Modify TX to always send carrier (signal=1 in TIM2 ISR)

3. Set threshold midway between ON and OFF:
   - If OFF=2000, ON=12000 → Threshold=7000
   - Update `ASK_RX_ENV_THRESHOLD` in ask_rx.h

### Troubleshooting Simple RX:

| Symptom | Cause | Fix |
|---------|-------|-----|
| Envelope ~0 always | ADC not sampling | Check TIM3 trigger, ADC GPIO |
| Envelope constant (~2000) | No carrier detection | Check LED is on, check photodiode |
| Envelope ~4000 (no change) | Weak signal | Reduce distance, check alignment |
| Ones% ~50% but no pattern | Timing drift | Check TIM2/TIM3 frequencies match |

---

## DAY 2-3: Frame Synchronization

### Step 3: Enable Full RX with Debug (2-3 hours)

1. **Restore full ask_rx.c:**
   - Revert changes in Step 1

2. **Add debug output to ask_rx.c:**
   - Copy functions from `ask_rx_debug.c` into `ask_rx.c`
   - Enable `DEBUG_STATE`, `DEBUG_FRAME`, `DEBUG_CRC_FAIL`

3. **Test with simple command:**
   - Send 'H' (HELLO) from laptop to TX
   - **EXPECTED output:**
     ```
     STATE: HUNT -> TYPE
     TYPE: 0x02
     LEN: 5
     PAYLOAD[0]: 0x48 (H)
     PAYLOAD[1]: 0x45 (E)
     ...
     CRC: recv=0xXXXX, calc=0xXXXX -> MATCH!
     ```

### Common Frame Sync Issues:

| Issue | Symptom | Fix |
|-------|---------|-----|
| Stuck in HUNT | Never finds preamble 0xAAE4 | Check DC_OFFSET, increase PREAMBLE_MAX_ERRORS |
| Finds preamble but CRC fails | Frame decoded but wrong | Check bit timing, verify baud rate |
| Stuck in PAYLOAD | Wrong length byte | Debug LEN reception |

---

## DAY 3-4: End-to-End Demo

### Step 4: Connect Laptop → TX → RX → LCD (2 hours)

1. **Verify connections:**
   ```
   Laptop USB → TX USART1 (PA9/PA10)
   TX DAC2 (PA6) → LED driver → LED
   LED → Photodiode (BPW34) → RX ADC1 (PB11)
   RX SPI → LCD display
   ```

2. **Test sequence:**
   ```
   Laptop: "HELLO\r\n"
   TX: Sends TEXT frame with "HELLO"
   RX: Decodes → GUI displays "HELLO" on LCD
   ```

3. **Verify on LCD:**
   - Press PA1 to cycle to TEXT mode
   - Press PA0 to select content
   - Send text from laptop
   - **EXPECTED:** Text appears on LCD

### Step 5: Add Image Support (if time permits)

1. **Store images in flash:**
   - Use existing `gImage_XHR128`, `gImage_XNH128` arrays

2. **Send image command:**
   - Laptop: "I1\r\n" → Display image index 1

---

## DAY 5: Defense Preparation

### Step 6: Practice Demo (2 hours)

1. **Rehearse multiple times:**
   - Set up in same conditions as defense
   - Practice quick recovery if link fails

2. **Prepare explanations:**
   - Explain ASK modulation (carrier ON/OFF)
   - Explain frame format (preamble, type, len, payload, CRC)
   - Show code structure (ask_tx.c, ask_rx.c, gui_menu.c)

3. **Backup plan:**
   - Have screenshots of working output
   - Have code ready to show
   - Explain debug process

---

## Quick Command Reference

### TX Commands (send via USART):
| Command | Function |
|---------|----------|
| `H` | Send HELLO text frame |
| `T<message>` | Send custom text |
| `I<idx>` | Send image index (0-9) |
| `R<data>` | Send raw bytes |
| `Z` | Test pattern (0x55) |

### RX Debug Commands (add to main.c):
| Function | Purpose |
|----------|---------|
| Print envelope stats | Check signal strength |
| Print state | Check frame progress |
| Print CRC status | Verify frame integrity |

---

## Hardware Checklist

- [ ] Two STM32G474 boards programmed
- [ ] LED driver connected to TX PA6
- [ ] Photodiode connected to RX PB11
- [ ] LCD display connected to RX SPI
- [ ] TX USART1 connected to laptop
- [ ] RX USART1 connected to laptop (for debug)
- [ ] Power supplies stable
- [ ] LED and photodiode aligned

---

## Critical Parameters to Verify

| Parameter | TX | RX | Match? |
|-----------|----|----|---|
| Carrier frequency | 50 kHz | - | ✓ |
| Baud rate | 1 kHz | 1 kHz | ✓ |
| Samples/bit | - | 180 | ✓ |
| Envelope size | - | 40 | ✓ |
| DC offset | 1.2V | 1490 ADC | ✓ |
| Threshold | - | 6000 | ? (calibrate) |

---

## Emergency Fixes (if stuck)

1. **Link doesn't work at all:**
   - Use oscilloscope on DAC PA6 → should see 50kHz sine
   - Check LED actually lights up visually
   - Reduce TX-RX distance to <10cm

2. **Frame sync fails:**
   - Increase `ASK_RX_PREAMBLE_MAX_ERRORS` to 3 or 4
   - Verify TIM2 frequency on both boards
   - Check DC offset by reading ADC with LED OFF

3. **CRC always fails:**
   - Implement simpler check first (e.g., just verify preamble)
   - Check endianness of CRC calculation
   - Verify payload length is correct

4. **LCD doesn't update:**
   - Check GUI_RX_OnFrame is called
   - Verify current_mode matches frame type
   - Test LCD directly with static text

---

## Files to Modify

1. **Receive/Src/adc.c** - Add debug printf in HAL_ADC_ConvCpltCallback
2. **Receive/Src/ask_rx.c** - Enable debug macros from ask_rx_debug.c
3. **Receive/Src/main.c** - Add diagnostic loop, connect LCD
4. **Transmit/Core/Src/main.c** - Add 'Z' test pattern command
5. **Receive/HARDWARE/LCD144/gui_menu.c** - Already correct, just populate content if needed

---

**Remember:** The GUI infrastructure is solid. Focus on getting the optical link to work first. Once bits flow correctly, frame sync should follow.