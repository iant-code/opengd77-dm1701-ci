# AnyTone D890UV firmware audio/RF-chip findings — for OpenGD77 AT1846S.c

**Source**: same static reverse-engineering pass as `anytone_d890uv_sms_findings.md` (same file,
same Ghidra decompile of all 3873 functions). Cross-referenced against this repo's
`AT1846S.c`/`AT1846S.h` (MDUV380/DM1701 driver) rather than re-deriving register meanings from
scratch.

**Purpose**: (1) document the D890UV's user-facing audio settings surface, (2) record the RF/audio
chip register writes found in the firmware for comparison against this project's own AT1846S
calibration values, (3) answer the "what is the second chip-select" question with actual evidence
rather than a guess.

```json
{
  "firmware": {
    "model": "AnyTone AT-D890UV", "version": "V1.05", "file": "D890UV_V1.05_20260521.CDD"
  },
  "confirmed": [
    {
      "id": "rf-chip-family",
      "confidence": "confirmed",
      "summary": "D890UV drives an AT1846S-family chip (or a register-compatible clone, e.g. RDA1846-series) via a bit-banged register bus. Function FUN_080e2ff0(reg, value16, chipSelect) matches this repo's I2C_AT1846_set_register()/radioSetClearReg2byteWithMask() pattern: register number, then a 16-bit value written as two bytes, gated by two GPIO lines (clock + chip-select).",
      "evidence": "Register numbers written by the firmware overlap directly with AT1846S.c's own commented register table: 0x41 (digital voice gain), 0x42, 0x43 (FM deviation), 0x44 (rx/tx AF gain), 0x47, 0x48, 0x53, 0x56-0x60, 0x66, 0x68, 0x69, 0x30 (soft reset/poweron in AT1846S.c) all appear in the D890UV's write list with plausible-looking values in the same numeric ranges this repo's driver uses."
    },
    {
      "id": "second-chip-select-is-second-identical-rf-chip",
      "confidence": "high, not 100% schematic-verified",
      "summary": "The chip-select argument (param_3 in FUN_080e2ff0) is not a mode flag on one chip -- it selects between two physically separate GPIO chip-select lines, and the firmware performs a FULL independent chip bring-up (soft reset + the entire register init sequence) for each selectable index. This is genuine dual-RF-chip hardware, not one chip with a TX/RX-select bit.",
      "evidence": [
        "FUN_080e2ff0's chip-select branch toggles two DIFFERENT GPIO port base addresses (0x42020400-region vs 0x42020c00-region, i.e. different physical pins/ports, not a bit within the same register) depending on the select value.",
        "FUN_0805ffc4(chipIndex): a ~30-register bring-up sequence (covers 0x41,0x42,0x44,0x45,0x46,0x47,0x49 x4,0x4d,0x57,0x5c,0x5d,0x5e,0x5f,0x60,0x2a,0x30,0x19,0x59,0x29,0x56,0x3c) entirely parameterized by chipIndex and threaded into every single write -- i.e. 'initialize RF chip N' as a standalone routine, implying it's called once per physical chip.",
        "FUN_081158f4(param_1, param_2, param_3): selects between TWO PARALLEL state structures, `_DAT_2000c230` and `_DAT_2000c234` (4 bytes apart -- consistent with an array of 2 receiver-state structs), based on the same index that also becomes the chip-select passed to FUN_0810ea9c. Two live, independently-tracked receiver state structs alongside two independently-initialized RF chips is the signature of true simultaneous dual-receive hardware, not a TX/RX role split on one chip.",
        "Register writes to the SAME register number appear with the SAME set of values across BOTH chip-select values (e.g. reg 0x48 gets both 0x82E and 0x16E on cs=0 AND on cs=1; reg 0x3C gets both 0xE0 and 0xE1 on both), i.e. each chip independently gets configured for whatever band/mode is active on its own path -- consistent with two independent, symmetric RF chains rather than a fixed primary/secondary asymmetry."
      ],
      "conclusion": "The D890UV very likely has genuine dual-receive hardware: two identical (or near-identical) AT1846S-family transceivers, each on its own chip-select line, each independently initialized and reconfigured -- matching AnyTone's known flagship 'independently monitor two channels simultaneously' feature (marketed on models like the D878UV II PLUS / D578UV III PLUS). This is NOT a different peripheral (not an EEPROM, not the HR-C6000/baseband chip -- those are addressed through separate functions elsewhere) and NOT simply 'one chip, alternate register bank for TX vs RX'.",
      "not_verified": "Did not trace actual schematic pin numbers or confirm which physical GPIO pins these base addresses correspond to on real PCB silkscreen/datasheet -- this is a logical/structural conclusion from the code, not a hardware teardown."
    }
  ],
  "audio_ui_settings": {
    "note": "Pulled from the firmware's UTF-16LE UI string table (see anytone_d890uv_sms_findings.md for how that table was located). These are CONFIRMED to exist as user-facing menu items; their exact numeric ranges/defaults were not traced.",
    "gain_and_volume": ["AnaMic Level", "DigiMic Level", "Max Vol Level / Max VOL Set", "Ear Max Vol (separate headset volume cap)", "RX Gain", "BT Mic Gain", "BT Spk Gain"],
    "dsp_processing": ["RX NR Filter", "TX NR Filter", "BT NR", "VOL DRC", "RX DRC (+ On/Off)", "MIC AGC", "TX Pow AGC", "Compander", "Enhance Sound / EnhancedSound", "Mic Enhance", "SubSpk In TX"],
    "squelch": ["AM Sq Level", "Ana Sq Level", "Squelch mode", "per-mode SQ 1-5 calibration entries"]
  },
  "register_writes_observed": {
    "note": "Grouped by register number from the compiled firmware. Format: reg -> {value: [chip-select values seen with it]}. Many values are parameterized (param_1/param_2/param_3 threaded from callers) rather than fixed literals -- those are marked as such rather than invented.",
    "0x41_digital_voice_gain": {"D890UV": ["0x111A (gain=26, cs=?)", "0x711A (cs=param)"], "opengd77_reference": ["0x4122 (DMR)", "0x4431 (FM)"]},
    "0x44_rx_tx_af_gain": {"D890UV": ["0x1490 (vol1=4, vol2=9, cs=param)"], "opengd77_reference": ["0x07FF (100%)", "0x06CC (80%)"]},
    "0x58_filters": {"D890UV": ["0xF1F0", "0x01F0"], "opengd77_reference": ["0x9CDD (DMR, filters off)", "0xBC85 (FM, HPF/LPF on)"]},
    "0x59_deviation": {"D890UV": ["0x221C"], "opengd77_reference": ["0x0B90", "0x0BA0"]},
    "0x43_fm_deviation": {"D890UV": ["0x1F4F", "0x1A4A"], "opengd77_reference": ["0x0100", "0x00A9"]},
    "0x30_soft_reset_poweron": {"D890UV": ["0x5F6B", "0x5F75", "0x5F74", "0x5F64", "0x5F00 (cs=0)"], "opengd77_reference": ["0x0001 (soft reset)", "0x0004 (poweron)", "0x40A4/0x40A6/0x4006 (calibration enable/disable)"]},
    "interpretation": "D890UV's 0x41/0x44 values are noticeably LOWER than this repo's DM1701/UV380 values for what should be the equivalent 'digital voice gain' / 'internal AF volume' settings. This repo's AT1846S.c already has a comment noting 'the DM1701 seems to generally need higher AF gain than the UV380' -- the D890UV gap is consistent with that same pattern (different board/calibration needing different absolute gain to hit the same acoustic output), not necessarily a bug or a value to copy over. Treat as calibration context, not a drop-in fix."
  },
  "mcu_confirmation": {
    "id": "gpio-register-offsets-confirm-gd32f4",
    "confidence": "confirmed",
    "summary": "The GPIO bit-set/bit-clear helper (FUN_0804fbd8) writes offset +0x18 to set pins and offset +0x28 to clear pins from a GPIO port base address. Offset +0x28 as a dedicated 'bit clear' register (GPIO_BC) is GD32F4xx-specific -- plain STM32F4 GPIO has no register at that offset (it uses the upper half of BSRR at +0x18 for both set and clear instead). This independently confirms the D890UV's main MCU is a GD32F4xx part, not plain STM32F4, consistent with the vector-table/flash-size observations from the original firmware-structure analysis."
  },
  "not_investigated_further": [
    "FUN_080efd20 / other TX-queue sink functions (out of scope for this pass, see the SMS findings doc)",
    "Exact GPIO port/pin identity for the two chip-select lines (would need schematic or continuity-test confirmation)",
    "The HR-C6000-equivalent baseband/vocoder chip's own register interface (separate from this AT1846S-family audio/RF chip; not traced in this pass)",
    "Precise mapping of AnaMic Level/DigiMic Level/Max Vol Level menu items to specific register writes -- confirmed as menu strings, not traced to code"
  ]
}
```

## Why this matters for OpenGD77

Your `AT1846S.c` already carries hard-won, capture/testing-verified register values for the
DM1701/UV380/RT84 targets, with comments acknowledging some values were empirically tuned (the
"DM1701 needs higher AF gain" comment). The D890UV's real-firmware register values above are a
second independent data point for the *same chip family* on different silicon/board revisions --
useful as a sanity check (e.g. "is our 0x58 filter mask in a plausible range compared to a shipped
product") rather than as values to copy directly, since gain/filter registers are inherently
board-specific calibration, not a fixed spec.

The dual-chip finding is the more structurally interesting one: if you ever look at implementing
or debugging simultaneous dual-receive on hardware that has two AT1846S-family chips, this is what
that looks like at the register level on a shipping AnyTone product -- two independent chip-selects,
two independent full bring-up sequences, two independent receiver-state structs, and register
writes that are genuinely symmetric between the two rather than one "primary" and one
special-cased "secondary."
