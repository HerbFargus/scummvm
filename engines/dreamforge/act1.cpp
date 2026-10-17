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

#include "dreamforge/act1_afterload.h"
#include "dreamforge/act1_click.h"
#include "dreamforge/act1_hooks.h"
#include "dreamforge/act1_post.h"
#include "dreamforge/act1_use.h"
#include "dreamforge/actor.h"
#include "dreamforge/banter.h"
#include "dreamforge/logic.h"
#include "dreamforge/detection.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/scene.h"

namespace DreamForge {

// Voice banks as the room code numbers them (C.PRG 0xdfb88, 0xdfb8c)
enum {
	kVoice10 = 10000,
	kVoice11 = 11000
};

void DreamForgeEngine::hideActors(int which) {
	for (int n = 1; n <= 2; n++)
		if (which == n || which == 3)
			getActor(n)->_hidden = true;
}

void DreamForgeEngine::showActors(int which) {
	for (int n = 1; n <= 2; n++)
		if (which == n || which == 3)
			getActor(n)->_hidden = false;
}

void DreamForgeEngine::setActorPos(int actor, int y, int x) {
	Actor *a = getActor(actor);
	a->_y = y;
	a->_x = x;
}

// Act 1 scene entry (C.PRG 0x41060, called as [0x88384 + act * 4](room, area)): hides
// the puppies where a room opens on a story moment
void DreamForgeEngine::act1Enter(int room, int area) {
	auto hideBoth = [this]() {
		hideActors(3);
		setFlag(0x11, true);
	};
	switch (area) {
	case 0:
		if (room == 1 && (isFlagSet(0xA0) || isFlagSet(0x42)))
			hideBoth();
		break;
	case 1:
		switch (room) {
		case 1:
			if (!isFlagSet(0x43))
				hideBoth();
			break;
		case 2:
			if (isFlagSet(0xCB)) {
				hideBoth();
			} else if (isFlagSet(0x4F)) {
				hideActors(2);
				showActors(1);
				setFlag(0x11, true);
			}
			break;
		case 3:
			if (isFlagSet(0x1B8)) {
				hideBoth();
			} else if (isFlagSet(0x46)) {
				hideBoth();
				setFlag(0x1B9, true);
			} else if (isFlagSet(0xD5)) {
				setFlag(0x1BA, true);
				hideBoth();
			}
			break;
		case 4: {
			// Any of the five groups of five flags at 0x12c.. and 0x140.. keeps puppy 1 away
			bool any = false;
			for (int f = 0x12C; f <= 0x153 && !any; f++)
				any = isFlagSet(f);
			if (any)
				hideActors(1);
			else
				setFlag(0x16A, true);
			for (int f = 0x196; f <= 0x19F; f++)
				setFlag(f, false);
			break;
		}
		case 5:
			setFlag(0x176, false);
			if (isFlagSet(0x42)) {
				setFlag(0x173, true);
				hideActors(3);
			}
			break;
		case 6:
			if (isFlagSet(0xEF))
				hideActors(3);
			break;
		case 7:
			if (isFlagSet(0x69) || isFlagSet(0x6C) || isFlagSet(0x16F))
				hideBoth();
			break;
		case 8:
		case 9:
		case 11:
		case 12:
			hideBoth();
			break;
		case 10:
			if (!_newGame)
				hideActors(3);
			break;
		default:
			break;
		}
		break;
	case 2:
		switch (room) {
		case 1:
			if (isFlagSet(100))
				setFlag(0x11, true);
			if (isFlagSet(0x1CC))
				hideActors(3);
			break;
		case 2:
			if (isFlagSet(0x74)) {
				hideBoth();
			} else if (isFlagSet(0x76)) {
				setFlag(0x1BF, true);
				hideActors(2);
				setFlag(0x11, true);
				setActorPos(1, 0x186, 0x122);
			}
			break;
		case 5:
			if (isFlagSet(0xD9)) {
				hideActors(1);
				setFlag(0x11, true);
			} else if (isFlagSet(0x16E)) {
				hideActors(3);
			}
			break;
		case 6:
		case 9:
			hideBoth();
			break;
		case 8:
			if (isFlagSet(0x42))
				hideBoth();
			break;
		case 10:
			if (isFlagSet(0x178)) {
				setFlag(0x99, true);
				setFlag(0x1BB, true);
			}
			break;
		case 11:
			if (isFlagSet(0x5A))
				hideBoth();
			break;
		default:
			break;
		}
		break;
	case 3:
		if (room == 1) {
			if (isFlagSet(0x35) || isFlagSet(0x9F))
				hideActors(3);
		} else if (room == 2) {
			if (isFlagSet(0xC9) || isFlagSet(0xC6)) {
				setFlag(0xC9, false);
				hideActors(1);
				setFlag(0x11, true);
			}
		} else if (room == 3) {
			hideBoth();
		}
		break;
	default:
		break;
	}
}

// Act 1 after the scene is on screen (C.PRG 0x418b0, [0x88394 + act * 4](room, area))
void DreamForgeEngine::act1AfterLoad(int room, int area) {
	act1_418b0(*_logic, room, area);
}

void DreamForgeEngine::initLogicVars() {
	_logic->clearVars();
	act1_afterload_initVars(*_logic);
	banter_initVars(*_logic);
	act1_click_initVars(*_logic);
	act1_hooks_initVars(*_logic);
	act1_post_initVars(*_logic);
	act1_use_initVars(*_logic);
}

// 0x477e0: in the tile puzzle room (1, 1, 4) a button shows no cursor while
// the tiles it would move are all in place
bool DreamForgeEngine::layerCursorAllowed(int layerId) const {
	if (!_scene || _scene->_act != 1 || _scene->_area != 1 || _scene->_room != 4)
		return true;
	// The four layers of a button, and the flags that must all be clear
	struct Button {
		int16 ids[4];
		int16 flags[11];  // -1 ends
	};
	static const Button kButtons[] = {
		{ { 0x2B, 0x9D, 0xB3, 0xC7 }, { 0x132, 0x146, -1 } },
		{ { 0x8A, 0x9E, 0xB4, 0xC8 }, { 0x131, 0x145, 0x133, 0x147, -1 } },
		{ { 0x8B, 0x9F, 0xB5, 0xC9 }, { 0x132, 0x146, 0x134, 0x148, -1 } },
		{ { 0x8C, 0xA0, 0xB6, 0xCA }, { 0x133, 0x147, 0x135, 0x149, -1 } },
		{ { 0x8D, 0xA1, 0xB7, 0xCB }, { 0x134, 0x148, -1 } },
		{ { 0x8E, 0xA2, 0xB8, 0xCC }, { 0x12D, 0x141, 0x137, 0x14B, -1 } },
		{ { 0x8F, 0xA3, 0xB9, 0xCD }, { 300, 0x140, 0x12E, 0x142, 0x136, 0x14A, 0x138, 0x14C, -1 } },
		{ { 0x90, 0xA4, 0xBA, 0xCE }, { 0x12D, 0x141, 0x12F, 0x143, 0x137, 0x14B, 0x139, 0x14D, -1 } },
		{ { 0x91, 0xA5, 0xBB, 0xCF }, { 0x12E, 0x142, 0x130, 0x144, 0x138, 0x14C, 0x13A, 0x14E, -1 } },
		{ { 0x92, 0xA6, 0xBC, 0xD0 }, { 0x12F, 0x143, 0x139, 0x14D, -1 } },
		{ { 0x93, 0xA7, 0xBD, 0xD1 }, { 0x132, 0x146, 0x13C, 0x150, -1 } },
		{ { 0x94, 0xA8, 0xBE, 0xD2 }, { 0x131, 0x145, 0x133, 0x147, 0x13B, 0x14F, 0x13D, 0x151, -1 } },
		{ { 0x95, 0xA9, 0xBF, 0xD3 }, { 0x132, 0x146, 0x134, 0x148, 0x13C, 0x150, 0x13E, 0x152, -1 } },
		{ { 0x96, 0xAA, 0xC0, 0xD4 }, { 0x133, 0x147, 0x135, 0x149, 0x13D, 0x151, 0x13F, 0x153, -1 } },
		{ { 0x97, 0xAB, 0xC1, 0xD5 }, { 0x134, 0x148, 0x13E, 0x152, -1 } },
		{ { 0x98, 0xAC, 0xC2, 0xD6 }, { 0x137, 0x14B, 0x16A, -1 } },
		{ { 0x99, 0xAD, 0xC3, 0xD7 }, { 0x136, 0x14A, 0x138, 0x14C, 0x16A, -1 } },
		{ { 0x9A, 0xAE, 0xC4, 0xD8 }, { 0x137, 0x14B, 0x139, 0x14D, 0x16A, -1 } },
		{ { 0x9B, 0xAF, 0xC5, 0xD9 }, { 0x138, 0x14C, 0x13A, 0x14E, 0x16A, -1 } },
		{ { 0x9C, 0xB0, 0xC6, 0xDA }, { 0x139, 0x14D, 0x16A, -1 } }
	};
	for (auto &b : kButtons) {
		if (b.ids[0] != layerId && b.ids[1] != layerId && b.ids[2] != layerId && b.ids[3] != layerId)
			continue;
		for (int i = 0; b.flags[i] != -1; i++)
			if (isFlagSet(b.flags[i]))
				return true;
		return false;
	}
	return true;
}

// 0x205c0: after a room's opening, mark the room seen
void DreamForgeEngine::roomEventDone() {
	if (!_scene || _scene->_act != 1)
		return;
	struct Seen {
		int8 area, room;
		int16 flag;
		bool counts;  // also count the room and raise flag 0x1b
	};
	static const Seen kSeen[] = {
		{ 0, 1, 399, true },
		{ 1, 1, 0x179, false }, { 1, 2, 0x17A, true }, { 1, 3, 0x17B, true }, { 1, 4, 0x17C, true },
		{ 1, 5, 0x17D, true }, { 1, 6, 0x17F, true }, { 1, 7, 0x181, false }, { 1, 9, 0x193, false },
		{ 1, 10, 0x182, true }, { 1, 11, 0x17E, false },
		{ 2, 1, 0x183, true }, { 2, 2, 0x185, true }, { 2, 5, 0x186, true }, { 2, 6, 0x187, true },
		{ 2, 7, 0x188, true }, { 2, 8, 0x189, true }, { 2, 9, 0x18A, false }, { 2, 10, 0x18B, true },
		{ 2, 11, 0x18C, true },
		{ 3, 1, 0xC0, true }, { 3, 2, 0x18D, true }, { 3, 3, 0x18E, true }
	};
	int area = _scene->_area, room = _scene->_room;
	if (area == 1 && room == 12) {
		setFlag(0x180, true);
	} else {
		for (auto &s : kSeen) {
			if (s.area != area || s.room != room || isFlagSet(s.flag))
				continue;
			setFlag(s.flag, true);
			if (s.counts) {
				_roomsSeen++;
				setFlag(0x1B, true);
			}
			break;
		}
	}
	// TODO: banter set 6 (0x1c720) over the first few rooms when flag 0x1b is up (0x20450)
	setFlag(0x1B, false);
}

} // End of namespace DreamForge
