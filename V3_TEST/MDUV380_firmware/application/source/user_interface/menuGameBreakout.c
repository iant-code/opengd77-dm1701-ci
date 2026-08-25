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

#define BREAKOUT_TICK_MS          30
#define BREAKOUT_HUD_HEIGHT       9
#define BREAKOUT_MARGIN           4
#define BREAKOUT_PLAY_TOP         (MENU_HEADER_HEIGHT + BREAKOUT_HUD_HEIGHT + 2)
#define BREAKOUT_PLAY_LEFT        0
#define BREAKOUT_PLAY_RIGHT       DISPLAY_SIZE_X

#define BREAKOUT_BRICK_COLS       8
#define BREAKOUT_BRICK_ROWS       5
#define BREAKOUT_BRICK_WIDTH      18
#define BREAKOUT_BRICK_HEIGHT     6
#define BREAKOUT_BRICK_GAP_X      2
#define BREAKOUT_BRICK_GAP_Y      3
// Centred rather than flush-left: the row width doesn't divide the 160px screen evenly, so this
// spreads the 2px leftover as an equal margin on both sides instead of an odd gap on the right.
#define BREAKOUT_BRICK_AREA_WIDTH ((BREAKOUT_BRICK_COLS * BREAKOUT_BRICK_WIDTH) + ((BREAKOUT_BRICK_COLS - 1) * BREAKOUT_BRICK_GAP_X))
#define BREAKOUT_BRICK_LEFT       ((DISPLAY_SIZE_X - BREAKOUT_BRICK_AREA_WIDTH) / 2)
#define BREAKOUT_BRICK_TOP        (BREAKOUT_PLAY_TOP + 2)

#define BREAKOUT_PADDLE_WIDTH     26
#define BREAKOUT_PADDLE_HEIGHT    4
#define BREAKOUT_PADDLE_Y         (DISPLAY_SIZE_Y - 8)
#define BREAKOUT_PADDLE_STEP      4
#define BREAKOUT_PADDLE_MIN_X     BREAKOUT_PLAY_LEFT
#define BREAKOUT_PADDLE_MAX_X     (BREAKOUT_PLAY_RIGHT - BREAKOUT_PADDLE_WIDTH)

#define BREAKOUT_BALL_SIZE        3
#define BREAKOUT_BALL_SPEED       2 // constant vertical speed magnitude; horizontal component varies, see launchBall()/paddle steering below

#define BREAKOUT_INITIAL_LIVES    3

#define BREAKOUT_COLOUR_PADDLE    0xE0E0E0U // light grey
#define BREAKOUT_COLOUR_BALL      0xFFFFFFU // white

typedef enum
{
	BREAKOUT_STATE_READY = 0,  // ball resting on the paddle, waiting for KEY_5 to launch
	BREAKOUT_STATE_PLAYING,
	BREAKOUT_STATE_GAME_OVER,
	BREAKOUT_STATE_WIN,
} breakoutState_t;

static menuStatus_t menuGameBreakoutExitCode = MENU_STATUS_SUCCESS;
static bool brickAlive[BREAKOUT_BRICK_ROWS][BREAKOUT_BRICK_COLS];
static int16_t paddleX;
static int16_t ballX, ballY;
static int8_t ballDx, ballDy;
static bool moveLeftHeld;
static bool moveRightHeld;
static breakoutState_t state;
static int lives;
static int score;
static uint32_t lastTickTime;
static uint32_t rngState;

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static void resetGame(void);
static void resetBall(void);
static void launchBall(void);
static void movePaddle(void);
static void advanceBall(void);
static uint32_t nextRandom(void);

menuStatus_t menuGameBreakout(uiEvent_t *ev, bool isFirstRun)
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
		menuGameBreakoutExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}

		if (((state == BREAKOUT_STATE_PLAYING) || (state == BREAKOUT_STATE_READY)) &&
				((ev->time - lastTickTime) > BREAKOUT_TICK_MS))
		{
			lastTickTime = ev->time;
			movePaddle();

			if (state == BREAKOUT_STATE_READY)
			{
				// Ball rides the paddle until launched, same as the classic arcade original.
				ballX = (int16_t)(paddleX + (BREAKOUT_PADDLE_WIDTH / 2) - (BREAKOUT_BALL_SIZE / 2));
			}
			else
			{
				advanceBall();
			}

			updateScreen(false);
		}
	}

	return menuGameBreakoutExitCode;
}

static void resetGame(void)
{
	for (int row = 0; row < BREAKOUT_BRICK_ROWS; row++)
	{
		for (int col = 0; col < BREAKOUT_BRICK_COLS; col++)
		{
			brickAlive[row][col] = true;
		}
	}

	paddleX = (int16_t)((DISPLAY_SIZE_X - BREAKOUT_PADDLE_WIDTH) / 2);
	moveLeftHeld = false;
	moveRightHeld = false;
	lives = BREAKOUT_INITIAL_LIVES;
	score = 0;

	resetBall();
}

static void resetBall(void)
{
	ballDx = 0;
	ballDy = 0;
	ballY = (int16_t)(BREAKOUT_PADDLE_Y - BREAKOUT_BALL_SIZE);
	ballX = (int16_t)(paddleX + (BREAKOUT_PADDLE_WIDTH / 2) - (BREAKOUT_BALL_SIZE / 2));
	state = BREAKOUT_STATE_READY;
}

static uint32_t nextRandom(void)
{
	// A small self-contained LCG instead of newlib's rand()/srand() -- see menuGameSnake.c, same
	// rationale (nothing else in this codebase calls them, sidesteps unverified reentrancy plumbing).
	rngState = ((rngState * 1103515245u) + 12345u);
	return ((rngState >> 16) & 0x7FFFu);
}

static void launchBall(void)
{
	static const int8_t launchDx[4] = { -2, -1, 1, 2 };

	ballDy = -BREAKOUT_BALL_SPEED;
	ballDx = launchDx[nextRandom() % 4];
	state = BREAKOUT_STATE_PLAYING;
}

static void movePaddle(void)
{
	if (moveLeftHeld && (paddleX > BREAKOUT_PADDLE_MIN_X))
	{
		paddleX = (int16_t)(paddleX - BREAKOUT_PADDLE_STEP);

		if (paddleX < BREAKOUT_PADDLE_MIN_X)
		{
			paddleX = BREAKOUT_PADDLE_MIN_X;
		}
	}

	if (moveRightHeld && (paddleX < BREAKOUT_PADDLE_MAX_X))
	{
		paddleX = (int16_t)(paddleX + BREAKOUT_PADDLE_STEP);

		if (paddleX > BREAKOUT_PADDLE_MAX_X)
		{
			paddleX = BREAKOUT_PADDLE_MAX_X;
		}
	}
}

static void advanceBall(void)
{
	int16_t newX = (int16_t)(ballX + ballDx);
	int16_t newY = (int16_t)(ballY + ballDy);

	if (newX <= BREAKOUT_PLAY_LEFT)
	{
		newX = BREAKOUT_PLAY_LEFT;
		ballDx = (int8_t)(-ballDx);
	}
	else if ((newX + BREAKOUT_BALL_SIZE) >= BREAKOUT_PLAY_RIGHT)
	{
		newX = (int16_t)(BREAKOUT_PLAY_RIGHT - BREAKOUT_BALL_SIZE);
		ballDx = (int8_t)(-ballDx);
	}

	if (newY <= BREAKOUT_PLAY_TOP)
	{
		newY = BREAKOUT_PLAY_TOP;
		ballDy = (int8_t)(-ballDy);
	}

	// Paddle: only tested while the ball is moving down and about to cross the paddle's Y band,
	// so a ball already past it (missed) can't get caught retroactively on some later tick.
	if ((ballDy > 0) && ((newY + BREAKOUT_BALL_SIZE) >= BREAKOUT_PADDLE_Y) &&
			((newY + BREAKOUT_BALL_SIZE) <= (BREAKOUT_PADDLE_Y + BREAKOUT_PADDLE_HEIGHT + BREAKOUT_BALL_SPEED)) &&
			((newX + BREAKOUT_BALL_SIZE) >= paddleX) && (newX <= (paddleX + BREAKOUT_PADDLE_WIDTH)))
	{
		int16_t ballCentreX = (int16_t)(newX + (BREAKOUT_BALL_SIZE / 2));
		int16_t paddleCentreX = (int16_t)(paddleX + (BREAKOUT_PADDLE_WIDTH / 2));
		int16_t offset = (int16_t)(ballCentreX - paddleCentreX); // roughly -13..+13 across the paddle face

		newY = (int16_t)(BREAKOUT_PADDLE_Y - BREAKOUT_BALL_SIZE);
		ballDy = (int8_t)(-BREAKOUT_BALL_SPEED);
		// Where the ball hits the paddle steers the rebound angle, like the arcade original --
		// centre hit goes straight back up, an edge hit sends it off at a sharper angle.
		ballDx = (int8_t)CLAMP((offset / 5), -2, 2);

		if (ballDx == 0)
		{
			// A dead-centre hit would otherwise bounce the ball straight up/down forever between
			// the paddle and a wall -- force a slight angle so a rally always eventually ends.
			ballDx = ((nextRandom() & 1) ? 1 : -1);
		}
	}

	// Bricks: AABB test against the ball's post-move box, one hit per tick (the ball is small and
	// slow enough relative to a brick that skipping straight through without registering isn't a
	// practical concern at this tick rate).
	for (int row = 0; row < BREAKOUT_BRICK_ROWS; row++)
	{
		bool hitThisRow = false;

		for (int col = 0; col < BREAKOUT_BRICK_COLS; col++)
		{
			if (brickAlive[row][col] == false)
			{
				continue;
			}

			int16_t brickX = (int16_t)(BREAKOUT_BRICK_LEFT + (col * (BREAKOUT_BRICK_WIDTH + BREAKOUT_BRICK_GAP_X)));
			int16_t brickY = (int16_t)(BREAKOUT_BRICK_TOP + (row * (BREAKOUT_BRICK_HEIGHT + BREAKOUT_BRICK_GAP_Y)));

			if (((newX + BREAKOUT_BALL_SIZE) > brickX) && (newX < (brickX + BREAKOUT_BRICK_WIDTH)) &&
					((newY + BREAKOUT_BALL_SIZE) > brickY) && (newY < (brickY + BREAKOUT_BRICK_HEIGHT)))
			{
				brickAlive[row][col] = false;
				score += ((BREAKOUT_BRICK_ROWS - row) * 10);
				ballDy = (int8_t)(-ballDy);
				hitThisRow = true;
				break;
			}
		}

		if (hitThisRow)
		{
			break;
		}
	}

	if (newY > DISPLAY_SIZE_Y)
	{
		lives--;

		if (lives <= 0)
		{
			state = BREAKOUT_STATE_GAME_OVER;
		}
		else
		{
			resetBall();
		}

		return;
	}

	ballX = newX;
	ballY = newY;

	bool anyBricksLeft = false;

	for (int row = 0; (row < BREAKOUT_BRICK_ROWS) && (anyBricksLeft == false); row++)
	{
		for (int col = 0; col < BREAKOUT_BRICK_COLS; col++)
		{
			if (brickAlive[row][col])
			{
				anyBricksLeft = true;
				break;
			}
		}
	}

	if (anyBricksLeft == false)
	{
		state = BREAKOUT_STATE_WIN;
	}
}

static void updateScreen(bool isFirstRun)
{
	char buffer[24];
	uint16_t savedFg, savedBg;
	// Every two rows share a colour (row/2) rather than one colour per row -- red top pair, yellow
	// middle pair, green for the final unpaired row (5 rows, odd count).
	static const uint32_t pairColours[] = { 0xE53935U, 0xFDD835U, 0x43A047U }; // red/yellow/green, top to bottom

	if (isFirstRun)
	{
		displayClearBuf();
		menuDisplayTitle("Breakout");
	}

	// Fixed black playfield regardless of the day/night UI theme -- the paddle/ball colours below
	// are tuned against a dark backdrop (near-white/light-grey) and wash out to invisible against
	// the day theme's light background otherwise. savedBg is captured straight from this, so every
	// sprite colour below stays correctly paired with it.
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x000000U), displayConvertRGB888ToNative(0x000000U));
	displayFillRect(0, MENU_HEADER_HEIGHT, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - MENU_HEADER_HEIGHT), true); // clear playfield to black

	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);

	for (int row = 0; row < BREAKOUT_BRICK_ROWS; row++)
	{
		displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(pairColours[row / 2]), savedBg);

		for (int col = 0; col < BREAKOUT_BRICK_COLS; col++)
		{
			if (brickAlive[row][col] == false)
			{
				continue;
			}

			int16_t brickX = (int16_t)(BREAKOUT_BRICK_LEFT + (col * (BREAKOUT_BRICK_WIDTH + BREAKOUT_BRICK_GAP_X)));
			int16_t brickY = (int16_t)(BREAKOUT_BRICK_TOP + (row * (BREAKOUT_BRICK_HEIGHT + BREAKOUT_BRICK_GAP_Y)));

			// displayFillRect() called directly is the "true=background" polarity (see
			// spectrumDrawMeter()/menuIcons.c) -- false here is what actually gets the foreground colour.
			displayFillRect(brickX, brickY, BREAKOUT_BRICK_WIDTH, BREAKOUT_BRICK_HEIGHT, false);
		}
	}

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(BREAKOUT_COLOUR_PADDLE), savedBg);
	displayFillRoundRect(paddleX, BREAKOUT_PADDLE_Y, BREAKOUT_PADDLE_WIDTH, BREAKOUT_PADDLE_HEIGHT, 1, true);

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(BREAKOUT_COLOUR_BALL), savedBg);
	displayFillCircle((int16_t)(ballX + (BREAKOUT_BALL_SIZE / 2)), (int16_t)(ballY + (BREAKOUT_BALL_SIZE / 2)), (BREAKOUT_BALL_SIZE / 2), true);

	// Explicit white-on-black (not savedFg/savedBg, both now black) -- this HUD text has to stay
	// legible against the fixed black playfield above, regardless of theme.
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFFFFFU), displayConvertRGB888ToNative(0x000000U));

	snprintf(buffer, sizeof(buffer), "Score:%d", score);
	displayPrintAt(BREAKOUT_MARGIN, (MENU_HEADER_HEIGHT + 1), buffer, FONT_SIZE_1);
	snprintf(buffer, sizeof(buffer), "Lives:%d", lives);
	displayPrintAt((DISPLAY_SIZE_X - BREAKOUT_MARGIN - 42), (MENU_HEADER_HEIGHT + 1), buffer, FONT_SIZE_1); // "Lives:N" is ~42px at FONT_SIZE_1

	if (state == BREAKOUT_STATE_GAME_OVER)
	{
		snprintf(buffer, sizeof(buffer), "Score: %d", score);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) - 8), "GAME OVER", FONT_SIZE_2);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) + 6), buffer, FONT_SIZE_1);
	}
	else if (state == BREAKOUT_STATE_WIN)
	{
		snprintf(buffer, sizeof(buffer), "Score: %d", score);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) - 8), "YOU WIN", FONT_SIZE_2);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) + 6), buffer, FONT_SIZE_1);
	}
	else if (state == BREAKOUT_STATE_READY)
	{
		displayPrintCentered((BREAKOUT_PADDLE_Y - 20), "KEY 5: LAUNCH", FONT_SIZE_1);
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

	if ((state == BREAKOUT_STATE_GAME_OVER) || (state == BREAKOUT_STATE_WIN))
	{
		return; // Only the exit keys do anything once the game has ended.
	}

	if ((state == BREAKOUT_STATE_READY) && KEYCHECK_SHORTUP(ev->keys, KEY_5))
	{
		launchBall();
	}

	// Track held state (rather than moving on each press event) so the paddle moves smoothly every
	// tick for as long as a direction key is held, instead of at the keyboard's own repeat rate --
	// same approach as the ship in menuGameSpace.c.
	if (KEYCHECK_DOWN(ev->keys, KEY_4))
	{
		moveLeftHeld = true;
	}
	else if (KEYCHECK_UP(ev->keys, KEY_4))
	{
		moveLeftHeld = false;
	}

	if (KEYCHECK_DOWN(ev->keys, KEY_6))
	{
		moveRightHeld = true;
	}
	else if (KEYCHECK_UP(ev->keys, KEY_6))
	{
		moveRightHeld = false;
	}
}
