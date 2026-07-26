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

#ifndef _OPENGD77_CSBK_H_
#define _OPENGD77_CSBK_H_

#include <stdint.h>
#include <stdbool.h>

// Standalone (non-preamble) CSBK services: Call Alert (page a radio, it notifies its user) and
// Radio Check (silently ask if a radio is on/in range; it auto-replies with no user interaction).
//
// Opcode values below are VERIFIED against MMDVMHost's own DMRCSBK.h/.cpp (a real, widely
// deployed, interoperating DMR gateway implementation -- source available locally, see
// DOCUMENTATIE/csbk_call_alert_radio_check_status.md for exactly where/how this was checked),
// not guessed. Two things changed from the original guess: Radio Check's opcode was wrong
// (0x1DU -- corrected to 0x24U, matching MMDVMHost's `RADIO_CHECK`), and Radio Check does NOT
// share CSBKO_ACK the way Call Alert does -- it reuses its own opcode for both request and reply,
// disambiguated by byte[3] (see csbk.c). MMDVMHost's CSBK dispatch does not check the FID byte at
// all for any of these, so FID=0 (this firmware's existing choice, unchanged) is not believed to
// be a problem despite ETSI TS 102 361-2's base-standard CSBKO table (FID=0 only) not listing
// these opcodes -- they're real, in-use, cross-vendor-interoperating values that simply live
// outside that narrow base table.
typedef enum
{
	CSBKO_CALL_ALERT  = 0x1FU, // VERIFIED (MMDVMHost CALL_ALERT) -- request: page destination, it shows/plays a notification.
	CSBKO_RADIO_CHECK = 0x24U, // VERIFIED (MMDVMHost RADIO_CHECK) -- SAME opcode for request and ack, see byte[3] in csbk.c.
	CSBKO_ACK         = 0x20U  // VERIFIED (MMDVMHost CALL_ALERT_ACK) -- Call Alert's ack ONLY, not Radio Check's.
} csbkOpcode_t;

typedef enum
{
	CSBK_KIND_CALL_ALERT = 0,
	CSBK_KIND_RADIO_CHECK
} csbkKind_t;

typedef enum
{
	CSBK_PENDING_NONE = 0,
	CSBK_PENDING_WAITING,
	CSBK_PENDING_ACKED,
	CSBK_PENDING_TIMEOUT
} csbkPendingState_t;

void csbkInit(void);

// Send a Call Alert / Radio Check request to destinationId. Returns false if one is already
// in flight (only one outbound request is tracked at a time) or destinationId is invalid.
bool csbkSendCallAlert(uint32_t destinationId, uint32_t sourceId);
bool csbkSendRadioCheck(uint32_t destinationId, uint32_t sourceId);

// Poll the outcome of the most recent csbkSendCallAlert()/csbkSendRadioCheck(). Returns
// CSBK_PENDING_NONE once there is nothing left to report (after a terminal state has been read
// once, it resets to NONE on the next call).
csbkPendingState_t csbkGetPendingResult(uint32_t *sourceIdOut, csbkKind_t *kindOut);

// Called from HR-C6000.c's RX dispatch when a standalone CSBK burst (not a preamble) is received.
void csbkHandleReceivedFrame(const uint8_t *buf, uint8_t length);

// Called from the same place smsTick() is, to start a queued request once the radio is idle and
// to time out a request that never got an Ack.
void csbkTick(void);

// True once per received Call Alert addressed to us (fills sourceIdOut, then clears until the
// next one) -- lets the UI layer show a tone/popup on receipt. Deliberately Call Alert only, not
// Radio Check: Radio Check is meant to be silent on receipt, that's what makes it a "check"
// rather than an "alert" (see csbk.c).
bool csbkConsumeIncomingCallAlert(uint32_t *sourceIdOut);

#endif
