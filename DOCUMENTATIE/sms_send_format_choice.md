# SMS send-side: format choice, SAP validation, and a known TX limitation (V3_TEST)

## "Send as" format choice

The compose flow (`menuSMS.c`) now shows a "Send as" menu step right after the
destination is resolved (contact, manual ID, or reply-to-sender), before the
message actually transmits:

```
Send as
> Motorola
  DMR_Standard
```

This threads a `smsEncoderFormat_t` (`SMS_ENCODER_MOTOROLA` /
`SMS_ENCODER_STANDARD`) through the whole TX pipeline --
`smsPackMessage()` -> `smsQueueMessage()` ->
`smsScheduleQueuedMessageTransmission()` -> the outgoing-tracking structs
(so a retry re-sends in the same format originally chosen). Resending a
message from the *Sent* list always uses Motorola, since that storage
doesn't currently record which format a message was originally sent in.

`smsBuildStandardPayload()`/`smsBuildStandardUdpHeader()` (new) implement
the "DMR_Standard" format -- UDP port `0x1398` (5016), a 4-byte internal
sub-header (`0x00 0x0D 0x00 0x0A`) instead of Motorola's 10 bytes, text at
offset 32 instead of 38. Built to match `smsDecodeStandardPayload()`
byte-for-byte (that decoder already worked against real captures), rather
than re-deriving from the spec PDF, which turned out to have some
ambiguous/garbled table formatting for this section.

**Important naming caveat**: despite the name, this "DMR_Standard" format is
**not** the same thing as the actual ETSI-standard "Defined Short Data"
format real network services and (per a real report) Anytone radios use --
see below. The name comes from the original spec document bundled with this
repo, which uses "DMR_Standard" for this specific IP/UDP-wrapped, CRC32-
terminated format. Confusingly, that's a different, apparently older/
alternate convention from the actual ETSI `Defined Short Data` PDU format
this session spent most of its time getting *receive* support for. Verified
correct on the wire (byte-for-byte: UDP port, sub-header constants, UDP
length math) but not yet confirmed against a real Anytone radio's screen.

## SAP validation for Defined Short/Raw Data headers

Per a report of the real spec (ETSI TS 102 361-1 clause 9.2.12, Defined Data
Short Data packet Header (DD_HEAD) PDU): SAP identifier is `0b1010` (0xA),
DPF is `0b1101` (0x0D, matching what was already implemented), and there's
no block CRC32. `smsHandleReceivedDataFrame()` previously skipped SAP
validation entirely for this DPF (since the correct expected value wasn't
known); it now requires `frame[1] & 0xF0 == 0xA0`. Verified against the two
real Defined Short Data headers already captured this session (`4D AA ...`
and `4D A8 ...` -- both have SAP nibble `0xA0`), so this tightening doesn't
regress anything already confirmed working.

## Update: SMS_ENCODER_ANYTONE now implemented (both original blockers resolved/attempted)

A third "Send as" option, **Anytone** (`SMS_ENCODER_ANYTONE` in `sms.h`), now exists,
implementing the real ETSI Defined Short Data format described below. The two blockers that
originally stopped this are addressed very differently -- one solved and verified, one
implemented but genuinely untested:

**Header CRC -- solved, verified against real captures.** The full ETSI TS 102 361-1 spec PDF
(`DOCUMENTATIE/etsi_ts_102_361-1.pdf`) was obtained and read. Clause B.3.12 (table B.21) gives a
per-Data-Type CRC mask table that this project's earlier CRC16-parameterization search didn't
know to look for -- the header CRC is a **plain CRC-CCITT (poly `0x1021`, init `0x0000`) XORed
with a second, separate mask** (`0xCCCC` for "Data Header", the burst type Defined Short Data
uses), not a single CRC16 with an unusual parameterization. Recomputed against both real captures
from the previous session:

```
cap1 (4D AA 23 F6 6E 04 03 51 53 20 D8 75): CRC-CCITT(bytes 0-9) ^ 0xCCCC = 0xD875  -- matches bytes 10-11 exactly
cap2 (4D A8 23 F6 6E 04 03 51 53 00 3A 70): CRC-CCITT(bytes 0-9) ^ 0xCCCC = 0x3A70  -- matches bytes 10-11 exactly
```

This also explains the previously-"unexplained" bytes 8-9: per the DD_HEAD PDU layout (ETSI
clause 9.2.12, table 9.17C), byte 8 is a fixed DD-format+SARQ+FullMessageFlag byte (`0x53` in
both captures -- same format used both times) and byte 9 is a padding-bit-count field (naturally
different between the two captures, since they were different-length messages). Implemented in
`smsBuildDefinedShortDataHeader()` in `sms.c`.

**TX hardware path -- implemented, NOT verified.** Register-level behaviour is now understood
with much more confidence than before, cross-checked against three independent sources (the
translated HR-C6000 register doc, the ETSI spec's Data Type table 9.22, and this firmware's own
existing, working voice/CSBK/data-header TX code that already writes register `0x50` with
type-specific values `0x30`/`0x64`/`0x70` matching the CSBK/Data-Header/Rate-½-Data nibbles from
table 9.22 exactly): a rate-¾ data block needs register `0x50` written as `0x80` (Data Slot Type
nibble `1000` = "Rate ¾ Data" per table 9.22) instead of `0x70`, and the SPI write at page `0x02`
needs to send `SMS_RATE34_DATA_LENGTH` (18) bytes instead of `LC_DATA_LENGTH` (12) for those
frames specifically -- preamble CSBKs and the data header stay 12 bytes regardless. No separate
`Code_Type1`/`Code_Type2` register configuration is needed or possible for this Data Type value
(those registers only cover nibbles `1011`/`1100`-`1111`, i.e. Unified Single Block Data and
reserved-for-future-use codes -- **not** `1000` -- so whatever FEC the chip applies for a
Data-Slot-Type-`1000` burst is presumably automatic/hardwired, consistent with ETSI table 6.1
fixing "Rate ¾ Data Continuation" to "Rate ¾ Trellis" coding with no alternative). Implemented in
`HR-C6000.c`: `hrc.smsRate34Active` flag, `hrc6000GetSmsDataType()`, `hrc6000SendSMSFrame()`,
`HRC6000StartQueuedSMS()`. **This entire mechanism has never been exercised on real hardware in
this firmware before and is unverified.**

**Data block format -- implemented, NOT verified (no real capture to check against).** Per ETSI
clause 8.2.2.2/figure 8.14: each rate-¾ Confirmed data block is 2 header bytes (7-bit block serial
number + 9-bit CRC-9, spanning bit 0 of byte 0 through all of byte 1) followed by 16 payload
bytes. The CRC-9 (clause B.3.10, masked per table B.21's `0x1FF` "Rate ¾ Data Continuation" entry)
is computed over the 7-bit serial number concatenated with the block's data -- implemented as a
bit-level (not byte-level) CRC in `smsCrc9()`/`smsBuildAnytoneDataBlock()` in `sms.c`. **Unlike
the header, no real rate-¾ data block was ever captured on this project** (only headers were --
see the two capture hex dumps above), so this piece could not be checked byte-for-byte the way
the header CRC was. It's a direct, careful reading of the spec text, not a guess, but it is
unverified until tested against a real receiver. A separate spec detail -- the last block in a
multi-block message reserving its final bytes for a whole-message ("Fragment"/Message) CRC
distinct from the per-block CRC-9 -- was noticed while implementing this but is **not yet
implemented**; currently every block (including the last) is encoded identically. This means
multi-block Anytone-format messages are more likely to be rejected by a strict real receiver than
single-block ones.

**Text encoding**: plain UTF-16BE, no IP/UDP wrapper, case preserved (not force-uppercased like
Motorola) -- mirrors `smsDecodeUtf16BePayload()`, the already-proven decoder for real
network-relayed messages of this type. Implemented as `smsConvertTextToUtf16Be()`.

**Not yet done**: the whole-message CRC for multi-block messages (see above), and (as ever) any
of this confirmed on real hardware -- build/flash/send/check test needed before trusting this for
anything that matters.

---

## Original writeup (kept for history)

Real Defined Short Data messages (RSSI/WX reports from network services,
and reportedly real Anytone radios) use "rate 3/4" DMR data bursts --
16 bytes of payload per block, not the 12 this firmware's rate-1/2 format
uses (this is what `SMS_RATE34_DATA_LENGTH` in `HR-C6000.h` fixed for
*receiving*). Building a matching *encoder* was attempted and stopped
partway for two independent reasons:

1. **Header CRC / bytes 8-9 unknown.** The two real Defined Short Data
   headers captured this session (`4D AA 23 F6 6E 04 03 51 53 20 D8 75` and
   `4D A8 23 F6 6E 04 03 51 53 00 3A 70`) don't decode cleanly under any
   common CRC16 parameterization (poly/init/reflection/XOR-out) tried
   against both samples simultaneously, and bytes 8-9 (`53 20` / `53 00`)
   don't have a confirmed meaning. Getting this wrong wouldn't just garble
   text -- since DMR bursts are CRC-checked in hardware before any
   application code runs, a wrong header CRC means the receiving radio's
   chip silently discards the whole burst, so the feature would appear to
   work (TX debug shows "keyed up") while never actually being received by
   anything.

2. **TX hardware path has no rate-3/4 support at all.** `HR-C6000.c`'s SMS
   TX frame handling (`hrc.smsFrames[SMS_MAX_TX_FRAMES][LC_DATA_LENGTH]`,
   `hrc6000SendSMSFrame()`, `hrc6000GetSmsDataType()`) is hardcoded to only
   ever send rate-1/2 (12-byte, `dataType 0x07`) blocks -- there's no code
   path that produces `dataType 0x08`. Even with a perfect header/payload,
   the hardware transmit path would still chop it into the wrong block
   structure. Extending this would need the frame buffer resized/made
   variable, the SPI write length changed, and (unverified, no chip
   datasheet access) figuring out whether/how the HR-C6000 needs additional
   register configuration to actually encode a rate-3/4 burst on transmit.

Given both blockers, this was intentionally stopped rather than shipping a
best-effort/unverified encoder. If picked up again: get more real Defined
Short Data header captures (different block counts/content) to have enough
data points to reverse-engineer the CRC and bytes 8-9 with confidence, and
separately investigate the HR-C6000 TX-side rate-3/4 question (ideally with
real chip register documentation, not just black-box capture-based
reverse-engineering as used for the receive side).
