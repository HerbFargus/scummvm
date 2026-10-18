/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef NIGHTMARENED_SLOTHELPERS_H
#define NIGHTMARENED_SLOTHELPERS_H

#include "common/scummsys.h"

namespace NightmareNed {

class Game;

/*
 * Small slot helpers several worlds call (NITENED.EXE).
 */

/** 0x41e060: visibility, then draw order, of a slot's sprite */
void slotShow(Game *game, int area, int id, int z, bool visible);
/** 0x479fb0: moves a slot's position and its sprite by (dx, dy) */
void slotMoveBy(Game *game, int area, int id, int dx, int dy);
/** 0x4558e0: plays a slot's sound looping, stopping its own first */
bool slotPlayLoop(Game *game, int area, int id);

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SLOTHELPERS_H
