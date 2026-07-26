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
`TX_END_1` log line, same as Status showed.

**Update 2026-07-25: Radio Check's opcode/scheme was genuinely wrong, now fixed and verified
against real reference source (see "CSBK opcode values" below) -- not just a spec-table check
this time, but a real, widely-deployed implementation's actual source code.** Call Alert's opcode
was already correct. This doesn't retroactively confirm real-hardware completion, but it's a much
stronger starting point for the next hardware test than "best recollection" was.

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

**CSBK opcode values -- history of this investigation:**

1. Originally shipped as best-recollection, not independently verified: `0x1F` (Call Alert),
   `0x1D` (Radio Check request), `0x20` (Ack).
2. First cross-check, against ETSI TS 102 361-1/-2 (both PDFs saved in this folder -- see
   [[reference_etsi_dmr_specs]]): TS 102 361-1 clause 9.3.32 delegates all CSBKO value assignments
   to TS 102 361-2's Table B.2 ("CSBKO List"), which only covers **FID=0** (the "standard,
   non-vendor" facility set this firmware's `csbkBuildFrame()` uses) and lists just six opcodes --
   none of which are `0x1F`/`0x1D`/`0x20`. This raised a real concern that FID=0 itself might be
   the problem, since Call Alert/Radio Check are genuine DMR features but not part of the open
   standard's base facility set.
3. **Second cross-check, against real reference source** (the user has a local copy of
   MMDVMHost's source, part of the WPSD hotspot distribution -- `MMDVMHost/DMRCSBK.h`/`.cpp`, a
   real, widely-deployed, interoperating DMR gateway implementation, not just a spec document).
   This resolved it properly:
   - **`CALL_ALERT = 0x1F` and its ack `CALL_ALERT_ACK = 0x20` are exactly correct** -- this
     firmware already matched MMDVMHost's own values precisely. Nothing to fix here.
   - **`RADIO_CHECK` was genuinely wrong**: MMDVMHost uses `0x24`, not `0x1D`. **Fixed** --
     `CSBKO_RADIO_CHECK_REQ` (`csbk.h`) renamed to `CSBKO_RADIO_CHECK = 0x24U`.
   - The request/ack **scheme** was also wrong, not just the opcode: unlike Call Alert (two
     distinct opcodes for request vs ack), Radio Check uses **the same opcode for both
     directions**, disambiguated by byte `[3]` of the CSBK frame (`0x80` = request, anything else
     = ack) -- and the destination/source address fields' *meaning* flips between the two
     directions (request: dst=bytes4-6/src=bytes7-9, same as Call Alert; ack: src=bytes4-6/
     dst=bytes7-9, reversed). **Fixed** -- `csbkBuildFrame()`/`csbkStartRequest()` gained a
     `directionByte` parameter (`CSBK_RADIO_CHECK_REQUEST_MARKER = 0x80U`), and
     `csbkHandleReceivedFrame()` was restructured so the generic "is this frame addressed to us"
     check no longer happens once, before the switch, for all opcodes uniformly -- Radio Check's
     two directions each validate and extract their own fields now, since a single shared
     pre-switch check would incorrectly reject a legitimate incoming Radio Check ack (whose
     "address meaning" differs from its own request).
   - **Also revises the FID=0 concern from step 2**: MMDVMHost's CSBK dispatch (`switch
     (m_CSBKO)`) never inspects the FID byte at all for any of these opcodes. So FID=0 is very
     likely *not* actually a problem for interop with real infrastructure -- these opcodes are
     real, in-use, cross-vendor values that simply live outside the ETSI base-standard's narrow
     FID=0 table, not something that needs a different FID to be recognised. Left unchanged
     (still `frame[1] = 0x00U`).
   - One more real opcode MMDVMHost has that this firmware doesn't implement at all:
     `CALL_EMERGENCY = 0x27` -- noted for awareness, not implemented, not asked for.

**Build status**: fix applied to `csbk.h`/`csbk.c`, compiled clean (DM1701_FW), new code confirmed
present in the linked binary's disassembly. **Not yet tested on real hardware** -- next real-radio
test of Radio Check should now have a much better chance of actually completing, but this needs a
real over-the-air confirmation before treating it as done.

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

## Call Emergency (`0x27`) -- considered, deliberately NOT built

While cross-referencing Call Alert/Radio Check against MMDVMHost's `DMRCSBK.h`, noticed it also
defines `CALL_EMERGENCY = 0x27` -- a real, standard DMR CSBK type this firmware doesn't implement.
Structurally it's a standalone CSBK like Call Alert/Radio Check (the existing `csbk.c` framework
could carry it with modest changes), but MMDVMHost's parser sets `m_GI = true` for it -- unlike
Call Alert/Radio Check (always individual/private), Call Emergency is **group-addressed**: a
broadcast to the whole talkgroup, not a page to one radio. Implementing it for real would need
group-addressing support this firmware's CSBK code doesn't currently have (Call Alert/Radio Check
are both hardcoded to private/individual).

**Deliberately not built, and this isn't a "maybe later" backlog item -- it's a considered
no, for a reason specific to amateur radio rather than any technical blocker:**

On a commercial/public-safety DMR system, an emergency CSBK reaches a staffed dispatch console
obligated to respond -- often bundled with a hot mic, an automatic channel switch, and priority
channel access that preempts normal traffic. None of that infrastructure exists on amateur radio.
Implementing "Emergency" here would only ever alert whoever else happens to be listening on that
talkgroup at that moment -- no dispatcher, no guaranteed responder, nothing that calls 911.

**The risk isn't a missing feature, it's a false one**: a user who presses "Emergency" expecting
it to behave like it would on a work radio -- summoning real help -- gets nothing but a group
alert to an unstaffed channel. False confidence in a genuine emergency is worse than not having
the button at all. Given this session already found a real, previously-unnoticed bug in Radio
Check (wrong opcode, sat unfixed for a while) using the *same* kind of unverified-until-tested CSBK
code, a safety-adjacent feature failing silently -- or working exactly as coded but not as a user
in a real emergency would assume -- is a materially worse failure mode than either of those.

If this ever gets reconsidered: it would need a UI that's unambiguous about what it actually does
("alerts other radios on this talkgroup" -- not "calls for help"), group-addressing support in
`csbk.c`, and a higher testing bar than Call Alert/Radio Check got before shipping, not a lower
one.

## Known gaps / not yet done

- ~~Call Alert's "notify the user" behaviour (tone/popup on receipt) is not implemented~~ --
  **added 2026-07-25**: `csbk.c` now flags a pending notification (`incomingAlertNotification`,
  consumed via `csbkConsumeIncomingCallAlert()`) whenever a genuine incoming Call Alert is
  received, separate from the auto-Ack (which still happens unconditionally regardless). Polled
  in `applicationMain.c` alongside the other SMS/CSBK notification checks: plays
  `MELODY_PRIVATE_CALL` (same tone as an incoming private voice call) and shows "Alert from
  `<ID>`". Radio Check remains deliberately silent on receipt -- unchanged, that's the point of a
  "check" vs an "alert". Compiled clean, confirmed present in the linked binary's disassembly.
  **Not yet tested on real hardware.**
- Not yet ported to `MD9600_RT90`, `V2_STM32-MOB`, or `V2_DEBUG` -- deliberately kept to V3_TEST
  only until proven out on real hardware first, matching how the SMS decode fixes were rolled out
  earlier this session.
- Only built for the `DM1701_FW` config of V3_TEST so far (this session's usual Docker-build
  target) -- not yet verified for `MDUV380_FW`/other V3_TEST configs.
- Call Alert/Radio Check opcodes: now verified against real reference source (see "CSBK opcode
  values" above) -- but still needs a real over-the-air test to confirm the fix actually works.
