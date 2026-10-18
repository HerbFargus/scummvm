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

#include "common/endian.h"

#include "nightmarened/data.h"

namespace NightmareNed {

// u16 'yX', ver 3, kind, sprite, 1, nsteps, rate; then nsteps x (u16 frame, u16 duration)
bool Sequence::load(Common::SeekableReadStream &stream) {
	if (stream.readUint16LE() != MKTAG16('X', 'y'))
		return false;
	stream.skip(2);
	kind = stream.readUint16LE();
	sprite = stream.readUint16LE();
	stream.skip(2);
	uint16 count = stream.readUint16LE();
	rate = stream.readUint16LE();
	steps.resize(count);
	for (uint i = 0; i < count; i++) {
		steps[i].frame = stream.readUint16LE();
		steps[i].duration = stream.readUint16LE();
	}
	return !stream.err();
}

bool Sequence::getStep(int step, int &frame, int &duration) const {
	if (step < 0 || step >= (int)steps.size())
		return false;
	frame = steps[step].frame;
	duration = steps[step].duration;
	return true;
}

// u16 'eD', ver 6, nframes, w, h, transparent colour, anchor x, y; per frame (0x68 bytes)
// i16 offx, offy, alt offx, alt offy, then 6 rects of i32 x0, y0, x1, y1 (x0 = -0x7fff: none)
bool HitBoxes::load(Common::SeekableReadStream &stream) {
	if (stream.readUint16LE() != MKTAG16('D', 'e'))
		return false;
	stream.skip(2);
	frameCount = stream.readUint16LE();
	width = stream.readUint16LE();
	height = stream.readUint16LE();
	transparentColor = stream.readUint16LE();
	anchorX = stream.readSint16LE();
	anchorY = stream.readSint16LE();
	frames.resize(frameCount);
	for (uint f = 0; f < frameCount; f++) {
		Frame &fr = frames[f];
		fr.offX = stream.readSint16LE();
		fr.offY = stream.readSint16LE();
		fr.altOffX = stream.readSint16LE();
		fr.altOffY = stream.readSint16LE();
		for (int k = 0; k < 6; k++) {
			int32 x0 = stream.readSint32LE(), y0 = stream.readSint32LE();
			int32 x1 = stream.readSint32LE(), y1 = stream.readSint32LE();
			fr.raw[k][0] = x0;
			fr.raw[k][1] = y0;
			fr.raw[k][2] = x1;
			fr.raw[k][3] = y1;
			fr.hasRect[k] = x0 != -0x7fff;
			fr.rects[k] = fr.hasRect[k] ? Common::Rect(x0, y0, x1, y1) : Common::Rect();
		}
	}
	return !stream.err();
}

bool HitBoxes::getRect(int frame, int k, Common::Rect &r) const {
	if (frame < 0 || frame >= (int)frames.size() || k < 0 || k >= 6 || !frames[frame].hasRect[k])
		return false;
	r = frames[frame].rects[k];
	return true;
}

void HitBoxes::getRawRect(int frame, int k, int32 out[4]) const {
	for (int i = 0; i < 4; i++)
		out[i] = (frame >= 0 && frame < (int)frames.size() && k >= 0 && k < 6) ? frames[frame].raw[k][i] : -0x7fff;
}

// u16 1, 'BC', i32 x0, y0, x1, y1, mask size, 0, i16 count (+pad);
// count x {u32 type, i32 id, i32 x0, y0, x1, y1}; then the 'cC' 1-bit mask
bool CollisionMap::load(Common::SeekableReadStream &stream) {
	stream.skip(2);
	if (stream.readUint16LE() != MKTAG16('C', 'B'))
		return false;
	int32 x0 = stream.readSint32LE(), y0 = stream.readSint32LE();
	int32 x1 = stream.readSint32LE(), y1 = stream.readSint32LE();
	bounds = Common::Rect(x0, y0, x1, y1);
	uint32 maskSize = stream.readUint32LE();
	stream.skip(4);
	uint16 count = stream.readUint16LE();
	stream.skip(2);
	regions.resize(count);
	for (uint i = 0; i < count; i++) {
		regions[i].type = stream.readUint32LE();
		regions[i].id = stream.readSint32LE();
		int32 rx0 = stream.readSint32LE(), ry0 = stream.readSint32LE();
		int32 rx1 = stream.readSint32LE(), ry1 = stream.readSint32LE();
		regions[i].rect = Common::Rect(rx0, ry0, rx1, ry1);
	}

	maskStride = 0;
	mask.clear();
	isColumn = false;
	columns.clear();
	uint16 tag = maskSize >= 0x18 ? stream.readUint16LE() : 0;
	if (tag == MKTAG16('C', 'V')) {
		isColumn = true;
		stream.skip(0x16);
		// (the size field counts only the columns here)
		columns.resize(maskSize / 2);
		for (uint16 &c : columns)
			c = stream.readUint16LE();
	} else if (tag == MKTAG16('C', 'c')) {
		stream.skip(2);
		maskStride = stream.readSint32LE();
		int32 mx0 = stream.readSint32LE(), my0 = stream.readSint32LE();
		int32 mx1 = stream.readSint32LE(), my1 = stream.readSint32LE();
		maskBounds = Common::Rect(mx0, my0, mx1, my1);
		mask.resize(maskSize - 0x18);
		stream.read(mask.data(), mask.size());
	}
	return !stream.err();
}

bool CollisionMap::isSolid(int x, int y) const {
	if (isColumn)
		return x >= 0 && x < (int)columns.size() && columns[x] == (uint16)y;
	// x1 is inclusive, y1 exclusive; the byte index rounds up, the bit index doesn't
	if (mask.empty() || x < maskBounds.left || x > maskBounds.right || y < maskBounds.top || y >= maskBounds.bottom)
		return false;
	int dx = x - maskBounds.left;
	uint32 index = (y - maskBounds.top) * maskStride + (dx + 7) / 8;
	return index < mask.size() && (mask[index] & (0x80 >> (dx % 8)));
}

// u16 'oO', u16 1, i16 count, pad, 0x104 reserved bytes; count x 14 i16
bool loadPlacements(Common::SeekableReadStream &stream, Common::Array<Placement> &list) {
	if (stream.readUint16LE() != MKTAG16('O', 'o'))
		return false;
	stream.skip(2);
	int16 count = stream.readSint16LE();
	stream.seek(0x108);
	list.resize(MAX<int16>(count, 0));
	for (Placement &p : list) {
		p.area = stream.readSint16LE();
		p.slot = stream.readSint16LE();
		p.sub = stream.readSint16LE();
		p.x = stream.readSint16LE();
		p.y = stream.readSint16LE();
		p.z = stream.readSint16LE();
		p.kind = stream.readSint16LE();
		p.offX = stream.readSint16LE();
		p.offY = stream.readSint16LE();
		p.unused = stream.readSint16LE();
		p.msPerStep = stream.readSint16LE();
		p.zero = stream.readSint16LE();
		p.transparentColor = stream.readSint16LE();
		p.flip = stream.readSint16LE();
	}
	return !stream.err();
}

} // End of namespace NightmareNed
