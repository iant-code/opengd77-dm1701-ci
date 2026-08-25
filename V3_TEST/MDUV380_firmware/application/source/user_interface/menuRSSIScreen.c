/*
 * Copyright (C) 2019-2025 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 *
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
#include "user_interface/uiGlobals.h"
#include "functions/calibration.h"
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "hardware/radioHardwareInterface.h"


#if defined(PLATFORM_MD2017)
#define SECONDARY_DISPLAY_OFFSET  50
#endif


static menuStatus_t menuRSSIExitCode = MENU_STATUS_SUCCESS;
//static calibrationRSSIMeter_t rssiCalibration; // UNUSED
static void updateScreen(bool forceRedraw, bool firstRun);
static void handleEvent(uiEvent_t *ev);
static void updateVoicePrompts(bool flushIt, bool spellIt);

static int dBm[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static uint8_t rawSignal[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static uint8_t rawNoise[RADIO_DEVICE_MAX] =
{
		0
#if defined(PLATFORM_MD2017)
		, 0
#endif
};

static bool displayRawValues = false;

static const int barX = 9;

// S-meter layout: S0..S9 gets the first two-thirds of the bar width, +10/+20 over S9 gets the
// final third (previously S0..S9 took ~70% and everything above S9 was crammed into the last 30%
// as a single unlabelled dashed/filled region -- this keeps S0..S9 roughly where it was, but the
// former blank/dashed region now gets two actually-labelled graduations). Signals stronger than
// S9+20 just peg at full scale: this hardware can read up to S9+60, but resolving finer than +20
// on a ~150px display, with only a third of the width to do it in, isn't useful.
#define RSSI_METER_SPLIT_NUM   2
#define RSSI_METER_SPLIT_DEN   3

// Historical RSSI strip-chart, drawn below the meter -- one column per pixel, oldest sample
// scrolls off the left. At the screen's own ~200ms update cadence (RSSI_UPDATE_COUNTER_RELOAD)
// this holds roughly (RSSI_HISTORY_COLS * 0.2)s of history.
#define RSSI_HISTORY_HEIGHT       22
#define RSSI_HISTORY_TOP_OFFSET   (15 + FONT_SIZE_2_HEIGHT + 3) // below the graticule's number row, +3px gap
// 9 == barX, duplicated as a literal: barX is a runtime variable, not usable to size a file-scope
// array. The extra "- 1" (vs. the meter's own full DISPLAY_SIZE_X-9 width) leaves a 1px margin on
// each side for drawRssiHistoryFrame()'s border -- without it the content already touches the true
// right screen edge with zero margin, and a border drawn "just outside" that would land 1px past
// the last valid column.
#define RSSI_HISTORY_COLS         ((DISPLAY_SIZE_X - 9) - 1)

static uint8_t rssiHistory[RADIO_DEVICE_MAX][RSSI_HISTORY_COLS]; // stored as pre-scaled bar height (0..RSSI_HISTORY_HEIGHT), not raw dBm

// Maps a dBm value onto a 0..scale range using the same two-zone (S0..S9 / S9..S9+20, pegged)
// layout described above. Used both for the meter's own bar width and (with RSSI_HISTORY_HEIGHT
// as the scale) the history graph's bar heights, so the two views agree on what "how strong" looks
// like.
static int rssiDbmToScale(int dbm, int scale)
{
	int splitPos = ((scale * RSSI_METER_SPLIT_NUM) / RSSI_METER_SPLIT_DEN);

	if (dbm <= SMETER_S0)
	{
		return 0;
	}

	if (dbm <= SMETER_S9)
	{
		return (((dbm - SMETER_S0) * splitPos) / (SMETER_S9 - SMETER_S0));
	}

	if (dbm >= SMETER_S9_20)
	{
		return scale;
	}

	return (splitPos + (((dbm - SMETER_S9) * (scale - splitPos)) / (SMETER_S9_20 - SMETER_S9)));
}

menuStatus_t menuRSSIScreen(uiEvent_t *ev, bool isFirstRun)
{
	static uint32_t m = 0;

	if (isFirstRun)
	{
		//calibrationGetRSSIMeterParams(&rssiCalibration); // UNUSED
		menuDataGlobal.numItems = 0;
		displayClearBuf();
		menuDisplayTitle(currentLanguage->rssi);
		displayRenderRows(0, 2);

		updateScreen(true, true);
	}
	else
	{
		menuRSSIExitCode = MENU_STATUS_SUCCESS;
		if (ev->hasEvent)
		{
			handleEvent(ev);
		}

		if((ev->time - m) > RSSI_UPDATE_COUNTER_RELOAD)
		{
			m = ev->time;
			updateScreen(false, false);
		}
	}

	return menuRSSIExitCode;
}

// Returns S-Unit 0..9..10(S9+10dB)..15(S9+60)
static int32_t getSignalStrength(int dbm)
{
	for (int8_t i = 15; i >= 0; i--)
	{
		if (dbm >= DBM_LEVELS[i])
		{
			return i;
		}
	}

	return 0;
}

static void drawMeterGraticule(int16_t vOffset)
{
	int totalWidth = (DISPLAY_SIZE_X - barX);
	int splitPos = rssiDbmToScale(SMETER_S9, totalWidth); // pixel boundary between the S0..S9 and +10/+20 zones

	// Draw S-Meter outer frame
	displayDrawRect((barX - 2), (vOffset - 2), (DISPLAY_SIZE_X - (barX - 2)), (8 + 4), true);
	// Clear the right V line of the frame
	displayDrawFastVLine((DISPLAY_SIZE_X - 1), (vOffset - 1), (8 + 2), false);
	// Dash the top edge of the frame over the extended (+10/+20) zone, so it reads as visually
	// distinct from the plain S0..S9 zone even before the numbers are read.
	for (int16_t i = (barX + splitPos + 1); i < DISPLAY_SIZE_X; i += 4)
	{
		displayDrawFastHLine(i, (vOffset - 2), 2, true);
	}

	// Draw S, Numbers and ticks
	displayPrintAt(1, vOffset, "S", FONT_SIZE_1_BOLD);

	// S0..S9 -- the meter's first two-thirds.
	for (uint8_t i = 0; i <= 9; i++)
	{
		int dbm = (SMETER_S0 + (((SMETER_S9 - SMETER_S0) * i) / 9));
		// rssiDbmToScale() can return totalWidth itself (a full-scale peg) -- one past the last
		// valid on-screen column at barX+totalWidth-1, so the tick/text position has to clamp
		// separately from the bar-fill *width* uses of this same function (where returning the
		// full totalWidth is correct: it's a pixel count starting at barX, not an absolute offset).
		int xPos = CLAMP(rssiDbmToScale(dbm, totalWidth), 0, (totalWidth - 1));

		displayDrawFastVLine((barX + xPos), (vOffset + 8) + 2, ((i % 2) ? 3 : 1), true);

		if (i % 2)
		{
			char buf[2];
			int16_t textX = (int16_t)(((barX + xPos) - 2) - 1)/* FONT_2 H offset */;

			sprintf(buf, "%d", i);
			textX = (int16_t)CLAMP(textX, 0, (DISPLAY_SIZE_X - FONT_SIZE_2_HEIGHT)); // 1 char == FONT_SIZE_2_HEIGHT px wide (font_8x8)
			displayPrintAt(textX, vOffset + 15
#if defined(PLATFORM_RD5R)
					-1
#endif
					, buf, FONT_SIZE_2);
		}
	}

	// +10/+20 over S9 -- the meter's final third, pegged at +20 (see rssiDbmToScale()). Capped at
	// +20 rather than +30: with S0..S9 now at two-thirds width, only a third is left for the
	// extended zone, and three graduations there read as cramped -- two fits cleanly. Labelled "+"
	// and "++" (one/two steps past S9), not "+10"/"+20" -- those 3-character labels were wide enough
	// that +20's edge-clamping (see below) pushed it left into +10's label. The short "+"/"++" pair
	// is narrow enough that both fit in their natural positions with no clamp collision.
	static const int extLevels[2] = { SMETER_S9_10, SMETER_S9_20 };
	static const char *extLabels[2] = { "+", "++" };

	for (uint8_t i = 0; i < 2; i++)
	{
		int xPos = CLAMP(rssiDbmToScale(extLevels[i], totalWidth), 0, (totalWidth - 1));
		int labelWidthPx = ((int)strlen(extLabels[i]) * FONT_SIZE_2_HEIGHT);
		int16_t textX = (int16_t)((barX + xPos) - (labelWidthPx / 2));

		displayDrawFastVLine((barX + xPos), (vOffset + 8) + 2, 3, true);
		// ++ pegs right at the edge of the meter, where there isn't room for a label centred on its
		// tick -- clamp so the label always stays fully on screen instead of running off the right edge.
		textX = (int16_t)CLAMP(textX, 0, (DISPLAY_SIZE_X - labelWidthPx));
		displayPrintAt(textX, vOffset + 15
#if defined(PLATFORM_RD5R)
				-1
#endif
				, extLabels[i], FONT_SIZE_2);
	}
}

static void drawRssiHistoryFrame(int16_t topY)
{
	displayDrawRect((barX - 1), (topY - 1), (RSSI_HISTORY_COLS + 2), (RSSI_HISTORY_HEIGHT + 2), true);
}

// Pushes one new sample (scrolling the oldest one off the left) and redraws every column from the
// stored buffer. Cheap at this screen's ~200ms update rate (RSSI_HISTORY_COLS short fillRects).
static void updateRssiHistoryGraph(RadioDevice_t device, int16_t topY, int dbm)
{
	uint8_t newBarHeight = (uint8_t)rssiDbmToScale(dbm, RSSI_HISTORY_HEIGHT);

	memmove(&rssiHistory[device][0], &rssiHistory[device][1], (RSSI_HISTORY_COLS - 1));
	rssiHistory[device][RSSI_HISTORY_COLS - 1] = newBarHeight;

	// Foreground/background set once, then plain displayFillRect()'s own polarity (true=background,
	// false=foreground -- see menuGameBreakout.c/spectrumDrawMeter()) picks between them per call,
	// rather than re-applying the theme on every one of these ~150 columns.
	displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);

	for (int col = 0; col < RSSI_HISTORY_COLS; col++)
	{
		int16_t x = (int16_t)(barX + col);
		uint8_t h = rssiHistory[device][col];

		if (h < RSSI_HISTORY_HEIGHT)
		{
			displayFillRect(x, topY, 1, (int16_t)(RSSI_HISTORY_HEIGHT - h), true);
		}

		if (h > 0)
		{
			displayFillRect(x, (int16_t)(topY + (RSSI_HISTORY_HEIGHT - h)), 1, h, false);
		}
	}

	displayThemeResetToDefault();
}

static void updateScreen(bool forceRedraw, bool isFirstRun)
{
	char buffer[LOCATION_TEXT_BUFFER_SIZE];
	int barWidth;
	int rssi[RADIO_DEVICE_MAX];
	int16_t yValuePos, yBarPos;

	for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
	{
		rssi[device] = dBm[device] = trxGetRSSIdBm(device);
		rawSignal[device] = trxGetSignalRaw(device);
		rawNoise[device] = trxGetNoiseRaw(device);
	}

	if (isFirstRun && (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD))
	{
		voicePromptsInit();
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		voicePromptsAppendLanguageString(currentLanguage->rssi);
		voicePromptsAppendLanguageString(currentLanguage->menu);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		updateVoicePrompts(false, true);
	}

	if (forceRedraw)
	{
		yBarPos = DISPLAY_Y_POS_RSSI_BAR;

		displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
		// Clear whole drawing region
		displayFillRect(0, 14, DISPLAY_SIZE_X, DISPLAY_SIZE_Y - 14, true);

		if (isFirstRun)
		{
			memset(rssiHistory, 0, sizeof(rssiHistory)); // start each visit to this screen with an empty history graph
		}

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			drawMeterGraticule(yBarPos);
			drawRssiHistoryFrame((int16_t)(yBarPos + RSSI_HISTORY_TOP_OFFSET));
#if defined(PLATFORM_MD2017)
			yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
		}

		displayThemeResetToDefault();
	}
	else
	{
		yValuePos = DISPLAY_Y_POS_RSSI_VALUE;

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			// Clear dBm region value
			displayFillRect((displayRawValues ? 0 : ((DISPLAY_SIZE_X - (7 * 8)) >> 1)), yValuePos, (displayRawValues ? DISPLAY_SIZE_X : (7 * 8)), FONT_SIZE_3_HEIGHT, true);
#if defined(PLATFORM_MD2017)
			yValuePos += SECONDARY_DISPLAY_OFFSET;
#endif
		}
	}

	yValuePos = DISPLAY_Y_POS_RSSI_VALUE;
	yBarPos = DISPLAY_Y_POS_RSSI_BAR;

	for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
	{
		if (displayRawValues)
		{
			snprintf(buffer, LOCATION_TEXT_BUFFER_SIZE, "%d%s [%u %u]", dBm[device], "dBm", rawSignal[device], rawNoise[device]);
		}
		else
		{
			snprintf(buffer, SCREEN_LINE_BUFFER_SIZE, "%d%s", dBm[device], "dBm");
		}
		displayPrintCentered(yValuePos, buffer, FONT_SIZE_3);

#if 0 // DEBUG
		sprintf(buffer, "%d", currentRadioDevice->trxRxSignal);
		displayFillRect((DISPLAY_SIZE_X - (4 * 8)), yValuePos, (4 * 8), 8, true);
		ucPrintCore((DISPLAY_SIZE_X - ((strlen(buffer) + 1) * 8)), yValuePos, buffer, FONT_SIZE_2, TEXT_ALIGN_RIGHT, false);
#endif

		barWidth = rssiDbmToScale(rssi[device], (DISPLAY_SIZE_X - barX));
		barWidth = CLAMP(barWidth, 0, (DISPLAY_SIZE_X - barX));

		if (barWidth)
		{
			displayThemeApply(THEME_ITEM_FG_RSSI_BAR, THEME_ITEM_BG);
			displayFillRect(barX, yBarPos, barWidth, 8, false);
			displayThemeResetToDefault();
		}

		// Clear the end of the bar area, if needed
		if (barWidth < (DISPLAY_SIZE_X - barX))
		{
			displayFillRect(barX + barWidth, yBarPos, (DISPLAY_SIZE_X - barX) - barWidth, 8, true);
		}

#if defined(HAS_COLOURS)
		if (rssi[device] > SMETER_S9)
		{
			int xPos = rssiDbmToScale(SMETER_S9, (DISPLAY_SIZE_X - barX));

			if (barWidth > xPos)
			{
				displayThemeApply(THEME_ITEM_FG_RSSI_BAR_S9P, THEME_ITEM_BG);
				displayFillRect((barX + xPos), yBarPos, (barWidth - xPos), 8, false);
				displayThemeResetToDefault();
			}
		}
#endif

		if (forceRedraw == false)
		{
			updateRssiHistoryGraph(device, (int16_t)(yBarPos + RSSI_HISTORY_TOP_OFFSET), rssi[device]);
		}

#if defined(PLATFORM_MD2017)
		yValuePos += SECONDARY_DISPLAY_OFFSET;
		yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
	}

	if (forceRedraw || uiNotificationIsVisible())
	{
		displayRender();
	}
	else
	{
		int16_t yStartValuePos = DISPLAY_Y_POS_RSSI_VALUE;
		int16_t yStartBarPos = DISPLAY_Y_POS_RSSI_BAR;

		// Y end positions
		yValuePos = DISPLAY_Y_POS_RSSI_VALUE + FONT_SIZE_3_HEIGHT;
		yBarPos = DISPLAY_Y_POS_RSSI_BAR + 8;

#if defined(PLATFORM_RD5R)
#warning CHECK ME (end pos)
		displayRenderRows((DISPLAY_Y_POS_RSSI_VALUE / 8), (yValuePos / 8) + 1);
#else

		for(RadioDevice_t device = RADIO_DEVICE_PRIMARY; device < RADIO_DEVICE_MAX; device++)
		{
			displayRenderRows((yStartValuePos / 8), (yValuePos / 8) + 1);
			displayRenderRows((yStartBarPos / 8), (yBarPos / 8) + 1);
			displayRenderRows(((yStartBarPos + RSSI_HISTORY_TOP_OFFSET) / 8), (((yStartBarPos + RSSI_HISTORY_TOP_OFFSET + RSSI_HISTORY_HEIGHT) / 8) + 1));

#if defined(PLATFORM_MD2017)
			yStartValuePos += SECONDARY_DISPLAY_OFFSET;
			yStartBarPos += SECONDARY_DISPLAY_OFFSET;
			yValuePos += SECONDARY_DISPLAY_OFFSET;
			yBarPos += SECONDARY_DISPLAY_OFFSET;
#endif
		}
#endif
	}
}

static void handleEvent(uiEvent_t *ev)
{
	if ((ev->events & FUNCTION_EVENT) && (ev->function == FUNC_REDRAW))
	{
		updateScreen(true, false);
		return;
	}

	if (ev->events & BUTTON_EVENT)
	{
		bool wasPlaying = false;

		if (BUTTONCHECK_SHORTUP(ev, BUTTON_SK1) && (ev->keys.key == 0))
		{
			// Stop playback or update signal strength
			if ((wasPlaying = voicePromptsIsPlaying()) == false)
			{
				updateVoicePrompts(true, false);
			}
		}

		if (repeatVoicePromptOnSK1(ev))
		{
			if (wasPlaying && voicePromptsIsPlaying())
			{
				voicePromptsTerminate();
			}
			return;
		}
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN) || KEYCHECK_SHORTUP(ev->keys, KEY_RED))
	{
		menuSystemPopPreviousMenu();
	}
	else if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
	{
		displayRawValues = !displayRawValues;
		updateScreen(true, false);
	}
	else if (KEYCHECK_SHORTUP_NUMBER(ev->keys) && (BUTTONCHECK_DOWN(ev, BUTTON_SK2)))
	{
		saveQuickkeyMenuIndex(ev->keys.key, menuSystemGetCurrentMenuNumber(), 0, 0);
	}
}

static void updateVoicePrompts(bool flushIt, bool spellIt)
{
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_VOICE_THRESHOLD)
	{
		uint8_t S = getSignalStrength(dBm[RADIO_DEVICE_PRIMARY]);

		if (flushIt)
		{
			voicePromptsInit();
		}

		voicePromptsAppendPrompt(PROMPT_S);
		voicePromptsAppendPrompt(PROMPT_SILENCE);
		if (S > 9)
		{
			voicePromptsAppendPrompt(PROMPT_9);
			voicePromptsAppendPrompt(PROMPT_PLUS);
			voicePromptsAppendInteger(10 * ((S - 10) + 1));
		}
		else
		{
			voicePromptsAppendPrompt(PROMPT_0 + S);
		}

		if (spellIt)
		{
			promptsPlayNotAfterTx();
		}
	}
}
