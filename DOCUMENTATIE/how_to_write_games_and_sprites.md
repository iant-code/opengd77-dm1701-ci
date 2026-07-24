# How to write a simple menu game and design its sprites (V3_TEST)

Written from building `menuGameSnake.c` and `menuGameSpace.c` (Games submenu, DM1701 only so
far). Covers wiring a new game into the menu system, the game-loop shape this firmware expects,
and how to design bitmap sprites without a simulator.

## 1. Wiring a new game into the menu system

A game is just another `MENU_SCREENS` entry. Three hand-maintained, position-matched lists in
`menuSystem.c` must all stay in exact 1:1 sync with the enum's order and length -- not just the
two that look obviously related:

1. `MENU_SCREENS` enum (`menuSystem.h`) -- add your new screen here.
2. `menuFunctions[]` (`menuSystem.c`) -- the function-pointer table, same position as the enum.
3. `menuDataGlobal.data[]` (inside the `menuDataGlobal` initializer, `menuSystem.c`) -- a flexible
   array member populated by a plain sequential `NULL`/`&someData` list with **no designated
   indices**. Easy to forget entirely since it's a nested initializer far from the enum, and
   there's no compiler error if it's short -- it just silently allocates a smaller array.

**Why list 2 matters**: missing an entry doesn't fail to compile or link. It causes an
out-of-bounds read the first time `menuSystemPushMenuFirstRun()` indexes `.data[]` at the new
enum's position -- read garbage gets dereferenced as a pointer a few lines later, causing a hard
fault. Symptom on real hardware: screen locks up entering the new menu, unresponsive to the power
button, *nothing* printed on debug serial.

Also register the new `.c` file in the target's `subdir.mk` (`C_SRCS`, `OBJS`, `C_DEPS` -- three
lists again, same file, same gotcha shape) -- e.g.
`DM1701_FW/application/source/user_interface/subdir.mk`. Miss this and the linker just won't see
your new symbols.

If your game needs a submenu entry point (e.g. a "Games" list holding multiple games rather than
one game directly off the main menu), add a `menuItemsList_t` + backing `menuItemNewData_t[]`
(see `menuDataGames` / `gamesMenuItems` in `menuSystem.c`) and point a `MENU_SCREENS` slot at
`menuDisplayMenuList` (the generic list-rendering menu function) instead of your own draw
function -- `menuDisplayMenuList.c` needs a `customMenuName`/`mName` case added for the new list's
title and a `menuName` override for each of its child entries' label text.

## 2. Game loop shape

Every menu screen function has the same signature:

```c
menuStatus_t menuGameWhatever(uiEvent_t *ev, bool isFirstRun)
```

- `isFirstRun`: reset all state, draw the initial frame, and return -- don't do gameplay logic
  here.
- Otherwise: handle input (`ev->hasEvent`), then advance game state on a **tick**, not on every
  call -- this function gets called far more often than you want to move anything. Gate ticks with
  `ev->time` deltas against a `static uint32_t lastTickTime`, e.g.

```c
if ((ev->time - lastTickTime) > TICK_MS)
{
    lastTickTime = ev->time;
    advanceGame();
    updateScreen(false);
}
```

Use separate timers for separate cadences (e.g. Space's movement tick, fire-rate timer, and
enemy-spawn timer all run independently off the same `ev->time` clock).

## 3. Input handling

For continuous movement (ship/snake moving every tick while a key is held), track **held state**
with `KEYCHECK_DOWN`/`KEYCHECK_UP`, not per-press events -- this makes movement smooth at the
game's own tick rate instead of the keyboard's repeat rate:

```c
if (KEYCHECK_DOWN(ev->keys, KEY_2)) { moveUpHeld = true; }
else if (KEYCHECK_UP(ev->keys, KEY_2)) { moveUpHeld = false; }
```

Use `KEYCHECK_SHORTUP(ev->keys, KEY_RED)` / `KEY_GREEN` for exit-back-to-menu (via
`menuSystemPopPreviousMenu()`).

## 4. Random numbers -- do not use libc `rand()`/`srand()`

This burned a hardware freeze in an earlier version of the Snake game. Use a small
self-seeded LCG local to the game file instead:

```c
static uint32_t rngState;
// seed once, e.g. in your reset function:
rngState = ticksGetMillis();

static uint32_t nextRandom(void)
{
    rngState = ((rngState * 1103515245u) + 12345u);
    return ((rngState >> 16) & 0x7FFFu);
}
```

Bound any retry loop that uses this (e.g. "find an empty cell") with a max-attempts counter --
don't loop until success.

## 5. Drawing

Two options: vector primitives (`displayFillRect`/`displayFillCircle`/`displayFillTriangle`/
`displayDrawLine`/`displaySetPixel`/etc., declared in `hardware/HX8353E.h`) or hand-authored
bitmaps (`displayDrawBitmap`). See section 6 for when to use which.

**Colour**: save/restore the current theme colours around any custom colour, same pattern
`menuIcons.c` uses:

```c
uint16_t savedFg, savedBg;
displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x00E5FFU), savedBg);
// ... draw ...
displaySetForegroundAndBackgroundColours(savedFg, savedBg);
```

**Gotcha**: `displayFillCircle`/`displayDrawCircle`/`displayDrawLine`/`displayFillTriangle`/
`displayDrawRect`/`displaySetPixel`/`displayDrawBitmap` all treat `isInverted=true` as "draw in
the current foreground colour". A **direct** call to `displayFillRect()` is the opposite:
`isInverted=false` draws in the foreground colour there. This is because
`displayDrawFastHLine`/`VLine` (which most other rect-ish drawing goes through) each negate the
flag before calling `displayFillRect` internally -- so only a *direct* `displayFillRect()` call
needs the flag flipped from what you'd otherwise expect.

## 6. Creating sprites

`menuIcons.h` documents this codebase's default position: hand-authored bitmap byte arrays are
risky because **there's no simulator here to visually verify pixel-exact bitmap data against** --
a wrong vector call is much easier to reason about (and fix) than a wrong byte in a hand-computed
array. Default to vector primitives (see `menuIcons.c` for many small examples) unless you
specifically need a bitmap silhouette that vector shapes can't reasonably approximate (e.g. a
recognisable "alien" or "ship" outline).

If you do need a bitmap, don't hand-transcribe bytes. The workflow that worked for the Space
game's ship/alien sprites:

1. Draw the sprite as ASCII art, one character per pixel (`#` on, `.` off), in a throwaway
   Python script.
2. Convert to bytes and immediately **render the bytes back to ASCII** and diff against the
   original grid -- this is the stand-in for the simulator this codebase doesn't have. Don't skip
   this step; it's what catches transcription/bit-order mistakes before they reach the firmware.
3. Only paste the byte array into the `.c` file once the round trip matches.
4. After building, pull the bytes back out of the *linked ELF* (not just re-read the source) and
   diff those against the same generated data, to also catch anything that went wrong between
   source and binary (wrong array, stale object file, etc.):

```
arm-none-eabi-nm MDUV380_firmware.elf | grep yourSpriteName
arm-none-eabi-objdump -s -j .rodata --start-address=0x<addr> --stop-address=0x<addr+n> MDUV380_firmware.elf
```

**`displayDrawBitmap()` bit format** (`hardware/HX8353E_display.c`): row-major, one byte per 8
pixels per row (`byteWidth = (w + 7) / 8`, so non-multiple-of-8 widths are fine -- the extra
padding bits in a row's last byte are simply never read), **MSB-first**: bit `0x80` of a row's
first byte is that row's leftmost pixel, reading left-to-right down to bit `0x01` being pixel 7,
then the next byte picks up at pixel 8. This means a binary literal or an ASCII-art row read
left-to-right maps straight across with no bit-reversal -- unlike `displayDrawXBitmap()` (XBM
format), which is LSB-first and *does* need the bits reversed relative to a naturally-read ASCII
row. Prefer `displayDrawBitmap()` for anything hand-authored to avoid that mental step entirely.

A sprite with multiple colours (e.g. a cyan hull + white cockpit) is just two (or more) separate
bitmaps of the same width/height, drawn at the same `(x, y)` back-to-back with different
foreground colours set first -- `displayDrawBitmap()` only touches pixels where a bit is set, so
later layers don't need to carry the earlier layers' pixels.

If you're stuck on getting a shape right purely from a text description (no way to see the actual
screen from here), consider dumping the sprite as an editable CSV (row, then one `0`/`1` column
per pixel) to a location the user can open in a spreadsheet/text editor and hand-edit directly,
then read it back and regenerate the bitmap from that -- much less lossy than iterating over
verbal descriptions of "shift the triangle left" one guess at a time.

## 7. Build & test workflow

See `docker_build_setup` notes (ask if not present locally) for the containerized
STM32CubeIDE build. In short: `docker cp` your changed file(s) in, `make all` inside the target's
config dir (e.g. `DM1701_FW/`), `docker cp` `OpenDM1701.bin` back out. Identical `.text/.data/.bss`
sizes between two builds after a real source change is *not* evidence of a stale build (`-Os` can
produce byte-identical section sizes for small edits) -- if in doubt, grep the post-build
`.list` file or `nm`/`objdump` the `.elf` for a symbol/byte pattern unique to your change.

Nothing here is confirmed correct until it's actually been flashed and looked at on real
hardware -- getting the build to compile and link only proves the code is *well-formed*, not that
it *looks right* or *behaves right*.
