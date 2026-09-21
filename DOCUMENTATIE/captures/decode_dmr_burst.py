#!/usr/bin/env python3
"""
Offline decoder for raw DMR data/CSBK bursts captured from a Homebrew/MMDVM
"DMRD" packet log (e.g. DMRGateway's debug dump).

BPTC(196,96) decode is a faithful port of MMDVMHost's BPTC19696.cpp /
Hamming.cpp (GPLv2, Jonathan Naylor G4KLX / Ian Wraith), pulled directly
from https://github.com/g4klx/MMDVMHost -- not reconstructed from memory,
since a hand-rolled first attempt at this produced garbage (no CSBK CRC
validated in any of 4 plausible variants).

Validated against this project's own known-good CSBK CRC scheme (CRC16-
CCITT over bytes[0:10], XOR-masked 0xA5 on bytes[10:12] -- see sms.c's
smsCrc16Ccitt usage) before trusting the Data Header / Data Block output.
"""
import re
import sys

# ---------------------------------------------------------------------------
# Hamming decoders -- verbatim port of Hamming.cpp
# ---------------------------------------------------------------------------

def decode_15_11_3_2(d):
    """d: list[15] bool, corrected in place. Returns True if a correction
    was applied (mirrors CHamming::decode15113_2)."""
    c0 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8]
    c1 = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9]
    c2 = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10]
    c3 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10]

    n = 0
    n |= 0x01 if (c0 != d[11]) else 0x00
    n |= 0x02 if (c1 != d[12]) else 0x00
    n |= 0x04 if (c2 != d[13]) else 0x00
    n |= 0x08 if (c3 != d[14]) else 0x00

    flip = {
        0x01: 11, 0x02: 12, 0x04: 13, 0x08: 14,
        0x09: 0, 0x0B: 1, 0x0F: 2, 0x07: 3, 0x0E: 4,
        0x05: 5, 0x0A: 6, 0x0D: 7, 0x03: 8, 0x06: 9, 0x0C: 10,
    }
    if n in flip:
        i = flip[n]
        d[i] = not d[i]
        return True
    return False


def decode_13_9_3(d):
    """d: list[13] bool, corrected in place. Mirrors CHamming::decode1393."""
    c0 = d[0] ^ d[1] ^ d[3] ^ d[5] ^ d[6]
    c1 = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7]
    c2 = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8]
    c3 = d[0] ^ d[2] ^ d[4] ^ d[5] ^ d[8]

    n = 0
    n |= 0x01 if (c0 != d[9]) else 0x00
    n |= 0x02 if (c1 != d[10]) else 0x00
    n |= 0x04 if (c2 != d[11]) else 0x00
    n |= 0x08 if (c3 != d[12]) else 0x00

    flip = {
        0x01: 9, 0x02: 10, 0x04: 11, 0x08: 12,
        0x0F: 0, 0x07: 1, 0x0E: 2, 0x05: 3, 0x0A: 4,
        0x0D: 5, 0x03: 6, 0x06: 7, 0x0C: 8,
    }
    if n in flip:
        i = flip[n]
        d[i] = not d[i]
        return True
    return False


# ---------------------------------------------------------------------------
# BPTC(196,96) -- verbatim port of BPTC19696.cpp
# ---------------------------------------------------------------------------

def bptc_196_96_decode(bits264):
    """bits264: the full 264-bit raw DMR burst (MSB-first per byte, as
    extracted from the 33-byte air-interface frame). Returns 96 decoded
    data bits (as a list of 0/1 ints)."""
    assert len(bits264) == 264

    # decodeExtractBinary: raw = info1 (bits 0-97) ++ info2 (bits 166-263)
    raw = bits264[0:98] + bits264[166:264]
    assert len(raw) == 196

    # decodeDeInterleave
    deinter = [False] * 196
    for a in range(196):
        seq = (a * 181) % 196
        deinter[a] = bool(raw[seq])

    # decodeErrorCheck: iterative row/col Hamming correction, up to 5 passes
    for _ in range(5):
        fixing = False

        # 15 columns, each 13 elements: col c = deinter[c+1], [c+1+15], ...
        for c in range(15):
            pos = c + 1
            col = []
            for _a in range(13):
                col.append(deinter[pos])
                pos += 15
            if decode_13_9_3(col):
                pos = c + 1
                for a in range(13):
                    deinter[pos] = col[a]
                    pos += 15
                fixing = True

        # 9 rows containing data, each 15 elements: row r = deinter[r*15+1 : r*15+16]
        for r in range(9):
            pos = (r * 15) + 1
            row = deinter[pos:pos + 15]
            if decode_15_11_3_2(row):
                deinter[pos:pos + 15] = row
                fixing = True

        if not fixing:
            break

    # decodeExtractData: fixed ranges (inclusive) into the 96-bit payload
    ranges = [
        (4, 11), (16, 26), (31, 41), (46, 56),
        (61, 71), (76, 86), (91, 101), (106, 116), (121, 131),
    ]
    out = []
    for lo, hi in ranges:
        for a in range(lo, hi + 1):
            out.append(1 if deinter[a] else 0)
    assert len(out) == 96
    return out


def bits_to_bytes(bits):
    out = bytearray()
    for i in range(0, len(bits), 8):
        chunk = bits[i:i + 8]
        val = 0
        for b in chunk:
            val = (val << 1) | b
        out.append(val)
    return bytes(out)


def bytes_to_bits(data: bytes):
    bits = []
    for byte in data:
        for i in range(8):
            bits.append((byte >> (7 - i)) & 1)
    return bits


# ---------------------------------------------------------------------------
# CRC check re-used from this project's own convention (sms.c smsCrc16Ccitt
# + 0xA5 mask over bytes[10:12] of a 12-byte CSBK/header buffer).
# ---------------------------------------------------------------------------

def crc16_ccitt(data: bytes, poly=0x1021, init=0x0000):
    crc = init
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ poly) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def check_csbk_style_crc(buf12: bytes):
    if len(buf12) != 12:
        return False
    crc = crc16_ccitt(buf12[0:10])
    expect_hi = (crc >> 8) & 0xFF
    expect_lo = crc & 0xFF
    got_hi = buf12[10] ^ 0xA5
    got_lo = buf12[11] ^ 0xA5
    return expect_hi == got_hi and expect_lo == got_lo


# ---------------------------------------------------------------------------
# Capture-file parsing
# ---------------------------------------------------------------------------

def parse_capture(path):
    text = open(path, "r", errors="replace").read()
    lines = text.splitlines()
    i = 0
    entries = []
    while i < len(lines):
        line = lines[i]
        if "Network Received" in line:
            hexbytes = []
            j = i + 1
            while j < len(lines) and re.search(r"^\S: \S+ \S+ [0-9A-F]{4}:", lines[j]):
                m = re.search(r"[0-9A-F]{4}:\s+([0-9A-F ]+?)\s{2,}\*", lines[j])
                if m:
                    hexbytes.extend(m.group(1).split())
                j += 1
            if hexbytes:
                data = bytes(int(h, 16) for h in hexbytes)
                if data[0:4] == b"DMRD":
                    entries.append(data)
            i = j
        else:
            i += 1

    seen = {}
    for data in entries:
        seq = data[4]
        if seq not in seen:
            seen[seq] = data

    out = []
    for seq in sorted(seen):
        data = seen[seq]
        out.append({
            "seq": seq,
            "src": int.from_bytes(data[5:8], "big"),
            "dst": int.from_bytes(data[8:11], "big"),
            "flags": data[15],
            "streamId": data[16:20],
            "burst33": data[20:53],
        })
    return out


DATA_TYPE_NAMES = {
    0: "PI_HEADER", 1: "VOICE_LC_HEADER", 2: "TERMINATOR_WITH_LC",
    3: "CSBK", 4: "MBC_HEADER", 5: "MBC_CONTINUATION", 6: "DATA_HEADER",
    7: "RATE_1_2_DATA", 8: "RATE_3_4_DATA", 9: "IDLE", 10: "RATE_1_DATA",
}


def main():
    if len(sys.argv) != 2:
        print("usage: decode_dmr_burst.py <capture.txt>")
        sys.exit(1)

    entries = parse_capture(sys.argv[1])
    print(f"Parsed {len(entries)} unique DMRD packets\n")

    results = []
    for e in entries:
        dtype_nibble = e["flags"] & 0x0F
        dtype_name = DATA_TYPE_NAMES.get(dtype_nibble, f"UNKNOWN({dtype_nibble})")
        bits264 = bytes_to_bits(e["burst33"])
        info96 = bptc_196_96_decode(bits264)
        header_bytes = bits_to_bytes(info96)
        crc_ok = check_csbk_style_crc(header_bytes)
        results.append({**e, "dtype_name": dtype_name, "decoded": header_bytes, "crc_ok": crc_ok})
        print(f"seq={e['seq']:3d}  src={e['src']:>8}  dst={e['dst']:>8}  "
              f"type={dtype_name:16s}  decoded={header_bytes.hex()}  crcOk={crc_ok}")

    print()
    csbk_results = [r for r in results if r["dtype_name"] == "CSBK"]
    if csbk_results:
        ok = sum(1 for r in csbk_results if r["crc_ok"])
        print(f"CSBK CRC validation: {ok}/{len(csbk_results)} bursts pass "
              f"this project's known-good CSBK CRC scheme.")
        if ok == len(csbk_results):
            print("=> BPTC(196,96) decode CONFIRMED CORRECT.\n")
        else:
            print("=> BPTC(196,96) decode still not validated -- treat "
                  "results below with caution.\n")

    for r in results:
        if r["dtype_name"] in ("DATA_HEADER", "RATE_1_2_DATA", "RATE_3_4_DATA"):
            hb = r["decoded"]
            print(f"{r['dtype_name']} seq={r['seq']}: {hb.hex()}")
            if r["dtype_name"] == "DATA_HEADER":
                print(f"  byte0={hb[0]:#04x} (DPF nibble={hb[0] & 0x0F:#03x})  "
                      f"byte1={hb[1]:#04x} (SAP nibble={hb[1] & 0xF0:#04x})")


if __name__ == "__main__":
    main()
