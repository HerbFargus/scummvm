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

#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_enemies2.h"

namespace NightmareNed {

enum {
	kArea = GraveyardRoom::kArea
};

Scene1Enemies2::Scene1Enemies2(Scene1 *scene) : _s(scene) {
	const ExeImage &exe = _s->game()->exe();
	_reaperSlot = (int16)exe.readUint16(0x4b5278);
	_reaperArm = (int16)exe.readUint16(0x4b527c);
	_reaperArm2 = (int16)exe.readUint16(0x4b5280);
	_reaperSave = exe.readSint32(0x4b5284);
	_reaperRiseL = exe.readSint32(0x4b5288);
	_reaperRiseR = exe.readSint32(0x4b528c);
	_reaperLoops1 = _reaperLoops2 = 0;
	_reaperSide = _reaperBeaten = 0;
	for (int i = 0; i < 6; i++)
		_reaperPos[i] = 0;
	_reaperMirror = 0;
	_reaperLeave = 0;
	_reaperTimeL = _reaperTimeR = 0;
	_reaperRegion = 0;

	_treeSlot = (int16)exe.readUint16(0x4b1fd4);
	_treeX = 0;

	_zombieBase = 0;
	_zombieBodySlot = (int16)exe.readUint16(0x4b12fc);
	_zombieArmSlot = (int16)exe.readUint16(0x4b1300);
	_zombieHeadSlot = (int16)exe.readUint16(0x4b1304);
	_zombieSpare = (int16)exe.readUint16(0x4b1308);
	_zombieThrown = exe.readUint32(0x4b130c);
	_zombieNext = exe.readUint32(0x4b1310);

	_ghoulSlot = (int16)exe.readUint16(0x4b6cdc);
	_ghoulSound = 0;
	_ghoulRegion = 0;
	_ghoulEat = 0;

	for (int i = 0; i < 4; i++)
		_goblin[i] = (int16)exe.readUint16(0x4a5534 + 4 * i);

	_swampSlot = (int16)exe.readUint16(0x49e228);
	_swampUp = 0;

	_statueSlot = (int16)exe.readUint16(0x4b4484);
	_statueNext = 0;
	_statueRegion = 0;
	_statueAwake = 0;

	_rec10Slot = (int16)exe.readUint16(0x4b4480);
	_rec10Up = 0;
	_rec10NedX = _rec10NedY = 0;
}

Scene1Enemies2::~Scene1Enemies2() {
}

// ---- Shared steps ----

Slot &Scene1Enemies2::sl(int id) {
	return _s->slot(id);
}

int Scene1Enemies2::placeP(int id, int ref, bool noMirror, int *frame) {
	return _s->place(id, ref, noMirror, Scene1::xformPlain, _s, frame);
}

void Scene1Enemies2::setDeadline(int id, uint32 now, int duration) {
	if (id >= 0)
		sl(id).deadline = now + (int16)duration * kMsPerStep;
}

void Scene1Enemies2::setZ(int id, int z) {
	if (sl(id).sprite)
		sl(id).sprite->setDrawOrder((int16)z);
}

void Scene1Enemies2::exits(int32 id, bool on) {
	GraveyardRoom::regionEnable(_s->map(Scene1::kMapExits), id, on);
}

void Scene1Enemies2::regionFollow(int32 region, int id, int ref, int frame, bool noMirror) {
	int32 rect[4];
	int pos[2];
	_s->boxRect(kArea, id, frame, noMirror ? 0 : 3, rect);
	_s->attachPos(id, ref, pos, noMirror);
	rect[0] += pos[0];
	rect[2] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	GraveyardRoom::regionSetRect(&_s->userMap(), region, rect);
}

void Scene1Enemies2::rectAt(int id, int ref, int32 pos[2], bool noMirror) {
	int p[2];
	_s->attachPos(id, ref, p, noMirror);
	pos[0] = p[0];
	pos[1] = p[1];
}

// 0x44ee30 (action, &DAT_005245e8): the original pushes now, 0, 0 after them, unused
void Scene1Enemies2::playAction(int action) {
	_s->ned()->playAction(action, _s->_nedPos, 0, 0);
}

// 0x43c880 (part): this module's 0xffff resets
void Scene1Enemies2::reset() {
	_ghoulSlot = -1;
	_reaperSlot = _reaperArm = _reaperArm2 = -1;
	_statueSlot = -1;
	_zombieBodySlot = _zombieArmSlot = _zombieHeadSlot = _zombieSpare = -1;
	_zombieBase = -1;
	for (int i = 0; i < 4; i++)
		_goblin[i] = -1;
	_rec10Slot = -1;
	_swampSlot = -1;
}

// ---- The Reaper (record 0) ----
// A cloaked figure over slot 5. Ned walking into region 0x834 (from the left) or 0x898
// (from the right) raises it (0x103); 0x835 / 0x899 then swing an arm at him (0x106 or
// 0x108, Ned knocked back by actions 0x1c / 0x21). Zapping the idle arm (7 or 0xd) breaks
// it off; the right one's fall (0x11) marks it beaten, and it drifts away (0xb), which sets
// vision flag 7 and asks the core to test the vision triggers.

// 0x43b170
void Scene1Enemies2::reaperShow() {
	static const int16 kSlots[7] = { 0x107, 0xc, 0xd, 0x108, 0xf, 0x10, 0x11 };
	int z = (int16)(_s->ned()->getZ() - 1);
	for (int i = 0; i < 7; i++) {
		setZ(kSlots[i], z);
		_s->ensureFlip(kArea, kSlots[i]);
	}
	reaperReset();
}

// 0x47ad10
void Scene1Enemies2::reaperHide() {
	_reaperArm = -1;
	_reaperArm2 = -1;
	if (0 < _reaperSlot) {
		_s->switchSlot(-1, _reaperSlot);
		sl(5).x = _reaperPos[4];
		sl(5).y = _reaperPos[5];
	}
	_reaperSlot = -1;
}

// 0x47ad70 (its two 0x43cb30 conversions of slot 5 and Ned's x are unused)
void Scene1Enemies2::reaperReset() {
	_reaperLoops1 = 0;
	_reaperLoops2 = 0;
	_reaperRiseR = 1;
	_reaperRiseL = 1;
	if (_reaperSave) {
		_reaperPos[0] = sl(0x105).x;
		_reaperPos[1] = sl(0x105).y;
		_reaperPos[2] = sl(0x107).x;
		_reaperPos[3] = sl(0x107).y;
		_reaperPos[4] = sl(5).x;
		_reaperPos[5] = sl(5).y;
		_reaperSave = 0;
	}
	reaperFlip(_s->_tile < 0x32 ? 0 : 1);
	_reaperLeave = 0;
	if (_reaperRegion == 0) {
		// The original passes an uninitialised rect; reaperStep sets it
		static const int32 kNone[4] = { 0, 0, 0, 0 };
		_s->regionAdd(2, 0x102, kNone, &_reaperRegion);
	}
	_s->targetRemoveSlot(_reaperSlot);
	_reaperSlot = _s->switchSlot(0x102, _reaperSlot);
	placeP(_reaperSlot, 5, _reaperMirror != 0);
	_s->targetAddSlot(_reaperSlot);
	_reaperRiseL = 1;
	_reaperRiseR = 1;
	exits(0x834, true);
	exits(0x898, true);
}

// 0x47af60: on = facing left (the flip effects on), anchors 0x105 / 0x107 moved to match
void Scene1Enemies2::reaperFlip(int on) {
	static const int16 kSlots1[11] = { 5, 0x102, 0x103, 0x104, 0xb, 0x105, 6, 9, 10, 0x106, 7 };
	static const int16 kSlots2[7] = { 0x107, 0xc, 0xd, 0x108, 0xf, 0x10, 0x11 };
	_reaperMirror = on == 0;
	for (int i = 0; i < 11; i++)
		_s->flipActive(kArea, kSlots1[i], on != 0);
	sl(0x105).x = _reaperPos[0];
	sl(0x105).y = _reaperPos[1];
	sl(0x107).x = _reaperPos[2];
	sl(0x107).y = _reaperPos[3];
	sl(5).x = _reaperPos[4];
	sl(5).y = _reaperPos[5];
	for (int i = 0; i < 7; i++)
		_s->flipActive(kArea, kSlots2[i], on != 0);
	if (on) {
		sl(0x105).x = _reaperPos[0] + 0x7c;
		sl(0x107).x = _reaperPos[2] + 0x4a;
	} else {
		sl(0x105).x = _reaperPos[0];
		sl(0x107).x = _reaperPos[2];
	}
}

// 0x47b150
void Scene1Enemies2::reaperScroll(int dx, int dy) {
	if (0 < _reaperSlot)
		_s->adj(_reaperSlot, dx, dy);
	if (0 < _reaperArm)
		_s->adj(_reaperArm, dx, dy);
	if (0 < _reaperArm2)
		_s->adj(_reaperArm2, dx, dy);
}

// 0x47b1c0
int Scene1Enemies2::reaperRegion(int32 id, uint32 flags, int x, int y, int16 *state) {
	if (_reaperSlot <= 0)
		return 0;
	bool m;
	uint32 now = _s->game()->now();
	if (id == 0x834 || id == 0x898) {
		reaperFlip(id == 0x834 ? 0 : 1);
		m = _reaperMirror != 0;
		if (id == 0x834 ? _reaperRiseL != 0 : _reaperRiseR != 0) {
			_reaperSlot = _s->switchSlot(0x103, _reaperSlot);
			_s->playSound(0x103);
			_reaperRiseL = 0;
			_reaperRiseR = 0;
		}
		_s->targetAddSlot(_reaperSlot);
		setDeadline(_reaperSlot, now, placeP(_reaperSlot, 5, m));
		exits(id, false);
		exits(id + 1, true);
		return 1;
	}
	if (id == 0x835 || id == 0x899) {
		m = _reaperMirror != 0;
		if (_reaperSide == 0) {
			_s->targetRemoveSlot(_reaperArm);
			_reaperArm = _s->switchSlot(0x106, _reaperArm);
			_s->playSound(_reaperArm);
			setDeadline(_reaperArm, now, placeP(_reaperArm, 0x105, m));
		} else {
			_s->targetRemoveSlot(_reaperArm2);
			_reaperArm2 = _s->switchSlot(0x108, _reaperArm2);
			setDeadline(_reaperArm2, now, placeP(_reaperArm2, 0x107, m));
		}
		exits(id, false);
		return 1;
	}
	return 0;
}

// 0x47b690
void Scene1Enemies2::reaperStep(uint32 now) {
	bool m = _reaperMirror != 0;
	int frame = 0;

	// The body, against slot 5
	if (_reaperSlot != -1) {
		if (sl(_reaperSlot).deadline < now) {
			Slot &s = sl(_reaperSlot);
			s.step++;
			if (s.step >= s.sequenceLength) {
				s.step = 0;
				switch (_reaperSlot) {
				case 0xb:
					// Five loops drifting away, then gone; the other arm comes next time
					if (++_reaperLeave > 4) {
						_reaperSlot = _s->switchSlot(-1, _reaperSlot);
						sl(5).x = _reaperPos[4];
						sl(5).y = _reaperPos[5];
						_reaperRiseL = 1;
						_reaperSide = _reaperSide == 0;
						_reaperRiseR = 1;
					}
					break;
				case 0x103:
					_s->targetRemoveSlot(_reaperSlot);
					_reaperSlot = _s->switchSlot(0x102, _reaperSlot);
					if (_reaperSide == 0) {
						_reaperArm = _s->switchSlot(6, _reaperArm);
						_s->playSound(_reaperArm);
						placeP(_reaperArm, 0x105, m);
					} else {
						_reaperArm2 = _s->switchSlot(0xc, _reaperArm2);
						placeP(_reaperArm2, 0x107, m);
					}
					_s->targetAddSlot(_reaperSlot);
					break;
				case 0x104:
					_s->targetRemoveSlot(_reaperSlot);
					_reaperSlot = _s->switchSlot(0x102, _reaperSlot);
					_s->targetAddSlot(_reaperSlot);
					break;
				default:
					break;
				}
			}
			if (_reaperSlot == 0xb)
				sl(5).x -= 0x16;
			if (0 < _reaperSlot)
				setDeadline(_reaperSlot, now, placeP(_reaperSlot, 5, m, &frame));
		} else {
			placeP(_reaperSlot, 5, m, &frame);
		}
		if (_reaperRegion != 0 && 0 < _reaperSlot)
			regionFollow(_reaperRegion, _reaperSlot, 5, frame, m);
	}

	// The left arm, against 0x105
	if (_reaperArm != -1) {
		if (sl(_reaperArm).deadline < now) {
			Slot &s = sl(_reaperArm);
			s.step++;
			if (s.step >= s.sequenceLength) {
				s.step = 0;
				int16 old;
				switch (_reaperArm) {
				case 6:
					_s->targetRemoveSlot(_reaperArm);
					_reaperArm = _s->switchSlot(7, _reaperArm);
					_s->targetAddSlot(_reaperArm);
					break;
				case 7:
					if (++_reaperLoops1 > 3) {
						_s->playSound(_reaperArm);
						_reaperLoops1 = 0;
					}
					break;
				case 9:
					_s->targetRemoveSlot(_reaperArm);
					old = _reaperArm;
					_reaperArm = _s->switchSlot(10, _reaperArm);
					_s->playSound(_reaperArm, old);
					_s->targetAddSlot(_reaperArm);
					_s->targetRemoveSlot(_reaperSlot);
					_reaperSlot = _s->switchSlot(-1, _reaperSlot);
					break;
				case 10:
					_s->targetRemoveSlot(_reaperArm);
					_reaperArm = _s->switchSlot(-1, _reaperArm);
					// The original adds slot -1 (a stray record) to the targets here
					_s->targetRemoveSlot(_reaperSlot);
					_reaperSlot = _s->switchSlot(0xb, _reaperSlot);
					_s->playSound(_reaperSlot);
					_s->regionRemove(_reaperRegion);
					_reaperRegion = 0;
					setDeadline(_reaperSlot, now, placeP(_reaperSlot, 5, m));
					exits(0x898, false);
					exits(0x834, false);
					exits(0x899, false);
					exits(0x835, false);
					if (_reaperBeaten) {
						_s->_done[7] = 1;
						_s->_visionCheck = 1;
					}
					break;
				case 0x106:
					_s->targetRemoveSlot(_reaperArm);
					_reaperArm = _s->switchSlot(7, _reaperArm);
					_s->targetAddSlot(_reaperArm);
					_s->targetRemoveSlot(_reaperSlot);
					_reaperSlot = _s->switchSlot(0x104, _reaperSlot);
					_s->playSound(0x104);
					_s->targetAddSlot(_reaperSlot);
					setDeadline(_reaperSlot, now, placeP(_reaperSlot, 5, m));
					break;
				default:
					break;
				}
			}
			if (_reaperArm == 0x106 && sl(0x106).step == 4)
				playAction(0x1c);
			if (0 < _reaperArm)
				setDeadline(_reaperArm, now, placeP(_reaperArm, 0x105, m));
		} else {
			placeP(_reaperArm, 0x105, m);
		}
	}

	// The right arm, against 0x107
	if (_reaperArm2 == -1)
		return;
	if (now <= sl(_reaperArm2).deadline) {
		placeP(_reaperArm2, 0x107, m);
		return;
	}
	Slot &s = sl(_reaperArm2);
	s.step++;
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		int16 old;
		switch (_reaperArm2) {
		case 0xc:
			_s->targetRemoveSlot(_reaperArm2);
			_reaperArm2 = _s->switchSlot(0xd, _reaperArm2);
			_s->playSound(_reaperArm2);
			_s->targetAddSlot(_reaperArm2);
			break;
		case 0xd:
			if (++_reaperLoops2 > 5) {
				_s->playSound(_reaperArm2);
				_reaperLoops2 = 0;
			}
			break;
		case 0xf:
			_s->targetRemoveSlot(_reaperArm2);
			_reaperArm2 = _s->switchSlot(0x10, _reaperArm2);
			_s->targetRemoveSlot(_reaperSlot);
			_reaperSlot = _s->switchSlot(-1, _reaperSlot);
			break;
		case 0x10:
			_reaperArm2 = _s->switchSlot(0x11, _reaperArm2);
			_s->playSound(_reaperArm2);
			_reaperSlot = _s->switchSlot(0x102, _reaperSlot);
			placeP(_reaperSlot, 5, m);
			break;
		case 0x11:
			_reaperArm2 = _s->switchSlot(-1, _reaperArm2);
			old = _reaperArm;
			_reaperArm = _s->switchSlot(10, _reaperArm);
			placeP(_reaperArm, 0x105, m);
			_s->playSound(_reaperArm, old);
			_s->targetAddSlot(_reaperArm);
			_s->targetRemoveSlot(_reaperSlot);
			_reaperSlot = _s->switchSlot(-1, _reaperSlot);
			_reaperBeaten = 1;
			break;
		case 0x108:
			_s->targetRemoveSlot(_reaperArm2);
			_reaperArm2 = _s->switchSlot(0xd, _reaperArm2);
			_s->targetAddSlot(_reaperArm2);
			_s->targetRemoveSlot(_reaperSlot);
			_reaperSlot = _s->switchSlot(0x104, _reaperSlot);
			_s->playSound(0x104);
			_s->targetAddSlot(_reaperSlot);
			setDeadline(_reaperSlot, now, placeP(_reaperSlot, 5, m));
			break;
		default:
			break;
		}
	}
	if (_reaperArm2 == 0x108 && sl(0x108).step == 4)
		playAction(0x21);
	if (_reaperArm2 < 1)
		return;
	setDeadline(_reaperArm2, now, placeP(_reaperArm2, 0x107, m));
}

// 0x47c590: every 6 s the idle Reaper's swing regions come back on
void Scene1Enemies2::reaperTick(uint32 now) {
	if (_reaperTimeL < now) {
		if (_reaperSlot == 0x102)
			exits(0x835, true);
		_reaperTimeL = now + 6000;
	}
	if (_reaperTimeR < now) {
		if (_reaperSlot == 0x102)
			exits(0x899, true);
		_reaperTimeR = now + 6000;
	}
	reaperStep(now);
}

// 0x47c3e0: zapping an idle arm breaks it off
int Scene1Enemies2::reaperHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	uint32 now = _s->game()->now();
	bool m = _reaperMirror != 0;
	if (*b == 7) {
		_s->targetRemoveSlot(_reaperArm);
		_reaperArm = _s->switchSlot(9, _reaperArm);
		setDeadline(_reaperArm, now, placeP(_reaperArm, 0x105, m));
	} else if (*b == 0xd) {
		_s->targetRemoveSlot(_reaperArm2);
		_reaperArm2 = _s->switchSlot(0xf, _reaperArm2);
		setDeadline(_reaperArm2, now, placeP(_reaperArm2, 0x107, m));
	} else {
		return 0;
	}
	exits(0x835, false);
	exits(0x899, false);
	return 1;
}

// 0x47c540
int Scene1Enemies2::reaperRect(int16 found, int32 pos[2]) {
	int ref;
	if (found == _reaperArm)
		ref = 0x105;
	else if (found == _reaperArm2)
		ref = 0x107;
	else
		return 0;
	rectAt(found, ref, pos, _reaperMirror != 0);
	return 1;
}

// ---- The Tree (record 1) ----
// Region 5000 makes the tree grab Ned (0xa7, input locked) and swing at him (0xa8; at
// step 5 Ned is knocked away, action 0x17). Zapped in the first two steps of 0xa8 it goes
// up in a puff. Past x 3000 the region comes back.

// 0x43b0f0
void Scene1Enemies2::treeShow() {
	int z = _s->ned()->getZ() - 1;
	setZ(0xa6, z);
	_treeX = (int16)sl(0xa6).x;
	setZ(0xa7, z);
	setZ(0xa9, z);
	setZ(0xa8, z);
}

// 0x467700
void Scene1Enemies2::treeHide() {
	_treeSlot = -1;
}

// 0x467710
void Scene1Enemies2::treeStep(uint32 now) {
	bool m = sl(0xa6).flip == nullptr;
	if (_treeSlot < 1)
		return;
	if (_treeSlot == 0xe) {
		_s->puffTick(now, Scene1::xformPlain, _s);
		return;
	}
	if (now <= sl(_treeSlot).deadline) {
		placeP(_treeSlot, 0xa6, m);
		return;
	}
	Slot &s = sl(_treeSlot);
	s.step++;
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		if (_treeSlot == 0xa7) {
			_treeSlot = _s->switchSlot(0xa8, 0xa7);
			_s->targetAddSlot(_treeSlot);
		} else if (_treeSlot == 0xa8) {
			_s->targetRemoveSlot(0xa8);
			_treeSlot = _s->switchSlot(0xa9, _treeSlot);
			_s->playSound(_treeSlot);
		} else if (_treeSlot == 0xa9) {
			_treeSlot = _s->switchSlot(-1, 0xa9);
			_s->_inputFree = 1;
		}
	}
	if (_treeSlot == 0xa8 && sl(0xa8).step == 4)
		_s->playSound(0xa8);
	if (0 < _treeSlot) {
		setDeadline(_treeSlot, now, placeP(_treeSlot, 0xa6, m));
		if (_treeSlot == 0xa8 && sl(0xa8).step == 5)
			playAction(0x17);
	}
}

// 0x467930
void Scene1Enemies2::treeTick(uint32 now, int16 x) {
	if (3000 < x)
		exits(5000, true);
	treeStep(now);
}

// 0x467960 (it never claims the region)
int Scene1Enemies2::treeRegion(int32 id, uint32 flags, int x, int y, int16 *state) {
	bool m = sl(0xa6).flip == nullptr;
	if (id == 5000 && _treeSlot < 0) {
		sl(0xa7).step = 0;
		sl(0xa8).step = 0;
		sl(0xa9).step = 0;
		_treeSlot = _s->switchSlot(0xa7, -1);
		_s->playSound(_treeSlot);
		placeP(_treeSlot, 0xa6, m);
		_s->_inputFree = 0;
		exits(5000, false);
		_s->targetAddSlot(_treeSlot);
	}
	return 0;
}

// 0x467a50
int Scene1Enemies2::treeHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	if (*b != 0xa8 || 2 <= sl(0xa8).step || _treeSlot != 0xa8)
		return 0;
	_s->targetRemoveSlot(0xa8);
	_treeSlot = _s->switchSlot(-1, _treeSlot);
	_s->puffStart(kArea, 0xa6, _s->ned()->getZ() - 1, _s->game()->now(), Scene1::xformPlain, _s);
	_treeSlot = 0xe;
	_s->_inputFree = 1;
	return 1;
}

// 0x467ae0
int Scene1Enemies2::treeRect(int16 found, int32 pos[2]) {
	if (found != _treeSlot)
		return 0;
	rectAt(found, 0xa6, pos, true);
	return 1;
}

// ---- The Zombie (record 3) ----
// Three parts over slot 8: body 0x1a0, arm 0x1a1, head 0x1a2, idling with random moves
// between tiles 0x172 and 0x1e0. Region 0x13ec makes it grab Ned (0xfe, 0x1a6, input
// locked) and throw him (action 0x1a); region 0x13eb starts a random move.

// 0x4646d0
void Scene1Enemies2::zombieShow() {
	static const int16 kSlots[15] = { 0x1a6, 0x1a0, 0x1a0, 0x1a1, 0x1a2, 0x1a3, 0x1a4, 0x1a5,
		0x1a7, 0x1a8, 0x1b6, 0x34, 0x35, 0xfe, 0x114 };
	bool m = sl(0x1a0).flip == nullptr;
	int z = _s->ned()->getZ();
	setZ(8, z + 2);
	for (int i = 0; i < 15; i++)
		setZ(kSlots[i], z + 1);
	_zombieBase = _s->switchSlot(8, -1);
	_zombieBodySlot = _s->switchSlot(0x1a0, -1);
	_zombieArmSlot = _s->switchSlot(0x1a1, -1);
	_zombieHeadSlot = _s->switchSlot(0x1a2, -1);
	placeP(_zombieBase, 8, m);
	placeP(_zombieBodySlot, 0x1a0, m);
	placeP(_zombieArmSlot, 0x1a1, m);
	placeP(_zombieHeadSlot, 0x1a2, m);
}

// 0x464980
void Scene1Enemies2::zombieHide() {
	_zombieBase = -1;
	_zombieBodySlot = -1;
	_zombieArmSlot = -1;
	_zombieHeadSlot = -1;
	_zombieThrown = 0xffffffff;
	_zombieNext = 0xffffffff;
}

// 0x4649b0
void Scene1Enemies2::zombieBody(uint32 now) {
	bool m = sl(0x1a0).flip == nullptr;
	if (_zombieBodySlot <= 0)
		return;
	if (sl(_zombieBodySlot).deadline < now) {
		Slot &s = sl(_zombieBodySlot);
		s.step++;
		if (s.step >= s.sequenceLength) {
			s.step = 0;
			if (_zombieBodySlot == 0x1a7) {
				_zombieBodySlot = _s->switchSlot(0x1a0, 0x1a7);
				_zombieNext = _s->game()->now() + ((int)_s->game()->crtRandom() % 5 + 1) * 1000;
			} else if (_zombieBodySlot == 0x1b6) {
				_zombieBodySlot = _s->switchSlot(0x1a0, 0x1b6);
			}
		}
		if (0 < _zombieBodySlot)
			setDeadline(_zombieBodySlot, now, placeP(_zombieBodySlot, 0x1a0, m));
	} else {
		placeP(_zombieBodySlot, 0x1a0, m);
		placeP(8, 8, m);
	}
}

// 0x464b40
void Scene1Enemies2::zombieArm(uint32 now) {
	bool m = sl(0x1a1).flip == nullptr;
	if (_zombieArmSlot < 1)
		return;
	if (now <= sl(_zombieArmSlot).deadline) {
		placeP(_zombieArmSlot, 0x1a1, m);
		placeP(8, 8, m);
		return;
	}
	Slot &s = sl(_zombieArmSlot);
	s.step++;
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		switch (_zombieArmSlot) {
		case 0xfe:
			_zombieArmSlot = _s->switchSlot(0x1a6, 0xfe);
			if (0x12d <= _s->_nedPos.y)
				_s->playSound(_zombieArmSlot);
			break;
		case 0x34:
			_zombieArmSlot = _s->switchSlot(0x1a6, _zombieArmSlot);
			break;
		case 0x35:
			_zombieArmSlot = _s->switchSlot(0x1a1, _zombieArmSlot);
			break;
		case 0x1a6:
			// The throw
			_zombieBodySlot = _s->switchSlot(0x1a7, _zombieBodySlot);
			placeP(_zombieBodySlot, 0x1a0, m);
			if (300 < _s->_nedPos.y)
				_s->playSound(_zombieBodySlot);
			_zombieArmSlot = _s->switchSlot(0x1a1, _zombieArmSlot);
			playAction(0x1a);
			_s->_inputFree = 1;
			_zombieThrown = now + 0x9c4;
			break;
		default:
			break;
		}
	}
	if (_zombieArmSlot < 1)
		return;
	setDeadline(_zombieArmSlot, now, placeP(_zombieArmSlot, 0x1a1, m));
}

// 0x464dc0
void Scene1Enemies2::zombieHead(uint32 now) {
	bool m = sl(0x1a2).flip == nullptr;
	if (_zombieHeadSlot <= 0)
		return;
	if (sl(_zombieHeadSlot).deadline < now) {
		Slot &s = sl(_zombieHeadSlot);
		s.step++;
		if (s.step >= s.sequenceLength) {
			s.step = 0;
			if (_zombieHeadSlot == 0x114 || _zombieHeadSlot == 0x1a8) {
				_zombieHeadSlot = _s->switchSlot(0x1a2, _zombieHeadSlot);
				_zombieNext = _s->game()->now() + ((int)_s->game()->crtRandom() % 5 + 1) * 1000;
			}
		}
		if (0 < _zombieHeadSlot)
			setDeadline(_zombieHeadSlot, now, placeP(_zombieHeadSlot, 0x1a2, m));
	} else {
		placeP(_zombieHeadSlot, 0x1a2, m);
		placeP(8, 8, m);
	}
}

// 0x464fb0
void Scene1Enemies2::zombieTick(uint32 now) {
	static const int16 kMoves[3] = { 0x1a7, 0x1a8, 0x114 };
	bool m = sl(0x1a1).flip == nullptr;
	if (_s->_tile <= 0x171 || 0x1e1 <= _s->_tile)
		return;
	if (0 < _zombieBodySlot) {
		zombieBody(now);
		zombieArm(now);
		zombieHead(now);
	}
	if (_zombieThrown < now) {
		// After a throw: a wave (0x35) or a laugh (0x1b6)
		_zombieThrown = 0xffffffff;
		if ((int)_s->game()->crtRandom() % 2 == 0) {
			_zombieArmSlot = _s->switchSlot(0x35, _zombieArmSlot);
			if (300 < _s->_nedPos.y)
				_s->playSound(_zombieArmSlot);
			placeP(_zombieArmSlot, 0x1a1, m);
		} else {
			_zombieBodySlot = _s->switchSlot(0x1b6, _zombieBodySlot);
			if (300 < _s->_nedPos.y)
				_s->playSound(_zombieBodySlot);
			placeP(_zombieBodySlot, 0x1a0, m);
		}
	}
	if (_zombieNext < now) {
		int16 i = (int16)((int)_s->game()->crtRandom() % 3);
		if (i < 1) {
			_zombieBodySlot = _s->switchSlot(kMoves[i], _zombieBodySlot);
			if (300 < _s->_nedPos.y)
				_s->playSound(_zombieBodySlot);
			placeP(_zombieBodySlot, 0x1a0, m);
		} else {
			_zombieHeadSlot = _s->switchSlot(kMoves[i], _zombieHeadSlot);
			if (300 < _s->_nedPos.y)
				_s->playSound(_zombieHeadSlot);
			placeP(_zombieHeadSlot, 0x1a2, m);
		}
		_zombieNext = 0xffffffff;
	}
}

// 0x464f40
void Scene1Enemies2::zombieScroll(int dx, int dy) {
	if (0 < _zombieBodySlot) {
		_s->adj(_zombieBodySlot, dx, dy);
		_s->adj(_zombieArmSlot, dx, dy);
		_s->adj(_zombieHeadSlot, dx, dy);
		_s->adj(8, dx, dy);
	}
}

// 0x465250 (both regions first spin until record 3 has loaded: here it has, or nothing is shown)
int Scene1Enemies2::zombieRegion(int32 id, uint32 flags, int x, int y, int16 *state) {
	static const int16 kMoves[3] = { 0x1a7, 0x1a8, 0x114 };
	bool m = sl(0x1a1).flip == nullptr;
	if (id == 0x13ec) {
		if (!_s->enemyLoaded(3))
			return 0;
		_s->stopAllSounds();
		_zombieNext = 0xffffffff;
		_zombieArmSlot = _s->switchSlot(0xfe, _zombieArmSlot);
		if (300 < _s->_nedPos.y)
			_s->playSound(_zombieArmSlot);
		placeP(_zombieArmSlot, 0x1a1, m);
		_s->_inputFree = 0;
		exits(0x13ec, false);
		return 1;
	}
	if (id != 0x13eb)
		return 0;
	if (!_s->enemyLoaded(3))
		return 0;
	int16 i = (int16)((int)_s->game()->crtRandom() % 3);
	if (i < 1) {
		_zombieBodySlot = _s->switchSlot(kMoves[i], _zombieBodySlot);
		if (300 < _s->_nedPos.y)
			_s->playSound(_zombieBodySlot);
		placeP(_zombieBodySlot, 0x1a0, m);
	} else {
		_zombieHeadSlot = _s->switchSlot(kMoves[i], _zombieHeadSlot);
		if (300 < _s->_nedPos.y)
			_s->playSound(_zombieHeadSlot);
		placeP(_zombieHeadSlot, 0x1a2, m);
	}
	exits(0x13eb, false);
	uint32 now = _s->game()->now();
	_zombieNext = now + ((int)_s->game()->crtRandom() % 8 + 5) * 1000;
	return 1;
}

// ---- The Ghoul (record 4) ----
// A mouth in the ground (0x4e) that groans every 3-8 s. If Ned steps on it (region 0x4e)
// while the hands hold him, it swallows him (0x121) and the core leaves for sub-scene 7.

// 0x484b10
void Scene1Enemies2::ghoulShow() {
	setZ(0x4e, _s->ned()->getZ() + 1);
	setZ(0x121, _s->ned()->getZ() - 1);
	_ghoulSlot = _s->switchSlot(0x4e, -1);
	_ghoulSound = _s->game()->now() + 3000;
	placeP(_ghoulSlot, 0x4e, true);
	_s->targetAddSlot(_ghoulSlot);
	// The original passes an uninitialised rect; ghoulStep sets it
	static const int32 kNone[4] = { 0, 0, 0, 0 };
	_s->regionAdd(2, 0x4e, kNone, &_ghoulRegion);
	_ghoulEat = 0;
}

// 0x484bd0 (the region stays)
void Scene1Enemies2::ghoulHide() {
	_s->targetRemoveSlot(_ghoulSlot);
	_ghoulSlot = -1;
	_ghoulSound = 0;
}

// 0x484c00
void Scene1Enemies2::ghoulStep(uint32 now) {
	if (_ghoulSlot == -1)
		return;
	bool m = sl(_ghoulSlot).flip == nullptr;
	if (_ghoulSound < now) {
		_s->playSound(0x4e);
		_ghoulSound = _s->game()->now() + ((int)_s->game()->crtRandom() % 6 + 3) * 1000;
	}
	if (_ghoulEat != 0) {
		_ghoulEat = 0;
		_s->ned()->show(false);
		_s->ned()->enable(false);
		_ghoulSlot = _s->switchSlotPre(0x121, _ghoulSlot);
		_s->playSound(0x121);
	}
	int frame = 0;
	if (now <= sl(_ghoulSlot).deadline) {
		placeP(_ghoulSlot, 0x4e, m, &frame);
	} else {
		Slot &s = sl(_ghoulSlot);
		s.step++;
		if (s.step >= s.sequenceLength) {
			_s->targetRemoveSlot(_ghoulSlot);
			if (_ghoulSlot == 0x4e) {
				sl(0x4e).step = 0;
				_s->targetAddSlot(_ghoulSlot);
			} else if (_ghoulSlot == 0x121) {
				_s->_toSub7 = 1;
				_ghoulSlot = _s->switchSlot(0x4e, 0x121);
				_s->targetAddSlot(_ghoulSlot);
				exits(0xce4, false);
				exits(0xce5, false);
			}
		}
		setDeadline(_ghoulSlot, now, placeP(_ghoulSlot, 0x4e, m, &frame));
	}
	if (_ghoulRegion != 0)
		regionFollow(_ghoulRegion, _ghoulSlot, 0x4e, frame, m);
}

// 0x484ee0
void Scene1Enemies2::ghoulTick(uint32 now) {
	ghoulStep(now);
}

// 0x484f00
void Scene1Enemies2::ghoulScroll(int dx, int dy) {
	_s->adj(_ghoulSlot, dx, dy);
}

// 0x484fa0
int Scene1Enemies2::ghoulRegion(int32 id, uint32 flags, int x, int y, int16 *state) {
	if (id != 0x4e)
		return 0;
	if (_s->_grabbed != 0) {
		*state = 4;
		_ghoulEat = 1;
	}
	return 1;
}

// ---- The Goblin (record 6) ----
// Four looping animations (0x82, 0x191, 0x65, 0x1e5) and a looping sound; no interaction.

// 0x44e660
void Scene1Enemies2::goblinShow() {
	int z = _s->ned()->getZ();
	setZ(0x82, z + 2);
	setZ(0x191, z + 1);
	setZ(0x65, z - 1);
	setZ(0x1e5, z - 1);
	_goblin[0] = _s->switchSlot(0x82, -1);
	_goblin[2] = _s->switchSlot(0x65, -1);
	_goblin[3] = _s->switchSlot(0x1e5, -1);
	_goblin[1] = _s->switchSlot(0x191, -1);
	goblinStep(0, _goblin[0], 0x82);
	goblinStep(0, _goblin[1], 0x191);
	goblinStep(0, _goblin[2], 0x65);
	goblinStep(0, _goblin[3], 0x1e5);
	goblinLoop();
	// The list's bitmap 24 loaded its palette into DAT_00524d80 (shared with sub-scene 2)
	const byte *pal = _s->palette(0x524d80);
	if (pal)
		memcpy(_s->_palette524d80, pal, sizeof(_s->_palette524d80));
	_s->game()->display().applyPalette(_s->_palette524d80, kPalStatic);
}

// 0x4558e0 on slot 0x82: its sound, looping
void Scene1Enemies2::goblinLoop() {
	Slot &s = sl(0x82);
	Game *g = _s->game();
	g->stopSlotSound(s);
	if (s.sound.size() < 18)
		return;
	uint32 pcmOffset = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcmOffset >= s.sound.size())
		return;
	g->playVoice(s.voice, s.sound.data(), s.sound.data() + pcmOffset, s.sound.size() - pcmOffset, 0, 0, true);
}

// 0x44e7a0
void Scene1Enemies2::goblinHide() {
	_s->stopSound(0x82);
	for (int i = 0; i < 4; i++)
		_goblin[i] = -1;
}

// 0x44e7d0
void Scene1Enemies2::goblinScroll(int dx, int dy) {
	_s->adj(_goblin[0], dx, dy);
	_s->adj(_goblin[2], dx, dy);
	_s->adj(_goblin[3], dx, dy);
	_s->adj(_goblin[1], dx, dy);
}

// 0x44e840
void Scene1Enemies2::goblinStep(uint32 now, int16 id, int ref) {
	if (id <= 0)
		return;
	Slot &s = sl(id);
	if (s.deadline < now) {
		s.step++;
		if (s.step >= s.sequenceLength)
			s.step = 0;
		setDeadline(id, now, placeP(id, ref, true));
		return;
	}
	placeP(id, ref, true);
}

// 0x44e910
void Scene1Enemies2::goblinTick(uint32 now) {
	goblinStep(now, _goblin[0], 0x82);
	goblinStep(now, _goblin[1], 0x191);
	goblinStep(now, _goblin[2], 0x65);
	goblinStep(now, _goblin[3], 0x1e5);
}

// ---- The Swamp Creature (record 8) ----
// A head in the swamp (slot 400). Each zap toggles it: up (played forward, holding the
// last frame) enables region 0x924 on the exit map, down plays it back and disables it.

// 0x428610
void Scene1Enemies2::swampShow() {
	setZ(400, _s->ned()->getZ() + 1);
	_swampSlot = _s->switchSlot(400, -1);
	_swampUp = 0;
	swampTick(0);
	_s->targetAddSlot(_swampSlot);
}

// 0x428670
void Scene1Enemies2::swampHide() {
	_s->targetRemoveSlot(_swampSlot);
	_swampSlot = -1;
}

// 0x428690
void Scene1Enemies2::swampScroll(int dx, int dy) {
	_s->adj(_swampSlot, dx, dy);
}

// 0x4286c0
void Scene1Enemies2::swampTick(uint32 now) {
	if (_swampSlot <= 0)
		return;
	Slot &s = sl(_swampSlot);
	if (now <= s.deadline) {
		placeP(_swampSlot, 400, true);
		return;
	}
	if (_swampUp == 0) {
		s.step--;
		if (s.step == s.sequenceLength - 3)
			_s->playSound(_swampSlot);
		if (s.step < 0)
			s.step = 0;
	} else {
		s.step++;
		if (s.step == 1)
			_s->playSound(_swampSlot);
		if (s.sequenceLength <= s.step)
			s.step = s.step - 1;
	}
	setDeadline(_swampSlot, now, placeP(_swampSlot, 400, true));
}

// 0x428840
int Scene1Enemies2::swampHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	if (*b != 400)
		return 0;
	_swampUp = _swampUp == 0;
	exits(0x924, _swampUp != 0);
	return 1;
}

// 0x4288a0
int Scene1Enemies2::swampRect(int16 found, int32 pos[2]) {
	if (found != 400)
		return 0;
	rectAt(400, 400, pos, true);
	return 1;
}

// ---- The Statue (record 9) ----
// A gargoyle over slot 1, asleep until Ned walks into region 2000 (facing right) or
// 0x7d1 (facing left): it wakes (2), then alternates poses (3, 4) and every 2-3 s breathes
// at Ned (0xff; at its step 6 Ned is knocked back, action 0x15) while he is between it and
// the region's far edge. Zapped in any pose it crumbles (0x100) and is gone.

// 0x43b360
void Scene1Enemies2::statueShow() {
	static const int16 kSlots[6] = { 1, 2, 3, 4, 0xff, 0x100 };
	int z = (int16)(_s->ned()->getZ() - 1);
	for (int i = 0; i < 6; i++) {
		Slot &s = sl(kSlots[i]);
		if (sl(1).x < _s->_nedPos.x) {
			_s->setFlip(kArea, kSlots[i], true);
		} else {
			// The record forgets its effect (dmix keeps it on the sprite)
			s.flip = nullptr;
		}
		setZ(kSlots[i], z);
	}
	statueInit();
}

// 0x477f10
void Scene1Enemies2::statueInit() {
	_statueSlot = -1;
	_statueNext = 0x7fffffff;
	_statueAwake = 0;
}

// 0x477f30
void Scene1Enemies2::statueHide() {
	_s->stopAllSounds();
	_statueSlot = -1;
}

// The breath test of 0x477f40: Ned in front of the statue, facing it, inside the region
bool Scene1Enemies2::statueSees(bool noMirror) {
	int x = _s->_nedPos.x;
	if (_s->ned()->getFacing() == 0) {
		if (noMirror)
			return false;
		// 0x487940's result is ignored; a missing region leaves the rect as it was
		CollisionMap::Region *r = GraveyardRoom::regionFind(_s->map(Scene1::kMapExits), 0x7d1);
		int32 right = r ? r->rect.right : 0;
		return 0x2986 < x && x < right;
	}
	if (!noMirror)
		return false;
	CollisionMap::Region *r = GraveyardRoom::regionFind(_s->map(Scene1::kMapExits), 2000);
	int32 left = r ? r->rect.left : 0;
	return x < 0x2986 && left < x;
}

// 0x477f40
void Scene1Enemies2::statueStep(uint32 now) {
	bool m = sl(1).flip == nullptr;
	if (_statueSlot == -1)
		return;
	int frame = 0;
	if (now <= sl(_statueSlot).deadline) {
		placeP(_statueSlot, 1, m, &frame);
	} else {
		Slot &s = sl(_statueSlot);
		s.step++;
		if (_statueSlot == 0xff && sl(0xff).step == 6) {
			if (statueSees(m))
				playAction(0x15);
			_statueNext = now + ((int)_s->game()->crtRandom() % 2 + 2) * 1000;
		}
		if (sl(_statueSlot).step >= sl(_statueSlot).sequenceLength) {
			_s->targetRemoveSlot(_statueSlot);
			bool add = true;
			int16 next;
			switch (_statueSlot) {
			case 2:
				_statueSlot = _s->switchSlot(0xff, _statueSlot);
				_s->playSound(_statueSlot);
				break;
			case 3:
				next = 4;
				if (_statueNext < now && statueSees(m))
					next = 0xff;
				if (next == 4)
					_s->playSound(4);
				_statueSlot = _s->switchSlot(next, _statueSlot);
				break;
			case 4:
				next = 3;
				if (_statueNext < now && statueSees(m))
					next = 0xff;
				_statueSlot = _s->switchSlot(next, _statueSlot);
				if (next == 0xff)
					_s->playSound(_statueSlot);
				break;
			case 0xff:
				_statueSlot = _s->switchSlot(3, _statueSlot);
				break;
			case 0x100:
				_s->visible(_statueSlot, false);
				_statueSlot = -1;
				// 0x4876d0 alone: the original does not refresh the world (0x487400) here
				_s->regionRemove(_statueRegion);
				_statueRegion = 0;
				return;
			default:
				add = false;
				break;
			}
			if (add)
				_s->targetAddSlot(_statueSlot);
		}
		setDeadline(_statueSlot, now, placeP(_statueSlot, 1, m, &frame));
	}
	if (_statueRegion != 0)
		regionFollow(_statueRegion, _statueSlot, 1, frame, m);
}

// 0x478590 (true for every x)
void Scene1Enemies2::statueTick(uint32 now, int16 x) {
	if (0x2774 < x || x < 0x2a94)
		statueStep(now);
}

// 0x4785c0
int Scene1Enemies2::statueHit(int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	bool m = sl(1).flip == nullptr;
	switch (*b) {
	case 2:
	case 3:
	case 4:
	case 0xff:
		_s->targetRemoveSlot(_statueSlot);
		_statueSlot = _s->switchSlot(0x100, _statueSlot);
		_s->playSound(_statueSlot);
		setDeadline(_statueSlot, _s->game()->now(), placeP(_statueSlot, 1, m));
		return 1;
	case 0x100:
		return 1;
	default:
		return 0;
	}
}

// 0x4787d0
void Scene1Enemies2::statueScroll(int dx, int dy) {
	_s->adj(_statueSlot, dx, dy);
}

// 0x4787f0
int Scene1Enemies2::statueRect(int16 found, int32 pos[2]) {
	if (found != _statueSlot)
		return 0;
	rectAt(found, 1, pos, sl(1).flip == nullptr);
	return 1;
}

// 0x478830
int Scene1Enemies2::statueRegion(int32 id, uint32 flags, int x, int y, int16 *state) {
	bool wake = false;
	if (_statueAwake == 0) {
		if (_s->ned()->getFacing() == 0)
			wake = id == 0x7d1;
		else
			wake = id == 2000;
	}
	if (wake) {
		bool m = sl(1).flip == nullptr;
		// The original passes an uninitialised rect; statueStep sets it
		static const int32 kNone[4] = { 0, 0, 0, 0 };
		_s->regionAdd(2, 2, kNone, &_statueRegion);
		_s->targetRemoveSlot(_statueSlot);
		_statueSlot = _s->switchSlot(2, _statueSlot);
		_s->playSound(_statueSlot);
		_s->targetAddSlot(_statueSlot);
		setDeadline(_statueSlot, _s->game()->now(), placeP(_statueSlot, 1, m));
		_statueAwake = 1;
	}
	return id == 2000 || id == 0x7d1 ? 1 : (int)wake;
}

// ---- Record 10 (unnamed) ----
// Slot 0xa5, a zap toggle like the Swamp Creature: up enables region 0x929 and
// remembers where Ned was (DAT_005228c0/c4, read nowhere else).

// 0x477c40
void Scene1Enemies2::record10Show() {
	setZ(0xa5, _s->ned()->getZ() + 1);
	_rec10Slot = _s->switchSlot(0xa5, -1);
	_rec10Up = 0;
	record10Tick(0);
	_s->targetAddSlot(_rec10Slot);
}

// 0x477ca0
void Scene1Enemies2::record10Hide() {
	_s->targetRemoveSlot(_rec10Slot);
	_rec10Slot = -1;
}

// 0x477cc0
void Scene1Enemies2::record10Scroll(int dx, int dy) {
	_s->adj(_rec10Slot, dx, dy);
}

// 0x477cf0
void Scene1Enemies2::record10Tick(uint32 now) {
	if (_rec10Slot <= 0)
		return;
	Slot &s = sl(_rec10Slot);
	if (now <= s.deadline) {
		placeP(_rec10Slot, 0xa5, true);
		return;
	}
	if (_rec10Up == 0) {
		s.step--;
		if (s.step == s.sequenceLength - 3)
			_s->playSound(_rec10Slot);
		if (s.step < 0)
			s.step = 0;
	} else {
		s.step++;
		if (s.step == 1)
			_s->playSound(_rec10Slot);
		if (s.sequenceLength <= s.step)
			s.step--;
	}
	setDeadline(_rec10Slot, now, placeP(_rec10Slot, 0xa5, true));
}

// 0x477e60
int Scene1Enemies2::record10Hit(int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	if (*b != 0xa5)
		return 0;
	_rec10Up = _rec10Up == 0;
	if (_rec10Up != 0) {
		exits(0x929, true);
		_rec10NedY = _s->_nedPos.y;
		_rec10NedX = _s->_nedPos.x;
		return 1;
	}
	exits(0x929, false);
	return 1;
}

// 0x477ee0
int Scene1Enemies2::record10Rect(int16 found, int32 pos[2]) {
	if (found != 0xa5)
		return 0;
	rectAt(0xa5, 0xa5, pos, true);
	return 1;
}

// 0x484f70
void Scene1Enemies2::ghoulVisible(bool on) {
	if (_ghoulSlot != -1)
		_s->visible(_ghoulSlot, on);
}

} // End of namespace NightmareNed
