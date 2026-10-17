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
#include "dreamforge/act1_post.h"

namespace DreamForge {

// Room-code globals (C.PRG addresses)
enum {
	kVarBarks = 0x6d264,      // bark/stage counter for the barking puzzles
	kVarLoopSfx = 0x6d26c,    // handle of a looping effect, -1 none
	kVarTalkDepth = 0xe0ad4,  // depth set before a "talk" script
	kVarCurLayer = 0x64a34,   // engine: index of the layer under the cursor, -1 none
	kVarCurHotspot = 0x64a38  // engine: index of the hotspot under the cursor, -1 none
};

// One step of the 0x35af0 tile puzzle: flag from -> to and play the move
static bool flipTile(Logic &L, int from, int to, int cine, int top, int left, int depth) {
	if (!L.isSet(from))
		return false;
	L.clear(from);
	L.set(to);
	L.sceneAnim(cine, top, left, 1, 0, depth, 0, 0);
	return true;
}

static void guy1WalkTo(Logic &L, int x) {
	int g = L.guy();
	L.guy() = 1;
	L.walk(0x1cd, x, 2);
	L.guy() = g;
}

// After a puppy pushes a mask-0x10 layer: the tile puzzle buttons (0xb3..0xda), 1 and 8. Returns 0
int act1_35af0(Logic &L, int layerId) {
	switch ((uint)layerId) {
	case 1:
		if (L.isSet(0x3f))
			L.changeScene(0x19b, 0x145, 4, 0x1d1, 0x4b, 3, 1, 3, 2);
		break;
	case 8:
		act1_485a0(L);
		break;
	case 0xb3:
	case 0xc7:
		flipTile(L, 0x146, 0x132, 0x104, 0xa6, 0x84, 0x11c);
		if (L.isSet(0x132))
			L.banter(0xd);
		break;
	case 0xb4:
	case 0xc8:
		if (!flipTile(L, 0x131, 0x145, 0x102, 0xa6, 7, 0x11c))
			flipTile(L, 0x147, 0x133, 0x104, 0xa6, 0xff, 0x11c);
		if (L.isSet(0x145) || L.isSet(0x133))
			L.banter(0xd);
		break;
	case 0xb5:
	case 0xc9:
		if (!flipTile(L, 0x132, 0x146, 0x102, 0xa6, 0x82, 0x11c))
			flipTile(L, 0x148, 0x134, 0x104, 0xa6, 0x17a, 0x11c);
		if (L.isSet(0x146) || L.isSet(0x134))
			L.banter(0xd);
		break;
	case 0xb6:
	case 0xca:
		if (!flipTile(L, 0x133, 0x147, 0x102, 0xa6, 0xfd, 0x11c))
			flipTile(L, 0x149, 0x135, 0x104, 0xa6, 0x1f5, 0x11c);
		if (L.isSet(0x147) || L.isSet(0x135))
			L.banter(0xd);
		break;
	case 0xb7:
	case 0xcb:
		flipTile(L, 0x134, 0x148, 0x102, 0xa6, 0x178, 0x11c);
		if (L.isSet(0x148))
			L.banter(0xd);
		break;
	case 0xb8:
	case 0xcc:
		if (!flipTile(L, 0x141, 0x12d, 0x104, 0x5b, 0x84, 0xd1))
			flipTile(L, 0x14b, 0x137, 0x104, 0xf1, 0x84, 0x167);
		if (L.isSet(0x12d) || L.isSet(0x137))
			L.banter(0xd);
		break;
	case 0xb9:
	case 0xcd:
		if (!flipTile(L, 0x12c, 0x140, 0x102, 0x5b, 7, 0xd1) &&
		    !flipTile(L, 0x142, 0x12e, 0x104, 0x5b, 0xff, 0xd1) &&
		    !flipTile(L, 0x136, 0x14a, 0x102, 0xf1, 7, 0x167))
			flipTile(L, 0x14c, 0x138, 0x104, 0xf1, 0xff, 0x167);
		if (L.isSet(0x140) || L.isSet(0x12e) || L.isSet(0x14a) || L.isSet(0x138))
			L.banter(0xd);
		break;
	case 0xba:
	case 0xce:
		if (!flipTile(L, 0x12d, 0x141, 0x102, 0x5b, 0x82, 0xd1) &&
		    !flipTile(L, 0x143, 0x12f, 0x104, 0x5b, 0x17a, 0xd1) &&
		    !flipTile(L, 0x137, 0x14b, 0x102, 0xf1, 0x82, 0x167))
			flipTile(L, 0x14d, 0x139, 0x104, 0xf1, 0x17a, 0x167);
		if (L.isSet(0x141) || L.isSet(0x12f) || L.isSet(0x14b) || L.isSet(0x139))
			L.banter(0xd);
		break;
	case 0xbb:
	case 0xcf:
		if (!flipTile(L, 0x12e, 0x142, 0x102, 0x5b, 0xfd, 0xd1) &&
		    !flipTile(L, 0x144, 0x130, 0x104, 0x5b, 0x1f5, 0xd1) &&
		    !flipTile(L, 0x138, 0x14c, 0x102, 0xf1, 0xfd, 0x167))
			flipTile(L, 0x14e, 0x13a, 0x104, 0xf1, 0x1f5, 0x167);
		if (L.isSet(0x142) || L.isSet(0x130) || L.isSet(0x14c) || L.isSet(0x13a))
			L.banter(0xd);
		break;
	case 0xbc:
	case 0xd0:
		if (!flipTile(L, 0x12f, 0x143, 0x102, 0x5b, 0x178, 0xd1))
			flipTile(L, 0x139, 0x14d, 0x102, 0xf1, 0x178, 0x167);
		if (L.isSet(0x143) || L.isSet(0x14d))
			L.banter(0xd);
		break;
	case 0xbd:
	case 0xd1:
		if (!flipTile(L, 0x146, 0x132, 0x104, 0xa6, 0x84, 0x11c))
			flipTile(L, 0x150, 0x13c, 0x104, 0x13c, 0x84, 0x1b2);
		if (L.isSet(0x132) || L.isSet(0x13c))
			L.banter(0xd);
		break;
	case 0xbe:
	case 0xd2:
		if (!flipTile(L, 0x131, 0x145, 0x102, 0xa6, 7, 0x11c) &&
		    !flipTile(L, 0x147, 0x133, 0x104, 0xa6, 0xff, 0x11c) &&
		    !flipTile(L, 0x13b, 0x14f, 0x102, 0x13c, 7, 0x1b2))
			flipTile(L, 0x151, 0x13d, 0x104, 0x13c, 0xff, 0x1b2);
		if (L.isSet(0x145) || L.isSet(0x133) || L.isSet(0x14f) || L.isSet(0x13d))
			L.banter(0xd);
		break;
	case 0xbf:
	case 0xd3:
		if (!flipTile(L, 0x132, 0x146, 0x102, 0xa6, 0x82, 0x11c) &&
		    !flipTile(L, 0x148, 0x134, 0x104, 0xa6, 0x17a, 0x11c) &&
		    !flipTile(L, 0x13c, 0x150, 0x102, 0x13c, 0x82, 0x1b2))
			flipTile(L, 0x152, 0x13e, 0x104, 0x13c, 0x17a, 0x1b2);
		if (L.isSet(0x146) || L.isSet(0x134) || L.isSet(0x150) || L.isSet(0x13e))
			L.banter(0xd);
		break;
	case 0xc0:
	case 0xd4:
		if (!flipTile(L, 0x133, 0x147, 0x102, 0xa6, 0xfd, 0x11c) &&
		    !flipTile(L, 0x149, 0x135, 0x104, 0xa6, 0x1f5, 0x11c) &&
		    !flipTile(L, 0x13d, 0x151, 0x102, 0x13c, 0xfd, 0x1b2))
			flipTile(L, 0x153, 0x13f, 0x104, 0x13c, 0x1f5, 0x1b2);
		if (L.isSet(0x147) || L.isSet(0x135) || L.isSet(0x151) || L.isSet(0x13f))
			L.banter(0xd);
		break;
	case 0xc1:
	case 0xd5:
		if (!flipTile(L, 0x134, 0x148, 0x102, 0xa6, 0x178, 0x11c))
			flipTile(L, 0x13e, 0x152, 0x102, 0x13c, 0x178, 0x1b2);
		if (L.isSet(0x148) || L.isSet(0x152))
			L.banter(0xd);
		break;
	case 0xc2:
	case 0xd6:
		if (!flipTile(L, 0x14b, 0x137, 0x104, 0xf1, 0x84, 0x167) && L.isSet(0x16a))
			guy1WalkTo(L, 0x4a);
		if (L.isSet(0x137) || L.isSet(0x16a))
			L.banter(0xd);
		break;
	case 0xc3:
	case 0xd7:
		if (!flipTile(L, 0x136, 0x14a, 0x102, 0xf1, 7, 0x167) &&
		    !flipTile(L, 0x14c, 0x138, 0x104, 0xf1, 0xff, 0x167) && L.isSet(0x16a))
			guy1WalkTo(L, 0xc5);
		if (L.isSet(0x14a) || L.isSet(0x138) || L.isSet(0x16a))
			L.banter(0xd);
		break;
	case 0xc4:
	case 0xd8:
		if (!flipTile(L, 0x137, 0x14b, 0x102, 0xf1, 0x82, 0x167) &&
		    !flipTile(L, 0x14d, 0x139, 0x104, 0xf1, 0x17a, 0x167) && L.isSet(0x16a))
			guy1WalkTo(L, 0x140);
		if (L.isSet(0x14b) || L.isSet(0x139) || L.isSet(0x16a))
			L.banter(0xd);
		break;
	case 0xc5:
	case 0xd9:
		if (!flipTile(L, 0x138, 0x14c, 0x102, 0xf1, 0xfd, 0x167) &&
		    !flipTile(L, 0x14e, 0x13a, 0x104, 0xf1, 0x1f5, 0x167) && L.isSet(0x16a))
			guy1WalkTo(L, 0x1bb);
		if (L.isSet(0x14c) || L.isSet(0x13a) || L.isSet(0x16a))
			L.banter(0xd);
		break;
	case 0xc6:
	case 0xda:
		if (!flipTile(L, 0x139, 0x14d, 0x102, 0xf1, 0x17a, 0x167) && L.isSet(0x16a))
			guy1WalkTo(L, 0x22c);
		if (L.isSet(0x14d) || L.isSet(0x16a))
			L.banter(0xd);
		break;
	default:
		break;
	}
	return 0;
}

// After a mask-4 hotspot's script
void act1_37540(Logic &L, int hotspotId) {
	switch ((uint)hotspotId) {
	case 0x1c:
		if (L.recState(hotspotId, kRecHotspot) == 4) {
			L.var(kVarBarks) = 3;
			L.music(0x12, 1);
			act1_470f0(L);
		}
		break;
	case 0x46:
		if (L.isSet(0x60)) {
			act1_473f0(L);
			L.changeScene(0x17d, 0xec, 4, 0x17e, 0xf2, 5, 1, 3, 1);
		}
		break;
	case 0x5a:
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 0xf2, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			act1_483b0(L);
		}
		break;
	case 0x76:
		if (L.recState(hotspotId, kRecHotspot) != 1)
			break;
		if (L.isClear(0x90)) {
			int g = L.guy();
			L.guy() = 1;
			L.walk(L.guyY(1), L.guyX(1), 2);
			L.guy() = g;
			L.say(10000 + 0x121, 1, false);
			L.say(11000 + 0x135, 2, false);
			L.say(10000 + 0x122, 1, false);
		} else {
			L.banter(4);
		}
		break;
	case 0xa6: {
		if (L.isClear(0xdb))
			break;
		L.clear(0xdb);
		L.clear(0xda);
		int g = L.guy();
		L.guy() = 1;
		L.walk(0x195, 0x1cb, 3);
		L.guy() = 2;
		L.walk(0x197, 0x15e, 4);
		L.guy() = g;
		L.clear(0x50);
		L.hide(3);
		L.sceneAnim(0x55, 0xda, 0x134, 1, 0, 0x1ba, 0, 0);
		if (L.isSet(0x72) && L.isSet(0x55)) {
			L.set(0x50);
			L.cutscene(0x53, 10, 0, 0, 1, 0, 1);
			L.set(0xa1);
		} else {
			int cine = -1;
			if (L.isClear(0x63) && L.isClear(0x56)) {
				L.clear(0x50);
				cine = 0x51;
			} else if (L.isClear(0x63) && L.isSet(0x56)) {
				L.clear(0x50);
				cine = 0x52;
			} else if (L.isSet(0x63) && L.isClear(0x56) && L.isClear(0x72)) {
				L.clear(0x50);
				cine = 0x5e;
			} else if (L.isSet(0x72) && L.isClear(0x56)) {
				L.clear(0x50);
				cine = 0x145;
			} else if (L.isSet(0x63) && L.isSet(0x56)) {
				L.clear(0x50);
				cine = 0x146;
			} else if (L.isSet(0x72) && L.isSet(0x56)) {
				L.clear(0x50);
				cine = 0x147;
			}
			if (cine != -1)
				L.cutscene(cine, 10, 0, 0, 1, 0, 1);
		}
		L.set(0xa0);
		L.stopSound(L.var(kVarLoopSfx));
		L.var(kVarLoopSfx) = -1;
		L.changeScene(0x181, 0x137, 4, 0x17a, 0x17b, 4, 1, 0, 1);
		L.clear(0xdc);
		break;
	}
	default:
		break;
	}
}

// After a mask-4 layer's script
void act1_379c0(Logic &L, int layerId) {
	switch ((uint)layerId) {
	case 0x14:
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 0xf2, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			act1_470f0(L);
		}
		break;
	case 0x17:
	case 0x18:
	case 0x1a:
	case 0x31:
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 0xf2, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			act1_472a0(L);
		}
		break;
	case 0x20: {
		L.set(0x1c9);
		int n = L.recState(0x20, kRecLayer);
		if (n == 100) {
			act1_2ba60(L);
		} else {
			n = n % 13;
			if (n == 0)
				n = 13;
		}
		L.busyOn();
		if (n == 1 || n == 7)
			L.say(10000 + 0x9f, 1, false);
		else if (n == 2 || n == 8)
			L.say(10000 + 0xa0, 1, false);
		L.guyY(1) = 0x18e;
		L.guyX(1) = 0xe5;
		L.sceneAnim(0x4f, 0x55, 0x62, 0, 0, 0x18e, 0, 1);
		for (int i = 0; i < n; i++)
			L.waitSound(L.sfx(21000 + 0x48, 0x7fff, 0), 1);
		int last = -1;
		if (n == 1 || n == 7) {
			L.waitSound(L.voice(21000 + 0x1b), 1);
			L.waitSound(L.voice(22000 + 0x12), 1);
			last = 21000 + 0x1c;
		} else if (n == 2 || n == 8) {
			last = 22000 + 0x13;
		} else if (n == 3 || n == 9) {
			L.waitSound(L.voice(21000 + 0x1d), 1);
			last = 22000 + 0x14;
		} else if (n == 4 || n == 10) {
			L.waitSound(L.voice(21000 + 0x1e), 1);
			last = 22000 + 0x15;
		} else if (n == 5 || n == 11) {
			L.waitSound(L.voice(21000 + 0x1f), 1);
			last = 22000 + 0x16;
		} else if (n == 6 || n == 12) {
			L.waitSound(L.voice(21000 + 0x20), 1);
			L.waitSound(L.voice(22000 + 0x17), 1);
			L.waitSound(L.voice(21000 + 0x21), 1);
			last = 22000 + 0x18;
		} else if (n == 13) {
			L.waitSound(L.voice(21000 + 0x22), 1);
			L.waitSound(L.voice(22000 + 0x19), 1);
			L.waitSound(L.voice(21000 + 0x23), 1);
			L.waitSound(L.voice(22000 + 0x1a), 1);
			last = 21000 + 0x24;
		}
		if (last != -1)
			L.waitSound(L.voice(last), 1);
		L.busyOff();
		L.clear(0x1c9);
		break;
	}
	case 0x43: {
		if (L.isSet(0x97) || L.isSet(0x96))
			break;
		int g = L.guy();
		L.guy() = 1;
		L.walk(0x177, 0x1a5, 4);
		L.hide(1);
		L.guy() = g;
		L.sceneAnim(0xaf, 0xd4, 0x176, 1, 0, 0x1ae, 0, 0);
		break;
	}
	default:
		break;
	}
}

// After a mask-4 object's script
void act1_371d0(Logic &L, int objectId) {
	if (objectId == 5) {
		while (L.objFrameOf(5) != 0)
			L.step();
		L.set(0xc3);
		L.sceneAnim(0x9e, 0x12a, 0x9c, 1, 0, L.screenHeight() - 1, 0, 0);
		if (L.recState(5, kRecObject) == 1) {
			L.say(10000 + 0x100, 1, false);
			L.say(11000 + 0x109, 2, false);
		} else if (L.recState(5, kRecObject) == 2) {
			L.say(10000 + 0x101, 1, false);
			L.say(11000 + 0x10a, 2, false);
		} else {
			L.say(10000 + 0x102, 1, false);
			L.say(11000 + 0x10b, 2, false);
			L.say(10000 + 0x103, 1, false);
		}
		return;
	}
	if (objectId != 4)
		return;

	L.objNoLoop(4);
	L.objNoLoop(5);
	int g;
	if (L.recState(4, kRecObject) == 1) {
		L.say(10000 + 0x108, 1, false);
		g = L.guy();
		L.guy() = 1;
		L.walk(0x1c9, 0x209, 1);
		L.guy() = g;
	} else if (L.recState(4, kRecObject) == 2) {
		L.say(10000 + 0x109, 1, false);
		g = L.guy();
		L.guy() = 1;
		L.walk(0x1c9, 0x209, 1);
		L.guy() = g;
	} else if (L.recState(4, kRecObject) == 3) {
		L.say(10000 + 0x10a, 1, false);
		g = L.guy();
		L.guy() = 1;
		L.walk(0x1b6, 0xac, 1);
		L.walk(0x19f, 0x7a, 4);
		L.guy() = g;
	}
	while (!L.objDone(5) || !L.objDone(4))
		L.step();
	if (L.recState(4, kRecObject) == 1) {
		L.set(0xc1);
		L.objLoop(4);
		L.objLoop(5);
		L.cutscene(0xa5, 10, 0, 0, 1, 0, 1);
	} else if (L.recState(4, kRecObject) == 2) {
		L.objLoop(4);
		L.objLoop(5);
		L.cutscene(0x15c, 10, 0, 0, 1, 0, 1);
		L.waitSound(L.voice(21000 + 0x2d), 1);
	} else if (L.recState(4, kRecObject) == 3) {
		L.clear(0xbf);
		L.cutscene(0xa9, 10, 0, 0, 1, 0, 1);
		L.cutscene(0x50, 10, 0, 0, 0, 0, 1);
		L.changeScene(0, 0, 6, 0, 0, 6, 2, 0, 1);
	}
}

// Clicked on a puppy (or a plain click): puppy 2's remark for the room
void act1_3d200(Logic &L) {
	if (L.isBusy(2))
		return;
	L.stopWalking(2);
	switch (L.area()) {
	case 0:
		if (L.room() != 1)
			return;
		if (L.isClear(0xa9)) {
			L.say(10000 + 0x13f, 1, false);
			L.say(11000 + 0x147, 2, false);
		} else {
			L.banter(4);
		}
		return;
	case 1:
		switch (L.room()) {
		case 1:
		case 2:
			L.banter(5);
			return;
		case 3:
			L.say(10000 + 0x6b, 1, false);
			L.say(11000 + 0x78, 2, false);
			return;
		case 4:
			if (L.isSet(0x16a))
				L.say(10000 + 0xc4, 1, false);
			L.say(11000 + 200, 2, false);
			return;
		case 5:
			L.say(10000 + 0xd1, 1, false);
			L.say(11000 + 0xd7, 2, false);
			return;
		case 6:
			L.say(10000 + 0x81, 1, false);
			L.say(11000 + 0x8e, 2, false);
			if (L.isSet(0x4e)) {
				int g = L.guy();
				L.guy() = 1;
				L.walk(0x110, 0x1ae, 4);
				L.guy() = g;
			}
			return;
		case 7:
			L.say(11000 + 0x93, 2, false);
			return;
		case 10:
			if (L.isClear(0x1c8)) {
				if (L.recState(0x8b, kRecHotspot) != 0)
					L.say(11000 + 0x104, 2, false);
				else
					L.say(11000 + 0x11b, 2, false);
			} else if (L.isSet(0xb9)) {
				if (L.isClear(0xbe)) {
					L.say(11000 + 0x11d, 2, false);
				} else {
					L.say(11000 + 0x11e, 2, false);
					L.say(10000 + 0x110, 1, false);
				}
			} else {
				L.say(11000 + 0x11c, 2, false);
			}
			return;
		default:
			return;
		}
	case 2:
		switch (L.room()) {
		case 1:
			if (L.isClear(0x194)) {
				L.say(10000 + 0x18b, 1, false);
				L.say(11000 + 0x183, 2, false);
			} else {
				L.say(10000 + 0x18d, 1, false);
				L.say(11000 + 0x186, 2, false);
			}
			return;
		case 2:
			L.banter(5);
			return;
		case 5:
			L.say(11000 + 0x167, 2, true);
			return;
		case 7:
			if (L.isClear(0x72)) {
				L.say(10000 + 0xb5, 1, false);
				L.say(11000 + 0xb3, 2, false);
				L.say(10000 + 0xbf, 1, false);
				L.say(11000 + 0xbf, 2, false);
				return;
			}
			if (L.isSet(0x72) && L.isClear(0x55)) {
				L.say(10000 + 0xaf, 1, false);
				L.say(11000 + 0xb4, 2, false);
				L.say(10000 + 0xb6, 1, false);
				L.say(11000 + 0xb5, 2, false);
				return;
			}
			if (L.isSet(0x72) && L.isSet(0x55)) {
				L.say(10000 + 0x158, 1, false);
				L.say(11000 + 0x155, 2, false);
				return;
			}
			break;
		case 8:
			if (L.isClear(0x93)) {
				L.say(11000 + 0x136, 2, false);
				return;
			}
			if (L.isSet(0x93) && L.isClear(0x8c)) {
				L.say(11000 + 0x137, 2, false);
				return;
			}
			if (L.isClear(0x92)) {
				L.say(11000 + 0x138, 2, false);
				return;
			}
			if (L.isSet(0x90) && L.isClear(0x95)) {
				L.say(10000 + 300, 1, false);
				L.say(11000 + 0x130, 2, false);
				return;
			}
			break;
		case 10:
			if (L.isSet(0xb1)) {
				L.clear(0xb1);
				L.guy() = 1;
				L.hide(1);
				L.sceneAnim(0xeb, 0x14d, 0xe1, 0, 0, 0x17c, 0, 0);
				L.guy() = 2;
				if (L.isLying(2))
					L.oneShot(0x1c, L.guyDir(L.guy()));
				else if (L.isSitting(2))
					L.oneShot(7, L.guyDir(L.guy()));
				L.oneShot(10, 0);
				L.clear(0xb6);
				L.guy() = 1;
				L.setDir(5);
				L.guy() = 1;
				L.guyY(1) = 0x177;
				L.guyX(1) = 0x121;
				L.sceneAnim(0x135, 0x113, 0xe6, 0, 0, 0x17c, 0, 1);
			}
			L.say(11000 + 0x151, 2, false);
			return;
		case 11:
			if (L.isClear(0x60))
				L.say(11000 + 0xdf, 2, false);
			else
				L.say(11000 + 0xe0, 2, false);
			return;
		default:
			return;
		}
		L.banter(4);
		return;
	case 3:
		if (L.room() == 1) {
			if (L.recState(4, kRecLayer) == 0 && L.isClear(0x3a)) {
				L.say(10000 + 0x1f, 1, false);
				L.say(11000 + 0x21, 2, false);
				return;
			}
			if (L.isClear(0x3a)) {
				L.say(11000 + 0x22, 2, false);
				return;
			}
			if (L.isSet(0x3b) && L.isClear(0x3c)) {
				L.say(11000 + 0x25, 2, false);
				return;
			}
			if (L.isClear(0x3b) && L.isSet(0x3a) && L.isClear(0x3c)) {
				L.say(11000 + 0x23, 2, false);
				L.say(10000 + 0x20, 1, false);
				L.say(11000 + 0x24, 2, false);
				L.say(10000 + 0x21, 1, false);
				return;
			}
			if (L.isSet(0x3c) && L.isClear(0x3d)) {
				L.say(11000 + 0x26, 2, false);
				return;
			}
			if (L.isSet(0x3d)) {
				L.say(11000 + 0x27, 2, false);
				return;
			}
			L.banter(4);
		} else if (L.room() == 2) {
			L.say(11000 + 0xee, 2, false);
		}
		return;
	default:
		return;
	}
}

// Puppy 1 clicked / a bark: the barking puzzles and puppy 2's replies
void act1_3c970(Logic &L) {
	if (L.area() == 0)
		return;
	int next = L.var(kVarBarks) + 1;
	if (L.area() > 1) {
		if (L.area() != 2 || L.room() == 0)
			return;
		if (L.room() == 1) {
			if (L.isClear(0x194))
				L.say(11000 + 0x182, 2, false);
			return;
		}
		if (L.room() != 7)
			return;
		if (next > 2) {
			L.var(kVarBarks) = next;
			if (next == 3) {
				L.music(0x12, 1);
				act1_483b0(L);
			}
			return;
		}
		L.var(kVarBarks) = next;
		if (L.isClear(0x72)) {
			L.say(11000 + 0xb2, 2, false);
			return;
		}
		if (L.isSet(0x72) && L.isClear(0x55)) {
			L.say(11000 + 0xb0, 2, false);
			return;
		}
		if (L.isClear(0x72) || L.isClear(0x55))
			return;
		L.set(0xdc);
		int g = L.guy();
		L.guy() = 1;
		L.walk(0x1a1, 0x1df, 1);
		L.guy() = 2;
		L.walk(0x19a, 0x109, 4);
		L.guy() = g;
		L.sceneAnim(0x143, 0x14e, 0xd9, 1, 0, L.screenHeight() - 1, 0, 3);
		L.var(kVarBarks) = 3;
		act1_483b0(L);
		L.music(0x12, 1);
		return;
	}

	switch (L.room()) {
	case 1:
		L.var(kVarBarks)++;
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 0xf2, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			act1_470f0(L);
		}
		break;
	case 2:
		L.var(kVarBarks)++;
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 0xf2, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			act1_471e0(L);
		}
		break;
	case 3:
		L.var(kVarBarks)++;
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 0xf2, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			act1_472a0(L);
		}
		break;
	case 4:
		L.var(kVarBarks)++;
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 199, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			L.set(0x172);
			L.say(10000 + 0xc5, 1, false);
			L.walk(0x1df, 0x13f, 6);
		}
		break;
	case 5: {
		L.var(kVarBarks)++;
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 199, 2, false);
			break;
		}
		if (L.var(kVarBarks) != 3)
			break;
		L.set(0x11);
		L.music(0x12, 1);
		L.waitSound(L.voice(21000 + 0x28), 1);
		L.waitSound(L.voice(22000 + 0x21), 1);
		L.var(kVarLoopSfx) = L.sfx(21000 + 0x97, 0x7fff, 1);
		L.faceEachOther();
		L.say(11000 + 0xd2, 2, false);
		int g = L.guy();
		L.guy() = 2;
		if (L.isLying(2))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(2))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0xc, L.guyDir(L.guy()));
		L.guy() = g;
		L.set(0x174);
		break;
	}
	case 6:
		L.var(kVarBarks)++;
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 0xf2, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			act1_48150(L);
		}
		break;
	case 7:
		L.var(kVarBarks)++;
		if (L.var(kVarBarks) == 2) {
			L.say(11000 + 0xf2, 2, false);
		} else if (L.var(kVarBarks) == 3) {
			L.music(0x12, 1);
			act1_481f0(L);
		}
		break;
	case 10: {
		L.var(kVarBarks) = next;
		if (L.isClear(0x1c8)) {
			if (L.var(kVarBarks) == 1) {
				L.waitSound(L.voice(22000 + 0x26), 1);
				L.waitSound(L.voice(21000 + 0x30), 2);
				L.waitSound(L.voice(21000 + 0x31), 3);
				L.say(11000 + 0x112, 2, false);
			} else if (L.var(kVarBarks) > 1) {
				L.banter(1);
				L.sfx(21000 + 0x97, 0x7fff, 0);
				int g = L.guy();
				L.guy() = 1;
				L.walk(0x19e, 0x172, 0);
				L.setDir(0);
				L.guy() = 2;
				L.walk(0x18e, 0x1d7, 7);
				L.setDir(7);
				L.guy() = g;
				L.cutscene(0xaa, 10, 0, 0, 1, 0, 1);
				L.sfx(21000 + 0x98, 0x7fff, 0);
				L.say(11000 + 0x113, 2, false);
			}
		} else {
			int g = L.guy();
			if (L.isClear(0xbe)) {
				L.say(11000 + 0x114, 2, false);
			} else if (L.var(kVarBarks) == 1) {
				L.banter(1);
				L.say(11000 + 0x115, 2, false);
			} else if (L.var(kVarBarks) == 2) {
				L.banter(1);
				L.say(11000 + 0x116, 2, false);
			} else if (L.var(kVarBarks) == 3) {
				L.guy() = 1;
				L.walk(0x1c9, 0x209, 1);
				L.setDir(1);
				L.guy() = 2;
				L.walk(0x19f, 0x18c, 4);
				L.setDir(4);
				L.guy() = g;
				L.waitSound(L.voice(21000 + 0x2f), 1);
				L.set(0xb5);
				L.set(0xbf);
				L.hide(2);
				L.cutscene(0xab, 10, 0, 0, 1, 0, 1);
				L.redraw(1, 1);
			}
		}
		break;
	}
	default:
		break;
	}
}

// Filter on plain walk clicks over a hotspot: 0 = the click is handled (puppies placed), 1 = walk normally
int act1_46c50(Logic &L, int hotspotId) {
	uint id = (uint)hotspotId;
	if ((uint)L.area() >= 2) {
		if (L.area() == 2) {
			switch (L.room()) {
			case 1:
				if (id == 0x53 && L.isClear(100)) {
					L.banter(8);
					return 1;
				}
				break;
			case 2:
				if (id > 0x5e && id < 99) {
					L.banter(8);
					return 1;
				}
				break;
			case 5:
				if (id > 0x6d && id < 0x70) {
					L.banter(8);
					return 1;
				}
				break;
			case 6:
				L.guyY(1) = 0x1c6;
				L.guyX(1) = 199;
				L.guyY(2) = 0x1bf;
				L.guyX(2) = 0x1fb;
				return 0;
			case 7:
				if (id == 0x3c) {
					L.banter(8);
					return 1;
				}
				break;
			case 8:
				if (id == 0x68) {
					L.banter(8);
					if (L.isSet(0x3a) && L.isClear(0x195)) {
						L.set(0x195);
						act1_473f0(L);
						L.changeScene(L.guyY(1), L.guyX(1), L.guyDir(1), L.guyY(2), L.guyX(2), L.guyDir(2), 1, 2, 8);
						return 1;
					}
				}
				break;
			case 9:
				L.guyY(1) = 0x1bf;
				L.guyX(1) = 0x94;
				L.guyY(2) = 0x1bb;
				L.guyX(2) = 0x1af;
				return 0;
			default:
				break;
			}
			return 1;
		}
		if (L.area() != 3 || L.room() != 3 || id < 0x18)
			return 1;
		if (id == 0x18) {
			L.guyY(1) = 0x169;
			L.guyX(1) = 0x187;
			L.guyY(2) = 0x170;
			L.guyX(2) = 0x1de;
			return 0;
		}
		if (id != 0x19)
			return 1;
		L.guyY(1) = 0x1cf;
		L.guyX(1) = 0x65;
		L.guyY(2) = 0x1d6;
		L.guyX(2) = 0x1c2;
		return 0;
	}
	if (L.area() != 1)
		return 1;

	switch (L.room()) {
	case 1:
		if (id < 0x1d)
			return 1;
		if (id == 0x1d) {
			if (L.isClear(0xcd)) {
				L.banter(8);
				L.set(0x4c);
				return 1;
			}
			L.clear(0xcd);
			L.clear(199);
			return 1;
		}
		if (id != 0x1e)
			return 1;
		if (L.isSet(0xcd)) {
			L.clear(0xcd);
			L.clear(199);
			return 1;
		}
		break;
	case 2:
		if (id < 0x2f)
			return 1;
		if (id > 0x2f) {
			if (id < 0x32 || id > 0x33)
				return 1;
			if (L.isSet(0xce)) {
				L.clear(0xcb);
				L.clear(0xce);
				return 1;
			}
		}
		break;
	case 3:
		if (id != 0x21)
			return 1;
		L.banter(8);
		return 1;
	case 6:
		if (id < 0x3a || id > 0x3b)
			return 1;
		if (L.isSet(0xcd)) {
			L.clear(0xcd);
			return 1;
		}
		L.banter(8);
		return 1;
	case 7:
		if (id != 0x56)
			return 1;
		L.banter(8);
		return 1;
	case 8:
		L.guyY(1) = 0x1d7;
		L.guyX(1) = 0xc6;
		L.guyY(2) = 0x1d4;
		L.guyX(2) = 0x1ab;
		return 0;
	case 9:
		L.guyY(1) = 0x1c0;
		L.guyX(1) = 0xa9;
		L.guyY(2) = 0x1be;
		L.guyX(2) = 0x1c1;
		return 0;
	case 10:
		L.clear(0xb5);
		return 1;
	case 11:
		L.guyY(1) = 0x1c2;
		L.guyX(1) = 0xdc;
		L.guyY(2) = 0x1c2;
		L.guyX(2) = 0xdd;
		return 0;
	case 12:
		L.guyY(1) = 0x1cc;
		L.guyX(1) = 200;
		L.guyY(2) = 0x1cc;
		L.guyX(2) = 400;
		return 0;
	default:
		return 1;
	}
	L.banter(8);
	return 1;
}

// The held item is dropped (0x257a0): puppy 2 remarks unless it was over its target
void act1_3c610(Logic &L) {
	int curLayer = L.var(kVarCurLayer);
	int curHotspot = L.var(kVarCurHotspot);
	switch ((uint)L.held()) {
	case 0:
		if (curLayer != -1) {
			if (L.layerIdAt(curLayer) == 0x10)
				return;
			if (L.layerIdAt(curLayer) == 0x11)
				return;
		}
		if (L.recState(0xe, kRecLayer) > 3) {
			L.say(11000 + 0x1f, 2, false);
			return;
		}
		break;
	case 1:
		if (curHotspot != -1) {
			int id = L.hotspotIdAt(curHotspot);
			if (id == 0x41 || id == 0x5c || id == 0x5d)
				return;
		}
		break;
	case 2:
		if (curLayer != -1 && L.layerIdAt(curLayer) == 0x75)
			return;
		if (L.recState(0x79, kRecLayer) > 3) {
			L.say(11000 + 0x11f, 2, false);
			return;
		}
		break;
	case 3:
		if (curLayer != -1 && L.layerIdAt(curLayer) == 0x75)
			return;
		if (L.recState(0x7a, kRecLayer) > 3) {
			L.say(11000 + 0x11f, 2, false);
			return;
		}
		break;
	case 4:
		if (curLayer != -1 && L.layerIdAt(curLayer) == 0x75)
			return;
		if (L.recState(0x78, kRecLayer) > 3) {
			L.say(11000 + 0x11f, 2, false);
			return;
		}
		break;
	case 5:
		if (curHotspot != -1 && L.hotspotIdAt(curHotspot) == 0x37)
			return;
		if (L.recState(0x26, kRecLayer) > 3) {
			L.say(11000 + 0xdf, 2, false);
			return;
		}
		break;
	default:
		return;
	}
	L.banter(3);
}

// Hotspot click: nonzero = run the script even if the puppy isn't at the walk-to point
int act1_3c860(Logic &L, int hotspotId, int held) {
	uint id = (uint)hotspotId;
	if (id != 0x37 && id != 0x41 && id != 0x5c && id != 0x5d)
		return 0;
	return held == 1 ? 1 : 0;
}

// Layer click: nonzero = run the script even if the puppy isn't at the walk-to point
int act1_3c890(Logic &L, int layerId, int held) {
	uint id = (uint)layerId;
	if (id < 0x10)
		return 0;
	if (id <= 0x11) {
		if (held != 0)
			return 0;
	} else {
		if (id != 0x75)
			return 0;
		if (held != 2 && held != 3 && held != 4)
			return 0;
	}
	return 1;
}

// Hotspot click with an item held: nonzero = the item can be used here
int act1_3c7e0(Logic &L, int hotspotId, int held) {
	if (held == 5 && hotspotId == 0x37)
		return 1;
	if ((held == 1 && hotspotId == 0x41) || (held == 1 && hotspotId == 0x5c) || (held == 1 && hotspotId == 0x5d))
		return held;
	return 0;
}

// Layer click with an item held: nonzero = the item can be used here
int act1_3c820(Logic &L, int layerId, int held) {
	if ((held == 0 && (layerId == 0x10 || layerId == 0x11)) || (held == 2 && layerId == 0x75) ||
	    (held == 3 && layerId == 0x75) || (held == 4 && layerId == 0x75))
		return 1;
	return 0;
}

// After a layer's script (called as (&result, arg3, id, held)); *result is left alone
void act1_3c8e0(Logic &L, int *result, int arg, int layerId, int held) {
	if (layerId == 0x75 && L.isSet(0xba) && L.isSet(0xbb) && L.isSet(0xbc)) {
		L.set(0xb9);
		L.var(kVarBarks) = 0;
	}
	L.dropHeld();
}

// Before a hotspot "talk" script: talk depth = bottom of hotspot[id] (indexed by id, as the original)
void act1_37e80(Logic &L, int hotspotId) {
	L.var(kVarTalkDepth) = L.hotspotTopAt(hotspotId) + L.hotspotHeightAt(hotspotId) - 1;
}

// Before a layer "talk" script: talk depth = layer[id] +0x10 (indexed by id, as the original)
void act1_37e20(Logic &L, int layerId) {
	L.var(kVarTalkDepth) = L.layerDepthAt(layerId);
}

// Before an object "talk" script: talk depth = object[id] +0x10 (indexed by id, as the original)
void act1_37e50(Logic &L, int objectId) {
	L.var(kVarTalkDepth) = L.objectDepthAt(objectId);
}

void act1_post_initVars(Logic &L) {
	// All int32
	L.var(kVarLoopSfx) = -1;
	L.var(kVarTalkDepth) = -1;
	L.var(kVarCurLayer) = -1;
	L.var(kVarCurHotspot) = -1;
}

} // End of namespace DreamForge
