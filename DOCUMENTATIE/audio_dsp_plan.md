# Audio DSP feasibility assessment: compander, noise reduction, general DSP

Scope: analog FM mode only. DMR digital voice uses the AMBE vocoder (parametric speech
compression on the HR-C6000/AMBE chip, entirely offloaded from the MCU) — "compander"/"noise
reduction" in the classic analog-audio sense aren't meaningful concepts for already-vocoded
digital audio. RX-side NR *could* theoretically apply to decoded DMR audio too, and TX-side mic
NR could help AMBE encode quality, but the primary, well-scoped target for all three asks is FM.

## What the codebase actually has today (researched, not assumed)

- **AT1846S** (`hardware/AT1846S.c`/`.h`) is the analog FM RX/TX front-end chip (separate from the
  HR-C6000 DMR codec). Confirmed existing register control: init/reset, per-bandwidth AGC/squelch
  tables, FM/DMR mode switch, CTCSS/DCS encode+decode, tone/DTMF deviation. No compander, ALC, or
  noise-reduction register writes exist anywhere in this file, and grepping for those terms finds
  nothing. De-emphasis is explicitly NOT done by this chip — a comment at `AT1846S.c:196` says
  it's handled by the HR-C6000 instead.
- **No AT1846S register-map documentation exists in this repo.** `DOCUMENTATIE/` has a full
  `HR-C6000 Registers_G4EML.txt` reference (which is why the DMR privacy and SMS rate-3/4 work
  earlier could be grounded in real register facts), but nothing equivalent for AT1846S beyond one
  incidental register (`0x1b`, RSSI, used by the Spectrum screen). This is the single biggest
  blocker for anything at the chip-register level — see "What's needed before Phase 1" below.
- **`menuSoundOptions.c`** already exposes: mic gain (DMR and FM separately), VOX threshold/tail,
  beep/prompt settings, and a **DMR RX AGC** toggle (`OPTIONS_AUDIO_DMR_RX_AGC`). No compander or
  NR option exists, wired up or otherwise.
- **`functions/sound.c` already does real-time sample-domain DSP** — not just tone/beep
  generation. `sound.c:340-377` implements a genuine software AGC applied per-sample, in floating
  point, to decoded DMR RX audio (peak-tracked gain, applied as
  `i2s_Tx_Buffer[...] = (int16_t)(sample * dmrRxAgcGain)`). This is the load-bearing fact for
  this whole plan: it proves the MCU has spare real-time compute for lightweight per-sample audio
  processing, *and* it's an existing, working hook point to extend rather than new plumbing to
  invent. `soundReceiveRefillData()` (TX mic buffering) is the equivalent hook on the TX side.

## Hardware headroom

STM32F405 Cortex-M4 with FPU (this firmware already links `-mfloat-abi=hard`, and the existing
AGC uses real float math, so hardware FPU use is already proven, not a new risk). Clocked at
**72MHz** (`Core/Src/main.c:266-289`) against a 168MHz-rated part — real headroom exists, though
raising SYSCLK has broad implications (flash wait states, every peripheral timing) and isn't
proposed here; the point is the current 72MHz already isn't the ceiling. FreeRTOS heap is a tight
20KB (`FreeRTOSConfig.h:67-71`), which constrains anything that wants dynamically-allocated filter
state/coefficient tables — new DSP work should use static buffers, matching how the existing AGC
and audio buffers are already declared.

No in-repo profiling/CPU-budget data exists beyond this. Feasibility below is "architecturally
plausible given the existing AGC precedent," not "proven safe under worst-case load" — that needs
an actual build-and-measure step per phase, not just this research pass.

## Per-feature assessment

### Compander — one viable path (hardware ruled out)

**Path A: hardware/register-level, via the AT1846S itself — RULED OUT, see update below.** This is how most real handheld FM
transceivers implement companding — many single-chip FM ICs in this class include a
compressor/expander as a built-in analog or mixed-signal block, controlled by a register bit, not
something the MCU computes. **UPDATE, 2026-07-30: ruled out, not just blocked.** Obtained and read
the real 25-page RDA1846S Programming Guide (the AT1846S's underlying OEM part) in full, including
its complete register introduction section. Every audio-processing register in the chip is
accounted for: fixed linear filters with bypass toggles (register `0x58h`: RSSI lowpass, VOX low/
highpass, pre/de-emphasis, voice low/highpass, CTCSS low/highpass), a target-power AGC (register
`0x32h`, `agc_target_pwr` — standard receiver leveling to a fixed target, not a compander), and a
multi-criteria squelch (RSSI/noise/RSSI+noise-block/adjacent-channel/FM-modulation detect,
register `0x3Ah` `sq_dten`) that only gates audio on/off, doesn't reduce noise within it. There is
**no envelope-dependent nonlinear compression/expansion register anywhere in the chip**, and no
noise-reduction/suppression register. This is a hardware fact now, not a documentation gap — Path
A doesn't exist on this silicon. All compander/NR work has to be Path B.

One genuine synergy the datasheet does offer for Path B: the chip exposes a live noise-floor
reading via a read-only register (`0x1Bh[7:0]`, `noise_db`, already read by this firmware for RSSI
purposes at the same address) — this is the same input the chip's own squelch algorithm uses, and
a software NR/gate on the MCU could read it directly as a real-time noise estimate instead of
having to derive one from the audio signal itself. Also noted in passing (not compander/NR, but a
free related finding): the chip has a full "tail elimination" feature (`tail_elim_en`, register
`0x30h[11]`, using CTCSS/DCS phase-shift signalling to suppress the squelch-tail noise burst at
end of transmission) that isn't obviously wired up in `AT1846S.c` currently — a separate, easy
audio-quality win if wanted, out of scope for this specific compander/NR/DSP request.

**Path B: software, in the existing sample-domain audio pipeline — RULED OUT for FM, 2026-07-30.**
This section originally assumed a syllabic compander could hook into `soundReceiveRefillData()`
(TX mic) and the RX AGC's sample loop (`sound.c:340-377`) the same way the DMR RX AGC does. Traced
both paths in full before writing any code, and that assumption was wrong:

- **RX**: FM RX audio is a fully analog signal path end-to-end -- AT1846S `AFOUT` -> HR-C6000
  analog feedthrough (`HRC6000SetFmAudio()`, SPI register `0x36=0x02`, literally commented "Enable
  the FM audio FeedThrough") -> an analog mux gated by GPIO (`radioAudioAmp()`) -> speaker amp. It
  never becomes MCU-visible PCM. The `i2s_Tx_Buffer`/`sound.c` AGC machinery this plan was built on
  is DMR/AMBE-decode-only -- there are no `trxGetMode()`/`RADIO_MODE_ANALOG` branches anywhere near
  it because it is simply never invoked for FM.
- **TX**: same story in reverse. `soundReceiveData()` (the mic-buffering function this plan meant
  to hook) has exactly one caller, inside DMR-TX-specific state-machine code in `HR-C6000.c`
  (feeding the AMBE encoder). FM TX mic audio goes mic pin -> AT1846S's own internal ADC -> its
  internal modulator, also never touching the MCU digitally.

**Conclusion: a software compander cannot be implemented in firmware on this hardware as currently
wired**, in either direction, for FM. This isn't a missing-buffer problem solvable by writing more
code in `sound.c` -- there is no digital sample stream for FM audio for firmware to intercept at
all. A genuine fix would mean *changing the analog signal routing itself* (adding an ADC tap on
the AFOUT path, or reconfiguring the HR-C6000/AT1846S to digitize FM audio instead of feeding it
through as an analog feedthrough) -- a hardware/routing-level change, not a firmware one, and a
materially bigger undertaking than "attempt a software compander" implied.

### Noise reduction — same routing problem as the compander, also ruled out for FM

Same conclusion as Path B above, for the same reason: NR (noise gate or real spectral NR) would
need to intercept FM audio as digital samples, and no such access exists on this hardware for FM
in either direction. The `noise_db` register synergy noted earlier is real, but it's moot without
a digital audio stream to apply it to.

### General DSP (beyond these two) — corrected

The original framing here ("the hook points and precedent already exist") was wrong for FM,
for the same reason as above -- that precedent (`sound.c`'s AGC) is DMR/AMBE-decode-specific, not
general-purpose. It genuinely does hold for **DMR digital audio** (which was never the target for
compander/NR to begin with -- see the top of this document), so if there's ever a want for some
other DSP effect applied specifically to decoded DMR RX audio, that hook point is real and proven.
For FM specifically, no software DSP hook exists at all right now.

## Where this leaves things

**Tail elimination**: real hardware feature, built, on/off under Sound Options ("Tail Elim"),
compiles clean, not yet hardware-tested. See the implementation for exact register/bit details.

**Compander and noise reduction**: not achievable in firmware as currently wired, in either
direction, for FM. This isn't a documentation or effort gap the way tail-elimination's register
map was -- it's a hardware signal-routing fact confirmed by tracing the actual FM RX and TX audio
paths in full. A real fix would require changing how FM audio is routed at the hardware level
(digitizing it somewhere between the AT1846S and the speaker/mic, which nothing in this firmware
currently does) -- out of scope for a firmware change, and a materially different (bigger, harder,
hardware-level) undertaking than what was originally asked for.
