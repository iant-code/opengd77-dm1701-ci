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
// "Chomp": a dot-eating maze chase, deliberately not named after the arcade game it's inspired by
// (that name is a live trademark and this firmware is redistributed publicly -- same reasoning as
// why menuGameSpace.c is called "Space" rather than "Space Impact").
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"

#define CHOMP_TICK_MS             180
#define CHOMP_CELL_SIZE           8
#define CHOMP_COLS                17
#define CHOMP_ROWS                11
#define CHOMP_HUD_HEIGHT          9
#define CHOMP_MARGIN_X            ((DISPLAY_SIZE_X - (CHOMP_COLS * CHOMP_CELL_SIZE)) / 2)
#define CHOMP_PLAY_TOP            (MENU_HEADER_HEIGHT + CHOMP_HUD_HEIGHT + 2)

#define CHOMP_GHOST_COUNT         2
#define CHOMP_INITIAL_LIVES       3
#define CHOMP_FRIGHTENED_TICKS    44 // ~8s at CHOMP_TICK_MS
#define CHOMP_FRIGHT_FLICKER_AT   12 // flash blue/white for the last ~2s as a warning it's ending
#define CHOMP_GHOST_RANDOM_PCT    25 // % chance a ghost ignores the chase heuristic and wanders, so it isn't perfectly inescapable
#define CHOMP_GHOST_STUN_TICKS    ((1000 + CHOMP_TICK_MS - 1) / CHOMP_TICK_MS) // ~1s frozen after being eaten, rounded up to a whole tick

#define CHOMP_SCORE_DOT           10
#define CHOMP_SCORE_PELLET        50
#define CHOMP_SCORE_GHOST         200

#define CHOMP_COLOUR_WALL         0x1E88E5U // classic maze blue
#define CHOMP_COLOUR_PLAYER       0xFDD835U // yellow
#define CHOMP_COLOUR_DOT          0xE0E0E0U // light grey
#define CHOMP_COLOUR_PELLET_HI    0xFFFFFFU
#define CHOMP_COLOUR_PELLET_LO    0x9E9E9EU
#define CHOMP_COLOUR_FRIGHT       0x2962FFU
#define CHOMP_COLOUR_FRIGHT_WARN  0xFFFFFFU
#define CHOMP_COLOUR_STUNNED      0x9E9E9EU // dull grey -- visibly distinct from both the normal and frightened colours
#define CHOMP_COLOUR_EYE          0xFFFFFFU

typedef enum
{
	CHOMP_DIR_UP = 0,
	CHOMP_DIR_DOWN,
	CHOMP_DIR_LEFT,
	CHOMP_DIR_RIGHT,
	CHOMP_DIR_NONE,
} chompDirection_t;

typedef enum
{
	CHOMP_CELL_WALL = 0,
	CHOMP_CELL_EMPTY,
	CHOMP_CELL_DOT,
	CHOMP_CELL_PELLET,
} chompCellType_t;

typedef enum
{
	CHOMP_STATE_PLAYING = 0,
	CHOMP_STATE_GAME_OVER,
	CHOMP_STATE_WIN,
} chompState_t;

typedef struct
{
	int8_t col;
	int8_t row;
	int8_t startCol;
	int8_t startRow;
	chompDirection_t lastDir;
	uint32_t colour;
	int stunTicksRemaining; // frozen in place after being eaten -- see CHOMP_GHOST_STUN_TICKS
} chompGhost_t;

static menuStatus_t menuGameChompExitCode = MENU_STATUS_SUCCESS;
static chompCellType_t maze[CHOMP_ROWS][CHOMP_COLS];
static int8_t playerCol, playerRow;
static int8_t playerStartCol, playerStartRow;
static chompDirection_t playerDir;
static chompDirection_t playerPendingDir;
static chompGhost_t ghosts[CHOMP_GHOST_COUNT];
static int dotsRemaining;
static int lives;
static int score;
static int frightenedTicksRemaining;
static chompState_t state;
static uint32_t lastTickTime;
static uint32_t rngState;
static bool pelletFlashOn;

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static void resetGame(void);
static void respawnAfterHit(void);
static void buildMaze(void);
static uint32_t nextRandom(void);
static bool isWall(int row, int col);
static void directionVector(chompDirection_t dir, int8_t *dCol, int8_t *dRow);
static void advancePlayer(void);
static void advanceGhosts(void);
static void checkGhostCollisions(void);
static void cellOrigin(int col, int row, int16_t *x, int16_t *y);

menuStatus_t menuGameChomp(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		lastTickTime = ev->time;
		rngState = ev->time;
		resetGame();
		updateScreen(true);
	}
	else
	{
		menuGameChompExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}

		if ((state == CHOMP_STATE_PLAYING) && ((ev->time - lastTickTime) > CHOMP_TICK_MS))
		{
			lastTickTime = ev->time;
			pelletFlashOn = !pelletFlashOn;

			if (frightenedTicksRemaining > 0)
			{
				frightenedTicksRemaining--;
			}

			advancePlayer();

			if (state == CHOMP_STATE_PLAYING)
			{
				checkGhostCollisions();
			}

			if (state == CHOMP_STATE_PLAYING)
			{
				advanceGhosts();
				checkGhostCollisions();
			}

			updateScreen(false);
		}
	}

	return menuGameChompExitCode;
}

static uint32_t nextRandom(void)
{
	// Same self-contained LCG as the other games here (menuGameSnake.c etc) instead of newlib's
	// rand()/srand() -- nothing else in this codebase calls them.
	rngState = ((rngState * 1103515245u) + 12345u);
	return ((rngState >> 16) & 0x7FFFu);
}

// The maze is generated, not hand-drawn, as a "comb": odd rows (1,3,5,7,9) are open horizontal
// corridors running the full width, separated by wall rows (2,4,6,8) that are solid except for a
// gap at the centre column (CHOMP_SPINE_COL) plus one extra side gap each. The centre-column gaps
// alone already touch every corridor row, so the whole maze is provably one connected piece by
// construction -- no dot can end up sealed in an unreachable pocket -- and the extra side gaps just
// add a couple of loops on top of that so it doesn't play as one single snaking corridor.
#define CHOMP_SPINE_COL  (CHOMP_COLS / 2)
static void buildMaze(void)
{
	dotsRemaining = 0;

	for (int row = 0; row < CHOMP_ROWS; row++)
	{
		for (int col = 0; col < CHOMP_COLS; col++)
		{
			bool isBorder = ((row == 0) || (row == (CHOMP_ROWS - 1)) || (col == 0) || (col == (CHOMP_COLS - 1)));
			bool isCorridorRow = ((row % 2) == 1);
			bool isOpen;

			if (isBorder)
			{
				isOpen = false;
			}
			else if (isCorridorRow)
			{
				isOpen = true;
			}
			else
			{
				// Wall row: open only at the centre spine column, plus one extra gap that
				// alternates side (row 2/6 gap near the left, row 4/8 gap near the right) to add
				// a loop rather than just a straight vertical spine.
				bool nearLeftGap = (((row % 4) == 2) && (col == 3));
				bool nearRightGap = (((row % 4) == 0) && (col == (CHOMP_COLS - 4)));

				isOpen = ((col == CHOMP_SPINE_COL) || nearLeftGap || nearRightGap);
			}

			if (isOpen)
			{
				maze[row][col] = CHOMP_CELL_DOT;
				dotsRemaining++;
			}
			else
			{
				maze[row][col] = CHOMP_CELL_WALL;
			}
		}
	}

	// Power pellets in the four corners of the maze -- row 1 / row (ROWS-2) are corridor rows, and
	// col 1 / col (COLS-2) are always open within them, so these are guaranteed valid regardless of
	// the wall-row gap placement above.
	maze[1][1] = CHOMP_CELL_PELLET;
	maze[1][CHOMP_COLS - 2] = CHOMP_CELL_PELLET;
	maze[CHOMP_ROWS - 2][1] = CHOMP_CELL_PELLET;
	maze[CHOMP_ROWS - 2][CHOMP_COLS - 2] = CHOMP_CELL_PELLET;

	// Player starts at the bottom, both ghosts start at the top, spread apart either side of the
	// spine -- previously everyone spawned within a cell or two of each other in the centre, so a
	// greedy ghost caught the player before the game had really begun.
	playerStartRow = (CHOMP_ROWS - 2);
	playerStartCol = CHOMP_SPINE_COL;
	maze[playerStartRow][playerStartCol] = CHOMP_CELL_EMPTY;

	ghosts[0].startRow = 1;
	ghosts[0].startCol = (CHOMP_SPINE_COL - 3);
	ghosts[1].startRow = 1;
	ghosts[1].startCol = (CHOMP_SPINE_COL + 3);
	maze[ghosts[0].startRow][ghosts[0].startCol] = CHOMP_CELL_EMPTY;
	maze[ghosts[1].startRow][ghosts[1].startCol] = CHOMP_CELL_EMPTY;

	dotsRemaining -= 3; // the three start cells above were counted as dots before being cleared
}

static void resetGame(void)
{
	buildMaze();

	playerCol = playerStartCol;
	playerRow = playerStartRow;
	playerDir = CHOMP_DIR_RIGHT;
	playerPendingDir = CHOMP_DIR_RIGHT;

	ghosts[0].colour = 0xE53935U; // red
	ghosts[1].colour = 0x00BCD4U; // cyan

	for (int i = 0; i < CHOMP_GHOST_COUNT; i++)
	{
		ghosts[i].col = ghosts[i].startCol;
		ghosts[i].row = ghosts[i].startRow;
		ghosts[i].lastDir = CHOMP_DIR_NONE;
		ghosts[i].stunTicksRemaining = 0;
	}

	lives = CHOMP_INITIAL_LIVES;
	score = 0;
	frightenedTicksRemaining = 0;
	pelletFlashOn = false;
	state = CHOMP_STATE_PLAYING;
}

// Repositions everyone back to their start cells after the player loses a life -- the maze/dots/
// score are untouched, only positions reset (no brief-invulnerability grace period; keeping this
// simple is judged an acceptable trade-off for a keypad radio game rather than a faithful arcade
// clone).
static void respawnAfterHit(void)
{
	playerCol = playerStartCol;
	playerRow = playerStartRow;
	playerDir = CHOMP_DIR_RIGHT;
	playerPendingDir = CHOMP_DIR_RIGHT;

	for (int i = 0; i < CHOMP_GHOST_COUNT; i++)
	{
		ghosts[i].col = ghosts[i].startCol;
		ghosts[i].row = ghosts[i].startRow;
		ghosts[i].lastDir = CHOMP_DIR_NONE;
		ghosts[i].stunTicksRemaining = 0;
	}

	frightenedTicksRemaining = 0;
}

static bool isWall(int row, int col)
{
	if ((row < 0) || (row >= CHOMP_ROWS) || (col < 0) || (col >= CHOMP_COLS))
	{
		return true;
	}

	return (maze[row][col] == CHOMP_CELL_WALL);
}

static void directionVector(chompDirection_t dir, int8_t *dCol, int8_t *dRow)
{
	switch (dir)
	{
		case CHOMP_DIR_UP:
			*dCol = 0; *dRow = -1;
			break;
		case CHOMP_DIR_DOWN:
			*dCol = 0; *dRow = 1;
			break;
		case CHOMP_DIR_LEFT:
			*dCol = -1; *dRow = 0;
			break;
		case CHOMP_DIR_RIGHT:
			*dCol = 1; *dRow = 0;
			break;
		case CHOMP_DIR_NONE:
		default:
			*dCol = 0; *dRow = 0;
			break;
	}
}

static chompDirection_t oppositeDirection(chompDirection_t dir)
{
	static const chompDirection_t opposite[4] = { CHOMP_DIR_DOWN, CHOMP_DIR_UP, CHOMP_DIR_RIGHT, CHOMP_DIR_LEFT };

	if (dir == CHOMP_DIR_NONE)
	{
		return CHOMP_DIR_NONE;
	}

	return opposite[dir];
}

static void advancePlayer(void)
{
	int8_t dCol, dRow;
	chompDirection_t moveDir = CHOMP_DIR_NONE;

	// A queued turn (set by the last keypress) is taken as soon as it's physically possible, not
	// only when the current direction is fully blocked -- this is what makes cornering feel
	// responsive rather than requiring you to stop dead at every junction.
	directionVector(playerPendingDir, &dCol, &dRow);

	if (isWall((playerRow + dRow), (playerCol + dCol)) == false)
	{
		moveDir = playerPendingDir;
	}
	else
	{
		directionVector(playerDir, &dCol, &dRow);

		if (isWall((playerRow + dRow), (playerCol + dCol)) == false)
		{
			moveDir = playerDir;
		}
	}

	if (moveDir == CHOMP_DIR_NONE)
	{
		return; // boxed in on all queued/current directions this tick -- stay put
	}

	directionVector(moveDir, &dCol, &dRow);
	playerDir = moveDir;
	playerCol = (int8_t)(playerCol + dCol);
	playerRow = (int8_t)(playerRow + dRow);

	chompCellType_t *cell = &maze[playerRow][playerCol];

	if (*cell == CHOMP_CELL_DOT)
	{
		*cell = CHOMP_CELL_EMPTY;
		score += CHOMP_SCORE_DOT;
		dotsRemaining--;
	}
	else if (*cell == CHOMP_CELL_PELLET)
	{
		*cell = CHOMP_CELL_EMPTY;
		score += CHOMP_SCORE_PELLET;
		dotsRemaining--;
		frightenedTicksRemaining = CHOMP_FRIGHTENED_TICKS;
	}

	if (dotsRemaining <= 0)
	{
		state = CHOMP_STATE_WIN;
	}
}

static void advanceGhosts(void)
{
	static const chompDirection_t allDirs[4] = { CHOMP_DIR_UP, CHOMP_DIR_DOWN, CHOMP_DIR_LEFT, CHOMP_DIR_RIGHT };
	bool frightened = (frightenedTicksRemaining > 0);

	for (int i = 0; i < CHOMP_GHOST_COUNT; i++)
	{
		chompGhost_t *g = &ghosts[i];

		if (g->stunTicksRemaining > 0)
		{
			g->stunTicksRemaining--;
			continue; // frozen in place after being eaten -- see CHOMP_GHOST_STUN_TICKS
		}

		chompDirection_t validDirs[4];
		int validCount = 0;
		chompDirection_t reverse = oppositeDirection(g->lastDir);

		for (int d = 0; d < 4; d++)
		{
			int8_t dCol, dRow;

			directionVector(allDirs[d], &dCol, &dRow);

			if ((allDirs[d] != reverse) && (isWall((g->row + dRow), (g->col + dCol)) == false))
			{
				validDirs[validCount++] = allDirs[d];
			}
		}

		// No-U-turn rule above can legitimately leave zero options only at a true dead end, which
		// this generated maze doesn't have -- but falling back to "allow reversal" keeps the ghost
		// from ever getting permanently stuck if that assumption is ever wrong.
		if (validCount == 0)
		{
			for (int d = 0; d < 4; d++)
			{
				int8_t dCol, dRow;

				directionVector(allDirs[d], &dCol, &dRow);

				if (isWall((g->row + dRow), (g->col + dCol)) == false)
				{
					validDirs[validCount++] = allDirs[d];
				}
			}
		}

		if (validCount == 0)
		{
			continue; // genuinely walled in this tick -- shouldn't happen, but don't crash if it does
		}

		chompDirection_t chosen;

		if (frightened || ((nextRandom() % 100) < CHOMP_GHOST_RANDOM_PCT))
		{
			// Frightened: flee-ish (random, not actively chasing). Also rolled a fraction of the
			// time even while hunting, so the ghost isn't a perfect, inescapable distance-minimiser.
			chosen = validDirs[nextRandom() % validCount];
		}
		else
		{
			// Greedy chase: among the still-valid directions, the one that moves the ghost's
			// Manhattan distance to the player down the most. Ties keep the first (fixed
			// up/down/left/right order) rather than breaking randomly -- deterministic enough
			// alongside the random-wander roll above that it doesn't need its own randomness too.
			int bestDist = 0x7FFF;
			chosen = validDirs[0];

			for (int d = 0; d < validCount; d++)
			{
				int8_t dCol, dRow;

				directionVector(validDirs[d], &dCol, &dRow);

				int candCol = (g->col + dCol);
				int candRow = (g->row + dRow);
				int dist = (abs(candCol - playerCol) + abs(candRow - playerRow));

				if (dist < bestDist)
				{
					bestDist = dist;
					chosen = validDirs[d];
				}
			}
		}

		int8_t dCol, dRow;

		directionVector(chosen, &dCol, &dRow);
		g->col = (int8_t)(g->col + dCol);
		g->row = (int8_t)(g->row + dRow);
		g->lastDir = chosen;
	}
}

static void checkGhostCollisions(void)
{
	bool frightened = (frightenedTicksRemaining > 0);

	for (int i = 0; i < CHOMP_GHOST_COUNT; i++)
	{
		if ((ghosts[i].col != playerCol) || (ghosts[i].row != playerRow))
		{
			continue;
		}

		if (frightened)
		{
			ghosts[i].col = ghosts[i].startCol;
			ghosts[i].row = ghosts[i].startRow;
			ghosts[i].lastDir = CHOMP_DIR_NONE;
			ghosts[i].stunTicksRemaining = CHOMP_GHOST_STUN_TICKS;
			score += CHOMP_SCORE_GHOST;
		}
		else
		{
			lives--;

			if (lives <= 0)
			{
				state = CHOMP_STATE_GAME_OVER;
			}
			else
			{
				respawnAfterHit();
			}

			return; // positions just got reset -- no point checking the other ghost against them
		}
	}
}

static void cellOrigin(int col, int row, int16_t *x, int16_t *y)
{
	*x = (int16_t)(CHOMP_MARGIN_X + (col * CHOMP_CELL_SIZE));
	*y = (int16_t)(CHOMP_PLAY_TOP + (row * CHOMP_CELL_SIZE));
}

static void updateScreen(bool isFirstRun)
{
	char buffer[24];
	uint16_t savedFg, savedBg;

	if (isFirstRun)
	{
		displayClearBuf();
		menuDisplayTitle("Chomp");
	}

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayFillRect(0, MENU_HEADER_HEIGHT, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - MENU_HEADER_HEIGHT), true); // clear playfield to background
	displayThemeResetToDefault();

	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(CHOMP_COLOUR_WALL), savedBg);

	for (int row = 0; row < CHOMP_ROWS; row++)
	{
		for (int col = 0; col < CHOMP_COLS; col++)
		{
			if (maze[row][col] != CHOMP_CELL_WALL)
			{
				continue;
			}

			int16_t x, y;

			cellOrigin(col, row, &x, &y);
			// displayFillRect() called directly is the "true=background" polarity (see
			// spectrumDrawMeter()/menuGameBreakout.c) -- false is what actually gets the foreground colour.
			displayFillRect(x, y, CHOMP_CELL_SIZE, CHOMP_CELL_SIZE, false);
		}
	}

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(CHOMP_COLOUR_DOT), savedBg);

	for (int row = 0; row < CHOMP_ROWS; row++)
	{
		for (int col = 0; col < CHOMP_COLS; col++)
		{
			if (maze[row][col] != CHOMP_CELL_DOT)
			{
				continue;
			}

			int16_t x, y;

			cellOrigin(col, row, &x, &y);
			displaySetPixel((int16_t)(x + (CHOMP_CELL_SIZE / 2)), (int16_t)(y + (CHOMP_CELL_SIZE / 2)), true);
		}
	}

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(pelletFlashOn ? CHOMP_COLOUR_PELLET_HI : CHOMP_COLOUR_PELLET_LO), savedBg);

	for (int row = 0; row < CHOMP_ROWS; row++)
	{
		for (int col = 0; col < CHOMP_COLS; col++)
		{
			if (maze[row][col] != CHOMP_CELL_PELLET)
			{
				continue;
			}

			int16_t x, y;

			cellOrigin(col, row, &x, &y);
			displayFillCircle((int16_t)(x + (CHOMP_CELL_SIZE / 2)), (int16_t)(y + (CHOMP_CELL_SIZE / 2)), 2, true);
		}
	}

	// Ghosts -- filled circle body, colour swapped to blue (flickering white/blue near the end of
	// the timer, as a warning) while frightened, plus a couple of white "eye" pixels.
	for (int i = 0; i < CHOMP_GHOST_COUNT; i++)
	{
		int16_t x, y, cx, cy;
		uint32_t bodyColour = ghosts[i].colour;

		if (ghosts[i].stunTicksRemaining > 0)
		{
			// Stunned takes priority over frightened -- a just-eaten ghost can respawn while the
			// pellet timer is still running, and it needs to read as "can't move" rather than
			// "still vulnerable", since it's neither right now.
			bodyColour = CHOMP_COLOUR_STUNNED;
		}
		else if (frightenedTicksRemaining > 0)
		{
			bool warnFlicker = ((frightenedTicksRemaining < CHOMP_FRIGHT_FLICKER_AT) && (((frightenedTicksRemaining / 2) % 2) == 0));
			bodyColour = (warnFlicker ? CHOMP_COLOUR_FRIGHT_WARN : CHOMP_COLOUR_FRIGHT);
		}

		cellOrigin(ghosts[i].col, ghosts[i].row, &x, &y);
		cx = (int16_t)(x + (CHOMP_CELL_SIZE / 2));
		cy = (int16_t)(y + (CHOMP_CELL_SIZE / 2));

		displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(bodyColour), savedBg);
		displayFillCircle(cx, cy, 3, true);

		displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(CHOMP_COLOUR_EYE), savedBg);
		displaySetPixel((int16_t)(cx - 1), (int16_t)(cy - 1), true);
		displaySetPixel((int16_t)(cx + 1), (int16_t)(cy - 1), true);
	}

	// Player -- filled circle with a wedge cut out of it (drawn in the background colour) facing
	// the direction of travel, for the classic open-mouth look.
	{
		int16_t x, y, cx, cy;

		cellOrigin(playerCol, playerRow, &x, &y);
		cx = (int16_t)(x + (CHOMP_CELL_SIZE / 2));
		cy = (int16_t)(y + (CHOMP_CELL_SIZE / 2));

		displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(CHOMP_COLOUR_PLAYER), savedBg);
		displayFillCircle(cx, cy, 3, true);

		displaySetForegroundAndBackgroundColours(savedFg, savedBg);

		switch (playerDir)
		{
			case CHOMP_DIR_RIGHT:
				displayFillTriangle(cx, cy, (int16_t)(cx + 4), (int16_t)(cy - 3), (int16_t)(cx + 4), (int16_t)(cy + 3), true);
				break;
			case CHOMP_DIR_LEFT:
				displayFillTriangle(cx, cy, (int16_t)(cx - 4), (int16_t)(cy - 3), (int16_t)(cx - 4), (int16_t)(cy + 3), true);
				break;
			case CHOMP_DIR_UP:
				displayFillTriangle(cx, cy, (int16_t)(cx - 3), (int16_t)(cy - 4), (int16_t)(cx + 3), (int16_t)(cy - 4), true);
				break;
			case CHOMP_DIR_DOWN:
			default:
				displayFillTriangle(cx, cy, (int16_t)(cx - 3), (int16_t)(cy + 4), (int16_t)(cx + 3), (int16_t)(cy + 4), true);
				break;
		}
	}

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);

	snprintf(buffer, sizeof(buffer), "Score:%d", score);
	displayPrintAt(4, (MENU_HEADER_HEIGHT + 1), buffer, FONT_SIZE_1);
	snprintf(buffer, sizeof(buffer), "Lives:%d", lives);
	displayPrintAt((DISPLAY_SIZE_X - 4 - 42), (MENU_HEADER_HEIGHT + 1), buffer, FONT_SIZE_1); // "Lives:N" is ~42px at FONT_SIZE_1

	if (state == CHOMP_STATE_GAME_OVER)
	{
		snprintf(buffer, sizeof(buffer), "Score: %d", score);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) - 8), "GAME OVER", FONT_SIZE_2);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) + 6), buffer, FONT_SIZE_1);
	}
	else if (state == CHOMP_STATE_WIN)
	{
		snprintf(buffer, sizeof(buffer), "Score: %d", score);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) - 8), "YOU WIN", FONT_SIZE_2);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) + 6), buffer, FONT_SIZE_1);
	}

	displayRender();
}

static void handleEvent(uiEvent_t *ev)
{
	if (ev->events & FUNCTION_EVENT)
	{
		if (ev->function == FUNC_REDRAW)
		{
			updateScreen(false);
		}
		return;
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_RED) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
	{
		menuSystemPopPreviousMenu();
		return;
	}

	if (state != CHOMP_STATE_PLAYING)
	{
		return; // Only the exit keys do anything once the game has ended.
	}

	// Unlike Snake, reversing is always allowed -- there's no tail to double back into, and being
	// able to reverse away from a ghost is a normal, expected move in this genre.
	if (KEYCHECK_PRESS(ev->keys, KEY_2))
	{
		playerPendingDir = CHOMP_DIR_UP;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_8))
	{
		playerPendingDir = CHOMP_DIR_DOWN;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_4))
	{
		playerPendingDir = CHOMP_DIR_LEFT;
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_6))
	{
		playerPendingDir = CHOMP_DIR_RIGHT;
	}
}
