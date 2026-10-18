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

#ifndef NIGHTMARENED_WORLD_H
#define NIGHTMARENED_WORLD_H

#include "common/rect.h"

namespace NightmareNed {

struct CollisionMap;

/**
 * The 0x4c0-byte collision world (0x487250): up to 50 'BC' maps, each with world
 * bounds and a disabled flag, searched for a solid pixel starting from the map
 * that last hit.
 */
class CollisionWorld {
public:
	enum {
		kMaxMaps = 50
	};

	CollisionWorld();

	/** 0x4872a0: bounds are inclusive */
	bool addMap(CollisionMap *map, int x0, int y0, int x1, int y1);
	/** 0x487320 */
	void removeMap(CollisionMap *map);
	/** 0x487fc0: moves a map's world bounds (and the map's own bounds and mask) */
	void moveMap(CollisionMap *map, const Common::Rect &bounds);
	void setDisabled(int index, bool disabled) { if (index >= 0 && index < kMaxMaps) _disabled[index] = disabled; }
	int getMapCount() const { return _count; }
	CollisionMap *getMap(int index) const { return index >= 0 && index < _count ? _maps[index] : nullptr; }
	/** 0x487400 / 0x487c90: the scene's own region map (a bc_new map that Ned's region tests read) */
	void setUser(CollisionMap *map);
	CollisionMap *getUser() const { return _user; }
	/** +0x3f0: a 'VC' column map goes here instead of the list, and is tested last */
	CollisionMap *getColumnMap() const { return _column; }

	/** 0x487d10 world_solid_at */
	bool isSolid(int x, int y);

private:
	bool testMap(int index, int16 x, int16 y) const;

	int _lastHit;
	int _count;
	bool _disabled[kMaxMaps];
	int _bounds[kMaxMaps][4];
	CollisionMap *_maps[kMaxMaps];
	CollisionMap *_user;
	CollisionMap *_column;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_WORLD_H
