# Call Alert, Radio Check and Status (V3_TEST)

This documents three new DMR services added to `V3_TEST/MDUV380_firmware`:
**Call Alert**, **Radio Check**, and **Status**. All three are reachable from the SMS menu
(`SEND SMS` / `INBOX` / `QUICK TEXT` / `SENT` / `CALL ALERT` / `RADIO CHECK` / `STATUS`), and
all three now offer the same "Select contact" / "Manual ID" destination picker that SMS Compose
uses, before sending.

Real hardware confirmed working as of this writeup: entering all three screens, the full
destination-select flow, and Status sending end-to-end (`TX_END_1: smsActive TX complete`).
**Call Alert and Radio Check's actual send/TX completion has not yet been explicitly confirmed**
on real hardware (only that queuing the request succeeds) -- worth one more test watching for a
`TX_END_1` log line, same as Status showed. **Update (see "CSBK opcode values" below): there is
now a concrete, spec-backed reason to suspect this gap isn't just "hasn't been tested yet" --
these opcodes are sent under FID=0, but ETSI TS 102 361-2's own FID=0 opcode table doesn't include
them, so a real receiving radio may simply be discarding them as unrecognised.**

## What each one does

- **Call Alert**: pages a destination radio -- sends a standalone CSBK, the destination is
  expected to notify its user (tone/popup). This firmware's RX side auto-Acks it; a user-facing
  notification/tone on receipt has not been added yet (see "Known gaps" below).
- **Radio Check**: silently asks whether a destination radio is on and in range. The destination
  auto-replies with no UI at all (that's what makes it a "check" rather than an "alert"). The
  requester sees "Alert: ID OK" / "Check: ID OK" or "... no answer" via the same notification
  mechanism SMS TX events already use.
- **Status**: sends a single numeric code (from a small fixed table -- Available, En Route, On
  Scene, Busy, Returning, Out of Svc, Need Help, Testing) instead of free text. Unlike Call
  Alert/Radio Check, this rides the same tested Confirmed/Unconfirmed Data transport SMS text
  uses (data header + one data block), just with a 1-byte payload and a distinct SAP.

## Architecture

### Call Alert / Radio Check (new file: `functions/csbk.c` / `csbk.h`)

These are **standalone CSBK bursts** -- a complete PDU in themselves, unlike SMS's CSBK-preamble-
then-data-header shape. To send one:

- `smsPreparedMessage_t` gained a `csbkOnly` flag and `csbkRepeatCount` field (`sms.h`). When set,
  `HRC6000StartQueuedSMS()` (`HR-C6000.c`) repeats the already-fully-built CSBK frame
  `csbkRepeatCount` times and sends nothing else -- no data header, no data blocks. Every existing
  SMS call site leaves `csbkOnly` false, so real SMS TX behaviour is provably unchanged.
- `csbk.c` builds the frame (opcode + FID + service byte + dest/source address + CRC16-CCITT,
  masked with `0xA5`, same construction as the already-proven SMS Preamble CSBK) and queues it via
  a new `smsQueueRawCsbkMessage()` entry point in `sms.c`.
- RX: `HR-C6000.c`'s data-sync dispatch already reads every data-class frame into `dataSyncBuf`
  regardless of type; a new branch for `rxDataType == 3` (CSBK, as opposed to SMS's data
  header/blocks) hands it to `csbkHandleReceivedFrame()`.
- A Radio Check/Call Alert request and its Ack share one pending-request slot (`csbk.c`'s
  `queuedRequest`/`pendingOutbound`), ticked from `csbkTick()` (called alongside `smsTick()`).

**CSBK opcode values are still best-recollection, not independently verified**: `0x1F` (Call
Alert), `0x1D` (Radio Check request), `0x20` (Ack). Unlike the SMS Preamble CSBK opcode (`0x3D`,
proven correct because the whole SMS send path works against real BrandMeister/MMDVMHost
traffic), these three have only been tested radio-to-itself (loopback) so far.

**Update, now backed by the actual spec** (both `ETSI TS 102 361-1` and `-2` PDFs are saved in
this folder as of this writeup -- see [[reference_etsi_dmr_specs]]): TS 102 361-1 clause 9.3.32
explicitly delegates all CSBKO *value* assignments to TS 102 361-2. That document's Table B.2
("CSBKO List") is the complete list of opcodes defined under FID=0 (the "standard, non-vendor"
facility set this firmware's `csbkBuildFrame()` uses -- `frame[1] = 0x00U`), and it contains
**only six values**: `0x04` (UU_V_Req), `0x05` (UU_Ans_Rsp), `0x07` (CT_CSBK), `0x26` (NACK_Rsp),
`0x38` (BS_Dwn_Act), `0x3D` (Pre_CSBK -- confirmed correct, matches this firmware's existing SMS
Preamble CSBK exactly). **`0x1F`, `0x1D`, and `0x20` are not on this list.** Call Alert and Radio
Check are real, widely-used DMR features, but not ETSI-standard ones -- they're implemented via
vendor-specific Feature IDs in real hardware (each manufacturer's own facility set), which is a
different, proprietary numbering space that isn't published in either ETSI document. Sending these
opcodes under FID=0 almost certainly means a real, spec-compliant radio just discards them as
unrecognised/reserved, rather than acting on them as Call Alert/Radio Check -- which lines up
exactly with "queuing/TX succeeds but completion was never confirmed" above.

**What would actually fix this**: the real vendor FID value (and possibly different opcode
numbers entirely, under that FID's own space) for a genuine Motorola/Anytone/Hytera Call
Alert/Radio Check -- not derivable from either ETSI document, since vendor FID facility sets
aren't published there. Same approach that worked for the SMS Defined-Short-Data header (see
`sms_send_format_choice.md`) would apply: get a real captured Call Alert/Radio Check burst from an
actual vendor radio (e.g. via an MMDVMHost verbose log) and reverse-engineer the FID + opcode from
that, rather than guessing further. **Deliberately not attempted yet** -- coming back to this
later.

### Status (extension to `sms.c` / `sms.h`)

- `SMS_STATUS_SAP_NIBBLE` (`0x90`) tags the data header -- ETSI reserves SAP value 9 for
  "Proprietary Packet Data", which is exactly what this is. This is a fork-internal convention,
  not a documented cross-vendor standard: it will only interoperate between radios running this
  same firmware, not a real Hytera/AnyTone Status feature.
- `smsHandleReceivedDataFrame()` gained an early branch (before the existing SAP 0x40/0xA0
  validation) that recognises SAP 0x9 and handles it as a **self-contained single-block
  mini-transaction** (`statusAssembly` in `sms.c`), completely separate from the multi-block
  `rxAssembly` state machine the text-decode path uses -- zero risk to the already-hard-won SMS
  decode fixes from earlier this session.
- Ack scheduling reuses `smsScheduleAckResponse()`, the same deferred mechanism SMS text uses.

## UI (`user_interface/menuCsbkActions.c` / `menuCsbkActions.h`, new files)

A single shared screen (`MENU_CSBK_ACTIONS`) handles all three, driven by
`menuCsbkActionsSetKind()` called from `menuSMS.c` before pushing the menu. Flow:

1. **Status only**: pick a code from the fixed table first.
2. **All three**: "Send to" screen -- Select contact (private contacts from the codeplug) or
   Manual ID (numeric entry). Mirrors `menuSMSCompose`'s destination-select pattern in `menuSMS.c`
   but is fully self-contained -- it does not touch or reuse any of Compose's internal state
   machine, to avoid any risk to that already-tested code.
3. Send, then pop back to the SMS menu. Results (Ack/timeout/status-received) surface later via
   the same global notification poll in `applicationMain.c` that already shows "SMS sent"/"SMS
   ACK" popups, regardless of which screen you've since navigated to.

## A real bug found and fixed along the way: `menuDataGlobal.data[]`

Adding `MENU_CSBK_ACTIONS` caused a **hard crash/hang on entering the menu** (screen frozen, radio
unresponsive to the power button, nothing on serial) that took several rounds of targeted debug
logging to isolate -- see the exact steps in `menuSystem.c`'s comments near `.data =`.

**Root cause**: `menuSystem.c` defines `menuDataGlobal.data[]` as a flexible array member,
populated via a hand-maintained, position-matched initializer list that has to mirror the
`MENU_SCREENS` enum in `menuSystem.h` *exactly*, entry-for-entry -- a comment right above it says
so explicitly. That list was **already silently short by two entries** (for
`MENU_SMS_QUICKTEXT`/`MENU_SMS_QUICKTEXT_EDIT`, a pre-existing gap unrelated to this work -- every
entry from there onward was already reading one slot forward). Adding `MENU_CSBK_ACTIONS` without
adding a matching entry here made it three short overall. Since every affected slot happened to be
`NULL` anyway, the pre-existing 2-entry shift was invisible; the *new* 3rd shortfall wasn't --
`MENU_CSBK_ACTIONS` was now the very last enum value (only `#if HAS_COLOURS` entries follow it),
so indexing `.data[MENU_CSBK_ACTIONS]` read genuinely out-of-bounds memory, past the array's
actual allocation, which then got dereferenced as a `menuItemsList_t*` a few lines later in
`menuSystemPushMenuFirstRun()` -- a classic wild-pointer hard fault.

**Fix**: added the 3 missing `NULL` entries (2 pre-existing + 1 new) to `.data{}` in the correct
positions, restoring 1:1 alignment with the enum.

**Lesson for future menu additions in this codebase**: adding a new `MENU_SCREENS` enum value
requires updating **three** places, not two -- `menuFunctions[]` *and* `menuDataGlobal.data{}` in
`menuSystem.c`, both hand-maintained positional lists with no compile-time check that they match
the enum's length or order. Getting only `menuFunctions[]` right (as this session first did) still
compiles and links cleanly; the bug only shows up at runtime, and only once you actually enter the
new/shifted screen.

## Known gaps / not yet done

- Call Alert's "notify the user" behaviour (tone/popup on receipt) is not implemented -- RX
  currently only auto-Acks silently, same as Radio Check.
- Not yet ported to `MD9600_RT90`, `V2_STM32-MOB`, or `V2_DEBUG` -- deliberately kept to V3_TEST
  only until proven out on real hardware first, matching how the SMS decode fixes were rolled out
  earlier this session.
- Only built for the `DM1701_FW` config of V3_TEST so far (this session's usual Docker-build
  target) -- not yet verified for `MDUV380_FW`/other V3_TEST configs.
- Call Alert/Radio Check opcodes: see the "unverified" note above.
