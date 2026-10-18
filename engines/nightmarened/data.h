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

#ifndef NIGHTMARENED_DATA_H
#define NIGHTMARENED_DATA_H

#include "common/array.h"
#include "common/rect.h"
#include "common/stream.h"

namespace NightmareNed {

/** 'yX' animation sequence (xsdt): steps of (sprite frame, duration in ticks) */
struct Sequence {
	uint16 kind;
	uint16 sprite;  // the slot whose frames this uses (itself unless aliased)
	uint16 rate;
	struct Step {
		uint16 frame;
		uint16 duration;
	};
	Common::Array<Step> steps;

	bool load(Common::SeekableReadStream &stream);
	/** NITENED.EXE 0x486930; false outside [0, steps) */
	bool getStep(int step, int &frame, int &duration) const;
};

/** 'eD' hit boxes (dsdt): per frame an offset pair and 6 rects (0-2 normal, 3-5 flipped) */
struct HitBoxes {
	uint16 frameCount;
	uint16 width, height;
	uint16 transparentColor;
	int16 anchorX, anchorY;
	struct Frame {
		int16 offX, offY, altOffX, altOffY;
		Common::Rect rects[6];
		bool hasRect[6];
		int32 raw[6][4]; // as stored, including absent rects
	};
	Common::Array<Frame> frames;

	bool load(Common::SeekableReadStream &stream);
	/** NITENED.EXE 0x4860d0 */
	bool getRect(int frame, int k, Common::Rect &r) const;
	/** NITENED.EXE 0x4860d0 as is: x0, y0, x1, y1, -0x7fff when absent or out of range */
	void getRawRect(int frame, int k, int32 out[4]) const;
};

/** 'BC' collision map (cbix): named regions plus a 1-bit solid mask */
struct CollisionMap {
	Common::Rect bounds;
	struct Region {
		uint32 type;  // bit 31 = disabled
		int32 id;
		Common::Rect rect;
	};
	Common::Array<Region> regions;

	// 'cC' mask: rows of maskStride bytes, MSB first, covering maskBounds
	int32 maskStride;
	Common::Rect maskBounds;
	Common::Array<byte> mask;
	// 'VC' column map instead: the floor's y for each x (0x48a040 tests column[x] == y)
	bool isColumn;
	Common::Array<uint16> columns;

	CollisionMap() : maskStride(0), isColumn(false) {}

	bool load(Common::SeekableReadStream &stream);
	/** NITENED.EXE 0x48a040, with its (dx + 7) / 8 byte index */
	bool isSolid(int x, int y) const;
};

/** 'oO' object list / .SST placement table entry (14 x i16) */
struct Placement {
	int16 area, slot, sub;
	int16 x, y, z;
	int16 kind;  // 2 = sprite slot, 0/1 = bitmap record
	int16 offX, offY;
	int16 unused, msPerStep, zero;
	int16 transparentColor;
	int16 flip;
};

bool loadPlacements(Common::SeekableReadStream &stream, Common::Array<Placement> &list);

} // End of namespace NightmareNed

#endif // NIGHTMARENED_DATA_H
