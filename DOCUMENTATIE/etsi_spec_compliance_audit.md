# ETSI spec compliance audit (V3_TEST SMS/data/CSBK code)

A systematic cross-check of this firmware's existing SMS/data/CSBK protocol code against the
real ETSI TS 102 361-1/-2/-3 DMR specification documents (all three saved in this folder --
`etsi_ts_102_361-1.pdf`, `-2.pdf`, `-3.pdf`). Prompted by building the Anytone/Defined-Short-Data
SMS send format (see `sms_send_format_choice.md`), which required actually reading the spec
closely enough that checking the rest of the existing, already-field-tested code against it
became cheap. Not exhaustive -- covers what was checked, not a guarantee everything else is fine.

## Confirmed correct (matches spec exactly)

- **SAP `0x4`** ("IP based Packet data", used for ordinary SMS text) -- matches Table 9.31.
- **DPF `0x02`** (Unconfirmed Data) paired with plain 12-byte blocks and no per-block checksum --
  correct per clause 8.2.2.1 (Unconfirmed rate-1/2 blocks carry no block-level serial/CRC field at
  all, unlike Confirmed blocks).
- **Trailing 4-byte CRC in the last block** (`smsBuildMotorolaPayload`/`smsBuildStandardPayload`'s
  `SMS_STANDARD_CRC32_BYTES`) -- both its *position* (last 4 octets of the final block, clause
  8.2.2.1) and its *actual polynomial* (`0x04C11DB7`, checked against `smsCrc32Table` in `sms.c`)
  match the spec's 32-bit "message CRC" (clause B.3.9) exactly.
- **ACK response DPF `0x01`** ("Response packet", `smsQueueAckResponseMessage()`) -- matches
  Table 9.30 exactly; reserved/FMF bits in that header are correctly zeroed per the C_RHEAD PDU
  table (9.13).
- **"Modern" ACK profile's Class/Type/Status byte (`0x08`)** -- decodes to Class=`00`, Type=`001`,
  Status=`000`, which is an **exact match** for "ACK: all blocks up to NI successfully received"
  in Table 8.3 (TS 102 361-1) / Table 5.7 (TS 102 361-3, a bearer-specific subset of the same
  table). This resolves what was previously "can't verify, Part 1 defers Class/Type/Status
  meaning to Part 3" -- now confirmed correct against real Part 3 text, not just a guess that
  happened to interoperate.
- **CSBK CRC mask (`0xA5`)** and **Data Header CRC mask (`0xCC`)** -- both match Table B.21 exactly
  (already confirmed while building the Anytone format).
- **Preamble CSBK opcode (`0x3D`)** -- matches TS 102 361-2 Table B.2 exactly.

## Confirmed NOT spec-standard, but deliberate (not a bug)

- **"Legacy" ACK profile's Class/Type/Status byte (`0x00`)** -- decodes to Class=`00`, Type=`000`,
  Status=`000`, which is **not a defined combination in either Table 8.3 or Table 5.7 at all**.
  Consistent with its own code comment ("legacy simplex/original firmware response profile") --
  this looks like it was reverse-engineered to match one specific real, older/simpler radio's
  actual observed (non-standard) behaviour, not derived from spec. Not a bug to fix; just
  confirmed as intentionally non-standard.

## Real spec deviation found

**The Unconfirmed Data Header (`smsBuildDataHeader()`, used by every ordinary text SMS send
today) sets the "Response Requested" (A) bit to 1.** Table 9.15 (U_HEAD PDU) is explicit: *"This
bit shall be set to 0"* for an Unconfirmed header -- that bit is only left unconstrained on the
Confirmed header variant (Table 9.10, C_HEAD). This firmware combines Unconfirmed-shaped data
blocks (no per-block overhead, simple, already proven working) with a header flag the spec ties
specifically to Confirmed delivery.

**Not treated as broken**: this already works against real BrandMeister/network traffic (the
whole SMS ack flow this project relies on depends on it), so real-world DMR infrastructure is
evidently more lenient here than the literal spec text -- likely common, pragmatic real-radio
behaviour rather than something unique to this firmware. Recorded here as a known, confirmed
deviation in case it ever matters for interop with a stricter receiver, not as an action item.

## Cross-referenced against real MMDVMHost source (not just spec text)

The user has a full local checkout of WPSD's source (`~/Downloads/WPSD-Bin_source/MMDVMHost/`,
see [[reference_wpsd_mmdvmhost_source]]) -- a real, widely-deployed, interoperating DMR gateway,
which lets some of the above be independently confirmed against working code rather than spec
text alone:

- **Confirmed independently**: the Defined Short Data header's block-count field
  (`(byte0 & 0x30) + (byte1 & 0x0F)`) and the Full-Message-Flag/SARQ bit positions in byte 8
  (`DMRDataHeader.cpp`'s `m_F`/`m_S` for `DPF_DEFINED_SHORT`) match this firmware's existing RX
  code and the new Anytone TX encoder exactly.
- **Confirmed independently, real-world harmlessness of the "A bit" deviation**: MMDVMHost's own
  header parser reads the Response-Requested bit unconditionally regardless of DPF, with no
  validation against "must be 0 for Unconfirmed" -- real gateway software doesn't enforce that
  spec constraint either, reinforcing that this firmware's deviation is genuinely harmless in
  practice, not just presumed so.
- **Could not resolve the rate-¾ per-block checksum (7-bit serial + 9-bit CRC) this way**:
  MMDVMHost's `DMRTrellis.cpp` implements the actual channel-coding Trellis encode/decode, but
  MMDVMHost only operates as a *repeater* -- it decodes a rate-¾ block only far enough to
  regenerate/relay it (`DMRSlot.cpp`), treating the 18 bytes as an opaque blob. It never
  interprets the serial-number/CRC-9 fields inside, since that's an endpoint-radio concern, not a
  repeater one. This piece remains exactly as unverified as before -- now for a documented reason.

## Still not verifiable (would need more real-world data, not more spec-reading)

- **Call Alert / Radio Check / Ack CSBK opcode values** (`0x1F`/`0x1D`/`0x20`) -- confirmed **not**
  in TS 102 361-2's Table B.2 (the FID=0 opcode list), meaning these are almost certainly
  vendor-specific-FID features neither ETSI document publishes. See
  `csbk_call_alert_radio_check_status.md` for full detail -- this needs a real vendor capture to
  resolve, not another spec read.
- Anything not explicitly listed above (e.g. LRRP's framing, MDC1200, GPS/APRS, hotspot mode) --
  out of scope for this pass; LRRP/MDC1200 in particular are not ETSI-standard protocols to begin
  with (proprietary/reverse-engineered by their nature), so an ETSI comparison wouldn't apply to
  them the way it does to the core DMR data-burst code above.
