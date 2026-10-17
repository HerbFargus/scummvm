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
#include "dreamforge/act1_afterload.h"

namespace DreamForge {

void act1_afterload_initVars(Logic &L) {
	L.var(0x6d26c) = -1;  // int32, looping sound handle
	// 0x6d264 (int32) and 0xe0fec (int32) start at 0
}

// Act 1 after-load handler: each room's arrival events once the scene is on screen
void act1_418b0(Logic &L, int room, int area) {
	int g;

	switch (area) {
	case 0:
		if (room == 1) {
			L.clear(0xa9);
			if (L.isSet(0xa0)) {
				g = L.guy();
				L.clear(0xa0);
				L.sceneAnim(0xd9, 0x103, 0xef, 0, 0, 0x16b, 0, 3);
				if (L.isSet(0xa1)) {
					L.clear(0xa1);
					L.stopSound(L.var(0x6d26c));
					L.var(0x6d26c) = -1;
					L.guy() = 1;
					L.setDir(5);
					L.guyY(1) = 0x180;
					L.guyX(1) = 0x136;
					L.guy() = 2;
					L.setDir(4);
					L.guyY(2) = 0x179;
					L.guyX(2) = 0x17c;
					L.sceneAnim(0xda, 0x139, 0x10e, 0, 0, 0x154, 0, 3);
				}
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0x42)) {
				g = L.guy();
				L.clear(0x42);
				L.clear(0xa6);
				L.guy() = 1;
				L.setDir(0);
				L.guyY(1) = 0x188;
				L.guyX(1) = 0x23f;
				L.guy() = 2;
				L.setDir(0);
				L.guyY(2) = 0x182;
				L.guyX(2) = 0x288;
				L.hide(3);
				L.set(0xa5);
				L.sceneAnim(0xdb, 0x74, 0x2cc, 0, 0, L.screenHeight() - 1, 0, 0);
				L.clear(0xaa);
				L.sceneAnim(0xdc, 0x73, 0x217, 0, 0, 0x109, 0, 0);
				L.guy() = 1;
				L.walk(0x188, 0x208, 0);
				L.guy() = g;
				L.roomDone();
				return;
			}
		}
		break;

	case 1:
		switch (room) {
		case 1:
			if (L.isClear(0x43)) {
				L.set(0x43);
				g = L.guy();
				L.guyX(1) = 0xf6;
				L.guyY(1) = 0x18a;
				L.hide(3);
				L.sceneAnim(0x20, 0x13c, 0x69, 0, 0, L.screenHeight() - 1, 0, 0);
				L.show(1);
				L.guy() = 1;
				L.walk(0x193, 0x186, 0);
				L.say(10000 + 0xea, 1, false);
				L.say(11000 + 0xf5, 2, false);
				L.guyY(2) = 0x18a;
				L.guyX(2) = 0xf6;
				L.sceneAnim(0x21, 0x13c, 0x69, 0, 0, L.screenHeight() - 1, 0, 0);
				L.guy() = 2;
				L.walk(0x1bb, 0x9d, 3);
				L.guy() = 1;
				L.walk(L.guyY(1), L.guyX(1), 7);
				L.say(10000 + 0xeb, 1, false);
				L.say(11000 + 0xf6, 2, false);
				L.say(10000 + 0xec, 1, false);
				L.say(11000 + 0xf7, 2, false);
				L.say(10000 + 0xed, 1, false);
				L.say(11000 + 0xf8, 2, false);
				L.say(10000 + 0xee, 1, false);
				L.say(11000 + 0xf9, 2, false);
				L.say(10000 + 0xef, 1, false);
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0x1c4)) {
				L.clear(0x1c4);
				g = L.guy();
				L.guy() = 2;
				L.walk(0x1bb, 0x9d, 3);
				L.guy() = 1;
				L.walk(0x191, 0x135, 6);
				L.guy() = g;
				L.roomDone();
				return;
			}
			break;

		case 2:
			if (L.isSet(0xcb)) {
				L.set(0x11);
				g = L.guy();
				L.guy() = 1;
				L.setDir(1);
				L.guyY(1) = 0x197;
				L.guyX(1) = 0x1a0;
				L.guy() = 2;
				L.setDir(1);
				L.guyY(2) = 0x197;
				L.guyX(2) = 0x136;
				L.guy() = g;
				L.sceneAnim(0x4c, 0x15a, 0x108, 0, 0, L.screenHeight() - 1, 0, 0);
				L.set(0xcc);
				L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
				L.roomDone();
				return;
			}
			if (L.isSet(0x4f)) {
				L.clear(0x4f);
				L.sceneAnim(0x4b, 0x140, 0x1c1, 0, 0, L.screenHeight() - 1, 0, 2);
			} else if (L.isSet(0x1c5)) {
				L.clear(0x1c5);
				g = L.guy();
				L.guy() = 1;
				L.walk(0x199, 0x13c, 5);
				L.guy() = g;
				L.roomDone();
				return;
			}
			break;

		case 3:
			if (L.isSet(0xd5)) {
				L.clear(0xd5);
				L.stopSound(L.var(0x6d26c));
				L.var(0x6d26c) = -1;
				g = L.guy();
				int n;
				if (!L.isClear(0xd4)) {
					L.clear(0x1ba);
					L.cutscene(0x161, 10, 0, 0, 1, 0, 1);
					L.cutscene(0x162, 10, 0, 0, 1, 0, 1);
					n = 0x163;
				} else {
					L.clear(0x1ba);
					n = 0x36;
				}
				L.cutscene(n, 10, 0, 0, 1, 0, 1);
				L.set(0x1b8);
				L.changeScene(0, 0, 0, 0, 0, 0, 1, 1, 11);
				L.say(10000 + 0xea, 1, false);
				L.changeScene(L.guyY(1), L.guyX(1), L.guyDir(1), L.guyY(2), L.guyX(2), L.guyDir(2), 1, 1, 3);
				L.clear(0x1b8);
				L.hide(3);
				// The original restores the current puppy from a stale register (0x426d4)
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0x46)) {
				g = L.guy();
				L.clear(0x46);
				L.guy() = 1;
				L.setDir(4);
				L.guyY(1) = 0x1b1;
				L.guyX(1) = 0x193;
				L.guy() = 2;
				L.setDir(0);
				L.guyY(2) = 0x1b0;
				L.guyX(2) = 0x1f5;
				L.clear(0x1b9);
				L.sceneAnim(0x30, 0x156, 0x152, 1, 0, L.screenHeight() - 1, 0, 3);
				L.guy() = g;
				L.say(11000 + 0x79, 2, false);
				L.say(10000 + 0x6c, 1, false);
				L.clear(0xd0);
				L.roomDone();
				return;
			}
			if (L.isSet(0x172)) {
				L.clear(0x172);
				L.var(0x6d264) = 3;
				act1_472a0(L);
				L.roomDone();
				return;
			}
			break;

		case 5:
			if (L.isSet(0x42)) {
				L.set(0x53);
				L.set(0x171);
				L.sceneAnim(99, 0x26, 99, 0, 0, 0xfd, 0, 0);
				L.clear(0x173);
				L.sceneAnim(100, 0x79, 100, 0, 0, 0xfd, 0, 2);
				L.clear(0x42);
				L.clear(0x171);
				L.sceneAnim(0x66, 0x28, 0x67, 0, 0, 0xfd, 0, 1);
				L.roomDone();
				return;
			}
			if (L.isSet(0x177)) {
				L.clear(0x177);
				L.roomDone();
				return;
			}
			break;

		case 6:
			g = L.guy();
			if (L.isSet(0xef)) {
				L.clear(0xef);
				L.set(0xcf);
				L.sceneAnim(0x39, 0x152, 0x164, 0, 0, L.screenHeight() - 1, 0, 0);
				L.waitSound(L.sfx(21000 + 0x97, 0x7fff, 0), 1);
				L.waitSound(L.voice(21000 + 0x2b), 1);
				L.waitSound(L.voice(22000 + 0x24), 1);
				L.waitSound(L.sfx(21000 + 0x98, 0x7fff, 0), 1);
				L.clear(0xcf);
				L.guy() = 1;
				L.setDir(7);
				L.guyY(1) = 0x186;
				L.guyX(1) = 0x10b;
				L.guy() = 2;
				L.setDir(7);
				L.guyY(2) = 0x17f;
				L.guyX(2) = 0x13b;
				L.sceneAnim(0x80, 0x141, 0xe1, 0, 0, L.screenHeight() - 1, 0, 0);
			}
			L.guy() = g;
			L.roomDone();
			return;

		case 7:
			g = L.guy();
			if (L.isSet(0x16f) && L.var(0xe0fec) == 0) {
				L.set(0x11);
				g = L.guy();
				L.guy() = 1;
				L.setDir(7);
				L.guyY(1) = 0x186;
				L.guyX(1) = 0xa9;
				L.guy() = 2;
				L.setDir(7);
				L.guyX(2) = 0x10d;
				L.guyY(2) = 0x186;
				L.guy() = g;
				L.sceneAnim(0x15d, 0x144, 0, 0, 0, L.screenHeight() - 1, 0, 0);
				L.set(0x170);
				L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
			} else if (L.isSet(0x69)) {
				L.clear(0x69);
				L.cutscene(0x3e, 10, 0, 0, 1, 0, 1);
				L.show(3);
				g = L.guy();
				L.guy() = 2;
				L.walk(0x165, 0x2b, 5);
				L.guy() = 1;
				L.walk(0x191, 0xaf, 1);
				L.guy() = g;
			} else if (L.isSet(0x6c)) {
				L.clear(0x6c);
				L.guy() = 1;
				L.setDir(7);
				L.guyY(1) = 0x175;
				L.guyX(1) = 0x90;
				L.guy() = 2;
				L.setDir(0);
				L.guyY(2) = 0x176;
				L.guyX(2) = 0x47;
				L.sceneAnim(0x3f, 0xf7, 0x23, 0, 0, L.screenHeight() - 1, 0, 0);
			} else if (L.isSet(0x6b)) {
				L.clear(0x6b);
				g = L.guy();
				L.guy() = 2;
				L.walk(0x165, 0x2b, 5);
				L.guy() = 1;
				L.walk(0x191, 0xaf, 1);
				L.guy() = g;
			}
			if (L.isClear(0x169) && L.isClear(0x16f)) {
				L.set(0x169);
				L.guy() = 2;
				L.walk(0x165, 0x2b, 3);
				L.say(11000 + 0x8f, 2, false);
				L.guy() = 1;
				L.walk(0x191, 0xaf, 2);
				L.say(10000 + 0x8c, 1, false);
				L.set(0xd6);
			}
			L.guy() = g;
			L.roomDone();
			return;

		case 10:
			if (L.isSet(0xaf) && L.var(0xe0fec) == 0) {
				g = L.guy();
				L.show(3);
				L.set(0xb8);
				L.cutscene(0x9c, 10, 0, 0, 1, 0, 1);
				L.say(10000 + 0xf9, 1, false);
				L.say(11000 + 0xff, 2, false);
				L.guy() = 1;
				L.walk(0x1aa, 0x204, 0);
				L.say(10000 + 0xfa, 1, false);
				L.say(11000 + 0x100, 2, false);
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0xae) && L.var(0xe0fec) == 0) {
				g = L.guy();
				L.show(3);
				L.cutscene(0x9b, 10, 0, 0, 1, 0, 1);
				L.guy() = 1;
				L.walk(0x1aa, 0x204, 0);
				L.say(10000 + 0xf8, 1, false);
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0xad) && L.var(0xe0fec) == 0) {
				g = L.guy();
				L.show(3);
				L.cutscene(0x9b, 10, 0, 0, 1, 0, 1);
				L.guy() = 1;
				L.walk(0x19e, 0x149, 0);
				L.say(10000 + 0xf7, 1, false);
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0xac) && L.var(0xe0fec) == 0) {
				g = L.guy();
				L.show(3);
				L.cutscene(0x9b, 10, 0, 0, 1, 0, 1);
				L.guy() = 1;
				L.say(11000 + 0xfd, 2, false);
				L.walk(0x1aa, 0x204, 0);
				L.say(10000 + 0xf3, 1, false);
				L.say(11000 + 0xfe, 2, false);
				L.say(10000 + 0xf4, 1, false);
				L.waitSound(L.voice(22000 + 0x25), 1);
				L.say(10000 + 0xf5, 1, false);
				L.guy() = g;
				L.roomDone();
				return;
			}
			break;

		default:
			break;
		}
		break;

	case 2:
		switch (room) {
		case 1:
			if (L.isSet(0x16f)) {
				L.clear(0x16f);
				L.clear(0x170);
				L.waitSound(L.voice(21000 + 0x3b), 1);
				L.roomDone();
				return;
			}
			if (L.isSet(100)) {
				L.set(0x11);
				L.var(0x6d26c) = L.sfx(21000 + 0x97, 0x7fff, 1);
				L.roomDone();
				return;
			}
			if (L.isSet(0x1ca)) {
				L.clear(0x1ca);
				g = L.guy();
				L.guy() = 2;
				L.walk(0x199, 0x20e, 0);
				L.guy() = 1;
				L.walk(0x199, 0x150, 4);
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0x1cb)) {
				L.clear(0x1cb);
				g = L.guy();
				L.guy() = 2;
				L.walk(0x199, 0x20e, 0);
				L.guy() = 1;
				L.walk(0x199, 0x150, 6);
				L.guy() = g;
				L.roomDone();
				return;
			}
			break;

		case 2:
			if (L.isSet(0x74)) {
				L.clear(0x74);
				g = L.guy();
				L.guyY(2) = 0x19f;
				L.guyX(2) = 0x148;
				L.hide(3);
				L.sceneAnim(0x47, 0x160, 0x11d, 0, 0, L.screenHeight() - 1, 0, 0);
				L.show(2);
				L.guy() = 2;
				L.walk(0x1b8, 100, 4);
				L.guyY(1) = 0x19f;
				L.guyX(1) = 0x148;
				L.guy() = g;
				L.sceneAnim(0x46, 0x160, 0x11d, 0, 0, L.screenHeight() - 1, 0, 0);
				if (L.isSet(0xef)) {
					L.waitSound(L.voice(21000 + 0x3b), 1);
					L.clear(0xef);
				}
				L.clear(0xcb);
				L.clear(0xce);
				L.roomDone();
				return;
			}
			if (L.isSet(0x76)) {
				L.clear(0x76);
				g = L.guy();
				L.guy() = 2;
				L.setDir(0);
				L.guyY(2) = 0x17d;
				L.guyX(2) = 0x166;
				L.hide(2);
				L.clear(0x1bf);
				L.sceneAnim(0x48, 0x140, 0x150, 0, 0, 0, 0, 2);
				L.show(2);
				L.guy() = 1;
				L.walk(0x19f, 0x148, 6);
				L.guy() = 2;
				L.walk(0x1b8, 100, 4);
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0x1c6)) {
				L.clear(0x1c6);
				g = L.guy();
				L.guy() = 1;
				L.walk(0x19f, 0x148, 4);
				L.guy() = g;
				L.roomDone();
				return;
			}
			if (L.isSet(0x1c7)) {
				L.clear(0x1c7);
				g = L.guy();
				L.guy() = 1;
				L.walk(0x19f, 0x148, 2);
				L.guy() = g;
				L.roomDone();
				return;
			}
			break;

		case 5:
			g = L.guy();
			if (L.isSet(0x16e)) {
				L.clear(0x16e);
				L.clear(100);
				L.guy() = 1;
				L.setDir(5);
				L.guyY(1) = 0x19a;
				L.guyX(1) = 0xcc;
				L.guy() = 2;
				L.setDir(5);
				L.guyX(2) = 0x155;
				L.guyY(2) = 0x19a;
				L.sceneAnim(0x14d, 0x154, 0x76, 1, 0, L.screenHeight() - 1, 0, 0);
				L.sfx(21000 + 0x98, 0x7fff, 0);
				L.guy() = 1;
				L.walk(0x1bc, 0xcd, 4);
			}
			L.guy() = g;
			L.roomDone();
			return;

		case 8:
			if (L.isSet(0x42)) {
				L.clear(0x42);
				L.clear(0x7e);
				L.set(0x52);
				g = L.guy();
				int cine, top, left;
				if (!L.isSet(0x7d)) {
					L.set(0x84);
					L.sceneAnim(0xba, 0x2d, 0x24b, 1, 0, L.screenHeight() - 1, 0, 0);
					L.guy() = 2;
					L.setDir(0);
					L.guyY(2) = 0x176;
					L.guyX(2) = 0x1d1;
					L.clear(0x80);
					L.set(0x81);
					L.sceneAnim(0xc3, 0x6b, 0x1a1, 1, 0, L.screenHeight() - 1, 0, 0);
					L.walk(0x1c1, 100, 4);
					L.guy() = 1;
					L.setDir(0);
					L.guyY(1) = 0x176;
					L.guyX(1) = 0x1d2;
					L.clear(0x81);
					left = 0x1a3;
					top = 0x6e;
					cine = 0xc4;
				} else {
					L.set(0x84);
					L.sceneAnim(0xba, 0x2d, 0x24b, 1, 0, L.screenHeight() - 1, 0, 0);
					L.guy() = 2;
					L.setDir(0);
					L.guyY(2) = 0x177;
					L.guyX(2) = 0x17d;
					L.clear(0x80);
					L.set(0x81);
					L.sceneAnim(0x131, 0x71, 0x150, 1, 0, L.screenHeight() - 1, 0, 0);
					L.walk(0x1c1, 100, 4);
					L.guy() = 1;
					L.setDir(0);
					L.guyY(1) = 0x177;
					L.guyX(1) = 0x17d;
					L.clear(0x81);
					left = 0x14e;
					top = 0x72;
					cine = 200;
				}
				L.sceneAnim(cine, top, left, 1, 0, L.screenHeight() - 1, 0, 0);
				L.sceneAnim(0xc5, 0x2c, 0x24c, 1, 0, L.screenHeight() - 1, 0, 0);
				L.guy() = g;
				L.roomDone();
				return;
			}
			break;

		case 10:
			if (L.isClear(0xab)) {
				L.set(0xab);
				L.say(10000 + 0x149, 1, false);
				L.say(11000 + 0x14c, 2, false);
				if (L.isSet(0x178)) {
					L.clear(0x178);
					g = L.guy();
					L.guy() = 1;
					L.setDir(7);
					L.guyY(1) = 0x10e;
					L.guyX(1) = 0x15e;
					L.guy() = 2;
					L.setDir(6);
					L.guyY(2) = 0x10e;
					L.guyX(2) = 0x181;
					L.guy() = g;
					L.clear(0x99);
					L.clear(0x1bb);
					L.sceneAnim(0xf2, 0xe8, 0xfc, 1, 0, L.screenHeight() - 1, 0, 3);
				}
				L.roomDone();
				return;
			}
			if (L.isSet(0x178)) {
				L.clear(0x178);
				g = L.guy();
				L.guy() = 1;
				L.setDir(7);
				L.guyY(1) = 0x10e;
				L.guyX(1) = 0x15e;
				L.guy() = 2;
				L.setDir(6);
				L.guyY(2) = 0x10e;
				L.guyX(2) = 0x181;
				L.guy() = g;
				L.clear(0x99);
				L.clear(0x1bb);
				L.sceneAnim(0xf2, 0xe8, 0xfc, 1, 0, L.screenHeight() - 1, 0, 3);
				L.roomDone();
				return;
			}
			break;

		case 11:
			if (L.isSet(0x5a)) {
				L.clear(0x5a);
				L.sceneAnim(0x73, 0xd6, 0x3c, 0, 0, 0, 0, 0);
				L.roomDone();
				return;
			}
			break;

		default:
			break;
		}
		break;

	case 3:
		if (room == 1) {
			if (L.isSet(0x35)) {
				L.clear(0x35);
				int cine, depth;
				if (!L.isClear(0x34)) {
					depth = 0x14c;
					cine = 0x158;
				} else {
					L.set(0x34);
					depth = 0x1df;
					cine = 0x1a;
				}
				L.sceneAnim(cine, 0x99, 0x89, 0, 0, depth, 0, 0);
				L.roomDone();
				return;
			}
			if (L.isSet(0x42)) {
				g = L.guy();
				if (!L.isClear(0x3d)) {
					L.guy() = 2;
					L.guyY(2) = 0x178;
					L.guyX(2) = 0x1e2;
					L.setDir(6);
					L.guy() = 1;
					L.guyY(1) = 0x177;
					L.guyX(1) = 0x182;
					L.setDir(7);
					L.guy() = g;
				} else {
					L.clear(0x40);
					L.guy() = 1;
					L.setDir(7);
					L.guy() = 2;
					L.setDir(6);
					L.guy() = g;
					L.sceneAnim(9, 0xd6, 0x164, 0, 0, 0x176, 0, 3);
					L.sceneAnim(7, 0xae, 0x1ca, 1, 0, 0x176, 0, 0);
				}
				L.clear(0x42);
				L.roomDone();
				return;
			}
			if (L.isSet(0x9f)) {
				L.clear(0x9f);
				L.sceneAnim(0x134, 0x148, 0x226, 1, 0, L.screenHeight() - 1, 0, 0);
				L.roomDone();
				return;
			}
		} else if (room == 2) {
			if (L.isClear(0xca)) {
				L.set(0xca);
				L.say(10000 + 0xde, 1, false);
				L.say(11000 + 0xe2, 2, false);
			}
		}
		break;

	default:
		break;
	}
	L.roomDone();
}

} // End of namespace DreamForge
