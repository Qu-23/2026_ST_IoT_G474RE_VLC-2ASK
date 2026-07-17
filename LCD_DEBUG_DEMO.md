# LCD Debug Demo - Ready to Test

## What I've Done

Created a **simple diagnostic display** on the LCD that shows real-time RX statistics instead of using USART.

## New Files Created

| File | Location | Purpose |
|------|----------|---------|
| `lcd_debug.h` | `Receive/Src/` | LCD debug display header |
| `lcd_debug.c` | `Receive/Src/` | LCD debug display implementation |
| `ask_rx_simple.h` | `Receive/Inc/` | Simple RX header (copied) |
| `ask_rx_simple.c` | `Receive/Src/` | Simple RX implementation (copied) |

## What the LCD Will Display

```
ASK RX DEBUG           <- Title
ENV: 12540             <- Average envelope value (LED ON should be ~8000-15000)
BITS: 125              <- Total bits received
ONES: 50%              <- Percentage of '1' bits
LAST: 55AA             <- Last 16 bits in hex
PATTERN: 0x55 OK       <- Pattern detection status
THRES: 6000            <- Current threshold
LINK OK!               <- Status indicator
```

## Test Procedure

### 1. Build and Flash RX
- Compile and flash the **Receive** project
- LCD should show "ASK RX DEBUG"

### 2. Build and Flash TX
- Compile and flash the **Transmit** project

### 3. Test Link

**Step 1: Check Signal Strength**
1. Keep TX LED OFF (no signal)
2. Look at LCD: ENV should be low (~500-3000)
3. Turn TX LED ON (continuous carrier)
4. Look at LCD: ENV should be high (~8000-15000)

**Step 2: Set Threshold**
1. Calculate threshold = (ENV_ON + ENV_OFF) / 2
2. For example: if ON=12000, OFF=2000, threshold = 7000
3. This can be adjusted later if needed

**Step 3: Test Data Transmission**
1. On TX side, send alternating bits (0x55 or 0xAA)
2. RX LCD should show:
   - BITS increasing
   - ONES around 50%
   - PATTERN: 0x55 OK or 0xAA OK
   - LINK OK! status

## Expected LCD Display During Test

### TX OFF (No Signal):
```
ASK RX DEBUG
ENV:  1200        <- Low
BITS:     0
ONES:   0%
LAST: 0000
PATTERN: NONE
THRES: 6000
NO SIGNAL       <- Red warning
```

### TX ON (Continuous Carrier):
```
ASK RX DEBUG
ENV: 12400        <- High
BITS:  125
ONES:  95%        <- Most bits = 1
LAST: FFFF
PATTERN: NONE
THRES: 6000
HIGH SIGNAL     <- Yellow
```

### TX Sending 0x55 Pattern:
```
ASK RX DEBUG
ENV:  8500
BITS:  250
ONES:  50%        <- Perfect!
LAST: 5555
PATTERN: 0x55 OK  <- Green
THRES: 6000
LINK OK!        <- Green success!
```

## Troubleshooting

| Problem | LCD Shows | Fix |
|---------|-----------|-----|
| No ADC sampling | ENV: 0, BITS: 0 | Check TIM3 trigger, ADC GPIO PB11 |
| LED not working | ENV stays same | Check TX DAC PA6, verify LED lights up |
| Signal too weak | ENV < 3000 | Reduce distance, check alignment |
| Signal saturated | ENV > 15000 | Check amplifier gain |
| Wrong threshold | ONES always 0% or 100% | Adjust threshold value |

## What to Do Next

1. **Test the basic signal** - Verify ENV changes when TX LED ON/OFF
2. **Test pattern detection** - Send 0x55 from TX, verify "0x55 OK" on LCD
3. **If working** - Switch back to full ask_rx.c for frame decoding
4. **If not working** - Adjust threshold, check hardware connections

## TX Test Commands (Add to ProcessCommand)

```c
case 'Z':  // Test 0x55 pattern
    {
        for (int i = 0; i < 50; i++) {
            ASK_TX_SendFrame(ASK_TYPE_RAW, (uint8_t*)"\x55", 1);
            HAL_Delay(10);
        }
    }
    break;

case 'A':  // Test 0xAA pattern
    {
        for (int i = 0; i < 50; i++) {
            ASK_TX_SendFrame(ASK_TYPE_RAW, (uint8_t*)"\xAA", 1);
            HAL_Delay(10);
        }
    }
    break;
```

Send 'Z' or 'A' from your laptop to TX to test pattern detection on RX LCD.
