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

#include "user_interface/menuIcons.h"
#include "hardware/HX8353E.h"

// Each icon is drawn inside a MENU_ICON_WIDTH(10) x MENU_ICON_WIDTH square with (x,y) as its
// top-left corner. Deliberately simple/geometric shapes -- see menuIcons.h for why vector
// primitives were chosen over hand-authored bitmap byte arrays.
//
// A few icons below use a fixed accent colour instead of the current theme colour (folder
// yellow, contacts white/blue, RSSI green, firmware info blue) -- these deliberately ignore the
// isInverted parameter and stay the same colour whether or not the row is highlighted (so e.g.
// the yellow folder doesn't disappear/recolour when you scroll onto it), using
// displaySetForegroundAndBackgroundColours() + displayConvertRGB888ToNative() (same pattern
// menuThemeOptions.c's colour picker uses) around the drawing calls, then restoring whatever
// colours were active before. Exact shades are a first guess -- adjust the RGB888 hex values
// below after seeing them on real hardware.
//
// IMPORTANT gotcha: displayFillRect(), called directly (not via displayDrawFastHLine/VLine),
// has the OPPOSITE isInverted convention to every other primitive used in this file
// (displayFillCircle/displayDrawCircle/displayDrawLine/displayFillTriangle/displayDrawRect all
// treat isInverted=true as "draw in foregroundColour"; displayFillRect treats isInverted=true as
// "draw in backgroundColour", presumably because displayDrawFastHLine/VLine each negate the flag
// before calling it, and every other caller in this codebase goes through those wrappers rather
// than calling displayFillRect directly). So a direct displayFillRect call needs `false` to draw
// in the current foreground colour, not `true`.

static void iconDrawZoneList(int16_t x, int16_t y, bool isInverted) // yellow folder
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFC107U), savedBg); // amber/yellow

	displayFillRect(x, (int16_t)(y + 2), 4, 2, false);  // folder tab
	displayFillRect(x, (int16_t)(y + 4), 9, 5, false);  // folder body

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawContacts(int16_t x, int16_t y, bool isInverted) // white head, blue body
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFFFFFU), savedBg); // white head
	displayFillCircle((int16_t)(x + 4), (int16_t)(y + 2), 2, true);

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x1976D2U), savedBg); // blue body
	displayFillTriangle(x, (int16_t)(y + 9), (int16_t)(x + 8), (int16_t)(y + 9), (int16_t)(x + 4), (int16_t)(y + 4), true);

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawChannelDetails(int16_t x, int16_t y, bool isInverted) // antenna
{
	displayDrawLine((int16_t)(x + 4), y, (int16_t)(x + 4), (int16_t)(y + 9), isInverted);
	displayDrawLine((int16_t)(x + 1), (int16_t)(y + 3), (int16_t)(x + 7), (int16_t)(y + 3), isInverted);
	displayDrawLine((int16_t)(x + 2), (int16_t)(y + 6), (int16_t)(x + 6), (int16_t)(y + 6), isInverted);
}

static void iconDrawRssi(int16_t x, int16_t y, bool isInverted) // green ascending signal bars
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x00C853U), savedBg); // signal green

	displayFillRect(x, (int16_t)(y + 7), 2, 3, false);
	displayFillRect((int16_t)(x + 3), (int16_t)(y + 5), 2, 5, false);
	displayFillRect((int16_t)(x + 6), (int16_t)(y + 2), 2, 8, false);

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawInfo(int16_t x, int16_t y, bool isInverted) // "i" in a circle (Firmware Info) -- always blue
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x1976D2U), savedBg); // blue

	displayDrawCircle((int16_t)(x + 4), (int16_t)(y + 4), 4, true);
	displayFillRect((int16_t)(x + 3), (int16_t)(y + 2), 2, 2, false); // dot
	displayFillRect((int16_t)(x + 3), (int16_t)(y + 5), 2, 3, false); // stem

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawOptions(int16_t x, int16_t y, bool isInverted) // simplified gear
{
	displayFillCircle((int16_t)(x + 4), (int16_t)(y + 4), 3, isInverted);
	displayDrawLine((int16_t)(x + 4), y, (int16_t)(x + 4), (int16_t)(y + 1), isInverted);
	displayDrawLine((int16_t)(x + 4), (int16_t)(y + 7), (int16_t)(x + 4), (int16_t)(y + 8), isInverted);
	displayDrawLine(x, (int16_t)(y + 4), (int16_t)(x + 1), (int16_t)(y + 4), isInverted);
	displayDrawLine((int16_t)(x + 7), (int16_t)(y + 4), (int16_t)(x + 8), (int16_t)(y + 4), isInverted);
}

static void iconDrawLastHeard(int16_t x, int16_t y, bool isInverted) // clock
{
	displayDrawCircle((int16_t)(x + 4), (int16_t)(y + 4), 4, isInverted);
	displayDrawLine((int16_t)(x + 4), (int16_t)(y + 4), (int16_t)(x + 4), (int16_t)(y + 1), isInverted);
	displayDrawLine((int16_t)(x + 4), (int16_t)(y + 4), (int16_t)(x + 6), (int16_t)(y + 5), isInverted);
}

static void iconDrawRadioInfo(int16_t x, int16_t y, bool isInverted) // handheld radio outline
{
	displayDrawRect((int16_t)(x + 2), (int16_t)(y + 2), 5, 7, isInverted);
	displayDrawLine((int16_t)(x + 4), y, (int16_t)(x + 4), (int16_t)(y + 2), isInverted);
}

static void iconDrawSatellite(int16_t x, int16_t y, bool isInverted) // body + two solar panels
{
	displayFillRect((int16_t)(x + 3), (int16_t)(y + 3), 3, 3, !isInverted); // direct fillRect call needs the flag negated, see note above
	displayDrawLine(x, y, (int16_t)(x + 2), (int16_t)(y + 2), isInverted);
	displayDrawLine((int16_t)(x + 7), (int16_t)(y + 7), (int16_t)(x + 9), (int16_t)(y + 9), isInverted);
}

#if defined(HAS_GPS)
static void iconDrawGps(int16_t x, int16_t y, bool isInverted) // bullseye/target
{
	displayDrawCircle((int16_t)(x + 4), (int16_t)(y + 4), 4, isInverted);
	displayDrawCircle((int16_t)(x + 4), (int16_t)(y + 4), 2, isInverted);
	displaySetPixel((int16_t)(x + 4), (int16_t)(y + 4), isInverted);
}
#endif

// --- SMS menu icons ---

static void iconDrawEnvelope(int16_t x, int16_t y, bool isInverted) // Send SMS
{
	displayDrawRect(x, (int16_t)(y + 1), 10, 7, isInverted);
	displayDrawLine(x, (int16_t)(y + 1), (int16_t)(x + 5), (int16_t)(y + 5), isInverted);
	displayDrawLine((int16_t)(x + 9), (int16_t)(y + 1), (int16_t)(x + 5), (int16_t)(y + 5), isInverted);
}

static void iconDrawInboxTray(int16_t x, int16_t y, bool isInverted) // Inbox: tray (theme) + always-blue arrow
{
	uint16_t savedFg, savedBg;

	displayDrawRect(x, (int16_t)(y + 6), 10, 4, isInverted);

	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x1976D2U), savedBg); // blue
	displayDrawLine((int16_t)(x + 5), y, (int16_t)(x + 5), (int16_t)(y + 5), true);
	displayDrawLine((int16_t)(x + 2), (int16_t)(y + 2), (int16_t)(x + 5), (int16_t)(y + 5), true);
	displayDrawLine((int16_t)(x + 8), (int16_t)(y + 2), (int16_t)(x + 5), (int16_t)(y + 5), true);
	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawOutboxTray(int16_t x, int16_t y, bool isInverted) // Sent: tray (theme) + always-red arrow
{
	uint16_t savedFg, savedBg;

	displayDrawRect(x, (int16_t)(y + 6), 10, 4, isInverted);

	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xD32F2FU), savedBg); // red
	displayDrawLine((int16_t)(x + 5), (int16_t)(y + 5), (int16_t)(x + 5), y, true);
	displayDrawLine((int16_t)(x + 2), (int16_t)(y + 3), (int16_t)(x + 5), y, true);
	displayDrawLine((int16_t)(x + 8), (int16_t)(y + 3), (int16_t)(x + 5), y, true);
	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawLightningBolt(int16_t x, int16_t y, bool isInverted) // Quick Text -- always yellow
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFC107U), savedBg); // amber/yellow

	displayDrawLine((int16_t)(x + 6), y, (int16_t)(x + 2), (int16_t)(y + 5), true);
	displayDrawLine((int16_t)(x + 2), (int16_t)(y + 5), (int16_t)(x + 5), (int16_t)(y + 5), true);
	displayDrawLine((int16_t)(x + 5), (int16_t)(y + 5), (int16_t)(x + 3), (int16_t)(y + 9), true);

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawBell(int16_t x, int16_t y, bool isInverted) // Call Alert -- always yellow
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFC107U), savedBg); // amber/yellow

	displayFillTriangle((int16_t)(x + 1), (int16_t)(y + 7), (int16_t)(x + 9), (int16_t)(y + 7), (int16_t)(x + 5), y, true);
	displayFillRect(x, (int16_t)(y + 7), 10, 2, false); // direct fillRect call needs `false` for foreground, see note above
	displayFillCircle((int16_t)(x + 5), (int16_t)(y + 9), 1, true);

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawCheckCircle(int16_t x, int16_t y, bool isInverted) // Radio Check -- always green
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x00C853U), savedBg); // signal green

	displayDrawCircle((int16_t)(x + 4), (int16_t)(y + 4), 4, true);
	displayDrawLine((int16_t)(x + 2), (int16_t)(y + 4), (int16_t)(x + 4), (int16_t)(y + 6), true);
	displayDrawLine((int16_t)(x + 4), (int16_t)(y + 6), (int16_t)(x + 7), (int16_t)(y + 2), true);

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawFlag(int16_t x, int16_t y, bool isInverted) // Status -- white pole, red flag
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFFFFFU), savedBg); // white pole
	displayDrawLine(x, y, x, (int16_t)(y + 9), true);

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xD32F2FU), savedBg); // red flag
	displayFillTriangle(x, y, (int16_t)(x + 7), (int16_t)(y + 2), x, (int16_t)(y + 4), true);

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawLocationPin(int16_t x, int16_t y, bool isInverted) // Send Location -- same bullseye/target as Main Menu's GPS icon
{
	displayDrawCircle((int16_t)(x + 4), (int16_t)(y + 4), 4, isInverted);
	displayDrawCircle((int16_t)(x + 4), (int16_t)(y + 4), 2, isInverted);
	displaySetPixel((int16_t)(x + 4), (int16_t)(y + 4), isInverted);
}

// --- "Send to" destination-select icons (Select contact / Manual ID) ---

static void iconDrawSelectContact(int16_t x, int16_t y, bool isInverted) // same white-head/blue-body person as Contacts
{
	iconDrawContacts(x, y, isInverted);
}

static void iconDrawManualIdBox(int16_t x, int16_t y, bool isInverted) // white box, "ID" text
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFFFFFU), savedBg); // white

	displayDrawRect(x, y, 14, 14, true);
	displayPrintCore((int16_t)(x + 1), (int16_t)(y + 3), "ID", FONT_SIZE_1, TEXT_ALIGN_LEFT, false); // false = foreground, text's own convention (opposite of displayFillRect's)

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

menuIconDrawFn_t menuIconForDestinationOption(int optionIndex)
{
	switch (optionIndex)
	{
		case 0: // Select contact
			return iconDrawSelectContact;
		case 1: // Manual ID
			return iconDrawManualIdBox;
		default:
			return NULL;
	}
}

menuIconDrawFn_t menuIconForSmsMenuItem(int itemIndex)
{
	switch (itemIndex)
	{
		case 0: // SMS_MENU_ITEM_COMPOSE
			return iconDrawEnvelope;
		case 1: // SMS_MENU_ITEM_INBOX
			return iconDrawInboxTray;
		case 2: // SMS_MENU_ITEM_QUICKTEXT
			return iconDrawLightningBolt;
		case 3: // SMS_MENU_ITEM_SENT
			return iconDrawOutboxTray;
		case 4: // SMS_MENU_ITEM_CALL_ALERT
			return iconDrawBell;
		case 5: // SMS_MENU_ITEM_RADIO_CHECK
			return iconDrawCheckCircle;
		case 6: // SMS_MENU_ITEM_STATUS
			return iconDrawFlag;
		case 7: // SMS_MENU_ITEM_SEND_LOCATION
			return iconDrawLocationPin;
		default:
			return NULL;
	}
}

static void iconDrawNewContact(int16_t x, int16_t y, bool isInverted) // New Contact -- always white box + "+"
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFFFFFU), savedBg); // white

	displayDrawRect(x, y, 10, 10, true);
	displayDrawLine((int16_t)(x + 5), (int16_t)(y + 2), (int16_t)(x + 5), (int16_t)(y + 7), true); // "+" vertical stroke
	displayDrawLine((int16_t)(x + 2), (int16_t)(y + 5), (int16_t)(x + 7), (int16_t)(y + 5), true); // "+" horizontal stroke

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawSpaceship(int16_t x, int16_t y, bool isInverted) // Play Space -- always cyan rocket
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x00B8D4U), savedBg); // cyan

	displayFillTriangle((int16_t)(x + 8), (int16_t)(y + 5), (int16_t)(x + 2), (int16_t)(y + 1), (int16_t)(x + 2), (int16_t)(y + 8), true); // hull, nose to the right
	displayFillTriangle((int16_t)(x + 2), (int16_t)(y + 8), (int16_t)(x), (int16_t)(y + 9), (int16_t)(x + 2), (int16_t)(y + 5), true); // fin

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawSnake(int16_t x, int16_t y, bool isInverted) // Play Snake -- always green zigzag body
{
	uint16_t savedFg, savedBg;

	(void)isInverted;
	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x00C853U), savedBg); // green

	displayDrawLine((int16_t)(x + 1), (int16_t)(y + 8), (int16_t)(x + 6), (int16_t)(y + 8), true);
	displayDrawLine((int16_t)(x + 6), (int16_t)(y + 8), (int16_t)(x + 3), (int16_t)(y + 5), true);
	displayDrawLine((int16_t)(x + 3), (int16_t)(y + 5), (int16_t)(x + 8), (int16_t)(y + 5), true);
	displayDrawLine((int16_t)(x + 8), (int16_t)(y + 5), (int16_t)(x + 5), (int16_t)(y + 2), true);
	displayDrawLine((int16_t)(x + 5), (int16_t)(y + 2), (int16_t)(x + 9), (int16_t)(y + 2), true);
	displayFillCircle((int16_t)(x + 9), (int16_t)(y + 2), 1, true); // head

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void iconDrawChessPiece(int16_t x, int16_t y, bool isInverted) // Games -- knight (horse) head silhouette
{
	// Neck/head wedge, tapering to a snout
	displayFillTriangle((int16_t)(x + 1), (int16_t)(y + 9), (int16_t)(x + 1), (int16_t)(y + 2), (int16_t)(x + 8), (int16_t)(y + 6), isInverted);
	// Ear
	displayFillTriangle((int16_t)(x + 1), (int16_t)(y + 2), (int16_t)(x + 4), y, (int16_t)(x + 5), (int16_t)(y + 3), isInverted);
	// Eye
	displaySetPixel((int16_t)(x + 5), (int16_t)(y + 5), isInverted);
	// displayFillRect() called directly uses the opposite isInverted convention to the shapes above.
	displayFillRect(x, (int16_t)(y + 9), 8, 1, (bool)(!isInverted));
}

menuIconDrawFn_t menuIconForMenuId(int menuId)
{
	switch (menuId)
	{
		case MENU_ZONE_LIST:
			return iconDrawZoneList;
		case MENU_CONTACTS_MENU:
			return iconDrawContacts;
		case MENU_CONTACT_LIST: // DMR contacts, within the Contacts submenu
			return iconDrawContacts;
		case MENU_DTMF_CONTACT_LIST: // FM/DTMF contacts, within the Contacts submenu
			return iconDrawChannelDetails;
		case MENU_CONTACT_NEW:
			return iconDrawNewContact;
		case MENU_CHANNEL_DETAILS:
			return iconDrawChannelDetails;
		case MENU_RSSI_SCREEN:
			return iconDrawRssi;
		case MENU_FIRMWARE_INFO:
			return iconDrawInfo;
		case MENU_OPTIONS:
			return iconDrawOptions;
		case MENU_LAST_HEARD:
			return iconDrawLastHeard;
		case MENU_RADIO_INFOS:
			return iconDrawRadioInfo;
		case MENU_SATELLITE:
			return iconDrawSatellite;
#if defined(HAS_GPS)
		case MENU_GPS:
			return iconDrawGps;
#endif
		case MENU_GAMES_MENU:
			return iconDrawChessPiece;
		case MENU_GAME_SNAKE: // within the Games submenu
			return iconDrawSnake;
		case MENU_GAME_SPACE: // within the Games submenu
			return iconDrawSpaceship;
		default:
			return NULL;
	}
}
