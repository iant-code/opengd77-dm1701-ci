/*
 * Copyright (C) 2025 OpenGD77 contributors
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED.
 */
#include "functions/dmrDisconnect.h"
#include "functions/trx.h"
#include "functions/ticks.h"
#include "functions/sound.h"
#include "hardware/HR-C6000.h"
#include "io/buttons.h"
#include "user_interface/menuSystem.h"

// BrandMeister "disconnect / unlink all" group ID. Sending a group call to it drops the hotspot's
// dynamic link to whatever talkgroup it was following.
#define DMR_DISCONNECT_TG_ID          4000U

// How long to keep PTT injected once we fire. A group call only needs its voice LC header (with
// dst = 4000) to reach the network for the disconnect to register, so this is deliberately short --
// just long enough for the header plus a few frames to go out, not a real "over".
#define DMR_DISCONNECT_TX_HOLD_MS     700U

// The channel must read continuously clear for this long before we fire, so we don't jump into a
// micro-gap in the middle of a talk-burst and collide with the next over. Long enough to be a real
// inter-over gap, short enough to still catch the brief gaps on a busy TG.
#define DMR_DISCONNECT_GAP_GUARD_MS   300U

// If no usable gap appears within this window, give up rather than stay armed forever.
#define DMR_DISCONNECT_ARM_TIMEOUT_MS 30000U

typedef enum
{
	DISCONNECT_IDLE = 0,
	DISCONNECT_ARMED,        // waiting for a clear gap to fire in
	DISCONNECT_TX_KEYING,    // gap found: PTT injected, transmitting to TG 4000
	DISCONNECT_TX_RELEASING, // PTT released, waiting for the TX chain to wind down before restoring
} disconnectState_t;

static disconnectState_t state = DISCONNECT_IDLE;
static uint32_t armStartMs = 0;
static uint32_t gapClearSinceMs = 0;
static bool     gapTiming = false;
static uint32_t txStartMs = 0;
static uint32_t savedTgOrPcId = 0;

static void notify(const char *msg, uint32_t ms)
{
	uiNotificationShow(NOTIFICATION_TYPE_MESSAGE, NOTIFICATION_ID_USER, ms, msg, true);
}

// "Nothing is happening on the channel right now" -- no DMR slot activity, no carrier, no RX audio,
// and we're not already transmitting or holding PTT. This is the gap the disconnect has to slip
// into (see the header for why it can't just transmit on demand).
static bool channelIsClear(uint32_t buttons)
{
	return ((trxGetMode() == RADIO_MODE_DIGITAL) &&
			(slotState == DMR_STATE_IDLE) &&
			(trxTransmissionEnabled == false) &&
			((buttons & BUTTON_PTT) == 0) &&
			(trxCarrierDetected(RADIO_DEVICE_PRIMARY) == false) &&
			((audioAmpGetStatus() & AUDIO_AMP_CHANNEL_RF) == 0));
}

void dmrDisconnectArm(void)
{
	if (trxGetMode() != RADIO_MODE_DIGITAL)
	{
		notify("DMR only", 2000);
		return;
	}

	// Second press while armed/running acts as a cancel.
	if (state != DISCONNECT_IDLE)
	{
		dmrDisconnectAbort();
		return;
	}

	state = DISCONNECT_ARMED;
	armStartMs = ticksGetMillis();
	gapTiming = false;
	notify("TG4000 armed", 2000);
}

void dmrDisconnectAbort(void)
{
	if (state == DISCONNECT_IDLE)
	{
		return;
	}

	// If we bailed out mid-transmission, put the previous TG back (defensive -- the channel screen
	// reload after TX normally rebuilds trxTalkGroupOrPcId anyway).
	if ((state == DISCONNECT_TX_KEYING) || (state == DISCONNECT_TX_RELEASING))
	{
		trxTalkGroupOrPcId = savedTgOrPcId;
	}

	state = DISCONNECT_IDLE;
	gapTiming = false;
	notify("Disconnect off", 1500);
}

bool dmrDisconnectIsActive(void)
{
	return (state != DISCONNECT_IDLE);
}

void dmrDisconnectTick(int currentMenuNumber, uint32_t *buttonsInOut, int *buttonEventInOut)
{
	uint32_t now;

	if (state == DISCONNECT_IDLE)
	{
		return;
	}

	now = ticksGetMillis();

	switch (state)
	{
		case DISCONNECT_IDLE:
			break;

		case DISCONNECT_ARMED:
			// Leaving DMR (mode changed) makes the whole thing moot.
			if (trxGetMode() != RADIO_MODE_DIGITAL)
			{
				dmrDisconnectAbort();
				break;
			}

			if ((now - armStartMs) > DMR_DISCONNECT_ARM_TIMEOUT_MS)
			{
				state = DISCONNECT_IDLE;
				gapTiming = false;
				notify("No gap-gave up", 2500);
				break;
			}

			// Only fire from a screen the injected PTT will actually transmit from.
			if ((currentMenuNumber != UI_CHANNEL_MODE) && (currentMenuNumber != UI_VFO_MODE))
			{
				gapTiming = false;
				break;
			}

			if (channelIsClear(*buttonsInOut))
			{
				if (gapTiming == false)
				{
					gapTiming = true;
					gapClearSinceMs = now;
				}
				else if ((now - gapClearSinceMs) >= DMR_DISCONNECT_GAP_GUARD_MS)
				{
					// Fire: switch the TX destination to TG 4000 (group call), then inject PTT the
					// same way VOX does so the normal DMR TX chain sends it.
					savedTgOrPcId = trxTalkGroupOrPcId;
					trxTalkGroupOrPcId = ((TG_CALL_FLAG << 24) | DMR_DISCONNECT_TG_ID);

					state = DISCONNECT_TX_KEYING;
					txStartMs = now;
					gapTiming = false;

					*buttonsInOut |= BUTTON_PTT;
					*buttonEventInOut = EVENT_BUTTON_CHANGE;
				}
			}
			else
			{
				// Channel went busy again before the guard elapsed -- wait for the next gap.
				gapTiming = false;
			}
			break;

		case DISCONNECT_TX_KEYING:
			if ((now - txStartMs) < DMR_DISCONNECT_TX_HOLD_MS)
			{
				*buttonsInOut |= BUTTON_PTT; // keep it keyed
			}
			else
			{
				*buttonsInOut &= ~BUTTON_PTT; // let go
				*buttonEventInOut = EVENT_BUTTON_CHANGE;
				state = DISCONNECT_TX_RELEASING;
			}
			break;

		case DISCONNECT_TX_RELEASING:
			// Don't re-assert PTT; wait for the TX chain to fully wind down, then restore the TG.
			*buttonsInOut &= ~BUTTON_PTT;
			if (trxTransmissionEnabled == false)
			{
				trxTalkGroupOrPcId = savedTgOrPcId;
				state = DISCONNECT_IDLE;
				gapTiming = false;
				notify("TG4000 sent", 2500);
			}
			break;
	}
}
