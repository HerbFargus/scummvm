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
#include "dreamforge/banter.h"

namespace DreamForge {

// Lines per banter set (C.PRG 0x15de0)
static const int kBanterCount[14] = { 6, 9, 4, 5, 8, 4, 4, 4, 13, 16, 9, 4, 4, 4 };

// 0xdf048 is a byte, initially 0: nothing to set
void banter_initVars(Logic &) {
}

// The puppies' random banter: pick line r of the set and play it
void banter_1c720(Logic &L, int set) {
	L.busyOn();
	bool wasIdle = L.var(0xdf048) == 0;
	L.var(0xdf048) = 1;
	// Out of range sets (none in the data) fall through to the cleanup
	uint32 count = (set >= 0 && set < 14) ? kBanterCount[set] : 0;
	uint32 r = ((uint32)L.rand15() * count) >> 15;
	int saved = L.guy();
	int voice = -1, who = 0;

	switch (set) {
	case 0:
		switch (r) {
		case 0: L.waitSound(L.voice(11000 + 0x3b), 0); break;
		case 1: L.waitSound(L.voice(11000 + 0x3c), 0); break;
		case 2: L.waitSound(L.voice(11000 + 0x3d), 0); break;
		case 3: L.waitSound(L.voice(11000 + 0x3e), 0); break;
		case 4: L.waitSound(L.voice(11000 + 0x3f), 0); break;
		case 5: L.waitSound(L.voice(11000 + 0x40), 0); break;
		default: break;
		}
		break;

	case 1:
		// A random puppy gets up if needed and does the banter gesture
		L.guy() = (L.rand15() >> 14) + 1;
		if (L.isLying(L.guy()))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(L.guy()))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0xd, L.guyDir(L.guy()));
		L.guy() = saved;
		switch (r) {
		case 0:
			L.waitSound(L.voice(22000 + 2), 1);
			L.waitSound(L.voice(21000 + 5), 1);
			L.waitSound(L.voice(21000 + 6), 1);
			break;
		case 1: L.waitSound(L.voice(22000 + 3), 1); break;
		case 2: L.waitSound(L.voice(22000 + 4), 1); break;
		case 3: L.waitSound(L.voice(22000 + 5), 1); break;
		case 4: L.waitSound(L.voice(21000 + 7), 1); break;
		case 5: L.waitSound(L.voice(21000 + 8), 1); break;
		case 6: L.waitSound(L.voice(21000 + 9), 1); break;
		case 7: L.waitSound(L.voice(21000 + 10), 1); break;
		case 8: L.waitSound(L.voice(21000 + 0xb), 1); break;
		default: break;
		}
		break;

	case 2:
		L.guy() = (L.rand15() >> 14) + 1;
		if (L.isLying(L.guy()))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(L.guy()))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0xd, L.guyDir(L.guy()));
		L.guy() = saved;
		switch (r) {
		case 0:
			L.waitSound(L.voice(21000 + 0xc), 1);
			L.waitSound(L.voice(22000 + 6), 1);
			break;
		case 1:
			L.waitSound(L.voice(21000 + 0xd), 1);
			L.waitSound(L.voice(22000 + 9), 1);
			break;
		case 2:
			L.waitSound(L.voice(22000 + 7), 1);
			break;
		case 3:
			L.waitSound(L.voice(21000 + 0xe), 1);
			L.waitSound(L.voice(22000 + 8), 1);
			break;
		default:
			break;
		}
		break;

	case 3:
		L.guy() = (L.rand15() >> 14) + 1;
		L.faceEachOther();
		L.guy() = saved;
		switch (r) {
		case 0: who = 1; voice = 10000 + 0x37; break;
		case 1: who = 2; voice = 11000 + 0x41; break;
		case 2: who = 1; voice = 10000 + 0x38; break;
		case 3: who = 2; voice = 11000 + 0x42; break;
		case 4: L.say(11000 + 0x43, 2, false); break;
		default: break;
		}
		break;

	case 4:
		L.guy() = (L.rand15() >> 14) + 1;
		L.faceEachOther();
		L.guy() = saved;
		switch (r) {
		case 0: who = 2; voice = 11000 + 0x44; break;
		case 1: who = 2; voice = 11000 + 0x45; break;
		case 2: who = 2; voice = 11000 + 0x46; break;
		case 3: who = 2; voice = 11000 + 0x47; break;
		case 4: who = 2; voice = 11000 + 0x48; break;
		case 5: who = 2; voice = 11000 + 0x49; break;
		case 6: who = 2; voice = 11000 + 0x4a; break;
		case 7: L.say(11000 + 0x4b, 2, false); break;
		default: break;
		}
		break;

	case 5:
		L.guy() = (L.rand15() >> 14) + 1;
		L.faceEachOther();
		L.guy() = saved;
		switch (r) {
		case 0:
			L.say(10000 + 0x39, 1, false);
			who = 2; voice = 11000 + 0x4c;
			break;
		case 1:
			who = 2; voice = 11000 + 0x4d;
			break;
		case 2:
			L.say(10000 + 0x3a, 1, false);
			who = 2; voice = 11000 + 0x4e;
			break;
		case 3:
			L.say(10000 + 0x3b, 1, false);
			L.say(11000 + 0x4f, 2, false);
			break;
		default:
			break;
		}
		break;

	case 6:
		L.guy() = (L.rand15() >> 14) + 1;
		L.faceEachOther();
		L.guy() = saved;
		switch (r) {
		case 0:
			L.say(11000 + 0x50, 2, false);
			who = 1; voice = 10000 + 0x3c;
			break;
		case 1:
			L.say(11000 + 0x51, 2, false);
			who = 1; voice = 10000 + 0x3d;
			break;
		case 2:
			L.say(11000 + 0x52, 2, false);
			who = 1; voice = 10000 + 0x3e;
			break;
		case 3:
			L.say(11000 + 0x188, 2, false);
			break;
		default:
			break;
		}
		break;

	case 7:
		L.guy() = (L.rand15() >> 14) + 1;
		L.faceEachOther();
		L.guy() = saved;
		switch (r) {
		case 0: who = 2; voice = 11000 + 0x53; break;
		case 1: who = 2; voice = 11000 + 0x54; break;
		case 2: who = 2; voice = 11000 + 0x55; break;
		case 3: L.say(11000 + 0x56, 2, false); break;
		default: break;
		}
		break;

	case 8:
		// The random puppy stays current through the lines; r 3, 4 and 11 say nothing
		L.guy() = (L.rand15() >> 14) + 1;
		switch (r) {
		case 0:
			L.faceEachOther();
			L.say(10000 + 0x41, 1, false);
			L.say(11000 + 0x57, 2, false);
			L.say(10000 + 0x42, 1, false);
			break;
		case 1:
			L.faceEachOther();
			L.say(10000 + 0x43, 1, false);
			break;
		case 2:
			L.faceEachOther();
			L.say(10000 + 0x44, 1, false);
			L.say(11000 + 0x58, 2, false);
			L.say(10000 + 0x45, 1, false);
			L.say(11000 + 0x59, 2, false);
			break;
		case 5:
			L.faceEachOther();
			L.say(10000 + 0x47, 1, false);
			break;
		case 6:
			L.faceEachOther();
			L.say(10000 + 0x4b, 1, false);
			break;
		case 7:
			L.faceEachOther();
			L.say(10000 + 0x48, 1, false);
			break;
		case 8:
			L.faceEachOther();
			L.say(10000 + 0x40, 1, false);
			break;
		case 9:
			L.faceEachOther();
			L.say(11000 + 0x5c, 2, false);
			break;
		case 10:
			L.faceEachOther();
			L.say(10000 + 0x4a, 1, false);
			break;
		case 12:
			L.faceEachOther();
			L.say(10000 + 0x3f, 1, false);
			break;
		default:
			break;
		}
		L.guy() = saved;
		break;

	case 9:
		L.guy() = (L.rand15() >> 14) + 1;
		if (L.isLying(L.guy()))
			L.oneShot(0x1c, L.guyDir(L.guy()));
		else if (L.isSitting(L.guy()))
			L.oneShot(7, L.guyDir(L.guy()));
		L.oneShot(0xd, L.guyDir(L.guy()));
		L.guy() = saved;
		switch (r) {
		case 0: L.waitSound(L.voice(21000 + 0xf), 1); break;
		case 1: L.waitSound(L.voice(21000 + 0x10), 1); break;
		case 2: L.waitSound(L.voice(21000 + 0x11), 1); break;
		case 3: L.waitSound(L.voice(21000 + 0x12), 1); break;
		case 4: L.waitSound(L.voice(21000 + 0x13), 1); break;
		case 5: L.waitSound(L.voice(21000 + 0x14), 1); break;
		case 6: L.waitSound(L.voice(21000 + 0x15), 1); break;
		case 7: L.waitSound(L.voice(21000 + 0x16), 1); break;
		case 8: L.waitSound(L.voice(22000 + 10), 1); break;
		case 9: L.waitSound(L.voice(22000 + 0xb), 1); break;
		case 10: L.waitSound(L.voice(22000 + 0xc), 1); break;
		case 11: L.waitSound(L.voice(22000 + 0xd), 1); break;
		case 12: L.waitSound(L.voice(22000 + 0xe), 1); break;
		case 13: L.waitSound(L.voice(22000 + 0xf), 1); break;
		case 14: L.waitSound(L.voice(22000 + 0x10), 1); break;
		case 15: L.waitSound(L.voice(22000 + 0x11), 1); break;
		default: break;
		}
		break;

	case 10:
		L.guy() = (L.rand15() >> 14) + 1;
		L.faceEachOther();
		L.guy() = saved;
		switch (r) {
		case 0:
			L.say(10000 + 0x4e, 1, false);
			L.say(11000 + 0x5d, 2, false);
			who = 1; voice = 10000 + 0x4f;
			break;
		case 1:
			who = 1; voice = 10000 + 0x50;
			break;
		case 2:
			who = 1; voice = 10000 + 0x51;
			break;
		case 3:
			L.say(10000 + 0x52, 1, false);
			who = 2; voice = 11000 + 0x5e;
			break;
		case 4:
			who = 1; voice = 10000 + 0x53;
			break;
		case 5:
			L.say(10000 + 0x54, 1, false);
			L.say(11000 + 0x5f, 2, false);
			who = 1; voice = 10000 + 0x55;
			break;
		case 6:
			L.say(10000 + 0x4c, 1, false);
			who = 2; voice = 11000 + 0x60;
			break;
		case 7:
			L.say(10000 + 0x4d, 1, false);
			who = 2; voice = 11000 + 0x61;
			break;
		case 8:
			L.say(11000 + 0x62, 2, false);
			break;
		default:
			break;
		}
		break;

	case 11:
		L.guy() = (L.rand15() >> 14) + 1;
		L.faceEachOther();
		L.guy() = saved;
		switch (r) {
		case 0: who = 2; voice = 11000 + 0x108; break;
		case 1: who = 2; voice = 11000 + 0x121; break;
		case 2: who = 2; voice = 11000 + 0x122; break;
		case 3: L.say(11000 + 0x123, 2, false); break;
		default: break;
		}
		break;

	case 12:
		// r 1 and 2 say nothing
		switch (r) {
		case 0: L.say(11000 + 0xc4, 2, false); break;
		case 3: L.say(11000 + 0xc5, 2, false); break;
		default: break;
		}
		break;

	case 13:
		// r 0 and 3 say nothing
		switch (r) {
		case 1: who = 2; voice = 11000 + 0xc2; break;
		case 2: who = 2; voice = 11000 + 0xc3; break;
		default: break;
		}
		break;

	default:
		break;
	}

	// The shared final line (0x1d603)
	if (voice >= 0)
		L.say(voice, who, false);

	if (wasIdle)
		L.var(0xdf048) = 0;
	L.busyOff();
}

} // End of namespace DreamForge
