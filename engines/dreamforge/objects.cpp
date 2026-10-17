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

#include "common/debug.h"

#include "dreamforge/detection.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/scene.h"

namespace DreamForge {

// 0x23160: the dice path objects roll to set off, 1..100 in random order
void DreamForgeEngine::shufflePathDice(bool reset) {
	if (reset) {
		for (int i = 0; i < 100; i++)
			_pathDice[i] = i + 1;
		_pathDie = 0;
	}
	for (int i = 0; i < 100; i++) {
		int j = getRandom15() * 100 >> 15;
		SWAP(_pathDice[i], _pathDice[j]);
	}
}

// The scaled size for the current row; neither side goes under 1
void DreamForgeEngine::sizePath(SceneObject &o) {
	// The original reads past the table on the step that overshoots; stay inside it
	int idx = CLIP<int>(o.pathRow - MIN(o.pathStart(), o.pathEnd()), 0, o.pathScale.size() - 1);
	int scale = o.pathScale[idx];
	o.pathH = MAX((scale * o.rect.height() + 50) / 100, 1);
	o.pathW = MAX((scale * o.rect.width() + 50) / 100, 1);
	if (o.pathH < 2)
		o.pathH = 1;
	if (o.pathW < 2)
		o.pathW = 1;
}

// 0x22e20: a parked object sets off from the start of its track on a lucky roll
bool DreamForgeEngine::launchPath(SceneObject &o) {
	bool go = (_pathTick % 5 == 0 || o.pathChance == 100) && _pathDice[_pathDie] <= o.pathChance;
	if (go) {
		o.flags2 = (o.flags2 & ~0x20) | 0x08;
		o.pathRow = o.pathStart();
		o.pathX = o.pathStartX();
		sizePath(o);
		o.nextTick = ticks() + o.delay;
		int scale = o.pathScale[o.pathRow - MIN(o.pathStart(), o.pathEnd())];
		o.depth = o.pathRow - (scale * o.pathDepthOff + 50) / 100;
		debugC(1, kDebugScript, "object %d sets off from (%d, %d)", o.id, o.pathX, o.pathRow);
		_dirty = true;
	}
	_pathDie = (_pathDie + 1) % 100;
	if (_pathDie == 0)
		shufflePathDice(false);
	return go;
}

// 0x22590: one step down the track, faster where it is drawn bigger; parks at the end
void DreamForgeEngine::stepPath(SceneObject &o) {
	int start = o.pathStart(), end = o.pathEnd();
	int lo = MIN(start, end);
	int startX = o.pathStartX(), endX = o.pathEndX();
	if (o.pathRow != end) {
		int step = MAX((o.pathScale[o.pathRow - lo] * 50 + 50) / 100, 1);
		if (step < 2)
			step = 1;
		o.pathRow += end < start ? -step : step;
		sizePath(o);
	}
	if (o.pathX != endX && end != start) {
		int range = end - start;
		o.pathX = (((range >> 1) + (o.pathRow - start) * 100) / range * (endX - startX) + 50) / 100 + startX;
	}
	if (o.pathRow == end) {
		o.flags2 |= 0x20;
	} else {
		// Past the end: back onto it (it parks on the next step)
		if ((end < start && o.pathRow < end) || (end > start && o.pathRow > end)) {
			o.pathRow = end;
			sizePath(o);
		}
		if ((startX < endX && o.pathX > endX) || (startX >= endX && o.pathX < endX))
			o.pathX = endX;
	}
	int scale = o.pathScale[o.pathRow - lo];
	o.depth = o.pathRow - (scale * o.pathDepthOff + 50) / 100;
	o.nextTick = 0;
	_dirty = true;
}

} // End of namespace DreamForge
