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
#include "functions/lrrp.h"
#include "functions/sms.h"
#include "functions/settings.h"
#include "user_interface/uiGlobals.h"

// LRRP rides the same DMR IP/UDP-over-Confirmed/Unconfirmed-Data transport as this firmware's own
// DMR_Standard SMS format (SAP=IP), just on a different UDP port and with an MBXML-encoded
// application payload instead of UTF16 text. Port 4001 (both source and destination) is what
// real LRRP traffic uses, confirmed via web search cross-referencing RadioReference forum threads
// analysing real captures.
#define LRRP_UDP_PORT 4001U

// MBXMLDocumentIdentifier value for "Triggered-Location-Report, NCDT (No Constant Data Table)"
// -- from ok-dmrlib's (github.com/OK-DMR/ok-dmrlib) mbxml.py MBXMLDocumentIdentifier enum, and
// independently confirmed by a real captured example on a RadioReference forum thread showing
// exactly this document id for a "triggered GPS location report".
#define LRRP_DOC_TRIGGERED_LOCATION_REPORT_NCDT 0x0DU

// Element token ids, from ok-dmrlib's lrrp.py ANSWER_AND_REPORT_MESSAGES_ELEMENT_TOKENS.
#define LRRP_TOKEN_INFO_TIME 0x34U // GlobalToken.INFO_TIME, 5-byte packed date+time, "info-data" path
#define LRRP_TOKEN_POINT_2D  0x66U // GlobalToken.POINT_2D, [lat 4 octets | lon 4 octets], "info-data.shape" path

#define LRRP_MAX_DOC_BYTES 24U

static uint8_t lrrpWriteUintvar(uint32_t value, uint8_t *out)
{
	uint8_t groups[5];
	uint8_t count = 0U;
	uint32_t v = value;

	do
	{
		groups[count] = (uint8_t)(v & 0x7FU);
		count++;
		v >>= 7;
	} while ((v != 0U) && (count < 5U));

	for (uint8_t i = 0U; i < count; i++)
	{
		uint8_t groupIndexFromMsb = (uint8_t)(count - 1U - i);
		out[i] = (uint8_t)(groups[groupIndexFromMsb] | ((i < (count - 1U)) ? 0x80U : 0x00U));
	}

	return count;
}

// Matches ok-dmrlib's MBXML.write_latitude()/write_longitude(): both are 32-bit big-endian
// fixed-point values, but with DIFFERENT scale factors (latitude: 2^31 / 90; longitude: 2^32 /
// 360) -- this asymmetry is a real detail of the format, not a mistake here. Clamped defensively
// since a double-to-int32 cast on an out-of-range value is undefined behaviour in C (Python's
// int() truncation has no such hazard, which the reference implementation relies on).
static void lrrpEncodeLatLon(double latitude, double longitude, uint8_t *latOut, uint8_t *lonOut)
{
	double latScaled = (latitude * 2147483648.0) / 90.0;
	double lonScaled = (longitude * 4294967296.0) / 360.0;
	int32_t latEncoded;
	int32_t lonEncoded;

	if (latScaled > 2147483647.0) { latScaled = 2147483647.0; }
	if (latScaled < -2147483648.0) { latScaled = -2147483648.0; }
	if (lonScaled > 2147483647.0) { lonScaled = 2147483647.0; }
	if (lonScaled < -2147483648.0) { lonScaled = -2147483648.0; }

	latEncoded = (int32_t)latScaled;
	lonEncoded = (int32_t)lonScaled;

	latOut[0] = (uint8_t)((uint32_t)latEncoded >> 24);
	latOut[1] = (uint8_t)((uint32_t)latEncoded >> 16);
	latOut[2] = (uint8_t)((uint32_t)latEncoded >> 8);
	latOut[3] = (uint8_t)((uint32_t)latEncoded);

	lonOut[0] = (uint8_t)((uint32_t)lonEncoded >> 24);
	lonOut[1] = (uint8_t)((uint32_t)lonEncoded >> 16);
	lonOut[2] = (uint8_t)((uint32_t)lonEncoded >> 8);
	lonOut[3] = (uint8_t)((uint32_t)lonEncoded);
}

// Standard Unix-epoch-seconds -> UTC civil calendar conversion (Howard Hinnant's well-known
// "civil_from_days" algorithm) -- generic, not DMR-specific, used only to build the 5-byte
// info-time token below.
static void lrrpEpochToCalendar(uint32_t epochSeconds, uint16_t *year, uint8_t *month, uint8_t *day,
	uint8_t *hour, uint8_t *minute, uint8_t *second)
{
	int64_t days = (int64_t)(epochSeconds / 86400U);
	uint32_t remSecs = epochSeconds % 86400U;
	int64_t z, era, doe, yoe, y, doy, mp;

	*hour = (uint8_t)(remSecs / 3600U);
	*minute = (uint8_t)((remSecs % 3600U) / 60U);
	*second = (uint8_t)(remSecs % 60U);

	z = days + 719468;
	era = ((z >= 0) ? z : (z - 146096)) / 146097;
	doe = z - (era * 146097);
	yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
	y = yoe + (era * 400);
	doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
	mp = ((5 * doy) + 2) / 153;

	*day = (uint8_t)(doy - (((153 * mp) + 2) / 5) + 1);
	*month = (uint8_t)(mp + ((mp < 10) ? 3 : -9));
	*year = (uint16_t)(y + ((*month <= 2) ? 1 : 0));
}

// Matches ok-dmrlib's MBXML.write_infotime(): 40 bits (5 bytes) packing year/month/day/hour/
// minute/second, big-endian.
static void lrrpEncodeInfoTime(uint32_t epochSeconds, uint8_t *out5)
{
	uint16_t year;
	uint8_t month, day, hour, minute, second;
	uint64_t value;

	lrrpEpochToCalendar(epochSeconds, &year, &month, &day, &hour, &minute, &second);

	value = ((uint64_t)year << 26) | ((uint64_t)month << 22) | ((uint64_t)day << 17) |
		((uint64_t)hour << 12) | ((uint64_t)minute << 6) | (uint64_t)second;

	out5[0] = (uint8_t)(value >> 32);
	out5[1] = (uint8_t)(value >> 24);
	out5[2] = (uint8_t)(value >> 16);
	out5[3] = (uint8_t)(value >> 8);
	out5[4] = (uint8_t)(value);
}

smsPackResult_t lrrpQueueLocationReport(uint32_t destinationId, uint32_t sourceId)
{
	uint8_t doc[LRRP_MAX_DOC_BYTES];
	uint16_t docLen = 0U;
	uint8_t body[16];
	uint16_t bodyLen = 0U;
	uint8_t lenBuf[5];
	uint8_t lenBytes;
	uint8_t payload[SMS_MAX_DATA_BLOCKS * SMS_BLOCK_DATA_BYTES];
	uint16_t payloadLength = 0U;
	uint8_t padOctetCount = 0U;
	smsPreparedMessage_t message;
	smsPackResult_t result;

	if (!settingsLocationIsValid())
	{
		return SMS_PACK_ERROR_EMPTY;
	}

	if ((destinationId == 0U) || (destinationId > 0x00FFFFFFU) || (sourceId == 0U) || (sourceId > 0x00FFFFFFU))
	{
		return SMS_PACK_ERROR_INVALID_DEST;
	}

	body[bodyLen] = LRRP_TOKEN_INFO_TIME;
	bodyLen += 1U;
	lrrpEncodeInfoTime(uiDataGlobal.dateTimeSecs, &body[bodyLen]);
	bodyLen = (uint16_t)(bodyLen + 5U);

	body[bodyLen] = LRRP_TOKEN_POINT_2D;
	bodyLen += 1U;
	lrrpEncodeLatLon(settingsLocationGetLatitude(), settingsLocationGetLongitude(), &body[bodyLen], &body[bodyLen + 4U]);
	bodyLen = (uint16_t)(bodyLen + 8U);

	doc[docLen] = LRRP_DOC_TRIGGERED_LOCATION_REPORT_NCDT;
	docLen += 1U;
	lenBytes = lrrpWriteUintvar(bodyLen, lenBuf);
	memcpy(&doc[docLen], lenBuf, lenBytes);
	docLen = (uint16_t)(docLen + lenBytes);
	memcpy(&doc[docLen], body, bodyLen);
	docLen = (uint16_t)(docLen + bodyLen);

	result = smsBuildIpUdpPayload(destinationId, sourceId, LRRP_UDP_PORT, doc, docLen, payload, &payloadLength, &padOctetCount);
	if (result != SMS_PACK_OK)
	{
		return result;
	}

	memset(&message, 0, sizeof(message));
	message.destinationId = destinationId;
	message.sourceId = sourceId;
	message.requestAck = true;
	message.payloadLength = payloadLength;
	message.padOctetCount = padOctetCount;
	message.blockCount = (uint8_t)(payloadLength / SMS_BLOCK_DATA_BYTES);

	if (message.blockCount > SMS_MAX_DATA_BLOCKS)
	{
		return SMS_PACK_ERROR_TOO_LONG;
	}

	for (uint8_t block = 0U; block < message.blockCount; block++)
	{
		memcpy(message.blocks[block], &payload[(uint16_t)block * SMS_BLOCK_DATA_BYTES], SMS_BLOCK_DATA_BYTES);
	}

	smsBuildTransportHeaders(&message);

	if (!smsQueuePreBuiltMessage(&message))
	{
		return SMS_PACK_ERROR_INVALID_INDEX;
	}

	return SMS_PACK_OK;
}
