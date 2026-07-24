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

#ifndef _OPENGD77_LRRP_H_
#define _OPENGD77_LRRP_H_

#include <stdint.h>
#include "functions/sms.h"

// Builds and queues the radio's current GPS position (from settingsLocationGetLatitude/Longitude)
// as an LRRP (Location Request Response Protocol) "Triggered Location Report" over the same DMR
// IP/UDP transport (port 4001) real AnyTone/Hytera radios use for this -- BrandMeister's APRS
// gateway requires LRRP specifically (this fork's own embedded-LC GPS via Talker Alias is a
// different mechanism it does not recognise).
//
// Wire format is built from the real ok-dmrlib (OK-DMR project) MBXML/LRRP source, cross-checked
// against a real captured example from a RadioReference forum thread (document id 0x0D, an
// info-time token then a location token) -- not purely reverse-engineered from an abstract spec.
// Still genuinely unverified: whether BrandMeister's specific gateway implementation accepts an
// *unprompted* Triggered-Location-Report (real usage is normally a response to a query) with no
// request-id token attached. Confirm with a real test (does a position show up on aprs.fi?)
// before relying on this.
//
// After a successful call, use smsScheduleQueuedStatusTransmission() (already generic despite the
// name) to actually key up and send it -- same as Status messages do.
smsPackResult_t lrrpQueueLocationReport(uint32_t destinationId, uint32_t sourceId);

#endif
