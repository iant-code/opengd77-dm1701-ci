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

#ifndef _OPENGD77_MENUICONS_H_
#define _OPENGD77_MENUICONS_H_

#include "user_interface/menuSystem.h"

// All icons are drawn in a MENU_ICON_WIDTH x MENU_ICON_WIDTH square using the existing vector
// drawing primitives (displayFillRect/displayDrawCircle/etc.) rather than hand-authored bitmap
// byte arrays -- deliberately, since there's no simulator to visually verify pixel-exact bitmap
// data against, and a wrong vector call is far easier to reason about (and fix) than a wrong byte
// in a hand-computed XBM array. Expect these to need real-hardware visual iteration.
#define MENU_ICON_WIDTH 10
// The "Send to" destination-select screens (Select contact / Manual ID) need a couple more
// pixels of width to fit real "ID" text via the font engine rather than a hand-drawn glyph.
#define MENU_ICON_WIDTH_WIDE 14

// Returns NULL if menuId has no icon defined (menuDisplayEntryEx() falls back to text-only,
// unaffected, in that case).
menuIconDrawFn_t menuIconForMenuId(int menuId);

// SMS menu items aren't MENU_SCREENS values (menuSMS.c's own SMS_MENU_ITEM_* enum is private to
// that file) -- itemIndex is just the plain 0-based position, matching that enum's order exactly:
// 0=Compose, 1=Inbox, 2=QuickText, 3=Sent, 4=CallAlert, 5=RadioCheck, 6=Status, 7=SendLocation.
menuIconDrawFn_t menuIconForSmsMenuItem(int itemIndex);

// Shared by both "Send to" screens (menuCsbkActions.c's and menuSMS.c's Compose) -- both use the
// identical 0=Select contact, 1=Manual ID option order. Use with MENU_ICON_WIDTH_WIDE.
menuIconDrawFn_t menuIconForDestinationOption(int optionIndex);

#endif
