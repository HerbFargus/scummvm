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

#ifndef NIGHTMARENED_DEMO_H
#define NIGHTMARENED_DEMO_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"

#include "nightmarened/data.h"
#include "nightmarened/resources.h"
#include "nightmarened/world.h"

namespace NightmareNed {

class Display;
class Game;
class NightmareNedEngine;

/**
 * Stands in for the scene code until M3: builds a scene from its request list and
 * its .SST placement table, then loops every sprite's sequence on the game's 41 ms
 * tick. Ned walks the attic on the input bits; the camera follows him.
 */
class PlacementDemo {
public:
	PlacementDemo(NightmareNedEngine *vm, Game *game);
	~PlacementDemo();

	/** The attic as S4 loads it: NED98000 list 0x497680 (scene 0), then 98999001.SST */
	bool loadAttic();
	void setScroll(const Common::Point &scroll) { _scroll = scroll; }
	/** One main-loop iteration; returns true when it drew a frame */
	bool tick(uint32 input, uint32 now);
	const Common::Point &getNedPosition() const { return _nedPos; }

private:
	int findSequenceScene(int id) const;
	bool loadSlot(int area, int id, int scene);
	void place(uint32 now, bool advance);
	void startNed();
	void follow();
	static void xform(void *user, int &x, int &y);

	NightmareNedEngine *_vm;
	Game *_game;
	Resources *_res;
	Display *_display;
	NafArchive *_naf;
	Common::Array<Request> _list, _extra;
	Common::Array<CollisionMap *> _collision;
	struct Placed {
		int area, slot;
		BitmapRec *bitmap;
	};
	Common::Array<Placed> _placed;
	Common::HashMap<int, int> _slotScenes;
	Common::Point _scroll;
	uint32 _lastTick;
	CollisionWorld _world;    // DAT_00525da4
	CollisionMap _userMap;    // DAT_00525da8 (bc_new)
	Common::Point _nedPos;    // DAT_00525df8/fc
	int _facing;              // the copy ned_tick writes (DAT_004c5750)
	bool _nedStarted;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_DEMO_H
