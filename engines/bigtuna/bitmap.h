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

#ifndef BIGTUNA_BITMAP_H
#define BIGTUNA_BITMAP_H

#include "common/array.h"
#include "common/stream.h"
#include "graphics/surface.h"

namespace BigTuna {

enum BitmapFormat {
	kBitmapRLE = 0,
	kBitmapRaw8 = 1,
	kBitmapRaw16 = 2,
	kBitmapLZ = 3
};

/**
 * Header: u16 width, u16 height, u8 format, u8 opaque, s16 reg x, s16 reg y,
 * u16 extra (missing in the first player). Rows are stored top first.
 */
class Bitmap {
public:
	Bitmap() : _format(0), _opaque(false), _regX(0), _regY(0) {}
	~Bitmap() { _surface.free(); }

	bool load(Common::SeekableReadStream &stream, bool v1);
	// Just Me and My Dad's .P pictures
	bool loadImp(Common::SeekableReadStream &stream);

	const Graphics::Surface &getSurface() const { return _surface; }
	// 1 where a pixel is drawn
	const byte *getMask() const { return _mask.empty() ? nullptr : &_mask[0]; }
	bool isOpaque() const { return _opaque; }
	byte getFormat() const { return _format; }
	int16 getRegX() const { return _regX; }
	int16 getRegY() const { return _regY; }

private:
	bool decodeRLE(Common::SeekableReadStream &stream);
	bool decodeLZ(Common::SeekableReadStream &stream);

	Graphics::Surface _surface;
	Common::Array<byte> _mask;
	byte _format;
	bool _opaque;
	int16 _regX, _regY;
};

} // End of namespace BigTuna

#endif // BIGTUNA_BITMAP_H
