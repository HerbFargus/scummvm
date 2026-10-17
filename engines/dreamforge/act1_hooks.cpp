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
#include "dreamforge/act1_hooks.h"

namespace DreamForge {

void act1_hooks_initVars(Logic &L) {
	L.var(0x6d26c) = -1;    // int32, looping sound handle
	// 0x6d270..0x6d2a0 (int32 tick deadlines) and 0x6de34 (int32) start at 0
}

// Per-object frame hook: positions, flags and puppy visibility keyed on the object id
void act1_3db10(Logic &L, int objectId, int frame, int flags30) {
	const bool playing = (flags30 & 0x10) != 0;
	int saved = L.guy();

	switch ((uint32)objectId) {
	case 0xca:
		if (playing)
			L.set(0x84);
		break;
	case 0xa2c2f:
		if (frame == 5) {
			L.guyY(1) = 0x17b;
			L.guyX(1) = 600;
		} else if (frame == 0x5b) {
			L.guyY(1) = 0x170;
			L.guyX(1) = 0x22e;
		}
		break;
	case 0xa2c31:
		if (playing)
			L.set(0x40);
		break;
	case 0xa2c33:
		if (frame == 9) {
			L.guyY(2) = 0x178;
			L.guyX(2) = 0x1e2;
		} else if (frame == 0x21) {
			L.guyY(1) = 0x177;
			L.guyX(1) = 0x182;
		}
		break;
	case 0xa2c35:
		if (frame == 6) {
			L.guyY(1) = 0x17b;
			L.guyX(1) = 600;
		} else if (frame == 0x61) {
			L.guyY(1) = 0x18c;
			L.guyX(1) = 0x2d1;
		}
		break;
	case 0xa2c38:
		L.guy() = 1;
		L.clearLying(1);
		L.setDir(0);
		L.guy() = saved;
		break;
	case 0xa2c3b:
		if (playing)
			L.set(0x38);
		break;
	case 0xa2c3c:
		if (frame == 0x37) {
			L.guyY(1) = 0x17b;
			L.guyX(1) = 600;
		} else if (frame == 0x67) {
			L.guyY(1) = 0x177;
			L.guyX(1) = 0x236;
		} else if (playing) {
			L.set(0x3c);
		}
		break;
	case 0xa2c3e:
		if (frame != 3) {
			if (playing) {
				L.guyY(1) = 0x170;
				L.guyX(1) = 0x22e;
				L.set(0x3d);
			}
		} else {
			L.guyY(1) = 0x17b;
			L.guyX(1) = 600;
		}
		break;
	case 0xa2c3f:
		if (playing)
			L.set(0x3f);
		break;
	case 0xa2c42:
		if (playing)
			L.set(0x33);
		break;
	case 0xa2c44:
		if (frame == 0x29) {
			L.guyY(1) = 0x179;
			L.guyX(1) = 0x169;
		} else if (playing) {
			L.show(3);
		}
		break;
	case 0xa2c46:
		if (frame == 0xd) {
			L.guyY(1) = 0xf4;
			L.guyX(1) = 500;
		} else if (frame == 0x31) {
			L.guyY(2) = 0xf7;
			L.guyX(2) = 0x1db;
		}
		break;
	case 0xa2c4a:
		if (playing)
			L.show(1);
		break;
	case 0xa2c4b:
		if (playing)
			L.show(2);
		break;
	case 0xa2c4e:
		if (playing)
			L.set(0x44);
		break;
	case 0xa2c52:
		if (playing)
			L.set(0x78);
		break;
	case 0xa2c54:
		if (playing) {
			L.set(0x48);
			L.show(1);
		}
		break;
	case 0xa2c55:
		if (playing)
			L.set(0x79);
		break;
	case 0xa2c57:
		if (playing) {
			L.clear(0x47);
			L.show(1);
		}
		break;
	case 0xa2c5e:
	case 0xa2c62:
		if (playing)
			L.set(0x4a);
		break;
	case 0xa2c67:
		if (playing)
			L.set(0x59);
		break;
	case 0xa2c69:
		if (playing)
			L.show(3);
		break;
	case 0xa2c70:
		if (playing)
			L.show(1);
		break;
	case 0xa2c71:
	case 0xa2c72:
		if (playing)
			L.show(2);
		break;
	case 0xa2c76:
		if (playing)
			L.show(3);
		break;
	case 0xa2c7e:
	case 0xa2c7f:
		if (playing)
			L.set(0x50);
		break;
	case 0xa2c81:
		if (playing)
			L.set(0x56);
		break;
	case 0xa2c82:
		if (playing)
			L.clear(0x56);
		break;
	case 0xa2c83:
		if (playing)
			L.clear(0x66);
		break;
	case 0xa2c84:
		if (playing)
			L.hide(1);
		break;
	case 0xa2c89:
		if (frame == 0) {
			L.guyY(1) = 400;
			L.guyX(1) = 0x189;
			L.guyY(2) = 400;
			L.guyX(2) = 0x189;
		} else if (frame != 0x18) {
			if (playing) {
				L.guyY(1) = 0x178;
				L.guyX(1) = 0xf4;
				L.guy() = 2;    // not restored in the original
				L.guyY(2) = 0x178;
				L.guyX(2) = 0x132;
				L.set(99);
			}
		} else {
			L.guyY(1) = 0x178;
			L.guyX(1) = 0xf4;
			L.guy() = 2;
			L.guyY(2) = 0x178;
			L.guyX(2) = 0x132;
		}
		break;
	case 0xa2c8a:
		if (playing)
			L.set(99);
		break;
	case 0xa2c8b:
		if (playing) {
			L.set(99);
			L.set(0x72);
			L.clear(0x71);
			L.set(0x73);
		}
		break;
	case 0xa2c9d:
		if (playing)
			L.show(3);
		break;
	case 0xa2c9e:
		if (playing) {
			L.hide(1);
			L.set(0xec);
		}
		break;
	case 0xa2c9f:
		if (playing)
			L.hide(3);
		break;
	case 0xa2ca5:
		if (playing) {
			L.set(0x5e);
			L.set(0x60);
		}
		break;
	case 0xa2ca7:
		if (!playing)
			break;
		if (L.isSet(0x145)) {
			L.clear(0x1a5);
			L.set(0xf1);
		} else if (L.isSet(0x146)) {
			L.clear(0x1a6);
			L.set(0xf2);
		} else if (L.isSet(0x147)) {
			L.clear(0x1a7);
			L.set(0xf3);
		} else if (L.isSet(0x148)) {
			L.clear(0x1a8);
			L.set(0xf4);
		} else if (L.isSet(0x14a)) {
			L.clear(0x1aa);
			L.set(0xf6);
		} else if (L.isSet(0x14b)) {
			L.clear(0x1ab);
			L.set(0xf7);
		} else if (L.isSet(0x14c)) {
			L.clear(0x1ac);
			L.set(0xf8);
		} else if (L.isSet(0x14d)) {
			L.clear(0x1ad);
			L.set(0xf9);
		} else if (L.isSet(0x14f)) {
			L.clear(0x1af);
			L.set(0xfb);
		} else if (L.isSet(0x150)) {
			L.clear(0x1b0);
			L.set(0xfc);
		} else if (L.isSet(0x151)) {
			L.clear(0x1b1);
			L.set(0xfd);
		} else if (L.isSet(0x152)) {
			L.clear(0x1b2);
			L.set(0xfe);
		}
		break;
	case 0xa2caa:
		if (playing)
			L.show(3);
		break;
	case 0xa2cb4:
		if (!playing)
			break;
		if (L.isSet(0x132)) {
			L.clear(0x82);
			L.set(0xf0);
		} else if (L.isSet(0x133)) {
			L.clear(0x1a5);
			L.set(0xf1);
		} else if (L.isSet(0x134)) {
			L.clear(0x1a6);
			L.set(0xf2);
		} else if (L.isSet(0x135)) {
			L.clear(0x1a7);
			L.set(0xf3);
		} else if (L.isSet(0x137)) {
			L.clear(0x1a9);
			L.set(0xf5);
		} else if (L.isSet(0x138)) {
			L.clear(0x1aa);
			L.set(0xf6);
		} else if (L.isSet(0x139)) {
			L.clear(0x1ab);
			L.set(0xf7);
		} else if (L.isSet(0x13a)) {
			L.clear(0x1ac);
			L.set(0xf8);
		} else if (L.isSet(0x13c)) {
			L.clear(0x1ae);
			L.set(0xfa);
		} else if (L.isSet(0x13d)) {
			L.clear(0x1af);
			L.set(0xfb);
		} else if (L.isSet(0x13e)) {
			L.clear(0x1b0);
			L.set(0xfc);
		} else if (L.isSet(0x13f)) {
			L.clear(0x1b1);
			L.set(0xfd);
		}
		break;
	case 0xa2cbc:
		if (!playing)
			break;
		if (L.isSet(0x1a0)) {
			L.clear(0x1a0);
			L.set(0x196);
		} else if (L.isSet(0x1a1)) {
			L.clear(0x1a1);
			L.set(0x197);
		} else if (L.isSet(0x1a2)) {
			L.clear(0x1a2);
			L.set(0x198);
		} else if (L.isSet(0x1a3)) {
			L.clear(0x1a3);
			L.set(0x199);
		} else if (L.isSet(0x1a4)) {
			L.clear(0x1a4);
			L.set(0x19a);
		}
		L.redraw(1, 1);
		break;
	case 0xa2cbd:
		if (!playing)
			break;
		if (L.isSet(0x1a0)) {
			L.clear(0x1a0);
			L.set(0x19b);
		} else if (L.isSet(0x1a1)) {
			L.clear(0x1a1);
			L.set(0x19c);
		} else if (L.isSet(0x1a2)) {
			L.clear(0x1a2);
			L.set(0x19d);
		} else if (L.isSet(0x1a3)) {
			L.clear(0x1a3);
			L.set(0x19e);
		} else if (L.isSet(0x1a4)) {
			L.clear(0x1a4);
			L.set(0x19f);
		}
		L.redraw(1, 1);
		break;
	case 0xa2cc2:
		if (playing) {
			L.hide(1);
			L.set(0xb2);
		}
		break;
	case 0xa2cc7:
		if (playing)
			L.clear(0xb7);
		break;
	case 0xa2cc8:
		if (playing)
			L.clear(0xc3);
		break;
	case 0xa2ccd:
		if (playing)
			L.set(0xbe);
		break;
	case 0xa2cd6:
		if (playing) {
			L.clear(0x97);
			L.show(1);
		}
		break;
	case 0xa2cd7:
		if (playing)
			L.show(1);
		break;
	case 0xa2cd8:
		if (playing) {
			L.set(0x88);
			L.show(1);
			L.set(0x85);
		}
		break;
	case 0xa2cd9:
		if (playing)
			L.set(0x96);
		break;
	case 0xa2cda:
		if (playing)
			L.set(0x9d);
		break;
	case 0xa2cdc:
		if (playing)
			L.show(1);
		break;
	case 0xa2cdd:
		if (frame == 5) {
			L.guyY(1) = 0xea;
			L.guyX(1) = 0xb9;
		} else if (playing) {
			L.set(0x87);
		}
		break;
	case 0xa2ce4:
		if (playing) {
			L.hide(3);
			L.set(0x80);
			L.set(0x84);
		}
		break;
	case 0xa2ce6:
		if (playing) {
			L.hide(1);
			L.set(0x81);
		}
		break;
	case 0xa2ce7:
		if (playing) {
			L.hide(2);
			L.set(0x80);
		}
		break;
	case 0xa2ce8:
		if (playing)
			L.show(1);
		break;
	case 0xa2ce9:
	case 0xa2cea:
		if (frame == 5) {
			L.guyY(1) = 0x15a;
			L.guyX(1) = 400;
		}
		break;
	case 0xa2ced:
		if (playing)
			L.show(2);
		break;
	case 0xa2cee:
		if (playing)
			L.show(1);
		break;
	case 0xa2cef:
		if (playing) {
			L.clear(0x84);
			L.clear(0x81);
			L.clear(0x80);
		}
		break;
	case 0xa2cf2:
		if (playing)
			L.show(1);
		break;
	case 0xa2cf3:
		if (playing)
			L.set(0x86);
		break;
	case 0xa2cf5:
		if (playing)
			L.set(0x89);
		break;
	case 0xa2cf7:
		if (playing) {
			L.set(0x8a);
			L.hide(1);
		}
		break;
	case 0xa2cf9:
	case 0xa2cfa:
	case 0xa2cfb:
		if (frame != 1 && playing)
			L.show(3);
		break;
	case 0xa2cfc:
		if (playing) {
			L.set(0x65);
			L.hide(3);
		}
		break;
	case 0xa2d01:
		if (playing)
			L.show(3);
		break;
	case 0xa2d05:
		if (playing) {
			L.set(0xa5);
			L.set(0xaa);
		}
		break;
	case 0xa2d06:
		if (playing) {
			L.show(3);
			L.clear(0xa5);
			L.clear(0xaa);
		}
		break;
	case 0xa2d09:
		if (playing)
			L.show(1);
		break;
	case 0xa2d0c:
		if (playing)
			L.set(0xa7);
		break;
	case 0xa2d0d:
		if (playing)
			L.show(3);
		break;
	case 0xa2d0e:
		if (playing)
			L.set(0xa3);
		break;
	case 0xa2d10:
		if (playing) {
			L.hide(1);
			L.set(0xa4);
		}
		break;
	case 0xa2d11:
		if (playing)
			L.clear(0xa5);
		break;
	case 0xa2d14:
		if (frame == 5) {
			L.guyY(2) = 0x110;
			L.guyX(2) = 0x15a;
		} else if (playing) {
			L.show(2);
		}
		break;
	case 0xa2d15:
		if (playing) {
			L.set(0xb6);
			L.hide(1);
		}
		break;
	case 0xa2d18:
		if (frame == 5) {
			L.guyY(1) = 0x10f;
			L.guyX(1) = 0x17b;
		} else if (playing) {
			L.set(0x99);
			L.show(1);
		}
		break;
	case 0xa2d1a:
		if (playing)
			L.clear(0x9e);
		break;
	case 0xa2d1b:
		if (frame == 0) {
			L.guyY(1) = 0x10f;
			L.guyX(1) = 0x17b;
		} else if (frame == 8) {
			L.guyY(1) = 0x176;
			L.guyX(1) = 0x178;
		} else if (playing) {
			L.show(1);
		}
		break;
	case 0xa2d1c:
		if (frame == 0) {
			L.guyY(1) = 0x10e;
			L.guyX(1) = 0x15e;
			L.guyY(2) = 0x10e;
			L.guyX(2) = 0x181;
		} else if (frame == 5) {
			L.guyY(2) = 0x177;
			L.guyX(2) = 0x180;
		} else if (frame == 7) {
			L.guyY(1) = 0x176;
			L.guyX(1) = 0x121;
		} else if (playing) {
			L.show(3);
		}
		break;
	case 0xa2d31:
		if (playing)
			L.set(0x96);
		break;
	case 0xa2d53:
		if (playing) {
			L.guyY(1) = 400;
			L.guyX(1) = 0x2e8;
			L.show(1);
		}
		break;
	case 0xa2d54:
		if (frame == 3) {
			L.guyY(1) = 0x19a;
			L.guyX(1) = 0x155;
		} else if (frame == 5) {
			L.guyY(1) = 0x12e;
			L.guyX(1) = 0x159;
		} else if (playing) {
			L.set(0x7a);
			L.hide(1);
		}
		break;
	case 0xa2d55:
		if (frame == 0x15) {
			L.guyY(1) = 400;
			L.guyX(1) = 0x101;
		}
		break;
	case 0xa2d56:
		if (frame == 0) {
			L.guyY(1) = 0x189;
			L.guyX(1) = 0x115;
		} else if (frame == 0x16) {
			L.guyY(1) = 0x18e;
			L.guyX(1) = 0x11e;
		} else if (playing) {
			L.show(1);
		}
		break;
	case 0xa2d58:
		if (playing)
			L.set(0xd8);
		break;
	case 0xa2d5b:
		if (playing)
			L.show(2);
		break;
	case 0xa2d5e:
		if (playing)
			L.show(3);
		break;
	case 0xa2d60:
		if (playing)
			L.set(0x9b);
		break;
	case 0xa2d6b:
		if (playing)
			L.show(3);
		break;
	case 0xa2d73:
		// which of five spots puppy 1 stands at
		if (!playing)
			break;
		if (L.guyX(1) == 0x4a) {
			L.clear(0x1b3);
			L.set(0xff);
		} else if (L.guyX(1) == 0xc5) {
			L.clear(0x1b4);
			L.set(0x100);
		} else if (L.guyX(1) == 0x140) {
			L.clear(0x1b5);
			L.set(0x101);
		} else if (L.guyX(1) == 0x1bb) {
			L.clear(0x1b6);
			L.set(0x102);
		} else if (L.guyX(1) == 0x236) {
			L.clear(0x1b7);
			L.set(0x103);
		}
		break;
	case 0xa2d77:
		if (playing)
			L.show(3);
		break;
	case 0xa2d82:
		if (frame == 0x29) {
			L.guyY(1) = 0x179;
			L.guyX(1) = 0x169;
		} else if (playing) {
			L.show(3);
		}
		break;
	case 0xa2d85:
		if (playing)
			L.show(1);
		break;
	case 0xa2d87:
		if (playing)
			L.show(3);
		break;
	case 0xa2d9b:
		if (playing)
			L.clear(0x1cd);
		break;
	case 0xa2d9c:
		if (playing)
			L.set(0x65);
		break;
	case 0xa2d9d:
		if (playing)
			L.clear(0x1cd);
		break;
	default:
		break;
	}
}

// Idle hook: per-room timers that end a room with a caught-by-the-baddies cutscene, plus idle lines
void act1_3f780(Logic &L) {
	int saved;
	uint32 r;
	int n;

	if (L.area() > 1) {
		if (L.area() > 2) {
			if (L.area() != 3)
				return;
			if (L.room() != 3)
				return;
			if (L.ticks() < (uint32)L.var(0x6de34))
				return;
			L.var(0x6de34) = (int32)(L.ticks() + 200);    // 0x107e0
			L.say(11000 + 0x3a, 2, false);
			return;
		}
		switch (L.room()) {
		case 1:
			if (!L.isSet(0x16b)) {
				L.var(0x6d27c) = (int32)(L.ticks() + 0x8c);
				return;
			}
			if (L.ticks() < (uint32)L.var(0x6d27c))
				return;
			L.clear(0x16b);
			L.stopSound(L.var(0x6d26c));
			saved = L.guy();
			L.var(0x6d26c) = -1;
			L.guy() = 1;
			L.walk(L.guyY(1), L.guyX(1), 0);
			if (L.isLying(2))
				L.oneShot(0x1c, L.guyDir(L.guy()));
			else if (L.isSitting(2))
				L.oneShot(7, L.guyDir(L.guy()));
			L.oneShot(0x19, L.guyDir(L.guy()));
			L.guy() = saved;
			L.cutscene(0x119, 10, 0, 0, 0, 0, 1);
			r = (uint32)L.rand15() * 3 >> 15;
			if (r == 0)
				L.cutscene(0x11a, 10, 0, 0, 0, 0, 1);
			else if (r == 1)
				L.cutscene(0x11c, 10, 0, 0, 0, 0, 1);
			else if (r == 2)
				L.cutscene(0x11d, 10, 0, 0, 0, 0, 1);
			L.cutscene(0x11b, 10, 0, 0, 0, 0, 1);
			act1_482c0(L);
			L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
			L.clear(100);
			return;
		case 6:
			if (L.isSet(0x17) && (uint32)L.var(0x6de34) <= L.ticks()) {
				L.var(0x6de34) = (int32)(L.ticks() + 200);    // 0x107e0
				L.say(10000 + 0xa5, 1, false);
				L.say(11000 + 0xa2, 2, false);
				return;
			}
			break;
		case 7:
			if (L.isClear(0x50)) {
				if (!L.isSet(0xda)) {
					L.var(0x6d288) = (int32)(L.ticks() + 200);
					return;
				}
				if (L.ticks() < (uint32)L.var(0x6d288))
					return;
				L.clear(0xda);
				L.stopSound(L.var(0x6d26c));
				saved = L.guy();
				L.var(0x6d26c) = -1;
				L.guy() = 1;
				L.walk(L.guyY(1), L.guyX(1), 6);
				if (L.isLying(2))
					L.oneShot(0x1c, L.guyDir(L.guy()));
				else if (L.isSitting(2))
					L.oneShot(7, L.guyDir(L.guy()));
				L.oneShot(0x19, L.guyDir(L.guy()));
				L.guy() = saved;
				L.cutscene(0x124, 10, 0, 0, 0, 0, 1);
				L.cutscene(0x125, 10, 0, 0, 0, 0, 1);
				act1_482c0(L);
				L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
				L.clear(0xdc);
				return;
			}
			if (!L.isSet(0xdd)) {
				L.var(0x6d28c) = (int32)(L.ticks() + 0x8c);
				return;
			}
			if (L.ticks() < (uint32)L.var(0x6d28c))
				return;
			L.clear(0xdd);
			L.stopSound(L.var(0x6d26c));
			saved = L.guy();
			L.var(0x6d26c) = -1;
			L.guy() = 1;
			L.walk(L.guyY(1), L.guyX(1), 6);
			if (L.isLying(2))
				L.oneShot(0x1c, L.guyDir(L.guy()));
			else if (L.isSitting(2))
				L.oneShot(7, L.guyDir(L.guy()));
			L.oneShot(0x19, L.guyDir(L.guy()));
			L.guy() = saved;
			L.cutscene(0x124, 10, 0, 0, 0, 0, 1);
			L.cutscene(0x125, 10, 0, 0, 0, 0, 1);
			act1_482c0(L);
			L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
			L.clear(0xdc);
			return;
		case 0xb:
			if (!L.isSet(0x5b)) {
				L.var(0x6d29c) = (int32)(L.ticks() + 200);
				return;
			}
			if ((uint32)L.var(0x6d29c) <= L.ticks()) {
				L.set(0xd0);
				L.clear(0x5b);
				saved = L.guy();
				L.guy() = 2;
				L.walk(0x171, 0xf9, 4);
				L.guy() = saved;
				L.hide(3);
				L.set(0x5d);
				L.clear(0xec);
				L.sceneAnim(0x75, 0x118, 0xd9, 0, 0, 0x1db, 0, 0);
				L.set(0x46);
				L.changeScene(0x1a1, 0x17e, 6, 0x1a1, 0x1f4, 6, 1, 1, 3);
				return;
			}
			break;
		default:
			break;
		}
		return;
	}

	if (L.area() != 1)
		return;

	switch (L.room()) {
	case 1:
		break;
	case 2:
		if (!L.isSet(0x16c)) {
			if (!L.isSet(0xcc)) {
				L.var(0x6d294) = (int32)(L.ticks() + 0x8c);
				return;
			}
			if (L.ticks() < (uint32)L.var(0x6d294))
				return;
			L.clear(0xcc);
			L.stopSound(L.var(0x6d26c));
			saved = L.guy();
			L.var(0x6d26c) = -1;
			L.guy() = 1;
			L.walk(L.guyY(1), L.guyX(1), 6);
			if (L.isLying(2))
				L.oneShot(0x1c, L.guyDir(L.guy()));
			else if (L.isSitting(2))
				L.oneShot(7, L.guyDir(L.guy()));
			L.oneShot(0x19, L.guyDir(L.guy()));
			L.guy() = saved;
			L.cutscene(0x10b, 10, 0, 0, 0, 0, 1);
			r = (uint32)L.rand15() * 3 >> 15;
			if (r == 0)
				L.cutscene(0x10c, 10, 0, 0, 0, 0, 1);
			else if (r == 1)
				L.cutscene(0x10e, 10, 0, 0, 0, 0, 1);
			else if (r == 2)
				L.cutscene(0x10f, 10, 0, 0, 0, 0, 1);
			L.cutscene(0x10d, 10, 0, 0, 0, 0, 1);
			act1_482c0(L);
			L.clear(0xce);
			L.clear(0xcb);
			L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
			return;
		}
		if (!L.isSet(0x16d)) {
			L.var(0x6d274) = (int32)(L.ticks() + 200);
			return;
		}
		if (L.ticks() < (uint32)L.var(0x6d274))
			return;
		L.clear(0x16d);
		L.stopSound(L.var(0x6d26c));
		saved = L.guy();
		L.var(0x6d26c) = -1;
		L.guy() = 1;
		L.walk(L.guyY(1), L.guyX(1), 6);
		if (L.isLying(2))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(2))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0x19, L.guyDir(L.guy()));
		L.guy() = saved;
		L.cutscene(0x10b, 10, 0, 0, 0, 0, 1);
		r = (uint32)L.rand15() * 3 >> 15;
		if (r == 0)
			L.cutscene(0x10c, 10, 0, 0, 0, 0, 1);
		else if (r == 1)
			L.cutscene(0x10e, 10, 0, 0, 0, 0, 1);
		else if (r == 2)
			L.cutscene(0x10f, 10, 0, 0, 0, 0, 1);
		L.cutscene(0x10d, 10, 0, 0, 0, 0, 1);
		act1_482c0(L);
		L.clear(0xce);
		L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
		return;
	case 3:
		if (!L.isSet(0xd3)) {
			L.var(0x6d290) = (int32)(L.ticks() + 200);
			return;
		}
		if (L.ticks() < (uint32)L.var(0x6d290))
			return;
		L.clear(0xd3);
		L.stopSound(L.var(0x6d26c));
		L.var(0x6d26c) = -1;
		saved = L.guy();
		if (!L.isClear(0x7c))
			return;
		L.guy() = 1;
		L.walk(L.guyY(1), L.guyX(1), 6);
		if (L.isLying(2))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(2))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0x19, L.guyDir(L.guy()));
		L.guy() = saved;
		L.cutscene(0x114, 10, 0, 0, 0, 0, 1);
		r = (uint32)L.rand15() * 3 >> 15;
		if (r == 0)
			L.cutscene(0x115, 10, 0, 0, 0, 0, 1);
		else if (r == 1)
			L.cutscene(0x117, 10, 0, 0, 0, 0, 1);
		else if (r == 2)
			L.cutscene(0x118, 10, 0, 0, 0, 0, 1);
		L.cutscene(0x116, 10, 0, 0, 0, 0, 1);
		act1_482c0(L);
		L.clear(0xcb);
		L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
		return;
	case 5:
		if (!L.isSet(0x174) || !L.isClear(0x176)) {
			n = L.isSet(0x175) ? 0x8c : 200;
			L.var(0x6d270) = (int32)(L.ticks() + n);
			return;
		}
		if (L.ticks() < (uint32)L.var(0x6d270))
			return;
		L.clear(0x174);
		L.stopSound(L.var(0x6d26c));
		saved = L.guy();
		L.var(0x6d26c) = -1;
		r = (uint32)L.rand15() * 2 >> 15;    // a random puppy
		L.guy() = r + 1;
		L.walk(L.guyY(r + 1), L.guyX(r + 1), 6);
		if (L.isLying(2))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(2))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0x19, L.guyDir(L.guy()));
		L.guy() = saved;
		L.cutscene(0x114, 10, 0, 0, 0, 0, 1);
		r = (uint32)L.rand15() * 3 >> 15;
		if (r == 0)
			L.cutscene(0x15a, 10, 0, 0, 0, 0, 1);
		else if (r == 1)
			L.cutscene(0x16d, 10, 0, 0, 0, 0, 1);
		else if (r == 2)
			L.cutscene(0x16e, 10, 0, 0, 0, 0, 1);
		L.cutscene(0x116, 10, 0, 0, 0, 0, 1);
		act1_482c0(L);
		L.changeScene(0x19a, 0x1ee, 6, 0x194, 0x1a6, 6, 1, 1, 10);
		return;
	case 6:
		if (!L.isSet(0x154)) {
			L.var(0x6d284) = (int32)(L.ticks() + 200);
			return;
		}
		if (L.ticks() < (uint32)L.var(0x6d284))
			return;
		L.clear(0x154);
		L.stopSound(L.var(0x6d26c));
		saved = L.guy();
		L.var(0x6d26c) = -1;
		L.guy() = 1;
		L.walk(L.guyY(1), L.guyX(1), 6);
		if (L.isLying(2))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(2))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0x19, L.guyDir(L.guy()));
		L.guy() = saved;
		L.cutscene(0x119, 10, 0, 0, 0, 0, 1);
		r = (uint32)L.rand15() * 3 >> 15;
		if (r == 0)
			L.cutscene(0x11a, 10, 0, 0, 0, 0, 1);
		else if (r == 1)
			L.cutscene(0x11c, 10, 0, 0, 0, 0, 1);
		else if (r == 2)
			L.cutscene(0x11d, 10, 0, 0, 0, 0, 1);
		L.cutscene(0x11b, 10, 0, 0, 0, 0, 1);
		act1_482c0(L);
		L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
		return;
	case 7:
		if (!L.isSet(0x16f)) {
			if (!L.isSet(0xee)) {
				if (!L.isSet(0xd2)) {
					if (L.isSet(0xd6)) {
						L.clear(0xd6);
						L.set(0xd2);
					}
					L.var(0x6d2a0) = (int32)(L.ticks() + 200);
					return;
				}
				if (L.ticks() < (uint32)L.var(0x6d2a0))
					return;
				L.clear(0xd2);
				L.say(11000 + 0x90, 2, false);
				return;
			}
			if (!L.isSet(0xed)) {
				L.var(0x6d280) = (int32)(L.ticks() + 200);
				return;
			}
			if (L.ticks() < (uint32)L.var(0x6d280))
				return;
			L.clear(0xed);
			L.stopSound(L.var(0x6d26c));
			saved = L.guy();
			L.var(0x6d26c) = -1;
			L.guy() = 1;
			L.walk(L.guyY(1), L.guyX(1), 6);
			if (L.isLying(2))
				L.oneShot(0x1c, L.guyDir(L.guy()));
			else if (L.isSitting(2))
				L.oneShot(7, L.guyDir(L.guy()));
			L.oneShot(0x19, L.guyDir(L.guy()));
			L.guy() = saved;
			L.cutscene(0x11f, 10, 0, 0, 0, 0, 1);
			r = (uint32)L.rand15() * 3 >> 15;
			if (r == 0)
				L.cutscene(0x120, 10, 0, 0, 0, 0, 1);
			else if (r == 1)
				L.cutscene(0x122, 10, 0, 0, 0, 0, 1);
			else if (r == 2)
				L.cutscene(0x123, 10, 0, 0, 0, 0, 1);
			L.cutscene(0x121, 10, 0, 0, 0, 0, 1);
			act1_482c0(L);
			L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
			return;
		}
		if (!L.isSet(0x170)) {
			L.var(0x6d278) = (int32)(L.ticks() + 0x8c);
			return;
		}
		if (L.ticks() < (uint32)L.var(0x6d278))
			return;
		L.clear(0x170);
		L.stopSound(L.var(0x6d26c));
		saved = L.guy();
		L.var(0x6d26c) = -1;
		L.guy() = 1;
		L.walk(L.guyY(1), L.guyX(1), 6);
		if (L.isLying(2))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(2))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0x19, L.guyDir(L.guy()));
		L.guy() = saved;
		L.cutscene(0x11f, 10, 0, 0, 0, 0, 1);
		r = (uint32)L.rand15() * 3 >> 15;
		if (r == 0)
			L.cutscene(0x120, 10, 0, 0, 0, 0, 1);
		else if (r == 1)
			L.cutscene(0x122, 10, 0, 0, 0, 0, 1);
		else if (r == 2)
			L.cutscene(0x123, 10, 0, 0, 0, 0, 1);
		L.cutscene(0x121, 10, 0, 0, 0, 0, 1);
		act1_482c0(L);
		L.clear(0x16f);
		L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
		return;
	default:
		return;
	}

	// room 1
	if (!L.isSet(200)) {
		L.var(0x6d298) = (int32)(L.ticks() + 200);
		return;
	}
	if (L.ticks() < (uint32)L.var(0x6d298))
		return;
	L.clear(200);
	L.stopSound(L.var(0x6d26c));
	saved = L.guy();
	L.var(0x6d26c) = -1;
	L.guy() = 1;
	L.walk(L.guyY(1), L.guyX(1), 6);
	if (L.isLying(2))
		L.oneShot(0x1c, L.guyDir(L.guy()));
	else if (L.isSitting(2))
		L.oneShot(7, L.guyDir(L.guy()));
	L.oneShot(0x19, L.guyDir(L.guy()));
	L.guy() = saved;
	L.cutscene(0x10b, 10, 0, 0, 0, 0, 1);
	r = (uint32)L.rand15() * 3 >> 15;
	if (r == 0)
		L.cutscene(0x10c, 10, 0, 0, 0, 0, 1);
	else if (r == 1)
		L.cutscene(0x10e, 10, 0, 0, 0, 0, 1);
	else if (r == 2)
		L.cutscene(0x10f, 10, 0, 0, 0, 0, 1);
	L.cutscene(0x10d, 10, 0, 0, 0, 0, 1);
	act1_482c0(L);
	L.clear(199);
	L.clear(0xcd);
	L.changeScene(0x199, 0x1ef, 4, 0x194, 0x1a7, 4, 1, 1, 10);
}

// Fidget veto: puppy g may fidget unless hidden or one of the scripted-moment flags is set
bool act1_40cb0(Logic &L, int g) {
	bool ok = !L.isHidden(g);
	if (g == 1) {
		ok = !L.isSet(0x42) && !L.isSet(0x41) && !L.isSet(0x3f) && !L.isSet(0x3e) && !L.isSet(0x65) && ok;
		if (L.isClear(0x16a) && L.area() == 1 && L.room() == 4)
			ok = false;
		if (L.isSet(0x85))
			ok = false;
		if (L.isSet(0x4e))
			ok = false;
		if (L.isSet(0x7a))
			ok = false;
		if (L.isSet(0x99))
			ok = false;
		if (L.isSet(0x1c9))
			ok = false;
		if (L.isSet(0xcc))
			ok = false;
		if (L.isSet(200))
			ok = false;
		if (L.isSet(0x16d))
			ok = false;
		if (L.isSet(0xd3))
			ok = false;
		if (L.isSet(0x154))
			ok = false;
		if (L.isSet(0xed))
			ok = false;
		if (L.isSet(0xdd))
			ok = false;
		if (L.isSet(0xda))
			ok = false;
		if (L.isSet(0x170))
			ok = false;
		if (L.isSet(0x174))
			return false;
	} else if (g == 2) {
		static const int kFlags[] = {
			0x65, 0x16f, 0x42, 0xd0, 0xdc, 0xcc, 100, 0x1bb, 0x1c9, 0xb5,
			0xcc, 200, 0x16d, 0xd3, 0x154, 0xed, 0xdd, 0xda, 0x170
		};
		for (uint i = 0; i < ARRAYSIZE(kFlags); i++) {
			if (L.isSet(kFlags[i]))
				ok = false;
		}
		if (L.isSet(0x174))
			ok = false;
	}
	return ok;
}

} // End of namespace DreamForge
