/*
 * Copyright (C) 2026
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <string.h>
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "functions/trx.h"
#include "functions/rxPowerSaving.h"
#include "functions/ticks.h"
#include "hardware/radioHardwareInterface.h"
#include "hardware/AT1846S.h"

// Swept-RSSI spectrum analyser. See DOCUMENTATIE/spectrum_analyser_plan.md for the full design
// and the hardware feasibility research behind it -- short version: the AT1846S has no FFT/
// panadapter hardware, only a single wideband RSSI reading at whatever frequency it's currently
// tuned to. So "spectrum" here means physically retuning across the span and sampling RSSI per
// bin (one screen column each), which is why max-hold (not the live trace) is the feature that
// actually catches short transmissions -- a full sweep takes on the order of a second, so the
// live trace alone would miss most of them.

// ---- Screen layout (160x128) ----
#define SPECTRUM_NUM_BINS       DISPLAY_SIZE_X // one bin per column, simplest possible mapping
#define SPECTRUM_TRACE_TOP      14             // title occupies rows 0-13, same convention as menuRSSIScreen.c
#define SPECTRUM_TRACE_HEIGHT   70
#define SPECTRUM_TRACE_BOTTOM   (SPECTRUM_TRACE_TOP + SPECTRUM_TRACE_HEIGHT)
#define SPECTRUM_READOUT_Y      86
#define SPECTRUM_AXIS_Y         108

// ---- Sweep engine pacing ----
#define SPECTRUM_BINS_PER_TICK   4  // bounded per-tick work so the UI/key handling stays responsive
#define SPECTRUM_SETTLE_DELAY_MS 2  // time between retune and RSSI read -- see plan doc, unverified on real hardware
#define SPECTRUM_MAXHOLD_DECAY_STEP 3 // per completed sweep, slow-fade mode only

// Trace vertical scale: a 60dB window from the noise floor, NOT a fixed raw-register full-scale
// (an earlier version used a guessed raw-value max, which made real signals barely nudge the
// bar; a 20dB window was tried next and turned out too zoomed-in). The floor is measured, not
// guessed: the minimum dBm seen across each just-completed sweep (see spectrumSweepStep()), so it
// self-calibrates to whatever the actual ambient noise is on this radio/location/band instead of
// relying on another untested constant.
#define SPECTRUM_DISPLAY_RANGE_DB 60

// ---- Band centres, in this firmware's 10Hz frequency units (matches RADIO_HARDWARE_FREQUENCY_BANDS in trx.c) ----
#define SPECTRUM_BAND_2M_CENTRE_10HZ   14500000UL // 145.00000 MHz
#define SPECTRUM_BAND_70CM_CENTRE_10HZ 44300000UL // 443.00000 MHz

typedef enum
{
	SPECTRUM_BAND_2M = 0,
	SPECTRUM_BAND_70CM
} spectrumBand_t;

typedef enum
{
	SPECTRUM_MAXHOLD_SLOWFADE = 0,
	SPECTRUM_MAXHOLD_PERSISTENT
} spectrumMaxHoldMode_t;

// Span options, in 10Hz units (0.5 / 1 / 2 / 5 / 10 MHz).
static const uint32_t SPECTRUM_SPAN_OPTIONS_10HZ[] = { 50000UL, 100000UL, 200000UL, 500000UL, 1000000UL };
#define SPECTRUM_NUM_SPAN_OPTIONS (sizeof(SPECTRUM_SPAN_OPTIONS_10HZ) / sizeof(SPECTRUM_SPAN_OPTIONS_10HZ[0]))
#define SPECTRUM_DEFAULT_SPAN_INDEX 2 // 2 MHz

// Full-sweep refresh cadence options, in ms (1 / 3 / 6 / 10 / 30 s).
static const uint32_t SPECTRUM_REFRESH_OPTIONS_MS[] = { 1000UL, 3000UL, 6000UL, 10000UL, 30000UL };
#define SPECTRUM_NUM_REFRESH_OPTIONS (sizeof(SPECTRUM_REFRESH_OPTIONS_MS) / sizeof(SPECTRUM_REFRESH_OPTIONS_MS[0]))
#define SPECTRUM_DEFAULT_REFRESH_INDEX 0 // 1 s

#define SPECTRUM_SETTINGS_NUM_ROWS 3
enum { SPECTRUM_SETTINGS_ROW_BAND = 0, SPECTRUM_SETTINGS_ROW_SPAN, SPECTRUM_SETTINGS_ROW_REFRESH };

// Keypad frequency entry: 3 MHz-integer digits + 4 fractional digits (0.0001 MHz = 100Hz
// resolution -- finer than any realistic channel step, and exact under the 10Hz internal unit
// with no rounding). '-' is the not-yet-typed placeholder, same convention uiVFOMode.c's own
// FreqEnter uses.
#define SPECTRUM_FREQ_ENTRY_DIGITS 7

static struct
{
	spectrumBand_t band;
	uint8_t spanIndex;
	uint8_t refreshIndex;
	spectrumMaxHoldMode_t maxHoldMode;

	uint32_t centreFreq;  // 10Hz units -- from the Band preset, or overridden by keypad entry
	uint32_t startFreq;   // 10Hz units, left edge of the sweep
	uint32_t binFreqStep; // 10Hz units per bin

	bool sweepRunning;    // GREEN (short, no pending entry) toggles this
	uint16_t sweepIndex;  // next bin to sample (0..SPECTRUM_NUM_BINS-1)
	bool sweepIdle;       // true while waiting out the Refresh interval between sweeps
	ticksTimer_t nextSweepTimer;

	uint8_t current[SPECTRUM_NUM_BINS];
	uint8_t maxHold[SPECTRUM_NUM_BINS];

	uint16_t cursorBin;
	int displayFloorDbm; // trace Y-axis floor -- see SPECTRUM_DISPLAY_RANGE_DB

	char freqEntryDigits[SPECTRUM_FREQ_ENTRY_DIGITS];
	uint8_t freqEntryIndex; // 0 = no entry in progress

	bool settingsOpen;
	uint8_t settingsRow;

	uint32_t savedRxFreq;
	uint32_t savedTxFreq;
} spectrum;

static menuStatus_t spectrumExitCode = MENU_STATUS_SUCCESS;

// Sets spectrum.centreFreq from the Band preset -- called when Band is cycled in settings, or on
// initial entry. NOT called when a keypad-entered frequency is committed, since that sets
// centreFreq directly to whatever was typed (see spectrumHandleNormalEvent()).
static void spectrumSetCentreFreqFromBandPreset(void)
{
	spectrum.centreFreq = (spectrum.band == SPECTRUM_BAND_70CM) ? SPECTRUM_BAND_70CM_CENTRE_10HZ : SPECTRUM_BAND_2M_CENTRE_10HZ;
}

// Mirrors trxGetRSSIdBm()'s per-band fixed-point conversion (trx.c) -- duplicated locally rather
// than calling it directly, since that function reads the *live* currentRadioDevice state, and
// during a sweep we want the reading for whichever band this sweep is fixed to, decoupled from
// whatever the last retune happened to leave in the global device state.
static int spectrumRawToDbm(uint8_t raw)
{
	if (spectrum.band == SPECTRUM_BAND_70CM)
	{
		return (-151 + raw);
	}

	return (-164 + (((int)raw * 32) / 27));
}

static void spectrumRecomputeSweepGeometry(void)
{
	uint32_t span = SPECTRUM_SPAN_OPTIONS_10HZ[spectrum.spanIndex];

	spectrum.startFreq = spectrum.centreFreq - (span / 2U);
	spectrum.binFreqStep = span / SPECTRUM_NUM_BINS;

	// Bin geometry just changed -- old current/maxHold samples no longer correspond to real
	// frequencies, and a stale cursor position could point outside the new span. Reset all of it
	// and restart the sweep from bin 0 rather than leaving misleading data on screen.
	memset(spectrum.current, 0, sizeof(spectrum.current));
	memset(spectrum.maxHold, 0, sizeof(spectrum.maxHold));
	spectrum.sweepIndex = 0U;
	spectrum.sweepIdle = false;
	if (spectrum.cursorBin >= SPECTRUM_NUM_BINS)
	{
		spectrum.cursorBin = SPECTRUM_NUM_BINS / 2U;
	}

	// Band may have just changed too (different dBm formula), and the old floor is meaningless
	// against a blanked trace anyway -- reset to the deepest possible reading (raw=0) until the
	// first sweep completes and measures the real one, see spectrumSweepStep().
	spectrum.displayFloorDbm = spectrumRawToDbm(0);
}

static uint32_t spectrumBinFrequency(uint16_t bin)
{
	return (spectrum.startFreq + ((uint32_t)bin * spectrum.binFreqStep));
}

static void spectrumFormatFreqMHz(uint32_t freq10Hz, char *buf, size_t bufSize)
{
	snprintf(buf, bufSize, "%lu.%05lu", (unsigned long)(freq10Hz / 100000UL), (unsigned long)(freq10Hz % 100000UL));
}

static void spectrumFreqEntryReset(void)
{
	memset(spectrum.freqEntryDigits, '-', SPECTRUM_FREQ_ENTRY_DIGITS);
	spectrum.freqEntryIndex = 0U;
}

// Parses the typed digits (any not-yet-typed '-' placeholders pad as '0') into a 10Hz-unit
// frequency: 3 MHz-integer digits + 4 fractional digits (0.0001 MHz = 100Hz, so *10 lands exactly
// on this firmware's 10Hz internal unit -- see the SPECTRUM_FREQ_ENTRY_DIGITS comment).
static uint32_t spectrumFreqEntryParse(void)
{
	uint32_t integerMHz = 0U;
	uint32_t fractionSteps = 0U; // units of 0.0001 MHz

	for (uint8_t i = 0U; i < 3U; i++)
	{
		char c = spectrum.freqEntryDigits[i];
		integerMHz = (integerMHz * 10U) + (uint32_t)(((c >= '0') && (c <= '9')) ? (c - '0') : 0);
	}

	for (uint8_t i = 3U; i < SPECTRUM_FREQ_ENTRY_DIGITS; i++)
	{
		char c = spectrum.freqEntryDigits[i];
		fractionSteps = (fractionSteps * 10U) + (uint32_t)(((c >= '0') && (c <= '9')) ? (c - '0') : 0);
	}

	return ((integerMHz * 100000UL) + (fractionSteps * 10UL));
}

// One bounded slice of sweep work per call -- retunes and samples a handful of bins, then
// returns, so the caller (menuSpectrum()'s tick handler) never blocks the UI for a whole sweep.
static void spectrumSweepStep(void)
{
	if (spectrum.sweepIdle)
	{
		if (ticksTimerHasExpired(&spectrum.nextSweepTimer))
		{
			spectrum.sweepIdle = false;
			spectrum.sweepIndex = 0U;
		}
		else
		{
			return;
		}
	}

	for (uint8_t i = 0U; (i < SPECTRUM_BINS_PER_TICK) && (spectrum.sweepIndex < SPECTRUM_NUM_BINS); i++)
	{
		uint32_t binFreq = spectrumBinFrequency(spectrum.sweepIndex);
		uint8_t val1 = 0U;
		uint8_t val2 = 0U;

		radioSetFrequency(binFreq, false);
		osDelay(SPECTRUM_SETTLE_DELAY_MS);

		if (radioReadReg2byte(0x1bU, &val1, &val2))
		{
			spectrum.current[spectrum.sweepIndex] = val1;

			if (val1 > spectrum.maxHold[spectrum.sweepIndex])
			{
				spectrum.maxHold[spectrum.sweepIndex] = val1;
			}
		}

		spectrum.sweepIndex++;
	}

	if (spectrum.sweepIndex >= SPECTRUM_NUM_BINS)
	{
		// Re-measure the display floor from this sweep's own minimum -- see
		// SPECTRUM_DISPLAY_RANGE_DB. Uses current[] (this sweep's fresh readings), not maxHold[]
		// (which can only ever be flat-out higher, never a valid "ambient" estimate).
		int sweepMinDbm = spectrumRawToDbm(spectrum.current[0]);

		for (uint16_t i = 1U; i < SPECTRUM_NUM_BINS; i++)
		{
			int binDbm = spectrumRawToDbm(spectrum.current[i]);

			if (binDbm < sweepMinDbm)
			{
				sweepMinDbm = binDbm;
			}
		}

		spectrum.displayFloorDbm = sweepMinDbm;

		if (spectrum.maxHoldMode == SPECTRUM_MAXHOLD_SLOWFADE)
		{
			for (uint16_t i = 0U; i < SPECTRUM_NUM_BINS; i++)
			{
				spectrum.maxHold[i] = (spectrum.maxHold[i] > SPECTRUM_MAXHOLD_DECAY_STEP) ? (spectrum.maxHold[i] - SPECTRUM_MAXHOLD_DECAY_STEP) : 0U;
			}
		}

		spectrum.sweepIdle = true;
		ticksTimerStart(&spectrum.nextSweepTimer, SPECTRUM_REFRESH_OPTIONS_MS[spectrum.refreshIndex]);
	}
}

static void spectrumDrawTrace(void)
{
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayFillRect(0, SPECTRUM_TRACE_TOP, DISPLAY_SIZE_X, SPECTRUM_TRACE_HEIGHT, true);
	displayThemeResetToDefault();

	// SPECTRUM_DISPLAY_RANGE_DB window from the measured noise floor. A signal at or below the
	// floor draws as flat/zero; one at/above floor+range fills the full trace height.
	for (uint16_t bin = 0U; bin < SPECTRUM_NUM_BINS; bin++)
	{
		int liveDbm = spectrumRawToDbm(spectrum.current[bin]);
		int holdDbm = spectrumRawToDbm(spectrum.maxHold[bin]);

		int liveHeight = ((liveDbm - spectrum.displayFloorDbm) * SPECTRUM_TRACE_HEIGHT) / SPECTRUM_DISPLAY_RANGE_DB;
		int holdHeight = ((holdDbm - spectrum.displayFloorDbm) * SPECTRUM_TRACE_HEIGHT) / SPECTRUM_DISPLAY_RANGE_DB;

		liveHeight = CLAMP(liveHeight, 0, SPECTRUM_TRACE_HEIGHT);
		holdHeight = CLAMP(holdHeight, 0, SPECTRUM_TRACE_HEIGHT);

		if (liveHeight > 0)
		{
			// displayDrawFastVLine/displaySetPixel follow the "normal" isInverted convention
			// (true=foreground, false=background) -- the OPPOSITE of displayFillRect() called
			// directly, which is true=background. See DOCUMENTATIE/how_to_write_games_and_sprites.md.
			displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
			displayDrawFastVLine((int16_t)bin, (int16_t)(SPECTRUM_TRACE_BOTTOM - liveHeight), (int16_t)liveHeight, true);
			displayThemeResetToDefault();
		}

		// Max-hold cap: a single bright pixel above the live trace at the peak height, using the
		// existing ">S9" theme colour as the "noticeable" contrast colour the plan calls for.
		if (holdHeight > 0)
		{
			displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
			displaySetPixel((int16_t)bin, (int16_t)(SPECTRUM_TRACE_BOTTOM - holdHeight), true);
			displayThemeResetToDefault();
		}
	}

	// Cursor: full-height vertical marker.
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayDrawFastVLine((int16_t)spectrum.cursorBin, SPECTRUM_TRACE_TOP, SPECTRUM_TRACE_HEIGHT, true);
	displayThemeResetToDefault();
}

static void spectrumDrawReadout(void)
{
	char freqBuf[16];
	char lineBuf[SCREEN_LINE_BUFFER_SIZE];

	displayFillRect(0, SPECTRUM_READOUT_Y, DISPLAY_SIZE_X, (FONT_SIZE_2_HEIGHT * 2), true);

	if (spectrum.freqEntryIndex > 0U)
	{
		// Entry in progress -- show the digits typed so far, '-' placeholders for the rest,
		// GREEN commits + starts sweeping there, RED abandons it (and exits, same as always).
		snprintf(lineBuf, SCREEN_LINE_BUFFER_SIZE, "%c%c%c.%c%c%c%c MHz?",
			spectrum.freqEntryDigits[0], spectrum.freqEntryDigits[1], spectrum.freqEntryDigits[2],
			spectrum.freqEntryDigits[3], spectrum.freqEntryDigits[4], spectrum.freqEntryDigits[5], spectrum.freqEntryDigits[6]);
		displayPrintCentered(SPECTRUM_READOUT_Y, lineBuf, FONT_SIZE_2);
		displayPrintCentered(SPECTRUM_READOUT_Y + FONT_SIZE_2_HEIGHT, "GREEN=go RED=cancel", FONT_SIZE_1);
		return;
	}

	uint32_t cursorFreq = spectrumBinFrequency(spectrum.cursorBin);
	int liveDbm = spectrumRawToDbm(spectrum.current[spectrum.cursorBin]);
	int holdDbm = spectrumRawToDbm(spectrum.maxHold[spectrum.cursorBin]);

	spectrumFormatFreqMHz(cursorFreq, freqBuf, sizeof(freqBuf));
	snprintf(lineBuf, SCREEN_LINE_BUFFER_SIZE, "%s MHz", freqBuf);
	displayPrintCentered(SPECTRUM_READOUT_Y, lineBuf, FONT_SIZE_2);

	snprintf(lineBuf, SCREEN_LINE_BUFFER_SIZE, "%ddBm Mx%ddBm", liveDbm, holdDbm);
	displayPrintCentered(SPECTRUM_READOUT_Y + FONT_SIZE_2_HEIGHT, lineBuf, FONT_SIZE_1);
}

static void spectrumDrawAxis(void)
{
	char lowBuf[16];
	char highBuf[16];
	char lineBuf[SCREEN_LINE_BUFFER_SIZE];

	displayFillRect(0, SPECTRUM_AXIS_Y, DISPLAY_SIZE_X, FONT_SIZE_1_HEIGHT, true);

	spectrumFormatFreqMHz(spectrum.startFreq, lowBuf, sizeof(lowBuf));
	spectrumFormatFreqMHz(spectrumBinFrequency(SPECTRUM_NUM_BINS - 1U), highBuf, sizeof(highBuf));

	displayPrintCore(0, SPECTRUM_AXIS_Y, lowBuf, FONT_SIZE_1, TEXT_ALIGN_LEFT, false);
	displayPrintCore(DISPLAY_SIZE_X, SPECTRUM_AXIS_Y, highBuf, FONT_SIZE_1, TEXT_ALIGN_RIGHT, false);

	snprintf(lineBuf, SCREEN_LINE_BUFFER_SIZE, "%s %s",
		(spectrum.sweepRunning ? "RUN" : "STOP"),
		(spectrum.maxHoldMode == SPECTRUM_MAXHOLD_PERSISTENT) ? "Mx:hold" : "Mx:fade");
	displayPrintCore(DISPLAY_SIZE_X / 2, SPECTRUM_AXIS_Y, lineBuf, FONT_SIZE_1, TEXT_ALIGN_CENTER, false);
}

// Prints one settings row, either as normal text or, when selected, as true inverse video: the
// row's box is filled in foreground colour first (displayFillRect's isInverted=false -- see the
// polarity note in spectrumDrawTrace()), then the text is printed with isInverted=true so its
// glyph pixels come out in background colour on top of that fill. (Passing isInverted=true to
// displayPrintCore alone, without the fill, would draw background-on-background -- invisible --
// since printCore's isInverted only recolours the glyph pixels, it isn't a self-contained
// inverse-video box.)
static void spectrumDrawSettingsRow(int16_t y, int16_t rowHeight, const char *text, bool selected)
{
	if (selected)
	{
		displayFillRect(10, y, (DISPLAY_SIZE_X - 20), rowHeight, false);
	}

	displayPrintCore(12, (y + ((rowHeight - FONT_SIZE_2_HEIGHT) / 2)), text, FONT_SIZE_2, TEXT_ALIGN_LEFT, selected);
}

static void spectrumDrawSettingsOverlay(void)
{
	char lineBuf[SCREEN_LINE_BUFFER_SIZE];
	int16_t y = SPECTRUM_TRACE_TOP;
	const int16_t rowHeight = 16;
	const int16_t boxHeight = (rowHeight * SPECTRUM_SETTINGS_NUM_ROWS) + 4;

	// Box background + border both use the "normal" convention (true=foreground) here, unlike the
	// direct displayFillRect() calls elsewhere in this file that clear to background (true) --
	// see the polarity note in spectrumDrawTrace().
	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayFillRect(8, y, (DISPLAY_SIZE_X - 16), boxHeight, true);
	displayDrawRect(8, y, (DISPLAY_SIZE_X - 16), boxHeight, true);
	displayThemeResetToDefault();

	y += 2;

	snprintf(lineBuf, SCREEN_LINE_BUFFER_SIZE, "Band: %s", (spectrum.band == SPECTRUM_BAND_70CM) ? "70cm" : "2m");
	spectrumDrawSettingsRow(y, rowHeight, lineBuf, (spectrum.settingsRow == SPECTRUM_SETTINGS_ROW_BAND));
	y += rowHeight;

	snprintf(lineBuf, SCREEN_LINE_BUFFER_SIZE, "Span: %lu0kHz", (unsigned long)(SPECTRUM_SPAN_OPTIONS_10HZ[spectrum.spanIndex] / 10UL));
	spectrumDrawSettingsRow(y, rowHeight, lineBuf, (spectrum.settingsRow == SPECTRUM_SETTINGS_ROW_SPAN));
	y += rowHeight;

	snprintf(lineBuf, SCREEN_LINE_BUFFER_SIZE, "Refresh: %lus", (unsigned long)(SPECTRUM_REFRESH_OPTIONS_MS[spectrum.refreshIndex] / 1000UL));
	spectrumDrawSettingsRow(y, rowHeight, lineBuf, (spectrum.settingsRow == SPECTRUM_SETTINGS_ROW_REFRESH));
}

static void spectrumUpdateScreen(bool isFirstRun)
{
	if (isFirstRun)
	{
		displayClearBuf();
		menuDisplayTitle("Spectrum");
		displayRenderRows(0, 2);
	}

	spectrumDrawTrace();

	if (spectrum.settingsOpen)
	{
		spectrumDrawSettingsOverlay();
	}
	else
	{
		spectrumDrawReadout();
		spectrumDrawAxis();
	}

	displayRenderRows((SPECTRUM_TRACE_TOP / 8), (DISPLAY_SIZE_Y / 8) + 1);
}

static void spectrumSettingsApplyChange(void)
{
	// Band or Span change invalidates bin geometry; Refresh only affects the idle gap between
	// sweeps, so it doesn't need a geometry reset.
	spectrumRecomputeSweepGeometry();
}

static void spectrumHandleSettingsEvent(uiEvent_t *ev)
{
	if (KEYCHECK_SHORTUP(ev->keys, KEY_UP))
	{
		spectrum.settingsRow = (uint8_t)((spectrum.settingsRow + SPECTRUM_SETTINGS_NUM_ROWS - 1U) % SPECTRUM_SETTINGS_NUM_ROWS);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
	{
		spectrum.settingsRow = (uint8_t)((spectrum.settingsRow + 1U) % SPECTRUM_SETTINGS_NUM_ROWS);
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT) || KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		bool increase = KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT);

		switch (spectrum.settingsRow)
		{
			case SPECTRUM_SETTINGS_ROW_BAND:
				spectrum.band = (spectrum.band == SPECTRUM_BAND_2M) ? SPECTRUM_BAND_70CM : SPECTRUM_BAND_2M;
				spectrumSetCentreFreqFromBandPreset();
				break;

			case SPECTRUM_SETTINGS_ROW_SPAN:
				if (increase && (spectrum.spanIndex < (SPECTRUM_NUM_SPAN_OPTIONS - 1U)))
				{
					spectrum.spanIndex++;
				}
				else if (!increase && (spectrum.spanIndex > 0U))
				{
					spectrum.spanIndex--;
				}
				break;

			case SPECTRUM_SETTINGS_ROW_REFRESH:
				if (increase && (spectrum.refreshIndex < (SPECTRUM_NUM_REFRESH_OPTIONS - 1U)))
				{
					spectrum.refreshIndex++;
				}
				else if (!increase && (spectrum.refreshIndex > 0U))
				{
					spectrum.refreshIndex--;
				}
				break;

			default:
				break;
		}

		spectrumSettingsApplyChange();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		spectrum.settingsOpen = false;
	}

	spectrumUpdateScreen(false);
}

static void spectrumHandleNormalEvent(uiEvent_t *ev)
{
	if (KEYCHECK_SHORTUP(ev->keys, KEY_UP))
	{
		if (spectrum.cursorBin < (SPECTRUM_NUM_BINS - 1U))
		{
			spectrum.cursorBin++;
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
	{
		if (spectrum.cursorBin > 0U)
		{
			spectrum.cursorBin--;
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
	{
		spectrum.cursorBin = (uint16_t)MIN(spectrum.cursorBin + 10U, (SPECTRUM_NUM_BINS - 1U));
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
	{
		spectrum.cursorBin = (uint16_t)((spectrum.cursorBin > 10U) ? (spectrum.cursorBin - 10U) : 0U);
	}
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_GREEN))
	{
		spectrum.settingsOpen = true;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		if (spectrum.freqEntryIndex > 0U)
		{
			// Keypad entry pending: commit it as the new centre frequency and start sweeping
			// there. Rejects frequencies the hardware can't actually tune to (out of range for
			// both the VHF/220/UHF bands) rather than silently sweeping somewhere meaningless.
			uint32_t enteredFreq = spectrumFreqEntryParse();

			if (trxGetBandFromFrequency(enteredFreq) != FREQUENCY_OUT_OF_BAND)
			{
				spectrum.band = (trxGetBandFromFrequency(enteredFreq) == RADIO_BAND_UHF) ? SPECTRUM_BAND_70CM : SPECTRUM_BAND_2M;
				spectrum.centreFreq = enteredFreq;
				spectrumRecomputeSweepGeometry();
				spectrum.sweepRunning = true;
			}

			spectrumFreqEntryReset();
		}
		else
		{
			// No pending entry: GREEN is a plain start/stop toggle -- RED (below) is the only way
			// to leave this screen, see DOCUMENTATIE/spectrum_analyser_plan.md.
			spectrum.sweepRunning = !spectrum.sweepRunning;
		}
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		// Always exits, restoring the original frequency -- regardless of running/stopped state
		// or a pending keypad entry (which is simply abandoned).
		trxSetFrequency(spectrum.savedRxFreq, spectrum.savedTxFreq, DMR_MODE_AUTO);
		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH))
	{
		// Tune-to-cursor: simplex assumption (TX follows RX to the cursor frequency), since this
		// is a "find a signal, then go listen/talk on it" action with no repeater-offset context
		// to infer -- see DOCUMENTATIE/spectrum_analyser_plan.md. Moved off GREEN once GREEN
		// became the start/stop toggle.
		uint32_t cursorFreq = spectrumBinFrequency(spectrum.cursorBin);

		trxSetFrequency(cursorFreq, cursorFreq, DMR_MODE_AUTO);
		menuSystemPopPreviousMenu();
		return;
	}
	else if (KEYCHECK_LONGDOWN(ev->keys, KEY_STAR))
	{
		memset(spectrum.maxHold, 0, sizeof(spectrum.maxHold));
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		spectrum.maxHoldMode = (spectrum.maxHoldMode == SPECTRUM_MAXHOLD_SLOWFADE) ? SPECTRUM_MAXHOLD_PERSISTENT : SPECTRUM_MAXHOLD_SLOWFADE;
	}
	else if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
	{
		// Keypad frequency entry -- see the GREEN handling above for where this commits.
		if (spectrum.freqEntryIndex < SPECTRUM_FREQ_ENTRY_DIGITS)
		{
			spectrum.freqEntryDigits[spectrum.freqEntryIndex] = (char)ev->keys.key;
			spectrum.freqEntryIndex++;
		}
	}

	spectrumUpdateScreen(false);
}

static void spectrumHandleEvent(uiEvent_t *ev)
{
	if (!ev->hasEvent)
	{
		return;
	}

	if (spectrum.settingsOpen)
	{
		spectrumHandleSettingsEvent(ev);
	}
	else
	{
		spectrumHandleNormalEvent(ev);
	}
}

menuStatus_t menuSpectrum(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;

		memset(&spectrum, 0, sizeof(spectrum));
		spectrum.band = SPECTRUM_BAND_2M;
		spectrum.spanIndex = SPECTRUM_DEFAULT_SPAN_INDEX;
		spectrum.refreshIndex = SPECTRUM_DEFAULT_REFRESH_INDEX;
		spectrum.maxHoldMode = SPECTRUM_MAXHOLD_SLOWFADE;
		spectrum.cursorBin = SPECTRUM_NUM_BINS / 2U;
		spectrum.sweepRunning = true; // sweeping starts immediately on entry; GREEN can pause it

		spectrum.savedRxFreq = currentRadioDevice->currentRxFrequency;
		spectrum.savedTxFreq = currentRadioDevice->currentTxFrequency;

		spectrumSetCentreFreqFromBandPreset();
		spectrumRecomputeSweepGeometry();
		spectrumFreqEntryReset();

		// Continuous RX for the duration of the sweep -- mirrors trxSetFrequency()'s own guard,
		// avoids the receiver duty-cycling away mid-sweep. One heavy trxSetFrequency() call here
		// (not per-bin -- see spectrumSweepStep()) cleanly detunes/terminates digital mode if the
		// radio was on a DMR channel, matching the same call used to restore on exit.
		rxPowerSavingSetState(ECOPHASE_POWERSAVE_INACTIVE);
		trxSetFrequency(spectrum.startFreq, spectrum.startFreq, DMR_MODE_AUTO);

		spectrumUpdateScreen(true);
	}
	else
	{
		spectrumExitCode = MENU_STATUS_SUCCESS;

		spectrumHandleEvent(ev);

		if (!spectrum.settingsOpen)
		{
			if (spectrum.sweepRunning && (spectrum.freqEntryIndex == 0U))
			{
				spectrumSweepStep();
			}

			spectrumUpdateScreen(false);
		}
	}

	return spectrumExitCode;
}
