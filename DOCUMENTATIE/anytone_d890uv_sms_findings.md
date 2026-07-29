# AnyTone D890UV firmware SMS findings — for OpenGD77 sms.c

**Source**: static reverse engineering of `D890UV_V1.05_20260521.CDD` (AnyTone AT-D890UV firmware
V1.05, main application MCU, raw ARM Cortex-M4 image, no encryption, loads at `0x0800C000`).
Analysis done with Ghidra (headless decompile of all 3873 functions) plus manual cross-referencing
against `sms.c`/`sms.h` in this repo and the ETSI spec PDFs already in this folder.

**Purpose**: hand real, byte/bit-level facts about a real AnyTone radio's SMS implementation to
whoever next works on `sms.c`, so format work can build on confirmed ground truth instead of
re-deriving everything from network captures. Read the `confidence` field on every claim below —
this file mixes fully-verified facts with partially-decoded structure, and the two must not be
treated the same way.

Machine-readable summary first, human-readable detail after.

```json
{
  "firmware": {
    "model": "AnyTone AT-D890UV",
    "version": "V1.05",
    "file": "D890UV_V1.05_20260521.CDD",
    "mcu": "ARM Cortex-M4, likely GD32F4xx family",
    "load_base": "0x0800C000",
    "note": "raw unencrypted binary; UI strings are UTF-16LE around file offset 0x160000-0x199090"
  },
  "sms_format_selector": {
    "variable": "DAT_2000fc55",
    "ram_address": "0x2000FC55",
    "type": "uint8_t global",
    "confirmed_values": {
      "4": "DMR_Standard (confirmed: selects UDP port 0x1398 and the 4-byte 00-0D-00-0A sub-header)",
      "3": "Hytera (confirmed by elimination + a dedicated, structurally distinct code path — see hytera_format below)"
    },
    "confidence": "high for value 4, high for value 3 mapping to Hytera though the CPS's exact enum ordering (0/1/2/5 = ?) was not fully mapped"
  },
  "findings": [
    {
      "id": "motorola-standard-tx-builder",
      "confidence": "confirmed_byte_exact",
      "function": "FUN_080f2b84",
      "address": "0x080F2B84",
      "summary": "Builds the IP/UDP-wrapped SMS payload for both Motorola and DMR_Standard formats. Byte layout matches DMR_Text_Message_Specification_rev_1.0.md (KY4YI) and this repo's smsBuildMotorolaPayload()/smsBuildStandardPayload() BYTE FOR BYTE, independently confirmed from the compiled firmware.",
      "details": "See 'Motorola / DMR_Standard TX builder' section below for the full annotated pseudocode and byte offset table."
    },
    {
      "id": "motorola-ttl-discrepancy",
      "confidence": "confirmed_byte_exact",
      "summary": "Real AnyTone firmware uses IPv4 TTL = 0x40 for Motorola-format SMS, NOT 0x01 as the KY4YI spec doc states and as SMS_MOTOROLA_IPV4_TTL is currently defined in sms.c. DMR_Standard format does use TTL = 0x01 as documented.",
      "location_in_opengd77": "sms.c: #define SMS_MOTOROLA_IPV4_TTL 0x01U (used by smsBuildIpHeader for both formats)",
      "recommendation": "Split the TTL constant per format: keep 0x01 for DMR_Standard, use 0x40 for Motorola, if byte-exact match to real Anytone traffic matters. Low real-world impact (IP TTL is not usually checked by DMR-over-IP relays), but it is a confirmed discrepancy from a real radio."
    },
    {
      "id": "rate34-tx-path-is-stub",
      "confidence": "confirmed_from_raw_disassembly",
      "function": "FUN_08100060",
      "address": "0x08100060",
      "summary": "The alternate SMS TX path (taken when DAT_200211cb bit0 is set, or when format==4 and FUN_0802c858() returns 0) is a genuine empty stub. Verified in raw ARM disassembly, not just decompiler output: sub sp,#4 / strb.w r0,[sp,#3] / add sp,#4 / bx lr — it stores its argument to the stack and does nothing else.",
      "implication": "In this specific firmware build (D890UV V1.05), that alternate TX path — which is presumably where the real ETSI rate-3/4 Defined Short Data encoder would live — is not implemented. This is worth knowing before assuming 'real Anytone radios use Defined Short Data for actual over-the-air SMS' (sms_send_format_choice.md's own wording is 'reportedly') is universally true across firmware versions/models. It may be TX-unimplemented on this radio/version while still being RX-capable (the RX decode path is separate and was not part of this stub check).",
      "caveat": "This does not disprove Defined Short Data TX exists elsewhere in the firmware (e.g. gated behind a different flag not reached in this investigation) — only that the specific dispatch path found routes to a no-op for the conditions checked."
    },
    {
      "id": "hytera-format-structure",
      "confidence": "structure_confirmed_bits_partial",
      "functions": ["FUN_0810b62c @ 0x0810B62C", "FUN_080efc40 @ 0x080EFC40"],
      "summary": "Hytera format (DAT_2000fc55 == 3) does NOT go through the IP/UDP builder at all — it's a completely separate function building a compact ~10-12 byte bitfield header, structurally similar to a native DMR CSBK/Data-Header PDU (Group/Individual bit, a Response-Requested-like bit, a 4-bit nibble built from DAT_200125bc bits[4:1], 3-byte LLID field copies), NOT an IP/UDP packet the way Motorola/DMR_Standard/Anytone-format are in this codebase.",
      "not_resolved": "Exact bit positions for every field, the SAP/DPF nibble's real numeric value, and the text/payload encoding step were not fully decoded in this pass — see raw pseudocode below to continue from.",
      "recommendation": "Do not guess-implement Hytera format from this alone. Treat the pseudocode below as a strong starting point for a follow-up static-analysis pass (or, better, an empirical capture of real Hytera<->Hytera or Hytera<->D890UV SMS traffic — this project's own proven methodology for the other formats)."
    },
    {
      "id": "no-static-byte-templates",
      "confidence": "confirmed",
      "summary": "AnyTone's firmware does not store any SMS header as a fixed byte blob/template in flash — every field is written by individual instructions. A raw byte-signature search for the exact Motorola/DMR_Standard sub-header templates from the KY4YI spec came up with zero hits in the binary. Don't waste time on blind byte-signature search for Hytera's template either — same will apply.",
      "also_ruled_out": "The two CRC-CCITT (poly 0x1021, byte-input, final ^0xFFFF) routines found in the firmware (FUN_0813d18c @ 0x0813D18C, FUN_0813d230 @ 0x0813D230) are NOT the DMR data-header CRC — both call sites in the firmware call them with length=4 bytes, not the 10 bytes a DD_HEAD/data-header CRC needs. They're some other subsystem's checksum (not identified)."
    },
    {
      "id": "dd-format-field-etsi-confirmed",
      "confidence": "confirmed_from_spec_and_cross_checked_against_your_own_captures",
      "summary": "Pulled the authoritative DD (Defined Data format) field definition straight from etsi_ts_102_361-1.pdf (table 9.50, clause 9.3.38), already in this DOCUMENTATIE folder. This resolves the 'unverified' status flagged in sms_send_format_choice.md for dataHeader byte 8.",
      "dd_format_table": {
        "0b000000": "Binary",
        "0b000001": "BCD",
        "0b000010": "7 bit character",
        "0b000011": "8 bit ISO/IEC 8859-1",
        "0b010100": "Unicode UTF-16BE"
      },
      "byte8_layout": "DD_HEAD PDU byte 8 = [DD:6 bits, MSB-first][SARQ:1 bit][Full Message Flag:1 bit], confirmed by decoding both real captures' byte-8 value 0x53 = 0b01010011 -> top 6 bits 0b010100 = 20 = Unicode UTF-16BE, matching exactly what smsBuildDefinedShortDataHeader() already hardcodes. This was previously matched byte-for-byte against captures but its *meaning* was unverified — now it is spec-derived and confirmed, not just capture-matched.",
      "action_items_for_sms_c": [
        {
          "mode": "7-bit character",
          "dd_value": "0b000010",
          "byte8_value_assuming_sarq1_fmf1": "0x0B",
          "note": "SARQ/FullMessageFlag bits carried over from the only two real captures on file (both had SARQ=1, FMF=1). Not independently reconfirmed for this specific DD mode — flag if a real 7-bit capture ever surfaces."
        },
        {
          "mode": "8-bit ISO/IEC 8859-1",
          "dd_value": "0b000011",
          "byte8_value_assuming_sarq1_fmf1": "0x0F",
          "note": "Same caveat as above."
        }
      ],
      "note_on_anytone_cps": "The D890UV's own CPS/menu exposes this as 'ISO 7bit' / 'ISO 8bit' options (confirmed present in the firmware's UI string table at file offsets 0x180222 / 0x180234) alongside the SMS Format dropdown (DMR Standard / Motorola SMS / Hytera SMS). The *code path* that reads these two specific settings and drives the DD field was not located in this pass (time-boxed) — the DD-format table above is implementable directly from the ETSI spec regardless, independent of confirming AnyTone's exact code path."
    }
  ]
}
```

## Detail: Motorola / DMR_Standard TX builder (`FUN_080f2b84 @ 0x080F2B84`)

Byte offsets below are relative to the start of the IP packet buffer this function builds (same
buffer layout as `DMR_Text_Message_Specification_rev_1.0.md`). `DAT_2000fc55` is the format
selector (`4` = DMR_Standard, else = Motorola, for this function — anything else routes elsewhere,
see `hytera-format-structure` and `rate34-tx-path-is-stub` above).

| Offset | Field | Motorola (else branch) | DMR_Standard (`fc55==4`) |
|---|---|---|---|
| 0 | Version/IHL | `0x45` | `0x45` (shared) |
| 1 | ToS | `0x00` | `0x00` (shared) |
| 2-3 | IP total length (BE) | computed | computed |
| 4-5 | IP sequence number | rolling counter | rolling counter (shared) |
| 6-7 | Flags/fragment | `0x0000` | `0x0000` (shared) |
| **8** | **TTL** | **`0x40`** ⚠️ spec/OpenGD77 says `0x01` | `0x01` (matches spec) |
| 9 | Protocol | `0x11` (UDP) | `0x11` (shared) |
| 10-11 | IP header checksum | computed (RFC791) | computed (shared logic) |
| 12 | Source addr byte0 | `0x0C` (user) | `0x0C` (shared) |
| 13-15 | Source addr (DMR ID, BE) | 3 bytes copied | 3 bytes copied (shared) |
| 16 | Dest addr byte0 | `0xE1` group / `0x0C` individual | same (shared) |
| 17-19 | Dest addr (DMR ID/TG, BE) | 3 bytes copied | 3 bytes copied (shared) |
| 20-21 | UDP src port | `0x0F 0xA7` (4007) | `0x13 0x98` (5016) |
| 22-23 | UDP dst port | `0x0F 0xA7` (4007) | `0x13 0x98` (5016) |
| 24-25 | UDP length | textLen\*2 + 18 | textLen\*2 + 12 |
| 26-27 | UDP checksum | computed (RFC768) | computed (shared logic) |
| 28 | — | `0x00` | `0x00` |
| 29 | — | Internal Length = textLen\*2+8 | `0x0D` |
| 30 | — | `0xA0` (or `0xE0` if a flag bit is set — purpose of that flag not identified) | `0x00` |
| 31 | — | Internal sequence = rolling counter `0x81..0x9F`, wraps to `0x81` | `0x0A` |
| 32 | — | `0x04` | text starts |
| 33 | — | `0x0D` | |
| 34 | — | `0x00` | |
| 35 | — | `0x0A` | |
| 36 | — | `0x00` | |
| 37+ | Text | UTF-16LE, uppercased | UTF-16LE, uppercased |

Every one of these (aside from the TTL discrepancy called out above) matches
`smsBuildMotorolaPayload()`/`smsBuildStandardPayload()`/`smsBuildIpHeader()`/`smsBuildMotorolaUdpHeader()`/
`smsBuildStandardUdpHeader()` in the current `sms.c` exactly. Treat this as strong, independent
confirmation that the existing Motorola/DMR_Standard implementation is correct against a real
AnyTone radio, not just against the KY4YI spec document.

## Detail: Hytera format raw pseudocode (unresolved bit layout)

Decompiled from `FUN_0810b62c @ 0x0810B62C` (builds a struct) feeding into
`FUN_080efc40 @ 0x080EFC40` (wraps it and calls `FUN_080efd20`, not investigated further):

```c
// FUN_0810b62c -- called when DAT_2000fc55 == 3 (Hytera)
void FUN_0810b62c(void)
{
    byte local_16, local_15;      // filled by FUN_080e882c(&local_15, &local_16) -- purpose unknown, called by ALL formats for some shared prep step
    undefined4 local_14, local_10;
    ushort local_c;

    FUN_080e882c(&local_15, &local_16);

    local_14 = (DAT_200125b6 == -1) ? (local_14 & 0xffffff7f) : (local_14 | 0x80);
    //   ^ looks like Group/Individual or similar single-bit flag in byte 0, bit 7

    byte bVar1 = (byte)local_14 & 0x9f | (DAT_200125bc & 1) << 6;
    //   ^ bit 6 of byte0 comes from DAT_200125bc bit0 -- possibly "Response Requested"

    if ((int)((uint)local_16 * 0x8000000) < 0) bVar1 |= 0x10;
    //   ^ bit4 of byte0 set based on bit4 of local_16 -- purpose unknown

    local_14._1_3_ = CONCAT21(local_14._2_2_, local_16) & 0xffff0f;
    byte bVar2 = local_14._1_1_ | (DAT_200125bc & 0x1e) << 3;
    //   ^ nibble from DAT_200125bc bits[4:1] packed into byte1 -- likely SAP or Format nibble

    local_14._0_2_ = CONCAT11(bVar2, (bVar1 & 0xf0) + (byte)((_DAT_200125bc << 0x15) >> 0x1c));
    local_c = CONCAT11(local_c._1_1_, ((byte)local_c & 0x7f) + ((byte)(_DAT_200125bc >> 4) & 0x80));

    FUN_080f39f0((int)&local_14 + 2, &DAT_2001258c);      // copy dest LLID (3 bytes, addr not shown but matches the same global used for dest ID elsewhere)
    FUN_080f39f0((int)&local_10 + 1, &DAT_2001258f, 3);   // copy source LLID (3 bytes, same global as source DMR ID elsewhere)

    local_c = (ushort)(byte)(((byte)local_c & 0x80) + (local_15 & 0x7f) | 0x80);

    FUN_080efc40(&local_14);   // hand off ~10-byte struct: local_14(4) + local_10(4) + local_c(2)
}

// FUN_080efc40 -- wraps the struct FUN_0810b62c built
void FUN_080efc40(undefined4 param_1)
{
    byte local_2c[32];
    FUN_080f3a24(local_2c, 0, 1);                  // init/zero, purpose not confirmed
    local_2c[0] = (local_2c[0] & 0xf0) + 6 & 0x4f;  // byte0 = some Format/DPF-like nibble derived from constant 6
    local_2c[1] = 10;                               // byte1 = fixed 0x0A (same constant reused in the Motorola/Standard sub-headers -- possibly coincidence, possibly a shared convention)
    FUN_080f39f0(local_2c + 2, param_1);            // copy the 10-byte struct from FUN_0810b62c starting at byte2
    FUN_080efd20(local_2c);                          // NOT investigated -- likely the actual CRC + TX-queue call
}
```

`DAT_200125bc` and `DAT_200125b6` are both referenced across many unrelated functions in this
firmware (general digital-mode state, not SMS-specific globals) — their exact bit meanings would
need to be cross-referenced against those other call sites to fully pin down. `FUN_080efd20` (the
final sink) was not opened in this pass.

## What NOT to re-investigate (dead ends already ruled out)

- **No static byte-template exists anywhere in the firmware** for any SMS header (Motorola,
  DMR_Standard, or Hytera) — everything is built field-by-field in code. Don't burn time on a raw
  byte-signature search for a Hytera template; it isn't there.
- **`FUN_0813d18c`/`FUN_0813d230`** (the two CRC-CCITT/poly-0x1021 routines in this firmware) are
  **not** the DMR data-header CRC — both are only ever called with a 4-byte length, not the 10
  bytes a DD_HEAD/data-header CRC needs. Don't assume these are re-usable for Defined Short Data
  header CRC work.
- Searching decompiled pseudocode for the literal strings `"Hytera"`, port numbers `0x0FA7`/`0x1398`
  as bare hex, or a 3-way `switch` on the format enum inside a single function: none of these exist.
  AnyTone's format dispatch is spread across multiple small functions gated by comparisons against
  the global `DAT_2000fc55`, not a single clean switch statement.

## Recommended next step

Static analysis of this firmware build has a real ceiling: the most interesting TX path
(`rate34-tx-path-is-stub`) turned out to be unimplemented, and the Hytera bitfield packing needs
either more RE budget or (more reliably, matching this project's own proven approach for the other
two formats) a real captured Hytera-format SMS to decode against — guessing further from static
code alone risks producing a plausible-looking but wrong encoder, the exact failure mode this
project has already been careful to avoid elsewhere (see `sms_send_format_choice.md`'s own
"unverified" callouts).
