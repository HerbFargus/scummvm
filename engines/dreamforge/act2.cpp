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

#include "dreamforge/logic.h"
#include "dreamforge/act1_helpers.h"
#include "dreamforge/act2.h"

namespace DreamForge {

void act2_initVars(Logic &L) {
	L.var(0xe1338) = -1;    // int32, voice handle the idle hook waits on
}

// Per-object frame hook: flags and held frames when an object reaches its last frame
void act2_48730(Logic &L, int objectId, int frame, int frameCount) {
	const int last = frameCount - 1;

	switch ((uint32)objectId) {
	case 2:
		if (!L.isSet(0x83))
			return;
		if (last != frame)
			return;
		L.clear(0x83);
		L.objFrame(2, last);
		break;
	case 0xc: {
		if (last != frame)
			return;
		if (L.objOnPath(3) || L.isSet(0x1c0)) {
			L.cutscene(0x144, 10, 0, 0, 0, 0, 1);
			L.cutscene(0xe8, 10, 0, 0, 0, 0, 1);
			L.cutscene(0x45, 10, 0, 0, 0, 0, 1);
			L.cutscene(0x159, 10, 0, 0, 0, 0, 1);
			L.busyOff();
			L.endGame();
			return;
		}
		L.clear(0xe5);
		L.objFrame(0xc, last);
		if (L.isClear(0xe1)) {
			L.set(0xe0);
			L.set(0xe1);
			L.var(0xe1338) = L.voice(11000 + 0x159);
			L.set(0xe9);
			return;
		}
		if (!L.isClear(0xe3))
			return;
		L.set(0xdf);
		L.set(0xe3);
		L.var(0xe1338) = L.voice(11000 + 0x159);
		L.set(0xe8);
		L.set(0xe9);
		break;
	}
	case 0xd:
		if (last != frame)
			return;
		L.clear(0xdf);
		L.clear(0xe2);
		L.objFrame(0xd, last);
		break;
	case 0xe:
		if (last != frame)
			return;
		L.clear(0xe0);
		L.clear(0xe2);
		L.objFrame(0xe, last);
		break;
	case 0xf:
		if (frame == 7) {
			L.set(0xe5);
			L.set(0xe2);
			return;
		}
		if (last != frame)
			return;
		L.clear(0xe4);
		L.objFrame(0xf, last);
		break;
	default:
		break;
	}
}

// Idle hook: in (1, 1) waits out the voice started by object 0xc, then sends object 3 on its path
void act2_48a80(Logic &L) {
	if (L.area() != 1 || L.room() != 1)
		return;
	if (L.isSet(0xe9)) {
		if (L.soundPlaying(L.var(0xe1338)))
			return;
		L.clear(0xe9);
		if (!L.isSet(0xe8)) {
			L.busyOff();    // a tail jump to 0x12d90
			return;
		}
		L.objPathChance(3, 100);
		return;
	}
	if (!L.isSet(0xe8))
		return;
	L.clear(0xe8);
	L.set(0xe4);
	L.set(0x1c0);
	L.voice(10000 + 0x15d);
}

// Fidget veto: neither puppy fidgets in (0, 1) or (1, 1)
bool act2_48b40(Logic &L, int g) {
	bool ok = !L.isHidden(g);
	if (g == 0)
		return ok;
	if (g != 1 && g != 2)
		return ok;
	if (L.area() == 0 && L.room() == 1)
		ok = false;
	if (L.area() == 1 && L.room() == 1)
		ok = false;
	return ok;
}

// Scene entry: both puppies start hidden in (0, 1) and (1, 1)
void act2_48bd0(Logic &L, int room, int area) {
	if (area == 0) {
		if (room != 1)
			return;
		L.set(0x11);
		L.hide(3);
		L.cutscene(0x96, 10, 0, 0, 0, 0, 1);
	} else if (area == 1) {
		if (room != 1)
			return;
		L.set(0x11);
		L.hide(3);
	}
}

// After-load: the act 2 opening in (0, 1) and the first arrival in (1, 1)
void act2_48c30(Logic &L, int room, int area) {
	int h;

	if (area == 0) {
		if (room != 1)
			return;
		L.sceneAnim(0x95, 0x112, 0, 0, 0, 0x1df, 0, 0);
		L.cutscene(0x109, 10, 0, 0, 1, 0, 1);
		L.cutscene(0x72, 10, 0, 0, 0, 0, 1);
		L.cutscene(0x6f, 10, 0, 0, 0, 0, 1);
		L.changeScene(0, 0, 0, 0, 0, 0, 2, 1, 1);
	} else if (area == 1) {
		if (room != 1)
			return;
		if (!L.isClear(0x192))
			return;
		L.set(0x192);
		h = L.voice(10000 + 0x15a);
		L.waitSound(h, 1);
		h = L.voice(20000 + 6);
		L.set(0x83);
		L.waitSound(h, 1);
		h = L.voice(11000 + 0x156);
		L.waitSound(h, 1);
		h = L.voice(10000 + 0x15b);
		L.waitSound(h, 1);
		h = L.voice(11000 + 0x157);
		L.waitSound(h, 1);
		h = L.voice(10000 + 0x15c);
		L.waitSound(h, 1);
		h = L.voice(11000 + 0x158);
		L.waitSound(h, 1);
		L.set(0x83);
		L.objNow(0);
		L.objNow(1);
		L.objNow(2);
	}
}

} // End of namespace DreamForge
