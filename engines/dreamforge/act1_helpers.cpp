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

namespace DreamForge {

// Overheard exchange (sets 199, 200), then restarts the 0x6d26c sound loop
void act1_470f0(Logic &L) {
	L.set(0x11);
	L.busyOn();
	L.set(199);
	L.waitSound(L.voice(21000 + 0xc), 1);
	L.waitSound(L.voice(22000 + 9), 2);
	int saved = L.guy();
	L.guy() = 1;
	L.faceEachOther();
	L.guy() = saved;
	L.say(11000 + 0xf3, 2, false);
	L.set(200);
	L.busyOff();
	L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
}

// Same exchange as 470f0 with flags 0x16c/0x16d
void act1_471e0(Logic &L) {
	L.set(0x11);
	L.busyOn();
	L.set(0x16c);
	L.waitSound(L.voice(21000 + 0xc), 1);
	L.waitSound(L.voice(22000 + 9), 2);
	int saved = L.guy();
	L.guy() = 1;
	L.faceEachOther();
	L.guy() = saved;
	L.say(11000 + 0xf3, 2, false);
	L.set(0x16d);
	L.busyOff();
	L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
}

// Exchange setting 0xd3, then restarts the 0x6d26c loop
void act1_472a0(Logic &L) {
	L.set(0x11);
	L.busyOn();
	L.waitSound(L.voice(21000 + 0xe), 1);
	L.waitSound(L.voice(22000 + 8), 2);
	L.say(11000 + 0x9c, 2, false);
	L.set(0xd3);
	L.busyOff();
	L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
}

// First visit to 1x1x4 / 1x2x8 / 1x2x11 plays the next of three cutscenes
void act1_473f0(Logic &L) {
	bool first = false;
	int flag = 0;
	if (L.area() == 1) {
		if (L.room() == 4 && L.isClear(0x1c1)) {
			flag = 0x1c1;
			first = true;
		}
	} else if (L.area() == 2) {
		if (L.room() == 8) {
			if (L.isClear(0x1c2)) {
				flag = 0x1c2;
				first = true;
			}
		} else if (L.room() == 0xb) {
			if (L.isClear(0x1c3)) {
				flag = 0x1c3;
				first = true;
			}
		}
	}
	if (first)
		L.set(flag);

	if (!first)
		return;
	if (L.isClear(0x1bc)) {
		L.set(0x1bc);
		L.cutscene(0x68, 10, 0, 0, 0, 0, 1);
	} else if (L.isClear(0x1bd)) {
		L.set(0x1bd);
		L.cutscene(0x67, 10, 0, 0, 0, 0, 1);
	} else if (L.isClear(0x1be)) {
		L.set(0x1be);
		L.cutscene(0x7c, 10, 0, 0, 0, 0, 1);
	}
}

// Exchange setting 0x154
void act1_48150(Logic &L) {
	L.set(0x11);
	L.busyOn();
	L.waitSound(L.voice(21000 + 0xc), 1);
	L.waitSound(L.voice(22000 + 9), 2);
	L.say(11000 + 0xf3, 2, false);
	L.set(0x154);
	L.busyOff();
	L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
}

// Exchange setting 0xee then 0xed
void act1_481f0(Logic &L) {
	L.set(0x11);
	L.busyOn();
	L.set(0xee);
	L.waitSound(L.voice(21000 + 0xc), 1);
	L.waitSound(L.voice(22000 + 9), 2);
	L.say(11000 + 0xf3, 2, false);
	L.set(0xed);
	L.busyOff();
	L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
}

// Sets the first clear flag of 0xac..0xaf (with its side flags)
void act1_482c0(Logic &L) {
	if (L.isClear(0xac)) {
		L.set(0xac);
		return;
	}
	if (L.isClear(0xad)) {
		L.set(0xad);
		L.set(0xb3);
		return;
	}
	if (L.isClear(0xae)) {
		L.set(0xae);
		return;
	}
	if (L.isClear(0xaf)) {
		L.set(0x1c8);
		L.set(0xaf);
		L.set(0xb4);
		L.set(0xb7);
	}
}

// Exchange that toggles 0xda / 0xdd depending on flag 0x50
void act1_483b0(Logic &L) {
	L.set(0x11);
	L.busyOn();
	L.waitSound(L.voice(22000 + 0x20), 1);
	L.say(11000 + 0x9c, 2, false);
	if (!L.isSet(0x50)) {
		L.clear(0xda);
		L.set(0xdd);
	} else {
		L.clear(0xdd);
		L.set(0xda);
	}
	L.busyOff();
	L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
}

// Reloads the walk map and waypoint pairs from 1X03X04.PIG
void act1_485a0(Logic &L) {
	L.loadWalkMap(1, 3, 4);
}

// Sets flags 1 and 2, and flag words 8 and 12 of the bit array to 1
void act1_2ba60(Logic &L) {
	L.set(1);
	L.set(2);
	// 0x64854 / 0x64864 = 1: whole-dword writes into the flag array (flags 0x100..0x11f, 0x180..0x19f)
	L.set(0x100);
	for (int f = 0x101; f < 0x120; f++)
		L.clear(f);
	L.set(0x180);
	for (int f = 0x181; f < 0x1a0; f++)
		L.clear(f);
}

} // End of namespace DreamForge
