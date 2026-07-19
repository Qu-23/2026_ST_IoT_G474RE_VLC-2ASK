def crc16_ccitt(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

def h(data):
    return ' '.join(f'{b:02X}' for b in data)

# What data gives CRC = 0x6EFE?
# Try various payload variations

tests = [
    # (description, data_bytes)
    ("A: correct data", [0x02, 0x04, 0x54, 0x65, 0x73, 0x74]),
    # Maybe payload has \r\n appended by serial terminal
    ("D1: len=6 +CR+LF", [0x02, 0x06, 0x54, 0x65, 0x73, 0x74, 0x0D, 0x0A]),
    ("D2: len=5 +CR", [0x02, 0x05, 0x54, 0x65, 0x73, 0x74, 0x0D]),
    ("D3: len=5 +LF", [0x02, 0x05, 0x54, 0x65, 0x73, 0x74, 0x0A]),
    ("D4: len=5 +null", [0x02, 0x05, 0x54, 0x65, 0x73, 0x74, 0x00]),
    # Maybe arg includes space: "T Test" -> " Test" but with different len
    ("E1: space+Test len=5", [0x02, 0x05, 0x20, 0x54, 0x65, 0x73, 0x74]),
    ("E2: Test+space len=5", [0x02, 0x05, 0x54, 0x65, 0x73, 0x74, 0x20]),
    # Maybe there's a null terminator included
    ("F1: len=5 +0x00 after Test", [0x02, 0x05, 0x54, 0x65, 0x73, 0x74, 0x00]),
    # Maybe TYPE byte is different
    ("G1: TYPE=0x01 RAW", [0x01, 0x04, 0x54, 0x65, 0x73, 0x74]),
    ("G2: TYPE=0x00", [0x00, 0x04, 0x54, 0x65, 0x73, 0x74]),
    # Maybe only payload (no TYPE/LEN) was CRC'd
    ("H1: just payload 4B", [0x54, 0x65, 0x73, 0x74]),
    ("H2: just payload 5B+0x00", [0x54, 0x65, 0x73, 0x74, 0x00]),
    # Maybe byte order is swapped (LE instead of BE)
    ("I1: LEN first then TYPE", [0x04, 0x02, 0x54, 0x65, 0x73, 0x74]),
    # Maybe 'T' command includes the 'T' character itself in payload
    ("J1: 'TTest' as payload len=4", [0x02, 0x04, 0x54, 0x65, 0x73, 0x74]),  # same as A
    ("J2: 'Test' with len=3 (truncated)", [0x02, 0x03, 0x54, 0x65, 0x73]),
    # Maybe there's a 1-byte offset in crc_buf
    ("K1: extra 0x00 before TYPE", [0x00, 0x02, 0x04, 0x54, 0x65, 0x73, 0x74]),
    # Maybe the "Test" includes a trailing \0 from cmd_line null terminator
    # and strlen counted it somehow
    ("L1: len=5 Test+0x00", [0x02, 0x05, 0x54, 0x65, 0x73, 0x74, 0x00]),
    # CRC over GAP data? No, that doesn't make sense
    # What about the full command line "TTest" being used as payload?
    ("M1: payload='TTest' len=4", [0x02, 0x04, 0x54, 0x54, 0x65, 0x73]),
    # What about 'Test\r' being in the arg?
    ("N1: len=5 Test+CR", [0x02, 0x05, 0x54, 0x65, 0x73, 0x74, 0x0D]),
]

target = 0x6EFE
print(f"Looking for CRC = 0x{target:04X}")
print("="*60)
for desc, data in tests:
    crc = crc16_ccitt(data)
    match = " <<< MATCH!" if crc == target else ""
    print(f"{desc:30s} CRC=0x{crc:04X}{match}")

# Also try: what if the CRC is computed with a DIFFERENT polynomial or init?
print("\n--- Alternative CRC parameters ---")
# CRC-16/XMODEM: poly=0x1021, init=0x0000
def crc16_xmodem(data):
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

data_a = [0x02, 0x04, 0x54, 0x65, 0x73, 0x74]
print(f"XMODEM init=0: CRC=0x{crc16_xmodem(data_a):04X}")

# CRC with init=0x0000 but same poly
def crc16_init0(data):
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

print(f"init=0x0000:    CRC=0x{crc16_init0(data_a):04X}")

# What about reflected input (LSB first)?
def crc16_reflected(data):
    def reflect_byte(b):
        r = 0
        for i in range(8):
            if b & (1 << i):
                r |= 1 << (7 - i)
        return r
    crc = 0xFFFF
    for byte in data:
        rb = reflect_byte(byte)
        crc ^= rb << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc

print(f"reflected in:   CRC=0x{crc16_reflected(data_a):04X}")
