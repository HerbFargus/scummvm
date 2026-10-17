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

#include "dreamforge/dreamforge.h"
#include "dreamforge/walkmap.h"

namespace DreamForge {

void WalkMap::load(const Common::Array<byte> &data) {
	_rows.clear();
	_rows.resize(kScreenHeight);
	uint pos = 0;
	while (pos + 8 <= data.size()) {
		int32 row = (int32)READ_LE_UINT32(&data[pos]);
		if (row == -1)
			break;
		int32 n = (int32)READ_LE_UINT32(&data[pos + 4]);
		pos += 8;
		Common::Array<Span> spans;
		for (int32 i = 0; i < n && pos + 24 <= data.size(); i++, pos += 24) {
			Span s;
			s.xmin = (int32)READ_LE_UINT32(&data[pos]);
			s.xmax = (int32)READ_LE_UINT32(&data[pos + 4]);
			s.scale = (int32)READ_LE_UINT32(&data[pos + 8]);
			for (uint c = 0; c < 3; c++)
				s.cond[c] = (int32)READ_LE_UINT32(&data[pos + 12 + c * 4]);
			spans.push_back(s);
		}
		if (row >= 0 && row < kScreenHeight)
			_rows[row] = spans;
	}
}

const WalkMap::Span *WalkMap::spanAt(const DreamForgeEngine *vm, int y, int x) const {
	if (y < 0 || y >= (int)_rows.size())
		return nullptr;
	for (auto &s : _rows[y]) {
		if (x < s.xmin || x > s.xmax)
			continue;
		bool on = true;
		for (uint c = 0; c < 3 && s.cond[c] != 0 && on; c++)
			on = s.cond[c] > 0 ? vm->isFlagSet(s.cond[c]) : !vm->isFlagSet(-s.cond[c]);
		if (on)
			return &s;
	}
	return nullptr;
}

// 0x19e10 takes the first span under x and ignores its conditions, so a puppy
// placed where it can't walk (on a chair, say) still gets drawn at that size
int WalkMap::scaleAt(const DreamForgeEngine *vm, int y, int x) const {
	if (y < 0 || y >= (int)_rows.size())
		return 0;
	for (auto &s : _rows[y])
		if (x >= s.xmin && x <= s.xmax)
			return s.scale;
	return 0;
}

bool WalkMap::lineClear(const DreamForgeEngine *vm, int y0, int x0, int y1, int x1) const {
	int dx = ABS(x1 - x0), dy = ABS(y1 - y0);
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int err = dx - dy;
	while (true) {
		if (!isWalkable(vm, y0, x0))
			return false;
		if (x0 == x1 && y0 == y1)
			return true;
		int e2 = err * 2;
		if (e2 > -dy) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dx) {
			err += dx;
			y0 += sy;
		}
	}
}

} // End of namespace DreamForge
