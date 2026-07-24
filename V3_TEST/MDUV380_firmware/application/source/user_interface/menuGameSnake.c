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
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"

#define SNAKE_CELL_SIZE       4
#define SNAKE_PLAYFIELD_Y     MENU_HEADER_HEIGHT
#define SNAKE_COLS            (DISPLAY_SIZE_X / SNAKE_CELL_SIZE)
#define SNAKE_ROWS            ((DISPLAY_SIZE_Y - SNAKE_PLAYFIELD_Y) / SNAKE_CELL_SIZE)
#define SNAKE_MAX_LENGTH      (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_TICK_MS         200
#define SNAKE_INITIAL_LENGTH  3

typedef enum
{
	SNAKE_DIR_UP = 0,
	SNAKE_DIR_DOWN,
	SNAKE_DIR_LEFT,
	SNAKE_DIR_RIGHT
} snakeDirection_t;

typedef struct
{
	uint8_t col;
	uint8_t row;
} snakeSegment_t;

static menuStatus_t menuGameSnakeExitCode = MENU_STATUS_SUCCESS;
static snakeSegment_t segments[SNAKE_MAX_LENGTH];
static int snakeLength;
static snakeDirection_t currentDirection;
static snakeDirection_t pendingDirection;
static snakeSegment_t food;
static bool gameOver;
static uint32_t lastMoveTime;
static uint32_t rngState;

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static void resetGame(void);
static void placeFood(void);
static void advanceSnake(void);
static void setDirection(snakeDirection_t newDirection);
static uint32_t nextRandom(void);
static bool positionOccupiesSegment(uint8_t col, uint8_t row, int count);
static void drawCell(uint8_t col, uint8_t row, bool erase);
static void drawFood(void);

menuStatus_t menuGameSnake(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		lastMoveTime = ev->time;
		resetGame();
		updateScreen(true);
	}
	else
	{
		menuGameSnakeExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}

		if ((gameOver == false) && ((ev->time - lastMoveTime) > SNAKE_TICK_MS))
		{
			lastMoveTime = ev->time;
			advanceSnake();
			updateScreen(false);
		}
	}

	return menuGameSnakeExitCode;
}

static void resetGame(void)
{
	snakeLength = SNAKE_INITIAL_LENGTH;

	segments[0].col = (SNAKE_COLS / 2);
	segments[0].row = (SNAKE_ROWS / 2);

	for (int i = 1; i < SNAKE_INITIAL_LENGTH; i++)
	{
		segments[i].col = (uint8_t)(segments[0].col - i); // trail to the left of the head
		segments[i].row = segments[0].row;
	}

	currentDirection = SNAKE_DIR_RIGHT;
	pendingDirection = SNAKE_DIR_RIGHT;
	gameOver = false;

	rngState = ticksGetMillis();
	placeFood();
}

static uint32_t nextRandom(void)
{
	// A small self-contained LCG instead of newlib's rand()/srand() -- nothing else in this
	// codebase calls them, and this sidesteps depending on reentrancy plumbing we haven't verified.
	rngState = ((rngState * 1103515245u) + 12345u);
	return ((rngState >> 16) & 0x7FFFu);
}

static bool positionOccupiesSegment(uint8_t col, uint8_t row, int count)
{
	for (int i = 0; i < count; i++)
	{
		if ((segments[i].col == col) && (segments[i].row == row))
		{
			return true;
		}
	}

	return false;
}

static void placeFood(void)
{
	// Bounded: an unlucky RNG run could otherwise spin forever landing on the snake. Falls back
	// to the first free cell found by linear scan, which always terminates.
	for (int attempt = 0; attempt < 200; attempt++)
	{
		uint8_t col = (uint8_t)(nextRandom() % SNAKE_COLS);
		uint8_t row = (uint8_t)(nextRandom() % SNAKE_ROWS);

		if (positionOccupiesSegment(col, row, snakeLength) == false)
		{
			food.col = col;
			food.row = row;
			return;
		}
	}

	for (uint8_t row = 0; row < SNAKE_ROWS; row++)
	{
		for (uint8_t col = 0; col < SNAKE_COLS; col++)
		{
			if (positionOccupiesSegment(col, row, snakeLength) == false)
			{
				food.col = col;
				food.row = row;
				return;
			}
		}
	}
}

static void setDirection(snakeDirection_t newDirection)
{
	static const snakeDirection_t opposite[4] = { SNAKE_DIR_DOWN, SNAKE_DIR_UP, SNAKE_DIR_RIGHT, SNAKE_DIR_LEFT };

	// Reject a direct reversal (checked against the direction actually being moved in, not
	// merely requested, so a quick double-tap can't sneak the snake into itself in one tick).
	if (newDirection != opposite[currentDirection])
	{
		pendingDirection = newDirection;
	}
}

static void advanceSnake(void)
{
	snakeSegment_t newHead;
	bool ateFood;
	int collisionCheckCount;

	currentDirection = pendingDirection;
	newHead = segments[0];

	switch (currentDirection)
	{
		case SNAKE_DIR_UP:
			newHead.row--;
			break;
		case SNAKE_DIR_DOWN:
			newHead.row++;
			break;
		case SNAKE_DIR_LEFT:
			newHead.col--;
			break;
		case SNAKE_DIR_RIGHT:
			newHead.col++;
			break;
	}

	ateFood = ((newHead.col == food.col) && (newHead.row == food.row));
	// The tail cell vacates on this same move (unless the snake is growing), so it must not
	// be treated as a collision -- otherwise the snake could never turn back along its own tail.
	collisionCheckCount = (ateFood ? snakeLength : (snakeLength - 1));

	if ((newHead.col >= SNAKE_COLS) || (newHead.row >= SNAKE_ROWS) ||
			positionOccupiesSegment(newHead.col, newHead.row, collisionCheckCount))
	{
		gameOver = true;
		return;
	}

	for (int i = snakeLength; i > 0; i--)
	{
		segments[i] = segments[i - 1];
	}
	segments[0] = newHead;

	if (ateFood)
	{
		if (snakeLength < SNAKE_MAX_LENGTH)
		{
			snakeLength++;
		}

		if (snakeLength < SNAKE_MAX_LENGTH)
		{
			placeFood();
		}
		else
		{
			gameOver = true; // Board filled -- win.
		}
	}
}

static void drawCell(uint8_t col, uint8_t row, bool erase)
{
	int16_t x = (int16_t)(col * SNAKE_CELL_SIZE);
	int16_t y = (int16_t)(SNAKE_PLAYFIELD_Y + (row * SNAKE_CELL_SIZE));

	// displayFillRect() called directly (not via the VLine/HLine wrappers) uses the opposite
	// isInverted convention to every other primitive: true -> background, false -> foreground.
	displayFillRect(x, y, SNAKE_CELL_SIZE, SNAKE_CELL_SIZE, erase);
}

static void drawFood(void)
{
	uint16_t savedFg, savedBg;

	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xD32F2FU), savedBg); // red, matches the icon palette

	drawCell(food.col, food.row, false);

	displaySetForegroundAndBackgroundColours(savedFg, savedBg);
}

static void updateScreen(bool isFirstRun)
{
	if (isFirstRun)
	{
		displayClearBuf();
		menuDisplayTitle("Snake");
	}

	displayThemeApply(THEME_ITEM_FG_DECORATION, THEME_ITEM_BG);
	displayFillRect(0, SNAKE_PLAYFIELD_Y, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - SNAKE_PLAYFIELD_Y), true); // clear playfield to background

	for (int i = 0; i < snakeLength; i++)
	{
		drawCell(segments[i].col, segments[i].row, false);
	}

	drawFood();

	if (gameOver)
	{
		char buffer[24];

		snprintf(buffer, sizeof(buffer), "Score: %d", (snakeLength - SNAKE_INITIAL_LENGTH));
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) - 8), "GAME OVER", FONT_SIZE_2);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) + 6), buffer, FONT_SIZE_1);
	}

	displayThemeResetToDefault();
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

	if (gameOver)
	{
		return; // Only the exit keys do anything once the game has ended.
	}

	if (KEYCHECK_PRESS(ev->keys, KEY_2))
	{
		setDirection(SNAKE_DIR_UP);
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_8))
	{
		setDirection(SNAKE_DIR_DOWN);
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_4))
	{
		setDirection(SNAKE_DIR_LEFT);
	}
	else if (KEYCHECK_PRESS(ev->keys, KEY_6))
	{
		setDirection(SNAKE_DIR_RIGHT);
	}
}
