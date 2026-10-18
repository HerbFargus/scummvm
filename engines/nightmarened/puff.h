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

#ifndef NIGHTMARENED_PUFF_H
#define NIGHTMARENED_PUFF_H

#include "common/scummsys.h"

#include "nightmarened/ned.h"

namespace NightmareNed {

class Game;

/**
 * The puff a zapped thing vanishes in (NITENED.EXE 0x4770a0-0x4772e0, shared by
 * scenes 1 and 3): Ned's area-1 slot 0x1a9 centred on the target slot, played once.
 */
class ZapPuff {
public:
	explicit ZapPuff(Game *game) : _game(game), _slot(-1) {}

	/** 0x4770a0: centres the puff on (area, slot), draws it at order z, stops every sound and plays its own */
	void start(int area, int slot, int z, uint32 now, NedXform xform, void *user);
	/** 0x4771f0: steps the puff until its sequence ends */
	void tick(uint32 now, NedXform xform, void *user);
	/** 0x4772e0: the camera moved */
	void nudge(int dx, int dy);
	bool isActive() const { return _slot != -1; }

private:
	enum {
		kArea = 1,
		kPuffSlot = 0x1a9
	};

	int place(NedXform xform, void *user);

	Game *_game;
	int16 _slot;  // DAT_004b4478
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_PUFF_H
