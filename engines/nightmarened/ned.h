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

#ifndef NIGHTMARENED_NED_H
#define NIGHTMARENED_NED_H

#include "common/scummsys.h"

namespace NightmareNed {

class Game;

/** Ned: the player controller and its state (area 1, NED00000) */
class Ned {
public:
	explicit Ned(Game *game);
	~Ned();

	/** 0x44ec00: on every scene switch, ends a scripted action and returns Ned to idle */
	void sceneSwitchBegin();
	/** 0x44eb00 ned_show */
	void show(bool visible);
	/** 0x44ebf0: restarts the idle timer (DAT_004a554c) */
	void markTime();

private:
	Game *_game;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_NED_H
