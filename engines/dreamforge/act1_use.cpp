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
#include "dreamforge/act1_use.h"

namespace DreamForge {

// Use on a layer (C.PRG 0x37ea0): the block-sliding layers and their exits; always 0
int act1_37ea0(Logic &L, int layerId) {
	int saved = L.guy();

	// 0x3bea6: the puppy drops through, then the shared ending
	auto dropThrough = [&](int left) {
		L.sceneAnim(0x90, 0x126, left, 1, 0, 0x1ce, 0, 0);
		L.guy() = saved;
		L.banter(0xc);
		return 0;
	};

	// 0x11b..0x11f: both puppies face the exit, a random block flag, fade and leave
	auto leave = [&](int flagLow, int flagHigh) {
		L.guy() = 1;
		L.turn(L.guyDir(1), 6);
		L.setDir(6);
		L.guy() = 2;
		L.turn(L.guyDir(2), 7);
		L.setDir(7);
		L.guy() = saved;
		if (((L.rand15() & 0x7fffffff) >> 14) == 0)
			L.set(flagLow);
		else
			L.set(flagHigh);
		L.fadeOut(0x10, 0xff, 0, 0, 0);
		L.unfade();
		L.changeScene(0x1cd, 0x140, 6, 0x1d6, 0x26c, 0, 1, 1, 4);
		return 0;
	};

	switch (layerId) {
	case 0x04:
		if (!L.isClear(0x3a))
			return 0;
		if (L.recState(4, kRecLayer) < 2)
			return 0;
		L.say(11000 + 0x19, 2, false);
		return 0;

	case 0x2b:
		if (L.isSet(0x146)) {
			L.clear(0x146);
			L.set(0x132);
			L.sceneAnim(0x104, 0xa6, 0x84, 1, 0, 0x11c, 0, 0);
		}
		if (!L.isSet(0x132))
			return 0;
		if (L.isSet(0x155)) {
			L.set(0x118);
			L.sceneAnim(0x8f, 0x8c, 0xe, 1, 0, 0x11a, 0, 0);
			return 0;
		}
		L.set(0x82);
		L.sceneAnim(0x8a, 0x95, 0xb, 1, 0, 0x11b, 0, 0);
		return 0;

	case 0x8a:
		if (L.isSet(0x131)) {
			L.clear(0x131);
			L.set(0x145);
			L.sceneAnim(0x102, 0xa6, 7, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x147)) {
			L.clear(0x147);
			L.set(0x133);
			L.sceneAnim(0x104, 0xa6, 0xff, 1, 0, 0x11c, 0, 0);
		}
		if (L.isSet(0x145)) {
			if (L.isSet(0x156)) {
				L.set(0x105);
				L.sceneAnim(0x8e, 0x95, 0x19, 1, 0, 0x11a, 0, 0);
				return 0;
			}
			L.set(0x1a5);
			L.sceneAnim(0x7d, 0x95, 0x19, 1, 0, 0x11b, 0, 0);
			return 0;
		}
		if (!L.isSet(0x133))
			return 0;
		if (L.isSet(0x156)) {
			L.set(0x119);
			L.sceneAnim(0x8f, 0x8c, 0x89, 1, 0, 0x11a, 0, 0);
			return 0;
		}
		L.set(0x1a5);
		L.sceneAnim(0x8a, 0x95, 0x86, 1, 0, 0x11b, 0, 0);
		return 0;

	case 0x8b:
		if (L.isSet(0x132)) {
			L.clear(0x132);
			L.set(0x146);
			L.sceneAnim(0x102, 0xa6, 0x82, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x148)) {
			L.clear(0x148);
			L.set(0x134);
			L.sceneAnim(0x104, 0xa6, 0x17a, 1, 0, 0x11c, 0, 0);
		}
		if (L.isSet(0x146)) {
			if (L.isSet(0x157)) {
				L.set(0x106);
				L.sceneAnim(0x8e, 0x95, 0x94, 1, 0, 0x11a, 0, 0);
				return 0;
			}
			L.set(0x1a6);
			L.sceneAnim(0x7d, 0x95, 0x94, 1, 0, 0x11b, 0, 0);
			return 0;
		}
		if (!L.isSet(0x134))
			return 0;
		if (L.isSet(0x157)) {
			L.set(0x11a);
			L.sceneAnim(0x8f, 0x8c, 0x104, 1, 0, 0x11a, 0, 0);
			return 0;
		}
		L.set(0x1a6);
		L.sceneAnim(0x8a, 0x95, 0x102, 1, 0, 0x11b, 0, 0);
		return 0;

	case 0x8c:
		if (L.isSet(0x133)) {
			L.clear(0x133);
			L.set(0x147);
			L.sceneAnim(0x102, 0xa6, 0xfd, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x149)) {
			L.clear(0x149);
			L.set(0x135);
			L.sceneAnim(0x104, 0xa6, 0x1f5, 1, 0, 0x11c, 0, 0);
		}
		if (L.isSet(0x147)) {
			if (L.isSet(0x158)) {
				L.set(0x107);
				L.sceneAnim(0x8e, 0x95, 0x10f, 1, 0, 0x11a, 0, 0);
				return 0;
			}
			L.set(0x1a7);
			L.sceneAnim(0x7d, 0x95, 0x10f, 1, 0, 0x11b, 0, 0);
			return 0;
		}
		if (!L.isSet(0x135))
			return 0;
		if (L.isSet(0x158)) {
			L.set(0x11b);
			L.sceneAnim(0x8f, 0x8c, 0x17f, 1, 0, 0x11a, 0, 0);
			return 0;
		}
		L.set(0x1a7);
		L.sceneAnim(0x8a, 0x95, 0x17d, 1, 0, 0x11b, 0, 0);
		return 0;

	case 0x8d:
		if (L.isSet(0x134)) {
			L.clear(0x134);
			L.set(0x148);
			L.sceneAnim(0x102, 0xa6, 0x178, 1, 0, 0x11c, 0, 0);
		}
		if (!L.isSet(0x148))
			return 0;
		if (L.isSet(0x159)) {
			L.set(0x108);
			L.sceneAnim(0x8e, 0x95, 0x18a, 1, 0, 0x11a, 0, 0);
			return 0;
		}
		L.set(0x1a8);
		L.sceneAnim(0x7d, 0x95, 0x18a, 1, 0, 0x11b, 0, 0);
		return 0;

	case 0x8e:
		if (L.isSet(0x14b)) {
			L.clear(0x14b);
			L.set(0x137);
			L.sceneAnim(0x104, 0xf1, 0x84, 1, 0, 0x167, 0, 0);
		}
		if (!L.isSet(0x137))
			return 0;
		if (L.isSet(0x15a)) {
			L.set(0x11d);
			L.sceneAnim(0x8f, 0xd7, 0xe, 1, 0, 0x165, 0, 0);
			return 0;
		}
		L.set(0x1a9);
		L.sceneAnim(0x8a, 0xe0, 0xb, 1, 0, 0x166, 0, 0);
		return 0;

	case 0x8f:
		if (L.isSet(0x136)) {
			L.clear(0x136);
			L.set(0x14a);
			L.sceneAnim(0x102, 0xf1, 7, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14c)) {
			L.clear(0x14c);
			L.set(0x138);
			L.sceneAnim(0x104, 0xf1, 0xff, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x14a)) {
			if (L.isSet(0x15b)) {
				L.set(0x10a);
				L.sceneAnim(0x8e, 0xe0, 0x19, 1, 0, 0x165, 0, 0);
				return 0;
			}
			L.set(0x1aa);
			L.sceneAnim(0x7d, 0xe0, 0x19, 1, 0, 0x166, 0, 0);
			return 0;
		}
		if (!L.isSet(0x138))
			return 0;
		if (L.isSet(0x15b)) {
			L.set(0x11e);
			L.sceneAnim(0x8f, 0xd7, 0x89, 1, 0, 0x165, 0, 0);
			return 0;
		}
		L.set(0x1aa);
		L.sceneAnim(0x8a, 0xe0, 0x86, 1, 0, 0x166, 0, 0);
		return 0;

	case 0x90:
		if (L.isSet(0x137)) {
			L.clear(0x137);
			L.set(0x14b);
			L.sceneAnim(0x102, 0xf1, 0x82, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14d)) {
			L.clear(0x14d);
			L.set(0x139);
			L.sceneAnim(0x104, 0xf1, 0x17a, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x14b)) {
			if (L.isSet(0x15c)) {
				L.set(0x10b);
				L.sceneAnim(0x8e, 0xe0, 0x94, 1, 0, 0x165, 0, 0);
				return 0;
			}
			L.set(0x1ab);
			L.sceneAnim(0x7d, 0xe0, 0x94, 1, 0, 0x166, 0, 0);
			return 0;
		}
		if (!L.isSet(0x139))
			return 0;
		if (L.isSet(0x15c)) {
			L.set(0x11f);
			L.sceneAnim(0x8f, 0xd7, 0x104, 1, 0, 0x165, 0, 0);
			return 0;
		}
		L.set(0x1ab);
		L.sceneAnim(0x8a, 0xe0, 0x102, 1, 0, 0x166, 0, 0);
		return 0;

	case 0x91:
		if (L.isSet(0x138)) {
			L.clear(0x138);
			L.set(0x14c);
			L.sceneAnim(0x102, 0xf1, 0xfd, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14e)) {
			L.clear(0x14e);
			L.set(0x13a);
			L.sceneAnim(0x104, 0xf1, 0x1f5, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x14c)) {
			if (L.isSet(0x15d)) {
				L.set(0x10c);
				L.sceneAnim(0x8e, 0xe0, 0x10f, 1, 0, 0x165, 0, 0);
				return 0;
			}
			L.set(0x1ac);
			L.sceneAnim(0x7d, 0xe0, 0x10f, 1, 0, 0x166, 0, 0);
			return 0;
		}
		if (!L.isSet(0x13a))
			return 0;
		if (L.isSet(0x15d)) {
			L.set(0x120);
			L.sceneAnim(0x8f, 0xd7, 0x17f, 1, 0, 0x165, 0, 0);
			return 0;
		}
		L.set(0x1ac);
		L.sceneAnim(0x8a, 0xe0, 0x17d, 1, 0, 0x166, 0, 0);
		return 0;

	case 0x92:
		if (L.isSet(0x139)) {
			L.clear(0x139);
			L.set(0x14d);
			L.sceneAnim(0x102, 0xf1, 0x178, 1, 0, 0x167, 0, 0);
		}
		if (!L.isSet(0x14d))
			return 0;
		if (L.isSet(0x15e)) {
			L.set(0x10d);
			L.sceneAnim(0x8e, 0xe0, 0x18a, 1, 0, 0x165, 0, 0);
			return 0;
		}
		L.set(0x1ad);
		L.sceneAnim(0x7d, 0xe0, 0x18a, 1, 0, 0x166, 0, 0);
		return 0;

	case 0x93:
		if (L.isSet(0x150)) {
			L.clear(0x150);
			L.set(0x13c);
			L.sceneAnim(0x104, 0x13c, 0x84, 1, 0, 0x1b2, 0, 0);
		}
		if (!L.isSet(0x13c))
			return 0;
		if (L.isSet(0x15f)) {
			L.set(0x122);
			L.sceneAnim(0x8f, 0x122, 0xe, 1, 0, 0x1b0, 0, 0);
			return 0;
		}
		L.set(0x1ae);
		L.sceneAnim(0x8a, 299, 0xb, 1, 0, 0x1b1, 0, 0);
		return 0;

	case 0x94:
		if (L.isSet(0x13b)) {
			L.clear(0x13b);
			L.set(0x14f);
			L.sceneAnim(0x102, 0x13c, 7, 1, 0, 0x1b2, 0, 0);
		} else if (L.isSet(0x151)) {
			L.clear(0x151);
			L.set(0x13d);
			L.sceneAnim(0x104, 0x13c, 0xff, 1, 0, 0x1b2, 0, 0);
		}
		if (L.isSet(0x14f)) {
			if (L.isSet(0x160)) {
				L.set(0x10f);
				L.sceneAnim(0x8e, 299, 0x19, 1, 0, 0x1b0, 0, 0);
				return 0;
			}
			L.set(0x1af);
			L.sceneAnim(0x7d, 299, 0x19, 1, 0, 0x1b1, 0, 0);
			return 0;
		}
		if (!L.isSet(0x13d))
			return 0;
		if (L.isSet(0x160)) {
			L.set(0x123);
			L.sceneAnim(0x8f, 0x122, 0x89, 1, 0, 0x1b0, 0, 0);
			return 0;
		}
		L.set(0x1af);
		L.sceneAnim(0x8a, 299, 0x86, 1, 0, 0x1b1, 0, 0);
		return 0;

	case 0x95:
		if (L.isSet(0x13c)) {
			L.clear(0x13c);
			L.set(0x150);
			L.sceneAnim(0x102, 0x13c, 0x82, 1, 0, 0x1b2, 0, 0);
		} else if (L.isSet(0x152)) {
			L.clear(0x152);
			L.set(0x13e);
			L.sceneAnim(0x104, 0x13c, 0x17a, 1, 0, 0x1b2, 0, 0);
		}
		if (L.isSet(0x150)) {
			if (L.isSet(0x161)) {
				L.set(0x110);
				L.sceneAnim(0x8e, 299, 0x94, 1, 0, 0x1b0, 0, 0);
				return 0;
			}
			L.set(0x1b0);
			L.sceneAnim(0x7d, 299, 0x94, 1, 0, 0x1b1, 0, 0);
			return 0;
		}
		if (!L.isSet(0x13e))
			return 0;
		if (L.isSet(0x161)) {
			L.set(0x124);
			L.sceneAnim(0x8f, 0x122, 0x104, 1, 0, 0x1b0, 0, 0);
			return 0;
		}
		L.set(0x1b0);
		L.sceneAnim(0x8a, 299, 0x102, 1, 0, 0x1b1, 0, 0);
		return 0;

	case 0x96:
		if (L.isSet(0x13d)) {
			L.clear(0x13d);
			L.set(0x151);
			L.sceneAnim(0x102, 0x13c, 0xfd, 1, 0, 0x1b2, 0, 0);
		} else if (L.isSet(0x153)) {
			L.clear(0x153);
			L.set(0x13f);
			L.sceneAnim(0x104, 0x13c, 0x1f5, 1, 0, 0x1b2, 0, 0);
		}
		if (L.isSet(0x151)) {
			if (L.isSet(0x162)) {
				L.set(0x111);
				L.sceneAnim(0x8e, 299, 0x10f, 1, 0, 0x1b0, 0, 0);
				return 0;
			}
			L.set(0x1b1);
			L.sceneAnim(0x7d, 299, 0x10f, 1, 0, 0x1b1, 0, 0);
			return 0;
		}
		if (!L.isSet(0x13f))
			return 0;
		if (L.isSet(0x162)) {
			L.set(0x125);
			L.sceneAnim(0x8f, 0x122, 0x17f, 1, 0, 0x1b0, 0, 0);
			return 0;
		}
		L.set(0x1b1);
		L.sceneAnim(0x8a, 299, 0x17d, 1, 0, 0x1b1, 0, 0);
		return 0;

	case 0x97:
		if (L.isSet(0x13e)) {
			L.clear(0x13e);
			L.set(0x152);
			L.sceneAnim(0x102, 0x13c, 0x178, 1, 0, 0x1b2, 0, 0);
		}
		if (!L.isSet(0x152))
			return 0;
		if (L.isSet(0x163)) {
			L.set(0x112);
			L.sceneAnim(0x8e, 299, 0x18a, 1, 0, 0x1b0, 0, 0);
			return 0;
		}
		L.set(0x1b2);
		L.sceneAnim(0x7d, 299, 0x18a, 1, 0, 0x1b1, 0, 0);
		return 0;

	case 0x98:
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0x4a, 2);
		if (L.isSet(0x164)) {
			L.set(0x113);
			L.sceneAnim(0x14a, 0x16c, 10, 1, 0, 0x1ce, 0, 1);
		} else {
			L.set(0x1b3);
			L.sceneAnim(0x149, 0x168, 7, 1, 0, 0x1ce, 0, 1);
		}
		L.guy() = saved;
		return 0;

	case 0x99:
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0xc5, 2);
		if (L.isSet(0x165)) {
			L.set(0x114);
			L.sceneAnim(0x14a, 0x16c, 0x85, 1, 0, 0x1ce, 0, 1);
		} else {
			L.set(0x1b4);
			L.sceneAnim(0x149, 0x168, 0x82, 1, 0, 0x1ce, 0, 1);
		}
		L.guy() = saved;
		return 0;

	case 0x9a:
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0x140, 2);
		if (L.isSet(0x166)) {
			L.set(0x115);
			L.sceneAnim(0x14a, 0x16c, 0x100, 1, 0, 0x1ce, 0, 1);
		} else {
			L.set(0x1b5);
			L.sceneAnim(0x149, 0x168, 0xfd, 1, 0, 0x1ce, 0, 1);
		}
		L.guy() = saved;
		return 0;

	case 0x9b:
		if (!L.isSet(0x16a))
			return 0;
		// The original does not switch to puppy 1 here (unlike 0x98..0x9a, 0x9c)
		L.walk(0x1cd, 0x1bb, 2);
		if (L.isSet(0x167)) {
			L.set(0x116);
			L.sceneAnim(0x14a, 0x16c, 0x17b, 1, 0, 0x1ce, 0, 1);
		} else {
			L.set(0x1b6);
			L.sceneAnim(0x149, 0x168, 0x178, 1, 0, 0x1ce, 0, 1);
		}
		L.guy() = saved;
		return 0;

	case 0x9c:
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0x236, 2);
		if (L.isSet(0x168)) {
			L.set(0x117);
			L.sceneAnim(0x14a, 0x16c, 0x1f6, 1, 0, 0x1ce, 0, 1);
		} else {
			L.set(0x1b7);
			L.sceneAnim(0x149, 0x168, 499, 1, 0, 0x1ce, 0, 1);
		}
		L.guy() = saved;
		return 0;

	case 0x9d:
		if (L.isSet(0x146)) {
			L.clear(0x146);
			L.set(0x132);
			L.sceneAnim(0x104, 0xa6, 0x84, 1, 0, 0x11c, 0, 0);
		}
		if (!L.isSet(0x132))
			return 0;
		L.clear(0x132);
		L.set(300);
		L.sceneAnim(0x8c, 0x5a, 4, 1, 0, 0x11a, 0, 0);
		L.banter(0xc);
		return 0;

	case 0x9e:
		if (L.isSet(0x131)) {
			L.clear(0x131);
			L.set(0x145);
			L.sceneAnim(0x102, 0xa6, 7, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x147)) {
			L.clear(0x147);
			L.set(0x133);
			L.sceneAnim(0x104, 0xa6, 0xff, 1, 0, 0x11c, 0, 0);
		}
		if (L.isSet(0x145)) {
			L.clear(0x145);
			L.set(0x141);
			L.sceneAnim(0x8b, 0x5c, 0x11, 1, 0, 0x11a, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x133))
			return 0;
		L.clear(0x133);
		L.set(0x12d);
		L.sceneAnim(0x8c, 0x5a, 0x7f, 1, 0, 0x11a, 0, 0);
		L.banter(0xc);
		return 0;

	case 0x9f:
		if (L.isSet(0x132)) {
			L.clear(0x132);
			L.set(0x146);
			L.sceneAnim(0x102, 0xa6, 0x82, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x148)) {
			L.clear(0x148);
			L.set(0x134);
			L.sceneAnim(0x104, 0xa6, 0x17a, 1, 0, 0x11c, 0, 0);
		}
		if (L.isSet(0x146)) {
			L.clear(0x146);
			L.set(0x142);
			L.sceneAnim(0x8b, 0x5c, 0x8c, 1, 0, 0x11a, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x134))
			return 0;
		L.clear(0x134);
		L.set(0x12e);
		L.sceneAnim(0x8c, 0x5a, 0xfa, 1, 0, 0x11a, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa0:
		if (L.isSet(0x133)) {
			L.clear(0x133);
			L.set(0x147);
			L.sceneAnim(0x102, 0xa6, 0xfd, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x149)) {
			L.clear(0x149);
			L.set(0x135);
			L.sceneAnim(0x104, 0xa6, 0x1f5, 1, 0, 0x11c, 0, 0);
		}
		if (L.isSet(0x147)) {
			L.clear(0x147);
			L.set(0x143);
			L.sceneAnim(0x8b, 0x5c, 0x107, 1, 0, 0x11a, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x135))
			return 0;
		L.clear(0x135);
		L.set(0x12f);
		L.sceneAnim(0x8c, 0x5a, 0x175, 1, 0, 0x11a, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa1:
		if (L.isSet(0x134)) {
			L.clear(0x134);
			L.set(0x148);
			L.sceneAnim(0x102, 0xa6, 0x178, 1, 0, 0x11c, 0, 0);
		}
		if (!L.isSet(0x148))
			return 0;
		L.clear(0x148);
		L.set(0x144);
		L.sceneAnim(0x8b, 0x5c, 0x182, 1, 0, 0x11a, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa2:
		if (L.isSet(0x141)) {
			L.clear(0x141);
			L.set(0x12d);
			L.sceneAnim(0x104, 0x5b, 0x84, 1, 0, 0xd1, 0, 0);
		} else if (L.isSet(0x14b)) {
			L.clear(0x14b);
			L.set(0x137);
			L.sceneAnim(0x104, 0xf1, 0x84, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x12d)) {
			L.clear(0x12d);
			L.set(0x131);
			L.sceneAnim(0x94, 0x5d, 4, 1, 0, 0x11b, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x137))
			return 0;
		L.clear(0x137);
		L.set(0x131);
		L.sceneAnim(0x8c, 0xa5, 4, 1, 0, 0x165, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa3:
		if (L.isSet(300)) {
			L.clear(300);
			L.set(0x140);
			L.sceneAnim(0x102, 0x5b, 7, 1, 0, 0xd1, 0, 0);
		} else if (L.isSet(0x142)) {
			L.clear(0x142);
			L.set(0x12e);
			L.sceneAnim(0x104, 0x5b, 0xff, 1, 0, 0xd1, 0, 0);
		} else if (L.isSet(0x136)) {
			L.clear(0x136);
			L.set(0x14a);
			L.sceneAnim(0x102, 0xf1, 7, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14c)) {
			L.clear(0x14c);
			L.set(0x138);
			L.sceneAnim(0x104, 0xf1, 0xff, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x140)) {
			L.clear(0x140);
			L.set(0x146);
			L.sceneAnim(0x8d, 0x5f, 0x20, 1, 0, 0x11b, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x12e)) {
			L.clear(0x12e);
			L.set(0x132);
			L.sceneAnim(0x94, 0x5d, 0x7f, 1, 0, 0x11b, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x14a)) {
			L.clear(0x14a);
			L.set(0x146);
			L.sceneAnim(0x8b, 0xa7, 0x11, 1, 0, 0x165, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x138))
			return 0;
		L.clear(0x138);
		L.set(0x132);
		L.sceneAnim(0x8c, 0xa5, 0x7f, 1, 0, 0x166, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa4:
		if (L.isSet(0x12d)) {
			L.clear(0x12d);
			L.set(0x141);
			L.sceneAnim(0x102, 0x5b, 0x82, 1, 0, 0xd1, 0, 0);
		} else if (L.isSet(0x143)) {
			L.clear(0x143);
			L.set(0x12f);
			L.sceneAnim(0x104, 0x5b, 0x17a, 1, 0, 0xd1, 0, 0);
		} else if (L.isSet(0x137)) {
			L.clear(0x137);
			L.set(0x14b);
			L.sceneAnim(0x102, 0xf1, 0x82, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14d)) {
			L.clear(0x14d);
			L.set(0x139);
			L.sceneAnim(0x104, 0xf1, 0x17a, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x141)) {
			L.clear(0x141);
			L.set(0x147);
			L.sceneAnim(0x8d, 0x5f, 0x9b, 1, 0, 0x11b, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x12f)) {
			L.clear(0x12f);
			L.set(0x133);
			L.sceneAnim(0x94, 0x5d, 0xfa, 1, 0, 0x11b, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x14b)) {
			L.clear(0x14b);
			L.set(0x147);
			L.sceneAnim(0x8b, 0xa7, 0x8c, 1, 0, 0x165, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x139))
			return 0;
		L.clear(0x139);
		L.set(0x133);
		L.sceneAnim(0x8c, 0xa5, 0xfa, 1, 0, 0x165, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa5:
		if (L.isSet(0x12e)) {
			L.clear(0x12e);
			L.set(0x142);
			L.sceneAnim(0x102, 0x5b, 0xfd, 1, 0, 0xd1, 0, 0);
		} else if (L.isSet(0x144)) {
			L.clear(0x144);
			L.set(0x130);
			L.sceneAnim(0x104, 0x5b, 0x1f5, 1, 0, 0xd1, 0, 0);
		} else if (L.isSet(0x138)) {
			L.clear(0x138);
			L.set(0x14c);
			L.sceneAnim(0x102, 0xf1, 0xfd, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14e)) {
			L.clear(0x14e);
			L.set(0x13a);
			L.sceneAnim(0x104, 0xf1, 0x1f5, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x142)) {
			L.clear(0x142);
			L.set(0x148);
			L.sceneAnim(0x8d, 0x5f, 0x116, 1, 0, 0x11b, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x130)) {
			L.clear(0x130);
			L.set(0x134);
			L.sceneAnim(0x94, 0x5d, 0x175, 1, 0, 0x11b, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x14c)) {
			L.clear(0x14c);
			L.set(0x148);
			L.sceneAnim(0x8b, 0xa7, 0x107, 1, 0, 0x165, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x13a))
			return 0;
		L.clear(0x13a);
		L.set(0x134);
		L.sceneAnim(0x8c, 0xa5, 0x175, 1, 0, 0x165, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa6:
		if (L.isSet(0x12f)) {
			L.clear(0x12f);
			L.set(0x143);
			L.sceneAnim(0x102, 0x5b, 0x178, 1, 0, 0xd1, 0, 0);
		} else if (L.isSet(0x139)) {
			L.clear(0x139);
			L.set(0x14d);
			L.sceneAnim(0x102, 0xf1, 0x178, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x143)) {
			L.clear(0x143);
			L.set(0x149);
			L.sceneAnim(0x8d, 0x5f, 0x191, 1, 0, 0x11b, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x14d))
			return 0;
		L.clear(0x14d);
		L.set(0x149);
		L.sceneAnim(0x8b, 0xa7, 0x182, 1, 0, 0x165, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa7:
		if (L.isSet(0x146)) {
			L.clear(0x146);
			L.set(0x132);
			L.sceneAnim(0x104, 0xa6, 0x84, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x150)) {
			L.clear(0x150);
			L.set(0x13c);
			L.sceneAnim(0x104, 0x13c, 0x84, 1, 0, 0x1b2, 0, 0);
		}
		if (L.isSet(0x132)) {
			L.clear(0x132);
			L.set(0x136);
			L.sceneAnim(0x94, 0xa8, 4, 1, 0, 0x166, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x13c))
			return 0;
		L.clear(0x13c);
		L.set(0x136);
		L.sceneAnim(0x8c, 0xf0, 4, 1, 0, 0x1b0, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa8:
		if (L.isSet(0x131)) {
			L.clear(0x131);
			L.set(0x145);
			L.sceneAnim(0x102, 0xa6, 7, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x147)) {
			L.clear(0x147);
			L.set(0x133);
			L.sceneAnim(0x104, 0xa6, 0xff, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x13b)) {
			L.clear(0x13b);
			L.set(0x14f);
			L.sceneAnim(0x102, 0x13c, 7, 1, 0, 0x1b2, 0, 0);
		} else if (L.isSet(0x151)) {
			L.clear(0x151);
			L.set(0x13d);
			L.sceneAnim(0x104, 0x13c, 0xff, 1, 0, 0x1b2, 0, 0);
		}
		if (L.isSet(0x145)) {
			L.clear(0x145);
			L.set(0x14b);
			L.sceneAnim(0x8d, 0xaa, 0x20, 1, 0, 0x166, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x133)) {
			L.clear(0x133);
			L.set(0x137);
			L.sceneAnim(0x94, 0xa8, 0x7f, 1, 0, 0x166, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x14f)) {
			L.clear(0x14f);
			L.set(0x14b);
			L.sceneAnim(0x8b, 0xf2, 0x11, 1, 0, 0x1b0, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x13d))
			return 0;
		L.clear(0x13d);
		L.set(0x137);
		L.sceneAnim(0x8c, 0xf0, 0x7f, 1, 0, 0x1b0, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xa9:
		if (L.isSet(0x132)) {
			L.clear(0x132);
			L.set(0x146);
			L.sceneAnim(0x102, 0xa6, 0x82, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x148)) {
			L.clear(0x148);
			L.set(0x134);
			L.sceneAnim(0x104, 0xa6, 0x17a, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x13c)) {
			L.clear(0x13c);
			L.set(0x150);
			L.sceneAnim(0x102, 0x13c, 0x82, 1, 0, 0x1b2, 0, 0);
		} else if (L.isSet(0x152)) {
			L.clear(0x152);
			L.set(0x13e);
			L.sceneAnim(0x104, 0x13c, 0x17a, 1, 0, 0x1b2, 0, 0);
		}
		if (L.isSet(0x146)) {
			L.clear(0x146);
			L.set(0x14c);
			L.sceneAnim(0x8d, 0xaa, 0x9b, 1, 0, 0x166, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x134)) {
			L.clear(0x134);
			L.set(0x138);
			L.sceneAnim(0x94, 0xa8, 0xfa, 1, 0, 0x166, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x150)) {
			L.clear(0x150);
			L.set(0x14c);
			L.sceneAnim(0x8b, 0xf2, 0x8c, 1, 0, 0x1b0, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x13e))
			return 0;
		L.clear(0x13e);
		L.set(0x138);
		L.sceneAnim(0x8c, 0xf0, 0xfa, 1, 0, 0x1b0, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xaa:
		if (L.isSet(0x133)) {
			L.clear(0x133);
			L.set(0x147);
			L.sceneAnim(0x102, 0xa6, 0xfd, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x149)) {
			L.clear(0x149);
			L.set(0x135);
			L.sceneAnim(0x104, 0xa6, 0x1f5, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x13d)) {
			L.clear(0x13d);
			L.set(0x151);
			L.sceneAnim(0x102, 0x13c, 0xfd, 1, 0, 0x1b2, 0, 0);
		} else if (L.isSet(0x153)) {
			L.clear(0x153);
			L.set(0x13f);
			L.sceneAnim(0x104, 0x13c, 0x1f5, 1, 0, 0x1b2, 0, 0);
		}
		if (L.isSet(0x147)) {
			L.clear(0x147);
			L.set(0x14d);
			L.sceneAnim(0x8d, 0xaa, 0x116, 1, 0, 0x166, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x135)) {
			L.clear(0x135);
			L.set(0x139);
			L.sceneAnim(0x94, 0xa8, 0x175, 1, 0, 0x166, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x151)) {
			L.clear(0x151);
			L.set(0x14d);
			L.sceneAnim(0x8b, 0xf2, 0x107, 1, 0, 0x1b0, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x13f))
			return 0;
		L.clear(0x13f);
		L.set(0x139);
		L.sceneAnim(0x8c, 0xf0, 0x175, 1, 0, 0x1b0, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xab:
		if (L.isSet(0x134)) {
			L.clear(0x134);
			L.set(0x148);
			L.sceneAnim(0x102, 0xa6, 0x178, 1, 0, 0x11c, 0, 0);
		} else if (L.isSet(0x13e)) {
			L.clear(0x13e);
			L.set(0x152);
			L.sceneAnim(0x102, 0x13c, 0x178, 1, 0, 0x1b2, 0, 0);
		}
		if (L.isSet(0x148)) {
			L.clear(0x148);
			L.set(0x14e);
			L.sceneAnim(0x8d, 0xaa, 0x191, 1, 0, 0x166, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x152))
			return 0;
		L.clear(0x152);
		L.set(0x14e);
		L.sceneAnim(0x8b, 0xf2, 0x182, 1, 0, 0x1b0, 0, 0);
		L.banter(0xc);
		return 0;

	case 0xac:
		if (L.isSet(0x14b)) {
			L.clear(0x14b);
			L.set(0x137);
			L.sceneAnim(0x104, 0xf1, 0x84, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x137)) {
			L.clear(0x137);
			L.set(0x13b);
			L.sceneAnim(0x94, 0xf3, 4, 1, 0, 0x1b1, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0x4a, 2);
		L.clear(0x16a);
		L.set(0x14f);
		L.hide(1);
		return dropThrough(0xd);

	case 0xad:
		if (L.isSet(0x136)) {
			L.clear(0x136);
			L.set(0x14a);
			L.sceneAnim(0x102, 0xf1, 7, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14c)) {
			L.clear(0x14c);
			L.set(0x138);
			L.sceneAnim(0x104, 0xf1, 0xff, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x14a)) {
			L.clear(0x14a);
			L.set(0x150);
			L.sceneAnim(0x8d, 0xf5, 0x20, 1, 0, 0x1b1, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x138)) {
			L.clear(0x138);
			L.set(0x13c);
			L.sceneAnim(0x94, 0xf3, 0x7f, 1, 0, 0x1b1, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0xc5, 2);
		L.clear(0x16a);
		L.set(0x150);
		L.hide(1);
		return dropThrough(0x88);

	case 0xae:
		if (L.isSet(0x137)) {
			L.clear(0x137);
			L.set(0x14b);
			L.sceneAnim(0x102, 0xf1, 0x82, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14d)) {
			L.clear(0x14d);
			L.set(0x139);
			L.sceneAnim(0x104, 0xf1, 0x17a, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x14b)) {
			L.clear(0x14b);
			L.set(0x151);
			L.sceneAnim(0x8d, 0xf5, 0x9b, 1, 0, 0x1b1, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x139)) {
			L.clear(0x139);
			L.set(0x13d);
			L.sceneAnim(0x94, 0xf3, 0xfa, 1, 0, 0x1b1, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0x140, 2);
		L.clear(0x16a);
		L.set(0x151);
		L.hide(1);
		return dropThrough(0x103);

	case 0xaf:
		if (L.isSet(0x138)) {
			L.clear(0x138);
			L.set(0x14c);
			L.sceneAnim(0x102, 0xf1, 0xfd, 1, 0, 0x167, 0, 0);
		} else if (L.isSet(0x14e)) {
			L.clear(0x14e);
			L.set(0x13a);
			L.sceneAnim(0x104, 0xf1, 0x1f5, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x14c)) {
			L.clear(0x14c);
			L.set(0x152);
			L.sceneAnim(0x8d, 0xf5, 0x116, 1, 0, 0x1b1, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (L.isSet(0x13a)) {
			L.clear(0x13a);
			L.set(0x13e);
			L.sceneAnim(0x94, 0xf3, 0x175, 1, 0, 0x1b1, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0x1bb, 2);
		L.clear(0x16a);
		L.set(0x152);
		L.hide(1);
		return dropThrough(0x17e);

	case 0xb0:
		if (L.isSet(0x139)) {
			L.clear(0x139);
			L.set(0x14d);
			L.sceneAnim(0x102, 0xf1, 0x17a, 1, 0, 0x167, 0, 0);
		}
		if (L.isSet(0x14d)) {
			L.clear(0x14d);
			L.set(0x153);
			L.sceneAnim(0x8d, 0xf5, 0x191, 1, 0, 0x1b1, 0, 0);
			L.banter(0xc);
			return 0;
		}
		if (!L.isSet(0x16a))
			return 0;
		L.guy() = 1;
		L.walk(0x1cd, 0x236, 2);
		L.clear(0x16a);
		L.set(0x153);
		L.hide(1);
		return dropThrough(0x1f9);

	case 0x11b:
		return leave(0x140, 300);
	case 0x11c:
		return leave(0x141, 0x12d);
	case 0x11d:
		return leave(0x142, 0x12e);
	case 0x11e:
		return leave(0x143, 0x12f);
	case 0x11f:
		return leave(0x144, 0x130);

	default:
		return 0;
	}
}

// Use on a hotspot (C.PRG 0x3bed0): hotspot 0x2a, leave by the block the puppy stands on; always 0
int act1_3bed0(Logic &L, int hotspotId) {
	if (hotspotId != 0x2a)
		return 0;

	// The shared ending: fade, then on to 1X01X05 (from either side of the floor)
	auto leave = [&](int x1, int d1) {
		L.set(0x177);
		L.fadeOut(0x10, 0xff, 0, 0, 0);
		L.unfade();
		act1_473f0(L);
		L.changeScene(0x107, x1, d1, 0x10e, 0x213, 0, 1, 1, 5);
		return 0;
	};

	if (L.isSet(300)) {
		L.clear(300);
		L.set(0x1a0);
		L.sceneAnim(0x93, 4, -7, 1, 0, 0xd0, 0, 0);
		return leave(0x150, 4);
	}
	if (L.isSet(0x140)) {
		L.clear(0x140);
		L.set(0x1a0);
		L.sceneAnim(0x92, 4, -0x10, 1, 0, 0xcf, 0, 0);
		return leave(0x12c, 0);
	}
	if (L.isSet(0x12d)) {
		L.clear(0x12d);
		L.set(0x1a1);
		L.sceneAnim(0x93, 4, 0x74, 1, 0, 0xd0, 0, 0);
		return leave(0x150, 4);
	}
	if (L.isSet(0x141)) {
		L.clear(0x141);
		L.set(0x1a1);
		L.sceneAnim(0x92, 4, 0x6b, 1, 0, 0xcf, 0, 0);
		return leave(0x12c, 0);
	}
	if (L.isSet(0x12e)) {
		L.clear(0x12e);
		L.set(0x1a2);
		L.sceneAnim(0x93, 4, 0xef, 1, 0, 0xd0, 0, 0);
		return leave(0x150, 4);
	}
	if (L.isSet(0x142)) {
		L.clear(0x142);
		L.set(0x1a2);
		L.sceneAnim(0x92, 4, 0xe6, 1, 0, 0xcf, 0, 0);
		return leave(0x12c, 0);
	}
	if (L.isSet(0x12f)) {
		L.clear(0x12f);
		L.set(0x1a3);
		L.sceneAnim(0x93, 4, 0x16a, 1, 0, 0xd0, 0, 0);
		return leave(0x150, 4);
	}
	if (L.isSet(0x143)) {
		L.clear(0x143);
		L.set(0x1a3);
		L.sceneAnim(0x92, 4, 0x161, 1, 0, 0xcf, 0, 0);
		return leave(0x12c, 0);
	}
	if (L.isSet(0x130)) {
		L.clear(0x130);
		L.set(0x1a4);
		L.sceneAnim(0x93, 4, 0x1e5, 1, 0, 0xd0, 0, 0);
		return leave(0x150, 4);
	}
	if (L.isSet(0x144)) {
		L.clear(0x144);
		L.set(0x1a4);
		L.sceneAnim(0x92, 4, 0x1dc, 1, 0, 0xcf, 0, 0);
		return leave(0x12c, 0);
	}
	if (L.isSet(0x16a)) {
		int saved = L.guy();
		L.guy() = 1;
		L.walk(L.guyY(1), L.guyX(1), 2);
		L.banter(10);
		L.guy() = saved;
	}
	return 0;
}

// Use on an object (C.PRG 0x3c5b0): an assignment where a compare was meant; always 0
int act1_3c5b0(Logic &L, int objectId) {
	// for each scene object (count 0x88dd4, records 0x2f0 at 0xc4f00): its global id
	// (+0x298, 0xc5198) = objectId; stop after the first if objectId != 0
	for (int i = 0; i < L.objectCount(); i++) {
		L.setObjectIdAt(i, objectId);
		if (objectId != 0)
			break;
	}
	return 0;
}

void act1_use_initVars(Logic &L) {
	// The use handlers keep no globals of their own (flags and puppy state only)
	(void)L;
}

} // End of namespace DreamForge
