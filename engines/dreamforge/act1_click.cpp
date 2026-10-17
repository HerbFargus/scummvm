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

#include "common/util.h"

#include "dreamforge/logic.h"
#include "dreamforge/act1_helpers.h"
#include "dreamforge/act1_click.h"

namespace DreamForge {

// Room code globals (all int32)
enum {
	kLoopSound = 0x6d26c,  // handle of the looping alarm sound, -1 none
	kNagTimer = 0x6d270,   // tick of the next reminder
	kGuardCount = 0xe1328  // times the area 2 room 7 scene has played
};

void act1_click_initVars(Logic &L) {
	L.var(kLoopSound) = -1;
}

// Stops the looping alarm and plays the "got it" effect
static void stopLoopSound(Logic &L) {
	L.stopSound(L.var(kLoopSound));
	L.var(kLoopSound) = -1;
	L.sfx(21000 + 0x98, 0x7fff, 0);
}

struct Act1Tile {
	int flag;
	int y, x;
	int cine, top, left, depth;
};

// Area 1 room 4, checked in this order
static const Act1Tile kRoom4Tiles[] = {
	{ 0x13b, 0x1d0, 0x4e,  0x14b, 0x10d, -10,   0x1b0 },
	{ 0x14f, 0x1d6, 0x43,  0x14c, 0x10f, 7,     0x1b0 },
	{ 0x13c, 0x1d5, 0xc9,  0x14b, 0x10d, 0x71,  0x1b1 },
	{ 0x150, 0x1d6, 0xbe,  0x14c, 0x10f, 0x82,  0x1b0 },
	{ 0x13d, 0x1d5, 0x144, 0x14b, 0x10d, 0xec,  0x1b1 },
	{ 0x151, 0x1d6, 0x139, 0x14c, 0x10f, 0xfd,  0x1b0 },
	{ 0x13e, 0x1d5, 0x1bf, 0x14b, 0x10d, 0x167, 0x1b1 },
	{ 0x152, 0x1d6, 0x1b4, 0x14c, 0x10f, 0x178, 0x1b0 },
	{ 0x13f, 0x1d5, 0x23a, 0x14b, 0x10d, 0x1e2, 0x1b1 },
	{ 0x153, 0x1d6, 0x22a, 0x14c, 0x10f, 0x1f3, 0x1b1 }
};

// Act 1 click handler (0x43d60), run by 0x20d90 before every click is dispatched. The ids are
// the global ids of the object, layer and hotspot under the cursor, -1 for none (clicks on a
// puppy never get here). Returns 0 when the click was handled here, 1 to dispatch it as usual.
int act1_43d60(Logic &L, int objectId, int layerId, int hotspotId) {
	int ret = 1;
	int g;
	const int h1 = L.screenHeight() - 1;

	switch (L.area()) {
	case 0:
		if (L.room() != 1)
			return 1;
		g = L.guy();
		if (L.isSet(0xa7)) {
			if (layerId == 0x6c)
				return 1;
			L.guy() = 1;
			L.setDir(4);
			L.guy() = 2;
			L.guyY(1) = 0x180;
			L.guyX(1) = 0x136;
			L.setDir(4);
			L.guyY(2) = 0x179;
			L.guyX(2) = 0x17c;
			L.guy() = g;
			L.clear(0xa7);
			L.set(0xa8);
			L.sceneAnim(0xe3, 0x115, 0xf9, 0, 0, 0x16a, 0, 0);
			return 1;
		}
		if (!L.isSet(0xa4))
			return 1;
		if (hotspotId == 0x87) {
			L.set(0xea);
			return 1;
		}
		if (hotspotId == 0x86)
			return 1;
		g = L.guy();
		if (!L.isSet(0xea)) {
			L.guy() = 1;
			L.setDir(0);
			// The original also writes dir 1 = 0 (0x647d8), which setDir(0) has just done
			L.guyY(1) = 0x182;
			L.guyX(1) = 0x261;
			L.guy() = g;
			L.clear(0xa4);
			L.hide(1);
			L.sceneAnim(0xdf, 0xbd, 0x236, 0, 0, 0x176, 0, 0);
			return 1;
		}
		L.clear(0xea);
		return 1;

	case 1:
		switch (L.room()) {
		case 1:
			if (!L.isSet(199))
				return 1;
			if (hotspotId != 0x1e && hotspotId != 0x1d) {
				L.say(11000 + 0xf4, 2, false);
				return 0;
			}
			stopLoopSound(L);
			L.set(0xcd);
			L.clear(200);
			if (hotspotId == 0x1e) {
				L.set(0x16f);
				return 1;
			}
			break;
		case 2:
			if (!L.isSet(0x16c)) {
				if (!L.isSet(0xcb))
					return 1;
				if (hotspotId != 0x32 && hotspotId != 0x33) {
					L.say(11000 + 0xf4, 2, false);
					return 0;
				}
				stopLoopSound(L);
				L.clear(0xcc);
				L.set(0xce);
				L.set(0xef);
				return 1;
			}
			if (hotspotId != 0x32 && hotspotId != 0x33) {
				L.say(11000 + 0xf4, 2, false);
				return 0;
			}
			stopLoopSound(L);
			L.clear(0x16d);
			L.clear(0x16c);
			L.set(0xce);
			L.set(0xef);
			break;
		case 3:
			if (L.isSet(0x7c)) {
				L.clear(0x7c);
				g = L.guy();
				L.guy() = 1;
				L.setDir(6);
				L.guyX(1) = 0x173;
				L.guy() = 2;
				L.guyY(1) = 0x17c;
				L.setDir(4);
				L.guyY(2) = 0x17a;
				L.guyX(2) = 0x19b;
				L.sceneAnim(0x141, 0x12e, 0x150, 0, 0, 0x176, 0, 0);
				L.show(3);
				L.guy() = g;
				return 1;
			}
			if (!L.isSet(0xd3))
				return 1;
			if (hotspotId == 0xa4)
				return 1;
			L.say(11000 + 0xf4, 2, false);
			return 0;
		case 4:
			g = L.guy();
			if (!L.isClear(0x16a))
				return 1;
			if (layerId != -1)
				return 1;
			if (hotspotId == 0x2a)
				return 1;
			L.guy() = 1;
			for (uint i = 0; i < ARRAYSIZE(kRoom4Tiles); i++) {
				const Act1Tile &t = kRoom4Tiles[i];
				if (L.isSet(t.flag)) {
					L.setPos(t.y, t.x);
					L.setDir(6);
					L.clear(t.flag);
					L.set(0x16a);
					L.sceneAnim(t.cine, t.top, t.left, 1, 0, t.depth, 0, 1);
					L.guy() = g;
					return 1;
				}
			}
			L.guy() = g;
			return 0;
		case 5:
			if (!L.isSet(0x174))
				return 1;
			if (hotspotId != 0xa2 && hotspotId != 0xa1) {
				L.say(11000 + 0xd8, 2, false);
				if (!L.isClear(0x175))
					return 0;
				L.set(0x175);
				L.var(kNagTimer) = (int32)L.ticks() + (L.isSet(0x175) ? 0x8c : 200);
				return 0;
			}
			stopLoopSound(L);
			L.set(0x176);
			L.var(kNagTimer) = (int32)L.ticks() + (L.isSet(0x175) ? 0x8c : 200);
			return 1;
		case 6:
			if (L.isSet(0x154)) {
				if (hotspotId == -1 && layerId == -1 && objectId == -1)
					return 1;
				if (hotspotId == 0x3a) {
					stopLoopSound(L);
					L.clear(0x154);
					L.set(0xcd);
					return 1;
				}
				L.say(11000 + 0xf4, 2, false);
				return 0;
			}
			if (hotspotId == 0x31)
				return 1;
			if (hotspotId == 0x30)
				return 1;
			if (!L.isSet(0x4e))
				return 1;
			L.clear(0x4e);
			g = L.guy();
			L.guy() = 1;
			L.setDir(6);
			L.guyY(1) = 0x19d;
			L.guyX(1) = 0x19c;
			L.guy() = g;
			L.sceneAnim(0x3b, 0xcc, 0x167, 0, 0, 0x176, 0, 1);
			return 1;
		case 7:
			if (L.isSet(0xed) || L.isSet(0x170)) {
				if (hotspotId == 0x4a) {
					stopLoopSound(L);
					L.set(0xcd);
					L.clear(0xed);
					L.clear(0x170);
				} else {
					ret = 0;
					L.say(11000 + 0xf4, 2, false);
				}
			}
			L.clear(0xd6);
			L.clear(0xd2);
			return ret;
		case 10:
			g = L.guy();
			if (!L.isSet(0xb2))
				return 1;
			if (hotspotId == 0x8b)
				return 1;
			if (hotspotId == 0x8c)
				return 1;
			if (hotspotId == 0x8d)
				return 1;
			if (layerId == 0x72)
				return 1;
			L.guy() = 1;
			L.setDir(0);
			L.guyY(1) = 0x188;
			L.guyX(1) = 0x110;
			L.guy() = g;
			L.clear(0xb2);
			L.sceneAnim(0x99, 0xc5, 0xf6, 1, 0, h1, 0, 1);
			return 1;
		default:
			return 1;
		}
		// Rooms 1 and 2 when the alarm was answered
		L.set(0xcb);
		return 1;

	case 2:
		break;

	case 3:
		if (L.room() != 1)
			return 1;
		if (layerId != 5 && L.isSet(0x3e)) {
			L.clear(0x3e);
			g = L.guy();
			L.guyY(1) = 0x17d;
			L.guyX(1) = 0x296;
			L.guy() = 1;
			L.clearLying(1);
			L.setDir(6);
			L.guy() = g;
			L.sceneAnim(0x13, 0x11c, 0x276, 1, 0, 0x176, 0, 1);
		}
		if (layerId != 1 && L.isSet(0x3f)) {
			L.clear(0x3f);
			g = L.guy();
			L.guyX(1) = 0x238;
			L.guyY(1) = 0x176;
			L.guy() = 1;
			L.setDir(4);
			L.guy() = g;
			L.sceneAnim(3, 0x103, 0x1c0, 1, 0, 0x176, 0, 1);
		}
		if (hotspotId == 6)
			return 1;
		if (layerId == 2)
			return 1;
		if (!L.isSet(0x41))
			return 1;
		L.clear(0x41);
		g = L.guy();
		L.guyY(1) = 0x177;
		L.guyX(1) = 0x1dc;
		L.guy() = 1;
		L.setDir(6);
		L.guy() = g;
		L.sceneAnim(6, 0xf6, 0x1ca, 0, 0, 0x176, 0, 1);
		return 1;

	default:
		return 1;
	}

	// Area 2. Room 8's sequences are repeated verbatim in the original; they are shared here.

	// Room 8, the kennel run via hotspot 0x71
	auto kennel71 = [&L, h1]() {
		L.set(0x42);
		if (L.isClear(0x36)) {
			L.hide(1);
			int s = L.guy();
			L.guy() = 1;
			L.setDir(5);
			L.guy() = s;
			L.sceneAnim(0xbe, 0x88, 0x1f9, 1, 0, h1, 0, 0);
			L.set(0x84);
			if (!L.isSet(0x52))
				L.sceneAnim(0xca, 0x2d, 0x24d, 1, 0, h1, 0, 0);
			else
				L.sceneAnim(0xc6, 0x2e, 0x24d, 1, 0, h1, 0, 0);
			L.hide(1);
			L.sceneAnim(0xbc, 0x6b, 0x1ff, 1, 0, h1, 0, 0);
			s = L.guy();
			L.guy() = 2;
			L.walk(0x177, 0x179, 4);
			L.guy() = s;
			L.hide(2);
			L.clear(0x85);
			L.sceneAnim(0xbd, 0x6a, 0x14a, 1, 0, h1, 0, 0);
			L.sceneAnim(0xc5, 0x2c, 0x24c, 1, 0, h1, 0, 0);
			L.say(10000 + 0x11c, 1, false);
			L.clear(0x52);
			L.set(0x51);
			if (L.isSet(0x3a) && L.isClear(0x195)) {
				L.set(0x195);
				act1_473f0(L);
			}
			L.changeScene(0xf4, 0x1f5, 6, 0xf4, 0x1df, 6, 1, 3, 1);
			return 1;
		}
		L.clear(0x42);
		L.hide(1);
		L.sceneAnim(0xbe, 0x88, 0x1f9, 1, 0, h1, 0, 0);
		L.say(10000 + 0x11b, 1, false);
		L.say(11000 + 0x125, 2, false);
		return 1;
	};

	// Room 8, the kennel run via hotspot 0x70
	auto kennel70 = [&L, h1]() {
		L.set(0x42);
		int s = L.guy();
		L.guy() = 1;
		L.setDir(5);
		L.guy() = s;
		L.sceneAnim(0xbe, 0x88, 0x1f9, 1, 0, h1, 0, 1);
		L.set(0x84);
		if (!L.isSet(0x52))
			L.sceneAnim(0xca, 0x2d, 0x24d, 1, 0, h1, 0, 0);
		else
			L.sceneAnim(0xc6, 0x2e, 0x24d, 1, 0, h1, 0, 0);
		L.hide(1);
		L.sceneAnim(0xbc, 0x6b, 0x1ff, 1, 0, h1, 0, 0);
		s = L.guy();
		L.guy() = 2;
		L.walk(0x177, 0x179, 4);
		L.guy() = s;
		L.hide(2);
		L.clear(0x85);
		L.sceneAnim(0xbd, 0x6a, 0x14a, 1, 0, h1, 0, 0);
		L.sceneAnim(0xc5, 0x2c, 0x24c, 1, 0, h1, 0, 0);
		L.say(10000 + 0x117, 1, false);
		L.clear(0x52);
		L.set(0x53);
		L.set(0x42);
		if (L.isSet(0x3a) && L.isClear(0x195)) {
			L.set(0x195);
			act1_473f0(L);
		}
		L.changeScene(0x10e, 0x105, 4, 0x10e, 0x213, 0, 1, 1, 5);
		return 1;
	};

	// Room 8, layer 0x49: the reply depends on that layer's state
	auto layer49 = [&L]() {
		int st = L.recState(0x49, kRecLayer);
		int line;
		if (st == 0) {
			line = 0x168;
		} else if (st == 1) {
			line = 0x131;
		} else if (st == 2) {
			line = 0x16a;
		} else {
			L.banter(4);
			return 1;
		}
		L.hide(1);
		L.sceneAnim(0xb0, 0x9a, 0x200, 1, 0, 0, 0, 0);
		L.say(11000 + line, 2, false);
		L.hide(1);
		L.clear(0x9d);
		L.sceneAnim(0xb2, 0x9f, 0x207, 1, 0, 0, 0, 0);
		return 1;
	};

	// Room 8, closing what flags 0x86 / 0x96 left open
	auto close86 = [&L, h1]() {
		if (L.isSet(0x86)) {
			L.clear(0x86);
			L.sceneAnim(0x107, 0xda, 0x149, 1, 0, h1, 0, 0);
		}
		L.clear(0x96);
		int s = L.guy();
		L.guy() = 1;
		L.setDir(5);
		L.guyY(1) = 0xdb;
		L.guyX(1) = 0x22c;
		L.guy() = s;
		L.sceneAnim(0xae, 0x9c, 0x1bf, 1, 0, h1, 0, 0);
	};

	switch (L.room()) {
	case 1:
		g = L.guy();
		if (!L.isSet(0x65)) {
			if (L.isSet(0x16b)) {
				if (hotspotId == 0x55) {
					L.clear(0x16b);
					g = L.guy();
					L.guy() = 2;
					L.walk(399, 0x119, 0);
					L.guy() = 1;
					L.walk(399, 0xe5, 0);
					L.guy() = g;
					L.hide(3);
					L.sceneAnim(0x172, 0x96, 0xa8, 0, 0, 0, 0, 0);
					L.set(0x1cd);
					L.sceneAnim(0x173, 0x92, 0xb8, 1, 0, 0xc2, 0, 0);
					L.stopSound(L.var(kLoopSound));
					L.var(kLoopSound) = -1;
					L.cutscene(0x15f, 10, 0, 0, 1, 0, 1);
					L.cutscene(0x15e, 10, 0, 0, 0, 0, 1);
					L.set(0x194);
					L.clear(100);
					L.set(0x1cc);
					L.changeScene(L.guyY(1), L.guyX(1), L.guyDir(1), L.guyY(2), L.guyX(2), L.guyDir(2), 1, 2, 1);
					L.clear(0x1cc);
					return 0;
				}
				if (hotspotId == 0x53) {
					stopLoopSound(L);
					L.clear(0x16b);
					L.set(0x16e);
					return 1;
				}
				L.say(11000 + 0xf4, 2, false);
				return 0;
			}
		} else if (hotspotId != 0x57 && layerId != 0x36 && layerId != 0x35) {
			L.guyY(1) = 399;
			L.guyY(2) = 399;
			L.guyX(1) = 0xf6;
			L.guyX(2) = 0x133;
			L.guy() = 1;
			L.setDir(5);
			L.guy() = 2;
			L.setDir(4);
			L.clear(0x65);
			L.sceneAnim(0xd7, 0x96, 0xb0, 1, 0, 0, 0, 0);
			L.guy() = g;
			return 1;
		}
		break;
	case 5:
		if (hotspotId == 0x6c)
			return 1;
		if (!L.isSet(0x7a))
			return 1;
		L.clear(0x7a);
		g = L.guy();
		L.guy() = 1;
		L.setDir(0);
		L.guy() = g;
		L.sceneAnim(299, 0xfa, 0xdb, 1, 0, 0x19, 0, 1);
		break;
	case 7:
		if (L.isSet(0xda)) {
			if (hotspotId == 0xa6) {
				L.set(0xdb);
				return 1;
			}
			L.say(11000 + 0xf4, 2, false);
			return 0;
		}
		if (L.isSet(0xdd)) {
			if (hotspotId == 0x3d) {
				L.set(0xe6);
				return 1;
			}
			L.say(11000 + 0xf4, 2, false);
			return 0;
		}
		if (L.isSet(0x57)) {
			L.clear(0x57);
			g = L.guy();
			L.guyY(1) = 0x1bd;
			L.guyX(1) = 0xa4;
			L.guy() = 1;
			L.setDir(4);
			L.guy() = g;
			L.sceneAnim(0x5d, 0x40, 0, 1, 0, 0x1db, 0, 1);
			if (L.var(kGuardCount) == 0) {
				L.say(10000 + 0xbd, 1, false);
				L.say(11000 + 0x189, 2, false);
				L.say(11000 + 0x18a, 2, false);
				L.say(10000 + 0xac, 1, false);
				L.say(11000 + 0xa6, 2, false);
			} else {
				L.say(11000 + 0xa7, 2, false);
			}
			L.var(kGuardCount)++;
			if (hotspotId == 0x40)
				return 0;
		}
		break;
	case 8:
		g = L.guy();
		if (L.isSet(0x8a)) {
			if (hotspotId == 0x74)
				return 1;
			L.guy() = 1;
			L.guyY(1) = 0x164;
			L.guyX(1) = 0x171;
			L.setDir(4);
			L.clear(0x8a);
			L.sceneAnim(0xb9, 0x130, 0x105, 1, 0, 0, 0, 1);
			L.guy() = g;
			return 1;
		}
		if (L.isSet(0x85)) {
			if (layerId != 0x49) {
				if (layerId == 0x43) {
					L.clear(0x85);
					L.set(0x97);
					L.hide(1);
					L.sceneAnim(0xc9, 0xa4, 0x1b0, 1, 0, h1, 0, 0);
					return 1;
				}
				if (hotspotId == 0x6d) {
					L.sceneAnim(0xbb, 0x85, 0x1fe, 1, 0, 0, 0, 1);
					L.say(10000 + 0x11a, 1, false);
					L.say(11000 + 0x124, 2, false);
					return 1;
				}
				if (hotspotId == 0x71)
					return kennel71();
				if (hotspotId != 0x70) {
					if (hotspotId == 0x72)
						return 1;
					L.clear(0x85);
					L.hide(1);
					L.sceneAnim(0xc9, 0xa4, 0x1b0, 1, 0, h1, 0, 0);
					g = L.guy();
					L.guy() = 1;
					L.setDir(0);
					L.guyY(1) = 0x178;
					L.guyX(1) = 0x17d;
					L.guy() = g;
					L.clear(0x86);
					L.clear(0x96);
					L.sceneAnim(0xac, 0xdf, 0x14b, 1, 0, h1, 0, 0);
					return 1;
				}
				return kennel70();
			}
			return layer49();
		}
		if (L.isSet(0x86) || L.isSet(0x96)) {
			if (hotspotId == 0x71) {
				close86();
				return kennel71();
			}
			if (hotspotId == 0x70) {
				close86();
				return kennel70();
			}
			if (hotspotId == 0x72) {
				close86();
				return 1;
			}
			if (hotspotId == 0x6d) {
				close86();
				L.sceneAnim(0xbb, 0x85, 0x1fe, 1, 0, 0, 0, 1);
				L.say(10000 + 0x11a, 1, false);
				L.say(11000 + 0x124, 2, false);
				return 1;
			}
			if (layerId != 0x49) {
				if (layerId == 0x43)
					return 1;
				L.hide(1);
				g = L.guy();
				L.guyX(1) = 0x17d;
				L.guy() = 1;
				L.guyY(1) = 0x178;
				L.setDir(0);
				L.guy() = g;
				if (!L.isSet(0x86)) {
					L.clear(0x96);
					L.sceneAnim(0x15b, 0xdf, 0x14b, 1, 0, h1, 0, 0);
					return 1;
				}
				L.clear(0x86);
				L.sceneAnim(0xac, 0xdf, 0x14b, 1, 0, h1, 0, 0);
				return 1;
			}
			close86();
			return layer49();
		}
		g = L.guy();
		if (!L.isSet(0x87))
			return 1;
		if (hotspotId == 0xa3) {
			L.guy() = 1;
			L.walk(L.guyY(1), L.guyX(1), 4);
			L.guy() = g;
			if (L.isSet(0x91)) {
				L.say(10000 + 0x130, 1, false);
				L.hide(1);
				L.sceneAnim(0x136, 0xaf, 0x9a, 1, 0, 0, 0, 0);
				g = L.guy();
				L.guy() = 1;
				L.guyY(1) = 0x15a;
				L.guyX(1) = 0x1f1;
				L.setDir(4);
				L.guy() = 2;
				L.walk(0x1c1, 100, 3);
				L.guy() = g;
				L.clear(0x91);
				L.set(0x90);
				L.set(0x92);
				L.set(0x9a);
				L.clear(0x9b);
				L.show(1);
				int cut;
				g = L.guy();
				if (!L.isSet(0x89)) {
					cut = 0xc2;
				} else {
					L.guyY(1) = 0x15a;
					L.guy() = 1;
					L.guyX(1) = 399;
					L.setDir(4);
					cut = 0xb1;
				}
				L.guy() = g;
				L.cutscene(cut, 10, 0, 0, 1, 0, 1);
				L.redraw(1, 1);
				if (L.isClear(0x89))
					L.sceneAnim(199, 0x12a, 0x1c7, 1, 0, 0, 0, 1);
				L.say(10000 + 300, 1, false);
				L.say(11000 + 0x130, 2, false);
				L.clear(0x87);
				L.set(0x8b);
				L.set(0x94);
				return 0;
			}
			if (!L.isSet(0x8d)) {
				L.say(10000 + 0x128, 1, false);
				L.hide(1);
				L.sceneAnim(0x136, 0xaf, 0x9a, 1, 0, 0, 0, 0);
				L.set(0x8c);
				L.set(0x8d);
				L.clear(0x9b);
				g = L.guy();
				L.guy() = 1;
				L.setDir(4);
				L.guy() = g;
				L.sceneAnim(0xbf, 0x72, 0x102, 1, 0, 300, 0, 1);
				L.say(11000 + 300, 2, false);
				L.say(10000 + 0x129, 1, false);
				L.say(11000 + 0x12d, 2, false);
				L.clear(0x87);
				L.set(0x8b);
				return 0;
			}
			L.hide(1);
			L.sceneAnim(0x136, 0xaf, 0x9a, 1, 0, 0, 0, 0);
			L.set(0x8e);
			L.set(0x91);
			L.clear(0x9b);
			g = L.guy();
			L.guy() = 1;
			L.setDir(4);
			L.guy() = g;
			L.sceneAnim(0xc0, 0x62, 0x102, 1, 0, 300, 0, 1);
			L.clear(0x87);
			L.set(0x8b);
			L.say(11000 + 0x169, 2, false);
			return 0;
		}
		if (hotspotId == 0x73) {
			L.sceneAnim(0xb7, 0x61, 0x95, 1, 0, 300, 0, 0);
			return 1;
		}
		L.clear(0x87);
		g = L.guy();
		L.guy() = 1;
		L.guyY(1) = 0x18a;
		L.guyX(1) = 0x117;
		L.setDir(5);
		L.guy() = g;
		L.sceneAnim(0xad, 0xaf, 0x97, 1, 0, 300, 0, 1);
		break;
	case 10:
		if (!L.isSet(0x99)) {
			if (!L.isSet(0xb1))
				return 1;
			if (layerId == 0x5a)
				return 1;
			L.clear(0xb1);
			g = L.guy();
			L.guy() = 1;
			L.hide(1);
			L.sceneAnim(0xeb, 0x14d, 0xe1, 0, 0, 0x17c, 0, 0);
			L.guy() = 2;
			// Puppy 2 gets up first
			if (L.isLying(2))
				L.oneShot(0x1c, L.guyDir(L.guy()));
			else if (L.isSitting(2))
				L.oneShot(7, L.guyDir(L.guy()));
			L.oneShot(10, 0);
			L.clear(0xb6);
			L.guy() = 1;
			L.setDir(5);
			L.guyX(1) = 0x121;
			L.guyY(1) = 0x177;
			L.guy() = g;
			L.sceneAnim(0x135, 0x113, 0xe6, 0, 0, 0x17c, 0, 1);
			return 1;
		}
		if (hotspotId == 0x7c)
			return 1;
		if (hotspotId == 0x82)
			return 1;
		L.clear(0x99);
		g = L.guy();
		L.guy() = 1;
		L.setDir(6);
		L.guy() = g;
		L.sceneAnim(0xf1, 0xe7, 0x15e, 1, 0, 0x176, 0, 1);
		break;
	case 11:
		if (hotspotId != 0x36 && L.isSet(0x5b)) {
			L.objNoLoop(0x10);
			while (!L.objDone(0x10))
				L.step();
			L.objLoop(0x10);
			L.clear(0xec);
			L.clear(0x5b);
			L.guyY(1) = 0x171;
			L.guyX(1) = 0xf9;
			L.sceneAnim(0x76, 0x140, 0xd3, 1, 0, 0x1db, 0, 1);
			return 1;
		}
		break;
	default:
		break;
	}
	return 1;
}

} // End of namespace DreamForge
