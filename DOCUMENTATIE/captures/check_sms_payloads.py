#!/usr/bin/env python3
"""
Verify the integrity fields of every SMS the radio transmitted, from a USB debug capture that
contains "SMS TX payload len=N: <hex>" lines (SMS_DEBUG_USB_SERIAL=1). Checks:
  - IPv4 header checksum
  - UDP checksum (standard RFC 768 pseudo-header algorithm)
  - trailing CRC32 (sms.c smsCrc32Compute: MSB-first poly 0x04C11DB7, init 0, byte-pair swapped)
and, where a following "SMS RX payload" echo exists, whether the echo matches what was sent.

The algorithms were validated against a real Anytone 890 message (IP 0D06, UDP F7B8, CRC 29733EB0).
Found a real bug this way: the UDP checksum was computed before the text was copied into the packet.

usage: check_sms_payloads.py <capture.log>
"""
import re
import sys

POLY = 0x04C11DB7
TAB = []
for _i in range(256):
    _c = _i << 24
    for _ in range(8):
        _c = ((_c << 1) ^ POLY) & 0xFFFFFFFF if _c & 0x80000000 else (_c << 1) & 0xFFFFFFFF
    TAB.append(_c)


def crc32(d):
    c = 0
    n = len(d)
    for i in range(0, n - 1, 2):
        c = TAB[d[i + 1] ^ ((c >> 24) & 0xFF)] ^ ((c << 8) & 0xFFFFFFFF)
        c = TAB[d[i] ^ ((c >> 24) & 0xFF)] ^ ((c << 8) & 0xFFFFFFFF)
    if n & 1:
        c = TAB[0 ^ ((c >> 24) & 0xFF)] ^ ((c << 8) & 0xFFFFFFFF)
        c = TAB[d[n - 1] ^ ((c >> 24) & 0xFF)] ^ ((c << 8) & 0xFFFFFFFF)
    return c


def csum(b):
    if len(b) % 2:
        b += b"\x00"
    s = sum((b[i] << 8) | b[i + 1] for i in range(0, len(b), 2))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def check(payload):
    b = bytes.fromhex(payload)
    ip = b[:20]
    ip_len = int.from_bytes(ip[2:4], "big")
    udp_len = int.from_bytes(b[24:26], "big")
    ip_ok = csum(ip[:10] + b"\x00\x00" + ip[12:]) == int.from_bytes(ip[10:12], "big")
    udp = b[20:20 + udp_len]
    stored = int.from_bytes(udp[6:8], "big")
    calc = csum(ip[12:20] + b"\x00\x11" + udp_len.to_bytes(2, "big") + udp[:6] + b"\x00\x00" + udp[8:]) or 0xFFFF
    udp_ok = stored == calc
    # CRC32 sits after IP data plus pad octets; find the offset whose CRC matches
    crc_ok = any(
        int.from_bytes(b[o:o + 4], "little") == crc32(b[:o]) for o in range(ip_len, len(b) - 3)
    )
    return ip_ok, udp_ok, crc_ok, ip[16] == 0xE1, int.from_bytes(b[20:22], "big") == 5016


def main():
    tx = None
    rows = 0
    bad = 0
    for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
        m = re.search(r"\[(\d\d:\d\d:\d\d)\] \[RADIO\] SMS TX payload len=\d+: ([0-9A-F]+)", line)
        if m:
            tx = (m.group(1), m.group(2))
            ip_ok, udp_ok, crc_ok, grp, std = check(tx[1])
            rows += 1
            ok = ip_ok and udp_ok and crc_ok
            bad += 0 if ok else 1
            print(f"{tx[0]} {'Standard' if std else 'Motorola':8} {'group  ' if grp else 'private'} "
                  f"IP:{'ok' if ip_ok else 'BAD'} UDP:{'ok' if udp_ok else 'BAD'} CRC32:{'ok' if crc_ok else 'BAD'}")
            continue
        m = re.search(r"\[(\d\d:\d\d:\d\d)\] \[RADIO\] SMS RX payload len=\d+: ([0-9A-F]+)", line)
        if m and tx:
            print(f"         echo received: {'identical to what was sent' if m.group(2) == tx[1] else 'DIFFERS from what was sent (over-the-air errors)'}")
            tx = None
    print(f"\n{rows} transmitted message(s), {bad} with a bad integrity field")


if __name__ == "__main__":
    main()
