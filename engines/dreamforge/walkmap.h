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

#ifndef DREAMFORGE_WALKMAP_H
#define DREAMFORGE_WALKMAP_H

#include "common/array.h"
#include "common/rect.h"

namespace DreamForge {

class DreamForgeEngine;

/**
 * Where the puppies may stand (C.PRG 0x19c20, 0x19e10): per screen row, spans
 * of {xmin, xmax, scale, c1, c2, c3}. A span counts while each non-zero
 * condition holds (c > 0: flag c set, c < 0: flag -c clear); the first 0 ends
 * the list. The span's scale sets the puppy's size against the scene's
 * reference scale.
 */
class WalkMap {
public:
	struct Span {
		int32 xmin, xmax, scale;
		int32 cond[3];
	};

	void load(const Common::Array<byte> &data);
	void clear() { _rows.clear(); }

	/** The enabled span under (y, x), or nullptr */
	const Span *spanAt(const DreamForgeEngine *vm, int y, int x) const;
	bool isWalkable(const DreamForgeEngine *vm, int y, int x) const { return spanAt(vm, y, x) != nullptr; }
	/** The scale of the first span under (y, x), enabled or not; 0 off the map */
	int scaleAt(const DreamForgeEngine *vm, int y, int x) const;
	/** Is the straight line between two points walkable all the way (0x1a4f0) */
	bool lineClear(const DreamForgeEngine *vm, int y0, int x0, int y1, int x1) const;

private:
	Common::Array<Common::Array<Span> > _rows;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_WALKMAP_H
