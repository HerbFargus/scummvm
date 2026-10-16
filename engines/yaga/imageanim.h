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

#ifndef YAGA_IMAGEANIM_H
#define YAGA_IMAGEANIM_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace Yaga {

// One bitmap in a frame, placed relative to the sprite. A mask other than 0
// picks the mouth shapes (lipsync) the layer belongs to
struct AnimLayer {
	Common::String name;
	int x, y;
	uint32 mask;
	Graphics::Surface surface; // in the blending format; empty if undecodable

	AnimLayer() : x(0), y(0), mask(0) {}
};

struct AnimFrame {
	Common::Array<AnimLayer> layers;
};

// A decoded .mng or .rle animation
struct AnimData {
	Common::Array<AnimFrame> frames;
	~AnimData();
	// The bounds of the layers drawn at rest (mask 0 or the rest bit)
	Common::Rect bounds() const;
};

// Decodes by signature; null if the data is neither format
AnimData *decodeAnim(const byte *data, uint32 size, const Common::String &path);

} // End of namespace Yaga

#endif // YAGA_IMAGEANIM_H
