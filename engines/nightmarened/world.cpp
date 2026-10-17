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

#include "nightmarened/data.h"
#include "nightmarened/world.h"

namespace NightmareNed {

CollisionWorld::CollisionWorld() : _lastHit(0), _count(0), _user(nullptr) {
	for (int i = 0; i < kMaxMaps; i++) {
		_disabled[i] = false;
		_maps[i] = nullptr;
		for (int j = 0; j < 4; j++)
			_bounds[i][j] = 0;
	}
}

bool CollisionWorld::addMap(CollisionMap *map, int x0, int y0, int x1, int y1) {
	if (!map)
		return true;
	if (_count >= kMaxMaps)
		return false;
	_maps[_count] = map;
	_bounds[_count][0] = x0;
	_bounds[_count][1] = y0;
	_bounds[_count][2] = x1;
	_bounds[_count][3] = y1;
	_count++;
	return true;
}

// Shifts the later maps' pointers, bounds and disabled flags down by one
void CollisionWorld::removeMap(CollisionMap *map) {
	int i = 0;
	while (i < _count && _maps[i] != map)
		i++;
	if (i >= _count)
		return;
	for (; i < _count - 1; i++) {
		_maps[i] = _maps[i + 1];
		for (int j = 0; j < 4; j++)
			_bounds[i][j] = _bounds[i + 1][j];
		_disabled[i] = _disabled[i + 1];
	}
	_count--;
}

void CollisionWorld::moveMap(CollisionMap *map, const Common::Rect &bounds) {
	for (int i = 0; i < _count; i++) {
		if (_maps[i] != map)
			continue;
		_bounds[i][0] = bounds.left;
		_bounds[i][1] = bounds.top;
		_bounds[i][2] = bounds.right;
		_bounds[i][3] = bounds.bottom;
		map->bounds = bounds;
		map->maskBounds = bounds;
	}
}

void CollisionWorld::setUser(CollisionMap *map) {
	// The original refuses once the world is full
	if (_count < kMaxMaps)
		_user = map;
}

bool CollisionWorld::testMap(int index, int16 x, int16 y) const {
	if (x < _bounds[index][0] || x > _bounds[index][2] || y < _bounds[index][1] || y > _bounds[index][3])
		return false;
	return !_disabled[index] && _maps[index] && _maps[index]->isSolid(x, y);
}

// The cached map first, then the rest from there to the end, then from 0 up to the cache
bool CollisionWorld::isSolid(int x, int y) {
	int16 sx = (int16)x, sy = (int16)y;
	if (_count == 1)
		return _maps[0] && _maps[0]->isSolid(sx, sy);
	int start = _lastHit;
	for (int i = start; i < _count; i++) {
		if (testMap(i, sx, sy)) {
			_lastHit = i;
			return true;
		}
	}
	for (int i = 0; i < start; i++) {
		if (testMap(i, sx, sy)) {
			_lastHit = i;
			return true;
		}
	}
	return false;
}

} // End of namespace NightmareNed
