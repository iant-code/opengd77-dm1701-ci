# Spectrum analyser plan (V3_TEST / DM1701)

A new full-screen `Spectrum` menu item: a configurable, graphical swept-RSSI spectrum
analyser with a movable frequency cursor and a max-hold trace. Planned on the `feature/games`
branch (shares the games' full-screen tick-loop UI pattern); could be split to its own branch
before implementation if preferred.

## What the hardware can actually do (feasibility, checked in the code first)

This is the load-bearing constraint the whole design follows from:

- The AT1846S radio chip provides **one wideband RSSI value at the currently-tuned frequency**
  (register `0x1b`, read via `radioReadReg2byte()` in `AT1846S.c`; already used by
  `radioReadRSSIAndNoiseForBand()`). There is **no FFT / panadapter hardware** on this radio.
- So a "spectrum" here is a **swept-tuned scalar measurement**: retune across the span with
  `radioSetFrequency()`, read RSSI at each step, plot power-vs-frequency. This is a real, valid
  analyser architecture (the same one every non-SDR service monitor uses), but it is **not** an
  FFT/live-video spectrum, and the plan is honest about that throughout.
- **Resolution** = the receiver IF bandwidth (12.5 / 25 kHz), NOT an FFT bin width.
- **Sweep speed floor**: each bin costs a retune (`radioSetFrequency()` = ~8 I2C register writes
  + PLL settle) plus one RSSI read. Realistically **~3-8 ms per bin** for a trustworthy reading.
  At ~128 screen columns that is **~0.6-1.0 s for one complete sweep** — this is a hard physical
  floor, so the fastest "refresh" option below is really "sweep continuously", and the slower
  options just idle between sweeps.
- **Amplitude is uncalibrated / relative.** `trxGetRSSIdBm()` has per-band offset/divisor
  constants (`VHF_RSSI_OFFSET` etc.), so an approximate dBm axis is possible, but it is NOT a
  lab-accurate, flatness-corrected amplitude across the span. Treat the vertical axis as relative
  signal strength.
- **Dedicated mode.** While sweeping, the radio is tuned off the current channel, so normal
  RX/audio is suspended for the duration. The screen MUST save the current frequency/mode on
  entry and **restore it on exit** (except the deliberate tune-to-cursor case below). TX is
  disabled in this mode.
- **One band per sweep.** The VHF↔UHF split has different LNA/band configuration
  (`radioSetRxLNAForDevice()`, `trxGetBandFromFrequency()`), so a single sweep stays within one
  band. The Band setting (below) selects which.

### Why max-hold matters here specifically

Because a full sweep takes ~1 s, the *live* trace will simply miss most short transmissions —
the sweep isn't sitting on their frequency when they key up. **Max-hold is what makes a slow
swept analyser actually useful**: it catches and retains the peak at each frequency even if the
signal was only present for one sweep. This is exactly the "visualise the last strong signal at
a particular freq" behaviour requested, and it's the headline feature, not a nicety.

## Configuration (the "hold GREEN" settings sub-menu)

Default on entry: **2 m band**. Holding **GREEN** (`KEY_GREEN` + `KEY_MOD_LONG`) opens a small
settings overlay with three rows (LEFT/RIGHT cycles each value, GREEN/RED closes it):

1. **Band** — `145 MHz (2m)` or `443 MHz (70cm)`. Selects the sweep's centre band and the RX
   band config. (Centre frequencies are single constants — 145.000 / 443.000 as requested; note
   433.000 is the UK 70 cm centre if that's preferred, a one-line change.)
2. **Span** — `0.5 / 1 / 2 / 5 / 10` MHz. The total width swept, centred on the band centre.
   10 MHz is included (feasible; just coarser Hz-per-column). Default **2 MHz**.
3. **Refresh** — full-sweep cadence: `1 / 3 / 6 / 10 / 30` s. At 1 s it sweeps essentially
   continuously (the physical floor); longer values do one sweep then idle until the interval
   elapses before the next — pairs naturally with persistent max-hold for long unattended
   monitoring. Default **1 s**.

## Controls (main screen)

- **Up / Down** — move the frequency cursor (a vertical marker line) up/down in frequency, one
  bin per press. (Left/Right = coarse jump, e.g. 10 bins, optional.)
- **GREEN (short press)** — exit AND retune the real VFO/channel to the cursor frequency
  ("find a signal, then go listen to it"). This is the deliberate exception to freq-restore.
- **GREEN (long press / hold)** — open the settings sub-menu above.
- **RED** — exit WITHOUT changing anything; restores the original frequency/mode.
- **A number/soft key** (e.g. `1`) — toggle max-hold mode: **slow-fade ↔ persistent**
  (both modes supported, toggle-able, per request).
- **Another key** (e.g. `0`) — clear/reset max-hold immediately.
  (Exact key assignments finalised at implementation; on-screen hints shown.)

## On-screen layout (160 × 128 colour)

- **Trace area**: ~128 columns wide × ~90 px tall. Each bin = a vertical bar
  (`displayDrawFastVLine`) whose height ∝ RSSI (live trace, normal colour).
- **Max-hold**: drawn as a bright/contrasting cap at the peak height per column. In slow-fade
  mode it decays one level every few sweeps; in persistent mode it stays until cleared.
- **Left margin**: a small relative-level scale.
- **Bottom**: frequency labels (span start / centre / end).
- **Cursor**: a full-height vertical marker at the selected bin, plus a readout box showing
  **cursor frequency + live level + max-hold level** at that frequency.
- Colour drawing uses the same approach the Snake/Space games already use on this display.

## Sweep engine (how it stays responsive)

The UI runs in the menu task; a blocking full sweep would freeze the UI for ~1 s. Instead,
model it on the games' tick loop:

- Keep two per-column arrays: `current[NUM_BINS]` and `maxHold[NUM_BINS]`.
- Each menu tick, advance the sweep by a **bounded handful of bins** (retune → short settle →
  read RSSI → store → update maxHold), then return so the UI stays live and keys stay responsive.
- When the sweep index wraps `NUM_BINS`, that's one complete trace; apply the Refresh cadence
  (either immediately start the next sweep, or idle a `ticksTimer_t` until the interval elapses).
- Redraw incrementally as columns update, so the user sees the sweep progress across the screen.

## Integration checklist (the standing gotchas this project has been bitten by)

- New `MENU_SPECTRUM` value in the `MENU_SCREENS` enum (`menuSystem.h`) needs **all three**
  hand-maintained synced lists updated or it's a silent hard-fault-on-entry:
  `menuFunctions[]`, `menuDataGlobal.data[]`, and the `menuDisplayMenuList.c` label override
  ("Spectrum") — same pattern as Snake/Space/Privacy Keys.
- Reachable from the **Main Menu** (`mainMenuItems[]` in `menuSystem.c`).
- New source file `user_interface/menuSpectrum.c`, function declared directly in `menuSystem.h`
  (no separate header — matches the games' precedent).
- Build-file plumbing: add `menuSpectrum.c`/`.o`/`.d` to the `user_interface/subdir.mk` **three
  lists** AND to `objects.list` (separate static linker input — missing it = undefined-reference
  at link).
- **No libc `rand()`** anywhere (not needed here anyway).
- `displayFillRect()` has the inverted `isInverted` polarity vs the other primitives — use
  `displayDrawFastVLine` for the bars (normal polarity) to avoid that trap.

## Phased build

1. **Skeleton screen + menu wiring** — `MENU_SPECTRUM` reachable, blank framed screen, RED exits
   and restores frequency. Verifies the integration (the risky-for-hard-fault part) in isolation.
2. **Sweep engine + live trace** — retune/read/plot across a fixed 2 m / 2 MHz span, incremental
   and non-blocking. Verify it doesn't stall the UI and that frequency is cleanly restored.
3. **Cursor + readout** — movable marker, freq/level readout, GREEN-short tune-to-cursor-on-exit.
4. **Max-hold** — dual-array, slow-fade + persistent toggle, clear key.
5. **Settings overlay** — hold-GREEN Band / Span / Refresh, applied live.
6. **Polish** — labels, colour, scale, on-screen key hints.

Each phase built via the docker DM1701 toolchain and (per project practice) flagged as
UI-unverified until actually exercised on real hardware — especially the sweep timing / PLL
settle margin, which can only really be judged on the radio.

## Honest unknowns / caveats

- **PLL settle time per bin is an estimate** (~3-8 ms) until measured on hardware. If RSSI reads
  taken too soon after a retune are unreliable, the per-bin time (and thus sweep rate) goes up.
  This is the single biggest thing that can only be confirmed on the real radio.
- **Not an FFT** — resolution is IF-bandwidth-limited, amplitude is relative/uncalibrated. This
  is a swept service-monitor-style analyser, correctly framed, not an SDR panadapter.
- **Audio is off while sweeping** (dedicated mode). A future enhancement could restore channel RX
  during the idle gap between sweeps at long Refresh intervals, but that's out of scope for v1.
