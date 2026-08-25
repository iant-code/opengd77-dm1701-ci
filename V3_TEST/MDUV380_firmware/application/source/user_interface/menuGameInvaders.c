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
// "Invaders": a classic-formula Space Invaders clone. The alien crab sprite, explosion burst, and
// bullet-trail styling are reused verbatim from menuGameSpace.c; the player cannon is that same
// game's ship-hull/cockpit bitmap rotated 90deg anti-clockwise (nose-right -> nose-up), regenerated
// with a small throwaway rotation script rather than hand-edited, then verified by eye against the
// expected orientation (single-pixel nose at the top row) before being pasted in below.
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"

#define INVADERS_MARGIN                 4
#define INVADERS_HUD_HEIGHT             9
#define INVADERS_PLAY_TOP               (MENU_HEADER_HEIGHT + INVADERS_MARGIN + INVADERS_HUD_HEIGHT)
#define INVADERS_PLAY_BOTTOM            (DISPLAY_SIZE_Y - INVADERS_MARGIN)
#define INVADERS_PLAY_LEFT              INVADERS_MARGIN
#define INVADERS_PLAY_RIGHT             (DISPLAY_SIZE_X - INVADERS_MARGIN)

#define INVADERS_TICK_MS_BASE           350
#define INVADERS_TICK_MS_MIN            90
#define INVADERS_INITIAL_LIVES          3

// Alien formation: moves as a single rigid block (formationX/Y), reversing direction and dropping
// a row whenever any *alive* alien would cross the playfield edge -- classic behaviour, and much
// simpler than tracking per-alien velocity.
#define INVADERS_ALIEN_ROWS             3 // 4 didn't leave enough vertical room on this screen before the formation reached the shields/player
#define INVADERS_ALIEN_COLS             6 // one column narrower on each side than the original 8
#define INVADERS_ALIEN_TOTAL            (INVADERS_ALIEN_ROWS * INVADERS_ALIEN_COLS)
#define INVADERS_ALIEN_SPRITE_W         11
#define INVADERS_ALIEN_SPRITE_H         8
#define INVADERS_ALIEN_CELL_W           15
#define INVADERS_ALIEN_CELL_H           11
#define INVADERS_ALIEN_GRID_WIDTH       (INVADERS_ALIEN_COLS * INVADERS_ALIEN_CELL_W)
#define INVADERS_ALIEN_GRID_LEFT0       (INVADERS_PLAY_LEFT + ((INVADERS_PLAY_RIGHT - INVADERS_PLAY_LEFT - INVADERS_ALIEN_GRID_WIDTH) / 2))
#define INVADERS_ALIEN_GRID_TOP0        (INVADERS_PLAY_TOP + 2)
#define INVADERS_ALIEN_STEP_X           3
#define INVADERS_ALIEN_DROP_Y           4

#define INVADERS_PLAYER_SPRITE_W        11
#define INVADERS_PLAYER_SPRITE_H        16
#define INVADERS_PLAYER_DRAW_X_OFFSET   (-5) // bitmap's nose (single lit pixel, top row) sits 5px right of its left edge
#define INVADERS_PLAYER_Y               (INVADERS_PLAY_BOTTOM - INVADERS_PLAYER_SPRITE_H)
#define INVADERS_PLAYER_STEP            3
#define INVADERS_PLAYER_MIN_X           (INVADERS_PLAY_LEFT + 6)
#define INVADERS_PLAYER_MAX_X           (INVADERS_PLAY_RIGHT - 6)

#define INVADERS_PLAYER_BULLET_SPEED    6
#define INVADERS_ALIEN_BULLET_SPEED     3
#define INVADERS_ALIEN_BULLET_MAX       3
#define INVADERS_ALIEN_FIRE_MIN_MS      500
#define INVADERS_ALIEN_FIRE_MAX_MS      1400

// Shields: 3 small destructible bunkers between the aliens and the player, each a coarse grid of
// cells that get knocked out one at a time by whichever bullet (player's or alien's) hits them --
// same as the arcade original, including that your own shots damage them too.
#define INVADERS_SHIELD_COUNT           3
#define INVADERS_SHIELD_COLS            4
#define INVADERS_SHIELD_ROWS            3
#define INVADERS_SHIELD_CELL_W          4
#define INVADERS_SHIELD_CELL_H          3
#define INVADERS_SHIELD_WIDTH           (INVADERS_SHIELD_COLS * INVADERS_SHIELD_CELL_W)
#define INVADERS_SHIELD_HEIGHT          (INVADERS_SHIELD_ROWS * INVADERS_SHIELD_CELL_H)
#define INVADERS_SHIELD_Y               (INVADERS_PLAYER_Y - 24)

#define INVADERS_EXPLOSION_MAX          3
#define INVADERS_EXPLOSION_TICKS        2

#define INVADERS_COLOUR_SHIP_HULL       0x00E5FFU // cyan, same as menuGameSpace.c
#define INVADERS_COLOUR_SHIP_COCKPIT    0xFFFFFFU
#define INVADERS_COLOUR_ALIEN_EYES      0xFFFFFFU
#define INVADERS_COLOUR_BULLET_PLAYER   0xFFEB3BU // yellow, same as menuGameSpace.c bullets
#define INVADERS_COLOUR_BULLET_ALIEN    0xFF6D00U // orange -- visually distinct from the player's own shot
#define INVADERS_COLOUR_SHIELD          0x66BB6AU // green, classic shield colour
#define INVADERS_COLOUR_EXPLOSION       0xFF6D00U

// Player cannon: menuGameSpace.c's 16x11 ship-hull/cockpit bitmap, rotated 90deg anti-clockwise
// (nose-right -> nose-up) via a one-off scratchpad script, ASCII-verified before pasting in --
// see the file header comment. Same "user-edited via scratchpad CSV round trip" provenance as the
// original, just regenerated by rotation instead of by hand.
static const uint8_t invadersShipHullBitmap[32] = // 11x16
{
	0x04U, 0x00U, // .....#.....
	0x04U, 0x00U, // .....#.....
	0x04U, 0x00U, // .....#.....
	0x0EU, 0x00U, // ....###....
	0x0EU, 0x00U, // ....###....
	0x0EU, 0x00U, // ....###....
	0x1FU, 0x00U, // ...#####...
	0x3FU, 0x80U, // ..#######..
	0x7FU, 0xC0U, // .#########.
	0x00U, 0x00U, // ...........
	0x04U, 0x00U, // .....#.....
	0x0EU, 0x00U, // ....###....
	0x1FU, 0x00U, // ...#####...
	0x04U, 0x00U, // .....#.....
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
};

static const uint8_t invadersShipCockpitBitmap[32] = // 11x16
{
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x0EU, 0x00U, // ....###....
	0x04U, 0x00U, // .....#.....
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
};

// Alien "crab" sprite -- reused verbatim from menuGameSpace.c (not directional, so no rotation
// needed, unlike the ship).
static const uint8_t invadersAlienBodyBitmap[16] = // 11x8
{
	0x20U, 0x80U, // ..#.....#..
	0x11U, 0x00U, // ...#...#...
	0x3FU, 0x80U, // ..#######..
	0x6EU, 0xC0U, // .##.###.##.
	0xFFU, 0xE0U, // ###########
	0xBFU, 0xA0U, // #.#######.#
	0xA0U, 0xA0U, // #.#.....#.#
	0x1BU, 0x00U, // ...##.##...
};

static const uint8_t invadersAlienEyesBitmap[16] = // 11x8
{
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x20U, 0x80U, // ..#.....#..
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
	0x00U, 0x00U, // ...........
};

typedef enum
{
	INVADERS_STATE_PLAYING = 0,
	INVADERS_STATE_GAME_OVER,
	INVADERS_STATE_WIN,
} invadersState_t;

typedef struct
{
	int16_t x;
	int16_t y;
	bool active;
} invadersBullet_t;

typedef struct
{
	int16_t x;
	int16_t y;
	uint8_t ttl;
} invadersExplosion_t;

static menuStatus_t menuGameInvadersExitCode = MENU_STATUS_SUCCESS;
static bool alienAlive[INVADERS_ALIEN_ROWS][INVADERS_ALIEN_COLS];
static int aliensRemaining;
static int16_t formationX, formationY;
static int8_t formationDir;
static bool shieldAlive[INVADERS_SHIELD_COUNT][INVADERS_SHIELD_ROWS][INVADERS_SHIELD_COLS];
static int16_t playerX;
static bool moveLeftHeld, moveRightHeld;
static invadersBullet_t playerBullet;
static invadersBullet_t alienBullets[INVADERS_ALIEN_BULLET_MAX];
static invadersExplosion_t explosions[INVADERS_EXPLOSION_MAX];
static int lives;
static int score;
static invadersState_t state;
static uint32_t lastTickTime;
static uint32_t lastAlienFireTime;
static uint32_t nextAlienFireIntervalMs;
static uint32_t rngState;

static void updateScreen(bool isFirstRun);
static void handleEvent(uiEvent_t *ev);
static void resetGame(void);
static uint32_t nextRandom(void);
static void alienCellRect(int row, int col, int16_t *x, int16_t *y);
static bool computeAliveExtents(int16_t *minX, int16_t *maxX);
static void shieldCellRect(int shieldIndex, int row, int col, int16_t *x, int16_t *y);
static bool damageShieldAt(int16_t x, int16_t y);
static void spawnExplosion(int16_t x, int16_t y);
static void firePlayerBullet(void);
static void spawnAlienBullet(void);
static void eatShieldsUnderAliens(void);
static bool aliensReachedRow(int16_t thresholdY);
static void advanceGame(void);

menuStatus_t menuGameInvaders(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		menuDataGlobal.numItems = 0;
		lastTickTime = ev->time;
		lastAlienFireTime = ev->time;
		rngState = ev->time;
		resetGame();
		updateScreen(true);
	}
	else
	{
		menuGameInvadersExitCode = MENU_STATUS_SUCCESS;

		if (ev->hasEvent)
		{
			handleEvent(ev);
		}

		if (state == INVADERS_STATE_PLAYING)
		{
			if ((ev->time - lastAlienFireTime) > nextAlienFireIntervalMs)
			{
				lastAlienFireTime = ev->time;
				nextAlienFireIntervalMs = (INVADERS_ALIEN_FIRE_MIN_MS + (nextRandom() % (INVADERS_ALIEN_FIRE_MAX_MS - INVADERS_ALIEN_FIRE_MIN_MS)));
				spawnAlienBullet();
			}

			// Tick speeds up as aliens die -- same escalating tension as the arcade original.
			uint32_t tickMs = (INVADERS_TICK_MS_BASE - (uint32_t)(((INVADERS_TICK_MS_BASE - INVADERS_TICK_MS_MIN) * (INVADERS_ALIEN_TOTAL - aliensRemaining)) / INVADERS_ALIEN_TOTAL));

			if ((ev->time - lastTickTime) > tickMs)
			{
				lastTickTime = ev->time;
				advanceGame();
				updateScreen(false);
			}
		}
	}

	return menuGameInvadersExitCode;
}

static uint32_t nextRandom(void)
{
	// Same self-contained LCG as the other games here (menuGameSnake.c etc) instead of newlib's
	// rand()/srand().
	rngState = ((rngState * 1103515245u) + 12345u);
	return ((rngState >> 16) & 0x7FFFu);
}

static void resetGame(void)
{
	for (int row = 0; row < INVADERS_ALIEN_ROWS; row++)
	{
		for (int col = 0; col < INVADERS_ALIEN_COLS; col++)
		{
			alienAlive[row][col] = true;
		}
	}

	aliensRemaining = INVADERS_ALIEN_TOTAL;
	formationX = 0;
	formationY = 0;
	formationDir = 1;

	for (int s = 0; s < INVADERS_SHIELD_COUNT; s++)
	{
		for (int row = 0; row < INVADERS_SHIELD_ROWS; row++)
		{
			for (int col = 0; col < INVADERS_SHIELD_COLS; col++)
			{
				shieldAlive[s][row][col] = true;
			}
		}
	}

	playerX = ((INVADERS_PLAY_LEFT + INVADERS_PLAY_RIGHT) / 2);
	moveLeftHeld = false;
	moveRightHeld = false;

	playerBullet.active = false;

	for (int i = 0; i < INVADERS_ALIEN_BULLET_MAX; i++)
	{
		alienBullets[i].active = false;
	}

	for (int i = 0; i < INVADERS_EXPLOSION_MAX; i++)
	{
		explosions[i].ttl = 0;
	}

	lives = INVADERS_INITIAL_LIVES;
	score = 0;
	nextAlienFireIntervalMs = INVADERS_ALIEN_FIRE_MIN_MS;
	state = INVADERS_STATE_PLAYING;
}

static void alienCellRect(int row, int col, int16_t *x, int16_t *y)
{
	*x = (int16_t)(INVADERS_ALIEN_GRID_LEFT0 + (col * INVADERS_ALIEN_CELL_W) + formationX);
	*y = (int16_t)(INVADERS_ALIEN_GRID_TOP0 + (row * INVADERS_ALIEN_CELL_H) + formationY);
}

// Extents of the currently-alive aliens only (a formation with dead edge columns is narrower than
// the full grid) -- used to decide when the block has reached a playfield edge.
static bool computeAliveExtents(int16_t *minX, int16_t *maxX)
{
	bool any = false;

	*minX = INT16_MAX;
	*maxX = INT16_MIN;

	for (int row = 0; row < INVADERS_ALIEN_ROWS; row++)
	{
		for (int col = 0; col < INVADERS_ALIEN_COLS; col++)
		{
			if (alienAlive[row][col] == false)
			{
				continue;
			}

			int16_t x, y;

			alienCellRect(row, col, &x, &y);
			any = true;

			if (x < *minX)
			{
				*minX = x;
			}

			if ((x + INVADERS_ALIEN_SPRITE_W) > *maxX)
			{
				*maxX = (int16_t)(x + INVADERS_ALIEN_SPRITE_W);
			}
		}
	}

	return any;
}

static void shieldCellRect(int shieldIndex, int row, int col, int16_t *x, int16_t *y)
{
	int spacing = ((INVADERS_PLAY_RIGHT - INVADERS_PLAY_LEFT) / INVADERS_SHIELD_COUNT);
	int16_t shieldLeft = (int16_t)(INVADERS_PLAY_LEFT + (spacing * shieldIndex) + ((spacing - INVADERS_SHIELD_WIDTH) / 2));

	*x = (int16_t)(shieldLeft + (col * INVADERS_SHIELD_CELL_W));
	*y = (int16_t)(INVADERS_SHIELD_Y + (row * INVADERS_SHIELD_CELL_H));
}

// Tests a small bullet-sized point/rect against every alive shield cell; knocks out the first one
// it overlaps and reports the hit (so the caller can also deactivate the bullet). Same shield
// erosion rule as the arcade original: it doesn't matter whether the bullet was the player's or an
// alien's, either one chips the shield.
static bool damageShieldAt(int16_t x, int16_t y)
{
	for (int s = 0; s < INVADERS_SHIELD_COUNT; s++)
	{
		for (int row = 0; row < INVADERS_SHIELD_ROWS; row++)
		{
			for (int col = 0; col < INVADERS_SHIELD_COLS; col++)
			{
				if (shieldAlive[s][row][col] == false)
				{
					continue;
				}

				int16_t cx, cy;

				shieldCellRect(s, row, col, &cx, &cy);

				if ((x >= cx) && (x < (cx + INVADERS_SHIELD_CELL_W)) && (y >= cy) && (y < (cy + INVADERS_SHIELD_CELL_H)))
				{
					shieldAlive[s][row][col] = false;
					return true;
				}
			}
		}
	}

	return false;
}

// Knocks out any shield cell that overlaps an alive alien's rect -- called every tick as the
// formation descends, so shields erode gradually as aliens pass through them instead of the
// formation being blocked (or the game ending) on contact.
static void eatShieldsUnderAliens(void)
{
	for (int row = 0; row < INVADERS_ALIEN_ROWS; row++)
	{
		for (int col = 0; col < INVADERS_ALIEN_COLS; col++)
		{
			if (alienAlive[row][col] == false)
			{
				continue;
			}

			int16_t ax, ay;

			alienCellRect(row, col, &ax, &ay);

			for (int s = 0; s < INVADERS_SHIELD_COUNT; s++)
			{
				for (int sRow = 0; sRow < INVADERS_SHIELD_ROWS; sRow++)
				{
					for (int sCol = 0; sCol < INVADERS_SHIELD_COLS; sCol++)
					{
						if (shieldAlive[s][sRow][sCol] == false)
						{
							continue;
						}

						int16_t sx, sy;

						shieldCellRect(s, sRow, sCol, &sx, &sy);

						if (((ax + INVADERS_ALIEN_SPRITE_W) > sx) && (ax < (sx + INVADERS_SHIELD_CELL_W)) &&
								((ay + INVADERS_ALIEN_SPRITE_H) > sy) && (ay < (sy + INVADERS_SHIELD_CELL_H)))
						{
							shieldAlive[s][sRow][sCol] = false;
						}
					}
				}
			}
		}
	}
}

// True once any alive alien's bottom edge has reached thresholdY -- the formation's only remaining
// instant-loss condition (shields no longer block or end the game on contact, see
// eatShieldsUnderAliens() above).
static bool aliensReachedRow(int16_t thresholdY)
{
	for (int row = 0; row < INVADERS_ALIEN_ROWS; row++)
	{
		for (int col = 0; col < INVADERS_ALIEN_COLS; col++)
		{
			if (alienAlive[row][col] == false)
			{
				continue;
			}

			int16_t x, y;

			alienCellRect(row, col, &x, &y);

			if ((y + INVADERS_ALIEN_SPRITE_H) >= thresholdY)
			{
				return true;
			}
		}
	}

	return false;
}

static void spawnExplosion(int16_t x, int16_t y)
{
	for (int i = 0; i < INVADERS_EXPLOSION_MAX; i++)
	{
		if (explosions[i].ttl == 0)
		{
			explosions[i].x = x;
			explosions[i].y = y;
			explosions[i].ttl = INVADERS_EXPLOSION_TICKS;
			return;
		}
	}
}

static void firePlayerBullet(void)
{
	if (playerBullet.active)
	{
		return; // one at a time, same rule as the arcade original
	}

	playerBullet.active = true;
	playerBullet.x = playerX;
	playerBullet.y = INVADERS_PLAYER_Y;
}

static void spawnAlienBullet(void)
{
	int freeSlot = -1;

	for (int i = 0; i < INVADERS_ALIEN_BULLET_MAX; i++)
	{
		if (alienBullets[i].active == false)
		{
			freeSlot = i;
			break;
		}
	}

	if (freeSlot < 0)
	{
		return; // all bullet slots busy -- skip this volley
	}

	// Only the frontmost (highest row index) alive alien in a column may fire, same as the arcade
	// original -- otherwise a back-row alien's shot would have to pass through its own formation.
	int col = (nextRandom() % INVADERS_ALIEN_COLS);
	int firingRow = -1;

	for (int row = (INVADERS_ALIEN_ROWS - 1); row >= 0; row--)
	{
		if (alienAlive[row][col])
		{
			firingRow = row;
			break;
		}
	}

	if (firingRow < 0)
	{
		return; // that column is empty
	}

	int16_t x, y;

	alienCellRect(firingRow, col, &x, &y);
	alienBullets[freeSlot].active = true;
	alienBullets[freeSlot].x = (int16_t)(x + (INVADERS_ALIEN_SPRITE_W / 2));
	alienBullets[freeSlot].y = (int16_t)(y + INVADERS_ALIEN_SPRITE_H);
}

static void advanceGame(void)
{
	for (int i = 0; i < INVADERS_EXPLOSION_MAX; i++)
	{
		if (explosions[i].ttl > 0)
		{
			explosions[i].ttl--;
		}
	}

	// Formation movement: step sideways; if that would push the currently-alive extent past the
	// playfield edge, reverse direction and drop a row instead of moving sideways this tick.
	int16_t minX, maxX;

	if (computeAliveExtents(&minX, &maxX))
	{
		bool wouldHitEdge = ((formationDir > 0) ? ((maxX + INVADERS_ALIEN_STEP_X) > INVADERS_PLAY_RIGHT)
				: ((minX - INVADERS_ALIEN_STEP_X) < INVADERS_PLAY_LEFT));

		if (wouldHitEdge)
		{
			formationDir = (int8_t)(-formationDir);
			formationY = (int16_t)(formationY + INVADERS_ALIEN_DROP_Y);
		}
		else
		{
			formationX = (int16_t)(formationX + (formationDir * INVADERS_ALIEN_STEP_X));
		}

		// Aliens don't stop at the shields, or end the game by touching them -- they just keep
		// descending, eating away whatever shield cells they pass through (same as the arcade
		// original: the shields erode from the top down as the formation advances). The only
		// instant-loss condition now is reaching the player's own row.
		eatShieldsUnderAliens();

		if (aliensReachedRow(INVADERS_PLAYER_Y))
		{
			state = INVADERS_STATE_GAME_OVER;
			return;
		}
	}

	// Player movement.
	if (moveLeftHeld && (playerX > INVADERS_PLAYER_MIN_X))
	{
		playerX = (int16_t)(playerX - INVADERS_PLAYER_STEP);
	}

	if (moveRightHeld && (playerX < INVADERS_PLAYER_MAX_X))
	{
		playerX = (int16_t)(playerX + INVADERS_PLAYER_STEP);
	}

	// Player bullet: move up, then test shields, aliens, and the top of the playfield in that order.
	if (playerBullet.active)
	{
		playerBullet.y = (int16_t)(playerBullet.y - INVADERS_PLAYER_BULLET_SPEED);

		if (damageShieldAt(playerBullet.x, playerBullet.y))
		{
			playerBullet.active = false;
		}
		else
		{
			bool hitAlien = false;

			for (int row = 0; (row < INVADERS_ALIEN_ROWS) && (hitAlien == false); row++)
			{
				for (int col = 0; col < INVADERS_ALIEN_COLS; col++)
				{
					if (alienAlive[row][col] == false)
					{
						continue;
					}

					int16_t ax, ay;

					alienCellRect(row, col, &ax, &ay);

					if ((playerBullet.x >= ax) && (playerBullet.x < (ax + INVADERS_ALIEN_SPRITE_W)) &&
							(playerBullet.y >= ay) && (playerBullet.y < (ay + INVADERS_ALIEN_SPRITE_H)))
					{
						static const int rowScore[INVADERS_ALIEN_ROWS] = { 30, 20, 10 };

						alienAlive[row][col] = false;
						aliensRemaining--;
						score += rowScore[row];
						spawnExplosion((int16_t)(ax + (INVADERS_ALIEN_SPRITE_W / 2)), (int16_t)(ay + (INVADERS_ALIEN_SPRITE_H / 2)));
						playerBullet.active = false;
						hitAlien = true;
						break;
					}
				}
			}

			if ((hitAlien == false) && (playerBullet.y < INVADERS_PLAY_TOP))
			{
				playerBullet.active = false;
			}
		}

		if (aliensRemaining <= 0)
		{
			state = INVADERS_STATE_WIN;
			return;
		}
	}

	// Alien bullets: move down, then test shields, the player, and the bottom of the playfield.
	for (int i = 0; i < INVADERS_ALIEN_BULLET_MAX; i++)
	{
		if (alienBullets[i].active == false)
		{
			continue;
		}

		alienBullets[i].y = (int16_t)(alienBullets[i].y + INVADERS_ALIEN_BULLET_SPEED);

		if (damageShieldAt(alienBullets[i].x, alienBullets[i].y))
		{
			alienBullets[i].active = false;
			continue;
		}

		if ((alienBullets[i].y >= INVADERS_PLAYER_Y) && (alienBullets[i].y < INVADERS_PLAY_BOTTOM) &&
				(alienBullets[i].x >= (playerX - (INVADERS_PLAYER_SPRITE_W / 2))) &&
				(alienBullets[i].x < (playerX + (INVADERS_PLAYER_SPRITE_W / 2))))
		{
			alienBullets[i].active = false;
			spawnExplosion(playerX, INVADERS_PLAYER_Y);
			lives--;

			if (lives <= 0)
			{
				state = INVADERS_STATE_GAME_OVER;
				return;
			}

			continue;
		}

		if (alienBullets[i].y >= INVADERS_PLAY_BOTTOM)
		{
			alienBullets[i].active = false;
		}
	}
}

static void updateScreen(bool isFirstRun)
{
	char buffer[24];
	uint16_t savedFg, savedBg;

	if (isFirstRun)
	{
		displayClearBuf();
		menuDisplayTitle("Invaders");
	}

	// Fixed black playfield regardless of the day/night UI theme -- same reasoning as the other
	// games here (menuGameSpace.c etc): the sprite palette is tuned against a black backdrop.
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0x000000U), displayConvertRGB888ToNative(0x000000U));
	displayFillRect(0, MENU_HEADER_HEIGHT, DISPLAY_SIZE_X, (DISPLAY_SIZE_Y - MENU_HEADER_HEIGHT), true);

	displayGetForegroundAndBackgroundColours(&savedFg, &savedBg);

	// Aliens -- colour banded by row for a bit of variety, same crab sprite throughout.
	static const uint32_t rowColours[INVADERS_ALIEN_ROWS] = { 0xFF5252U, 0xAB47BCU, 0x66BB6AU };

	for (int row = 0; row < INVADERS_ALIEN_ROWS; row++)
	{
		for (int col = 0; col < INVADERS_ALIEN_COLS; col++)
		{
			if (alienAlive[row][col] == false)
			{
				continue;
			}

			int16_t x, y;

			alienCellRect(row, col, &x, &y);

			displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(rowColours[row]), savedBg);
			displayDrawBitmap(x, y, (uint8_t *)invadersAlienBodyBitmap, INVADERS_ALIEN_SPRITE_W, INVADERS_ALIEN_SPRITE_H, true);

			displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(INVADERS_COLOUR_ALIEN_EYES), savedBg);
			displayDrawBitmap(x, y, (uint8_t *)invadersAlienEyesBitmap, INVADERS_ALIEN_SPRITE_W, INVADERS_ALIEN_SPRITE_H, true);
		}
	}

	// Shields -- each alive cell a small filled square.
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(INVADERS_COLOUR_SHIELD), savedBg);

	for (int s = 0; s < INVADERS_SHIELD_COUNT; s++)
	{
		for (int row = 0; row < INVADERS_SHIELD_ROWS; row++)
		{
			for (int col = 0; col < INVADERS_SHIELD_COLS; col++)
			{
				if (shieldAlive[s][row][col] == false)
				{
					continue;
				}

				int16_t x, y;

				shieldCellRect(s, row, col, &x, &y);
				// displayFillRect() called directly is the "true=background" polarity (see
				// spectrumDrawMeter()/menuGameBreakout.c) -- false is what actually gets the foreground colour.
				displayFillRect(x, y, INVADERS_SHIELD_CELL_W, INVADERS_SHIELD_CELL_H, false);
			}
		}
	}

	// Player cannon -- rotated ship-hull/cockpit bitmap, see the top-of-file comment.
	int16_t shipDrawX = (int16_t)(playerX + INVADERS_PLAYER_DRAW_X_OFFSET);

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(INVADERS_COLOUR_SHIP_HULL), savedBg);
	displayDrawBitmap(shipDrawX, INVADERS_PLAYER_Y, (uint8_t *)invadersShipHullBitmap, INVADERS_PLAYER_SPRITE_W, INVADERS_PLAYER_SPRITE_H, true);

	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(INVADERS_COLOUR_SHIP_COCKPIT), savedBg);
	displayDrawBitmap(shipDrawX, INVADERS_PLAYER_Y, (uint8_t *)invadersShipCockpitBitmap, INVADERS_PLAYER_SPRITE_W, INVADERS_PLAYER_SPRITE_H, true);

	// Player bullet.
	if (playerBullet.active)
	{
		displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(INVADERS_COLOUR_BULLET_PLAYER), savedBg);
		displayFillRect((int16_t)(playerBullet.x - 1), (int16_t)(playerBullet.y - 1), 2, 3, false); // direct fillRect: false=foreground
	}

	// Alien bullets.
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(INVADERS_COLOUR_BULLET_ALIEN), savedBg);
	for (int i = 0; i < INVADERS_ALIEN_BULLET_MAX; i++)
	{
		if (alienBullets[i].active)
		{
			displayFillRect((int16_t)(alienBullets[i].x - 1), (int16_t)(alienBullets[i].y - 1), 2, 3, false);
		}
	}

	// Explosions -- a brief 4-point burst that shrinks over its lifetime, same as menuGameSpace.c.
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(INVADERS_COLOUR_EXPLOSION), savedBg);
	for (int i = 0; i < INVADERS_EXPLOSION_MAX; i++)
	{
		if (explosions[i].ttl > 0)
		{
			int16_t r = (int16_t)(explosions[i].ttl == INVADERS_EXPLOSION_TICKS ? 4 : 2);
			int16_t ex = explosions[i].x;
			int16_t ey = explosions[i].y;

			displayDrawLine((int16_t)(ex - r), (int16_t)(ey - r), (int16_t)(ex + r), (int16_t)(ey + r), true);
			displayDrawLine((int16_t)(ex - r), (int16_t)(ey + r), (int16_t)(ex + r), (int16_t)(ey - r), true);
		}
	}

	// Explicit white-on-black (not savedFg/savedBg, both now black) -- this HUD text has to stay
	// legible against the fixed black playfield above, regardless of theme.
	displaySetForegroundAndBackgroundColours(displayConvertRGB888ToNative(0xFFFFFFU), displayConvertRGB888ToNative(0x000000U));

	snprintf(buffer, sizeof(buffer), "Score:%d", score);
	displayPrintAt(INVADERS_MARGIN, (MENU_HEADER_HEIGHT + 1), buffer, FONT_SIZE_1);
	snprintf(buffer, sizeof(buffer), "Lives:%d", lives);
	displayPrintAt((DISPLAY_SIZE_X - INVADERS_MARGIN - 42), (MENU_HEADER_HEIGHT + 1), buffer, FONT_SIZE_1); // "Lives:N" is ~42px at FONT_SIZE_1

	if (state == INVADERS_STATE_GAME_OVER)
	{
		snprintf(buffer, sizeof(buffer), "Score: %d", score);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) - 8), "GAME OVER", FONT_SIZE_2);
		displayPrintCentered(((DISPLAY_SIZE_Y / 2) + 6), buffer, FONT_SIZE_1);
	}
	else if (state == INVADERS_STATE_WIN)
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

	if (state != INVADERS_STATE_PLAYING)
	{
		return; // Only the exit keys do anything once the game has ended.
	}

	if (KEYCHECK_SHORTUP(ev->keys, KEY_5))
	{
		firePlayerBullet();
	}

	// Track held state (rather than moving on each press event) so the cannon moves smoothly every
	// tick for as long as a direction key is held -- same approach as menuGameBreakout.c's paddle.
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
