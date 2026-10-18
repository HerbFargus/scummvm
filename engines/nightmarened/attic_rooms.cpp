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
#include "common/textconsole.h"

#include <math.h>

#include "nightmarened/attic.h"
#include "nightmarened/detection.h"
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

// ==== Room 2: the start room (0x4094e0..0x40ac30) ====

// 0x409450
void AtticScene::r2Loaded() {
	Room &r = _rooms[2];
	r.stages++;
	if (r.stages == 1) {
		if (!(r.flags & kRoomCancel)) {
			r.flags |= kRoomMore;
			goto done;
		}
	} else if (r.stages == 2) {
		if (!(r.flags & kRoomCancel)) {
			if (!_r2Open) {
				r.flags |= kRoomMore;
				goto done;
			}
			r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
			goto done;
		}
	} else {
		if (r.stages == 3)
			r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
		goto done;
	}
	r.flags |= kRoomAbort;
done:
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x4094e0
int AtticScene::r2Load() {
	switch (_rooms[2].stages) {
	case 0:
		return submit(0x498240, 6, 2);
	case 1:
		return submit(0x498318, 0xb, 2);
	case 2:
		return submit(0x4984a4, 10, 2);
	default:
		return _rooms[2].stages - 2;
	}
}

// 0x409540
int AtticScene::r2Unload() {
	int n = _rooms[2].stages;
	if (n > 0) {
		bmpVisible(0x4df61c, false);
		visible(0x14, false);
		visible(0x15, false);
		if (_r2Hits4 > 2 && _r2MapB0Added) {
			removeMap(map(0x5258b0));
			_r2MapB0Added = 0;
		}
		if (_r2HitsC9 > 2 && _r2MapB4Added) {
			removeMap(map(0x5258b4));
			_r2MapB4Added = 0;
		}
		_r2s14 = -1;
		stopSound(0x15);
	}
	if (n > 1) {
		static const int kHide[] = { 4, 0xc9, 5, 6, 0xb, 0xd, 0xca, 0xcb, 0xcf, 0xd1 };
		for (int id : kHide)
			visible(id, false);
		_r2s4 = -1;
		_r2sC9 = -1;
		stopSound(0xd);
	}
	if (n > 2) {
		static const int kHide[] = { 0xc, 0xd0, 7, 9, 10, 0xcc, 0xcd, 0xce };
		for (int id : kHide)
			visible(id, false);
		_r2s9 = -1;
		_r2sCD = -1;
		stopSound(10);
		stopSound(7);
	}
	int count = n == 1 ? 6 : n == 2 ? 0x11 : n == 3 ? 0x1b : 0;
	release(0x498240, count);
	_rooms[2].flags = (_rooms[2].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x409810
int AtticScene::r2Poll() {
	Room &r = _rooms[2];
	r.polled++;
	if (r.polled == 1) {
		if (resWait(0x498240))
			return 5;
		showBitmap(1, 2, 1, 30000, false);
		bmpSet(0x4df61c, r.view[0], r.view[1]);
		bmpPlace(0x4df61c);
		bmpVisible(0x4df61c, true);
		if (resWait(0x4982ac))
			return 5;
		showSlot(0x14, 100, true);
		if (resWait(0x4982d0))
			return 5;
		showSlot(0x15, 100, false);
		if (_r2HitsC9 > 2 && !_r2MapB4Added) {
			addMap(map(0x5258b4));
			_r2MapB4Added = 1;
		}
		if (_r2Hits4 > 2 && !_r2MapB0Added) {
			addMap(map(0x5258b0));
			_r2MapB0Added = 1;
		}
		_r2sC9 = -1;
		_r2s4 = -1;
		r.exits[1] = _r2Open ? 0xc : -1;
		_r2s9 = -1;
		_r2sCD = -1;
		_r2s14 = 0x14;
		_r2Fall = 0;
		_r2NedHit = 0;
	} else if (r.polled == 2) {
		if (resWait(0x498318))
			return 5;
		showSlot(4, 100, true);
		_r2Pos4X = slot(4).x;
		_r2Pos4Y = slot(4).y;
		if (resWait(0x49833c))
			return 5;
		showSlot(0xc9, 100, true);
		_r2PosC9X = slot(0xc9).x;
		_r2PosC9Y = slot(0xc9).y;
		static const struct {
			uint32 entry;
			int id;
		} kStage2[] = {
			{ 0x498360, 6 }, { 0x498384, 0xcb }, { 0x4983a8, 0xb }, { 0x4983cc, 0xcf }, { 0x4983f0, 0xd },
			{ 0x498438, 0xd1 }, { 0x49845c, 5 }, { 0x498480, 0xca }
		};
		for (const auto &e : kStage2) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, 100, false);
		}
		_r2sC9 = 0xc9;
		_r2TimeC9 = _game->now() + 0x9c4 + _game->crtRandom() % 0x9c4;
		targetAdd(0xcf, true);
		_r2s4 = 4;
		_r2Time4 = _game->now() + 0x9c4 + _game->crtRandom() % 0x9c4;
		targetAdd(0xb, true);
		r.flags |= kRoomActive;
		return 0;
	} else if (r.polled == 3) {
		static const struct {
			uint32 entry;
			int id, z;
		} kStage3[] = {
			{ 0x4984a4, 0xc, 100 }, { 0x4984c8, 0xd0, 100 }, { 0x4984ec, 7, 100 }, { 0x498534, 0xcc, 100 },
			{ 0x498558, 9, 0x50 }, { 0x49857c, 0xcd, 0x50 }, { 0x4985a0, 10, 0x50 }, { 0x4985c4, 0xce, 0x50 }
		};
		for (const auto &e : kStage3) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, false);
		}
		if (slot(10).sprite)
			slot(10).flip = slot(10).sprite->applyEffect(kFxHorizFlip);
		return 0;
	}
	return 0;
}

// 0x409e10
void AtticScene::r2Tick(uint32 now) {
	bool hintNow = false;
	if (_r12Hint && _r12HintTime < now) {
		_r12Hint = 0;
		if (!_r12HintFlag)
			_inputLocked = 0;
		else
			hintNow = true;
	}
	if (hintNow) {
		_r12HintFlag = 0;
		hint(2, facing() != 0);
		_inputLocked = 0;
	}
	if (_r2DropWait && _r2DropTime < now) {
		_r2Fall = 1;
		_r2DropWait = 0;
	}
	if (_r2Fall && (_rooms[12].flags & kRoomActive)) {
		// Ned fell through the trapdoor: the camera jumps to room 12
		const Room &r12 = _rooms[12];
		_jumpX = -(_scrollX + r12.view[0]);
		_nedPos.x = r12.view[2] - 200;
		_jumpY = -(_scrollY + r12.view[1]);
		_cameraJump = 1;
		for (int16 y = (int16)r12.view[3]; r12.view[1] <= y; y--) {
			if (solidAt(0x525954, _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
		_r2Fall = 0;
		_r12Time700 = _game->now() + 0xdac;
		_r12Armed = 0;
		ned()->resetIdle();
		ned()->enable(true);
		_inputLocked = 0;
	}
	if (_rooms[2].polled > 1) {
		r2Tick4(now);
		r2TickC9(now);
	}
	if (_rooms[2].polled > 2)
		r2TickDrops(now);
	r2Tick14(now);
}

// 0x409ff0
void AtticScene::r2TickDrops(uint32 now) {
	if (_r2s9 == -1) {
		visible(10, false);
		visible(9, false);
	} else if (_r2s9 == 9) {
		visible(10, false);
		visible(9, true);
		r2Drop(now, 0);
	} else if (_r2s9 == 10) {
		visible(10, true);
		visible(9, false);
		r2Splash(now, 0);
	}
	if (_r2sCD == -1) {
		visible(0xce, false);
		visible(0xcd, false);
	} else if (_r2sCD == 0xcd) {
		visible(0xce, false);
		visible(0xcd, true);
		r2Drop(now, 1);
	} else if (_r2sCD == 0xce) {
		visible(0xce, true);
		visible(0xcd, false);
		r2Splash(now, 1);
	}
}

// 0x40a100: a drop (9 or 0xcd) falls 12 px a tick to its floor, then splashes
void AtticScene::r2Drop(uint32 now, int k) {
	Slot &s = slot(k ? 0xcd : 9);
	Slot &splash = slot(k ? 0xce : 10);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	if (s.sprite)
		s.sprite->setCurrentImage(frame);
	s.y = k ? _r2DropYb : _r2DropY;
	s.x = k ? _r2DropXb : _r2DropX;
	duration = place(k ? _r2sCD : _r2s9, k ? 0xcd : 9, true);
	if (s.deadline < now) {
		s.step++;
		if (s.sequence)
			s.sequence->getStep(s.step, frame, duration);
		s.deadline = now + (int16)duration * kMsPerStep;
	}
	if (s.sequenceLength <= s.step)
		s.step = 0;
	if (!k) {
		_r2DropY += 0xc;
		if (_r2Floor <= _r2DropY + s.h) {
			_r2s9 = 10;
			_r2DropY = (s.h - splash.h) + _r2Floor;
			s.step = 0;
		}
	} else {
		_r2DropYb += 0xc;
		if (_r2FloorB <= _r2DropYb + s.h) {
			_r2sCD = 0xce;
			_r2DropYb = (s.h - splash.h) + _r2FloorB;
			s.step = 0;
		}
	}
}

// 0x40a2d0: the splash; it knocks Ned back once if it reaches him
void AtticScene::r2Splash(uint32 now, int k) {
	int id = k ? 0xce : 10;
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	if (s.sprite)
		s.sprite->setCurrentImage(frame);
	s.x = k ? _r2DropXb : _r2DropX;
	s.y = k ? _r2DropYb : _r2DropY;
	duration = place(k ? _r2sCD : _r2s9, id, k != 0);
	if (s.deadline < now) {
		if (s.step == 0)
			playAt(10);
		s.step++;
		if (s.sequence)
			s.sequence->getStep(s.step, frame, duration);
		s.deadline = now + (int16)duration * kMsPerStep;
	}
	int32 rect[4];
	if (s.boxes)
		s.boxes->getRawRect(frame, k ? 0 : 3, rect);
	else
		rect[0] = rect[1] = rect[2] = rect[3] = -0x7fff;
	Common::Point p = s.sprite ? s.sprite->getPosition() : Common::Point();
	if (!_r2NedHit) {
		rect[0] += p.x - _scrollX;
		rect[2] += p.x - _scrollX;
		rect[1] += p.y - _scrollY;
		rect[3] += p.y - _scrollY;
		int32 box[4];
		ned()->getBox(ned()->getCurSlot(), _nedPos, box);
		if (MAX(box[0], rect[0]) < MIN(box[2], rect[2]) && MAX(box[1], rect[1]) < MIN(box[3], rect[3])) {
			playAction(6);
			_r2NedHit = 1;
		}
	}
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		if (k)
			_r2sCD = -1;
		else
			_r2s9 = -1;
	}
}

// 0x40a570: the trapdoor (0x14 idle, 0x15 opening)
void AtticScene::r2Tick14(uint32 now) {
	if (_r2s14 == -1 || now <= slot(_r2s14).deadline)
		return;
	slot(_r2s14).step++;
	int16 cur = _r2s14;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		if (cur == 0x14) {
			cur = _r2s14 = slotSwitch(0x14, cur);
		} else if (cur == 0x15) {
			cur = slotSwitch(-1, cur);
			_r2DropTime = now + 2000;
			_r2s14 = cur;
			_r2DropWait = 1;
		}
	}
	int d = 0;
	if (cur != -1) {
		d = place(cur, 0x14, true);
		cur = _r2s14;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x40a670: the actor at slot 4 (4 idle, 0xb..0xd and 5..7 its other moves)
void AtticScene::r2Tick4(uint32 now) {
	if (_r2s4 == -1 || now <= slot(_r2s4).deadline)
		return;
	slot(_r2s4).step++;
	int16 cur = _r2s4;
	int next = -2;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		switch (cur) {
		case 4:
			if (_r2Time4 < now) {
				next = 0xb;
			} else {
				next = 4;
				_r2Pos4X = slot(4).x;
				_r2Pos4Y = slot(4).y;
			}
			break;
		case 5:
			cur = _r2s4 = slotSwitch(-1, cur);
			if (!_r2MapB0Added) {
				addMap(map(0x5258b0));
				_r2MapB0Added = 1;
			}
			break;
		case 6: {
			_r2s4 = slotSwitch(4, cur);
			_r2Pos4X = slot(4).x;
			_r2Pos4Y = slot(4).y;
			int n = (int)(_game->crtRandom() % 4) + 2;
			_r2Time4 = now + n * 1000;
			cur = _r2s4;
			break;
		}
		case 7:
			_r2s4 = slotSwitch(6, cur);
			_r2s9 = 9;
			_r2DropX = _r2Pos4X + (int)(_game->crtRandom() % 0xe) * 8;
			_r2DropY = _r2Pos4Y - 100;
			_r2Pos4Y += 0xbe;
			_r2Floor = r2FindFloor(_r2DropX, _r2DropY, _r2Floor);
			cur = _r2s4;
			break;
		case 0xb:
			if (!_r2Open)
				next = _rooms[2].polled < 3 ? 6 : 0xc;
			else
				next = 5;
			break;
		case 0xc:
			next = 7;
			break;
		case 0xd:
			if (_r2Hits4 > 2) {
				if (_r2HitsC9 > 2) {
					_r2Open = 1;
					_rooms[2].exits[1] = 0xc;
				}
				next = 5;
			} else {
				next = 6;
			}
			break;
		default:
			break;
		}
	}
	if (next != -2)
		cur = _r2s4 = slotSwitch(next, cur);
	int d = 0;
	if (cur != -1) {
		d = place(cur, 4, true);
		cur = _r2s4;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x40a990: the actor at slot 0xc9, the twin of 0x40a670
void AtticScene::r2TickC9(uint32 now) {
	if (_r2sC9 == -1 || now <= slot(_r2sC9).deadline)
		return;
	slot(_r2sC9).step++;
	int16 cur = _r2sC9;
	int next = -2;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		switch (cur) {
		case 0xc9:
			if (_r2TimeC9 < now) {
				next = 0xcf;
			} else {
				next = 0xc9;
				_r2PosC9X = slot(0xc9).x;
				_r2PosC9Y = slot(0xc9).y;
			}
			break;
		case 0xca:
			cur = _r2sC9 = slotSwitch(-1, cur);
			if (!_r2MapB4Added) {
				addMap(map(0x5258b4));
				_r2MapB4Added = 1;
			}
			break;
		case 0xcb: {
			_r2PosC9X = slot(0xc9).x;
			_r2PosC9Y = slot(0xc9).y;
			_r2sC9 = slotSwitch(0xc9, cur);
			int n = (int)(_game->crtRandom() % 4) + 2;
			_r2TimeC9 = now + n * 1000;
			cur = _r2sC9;
			break;
		}
		case 0xcc:
			_r2sC9 = slotSwitch(0xcb, cur);
			_r2sCD = 0xcd;
			_r2DropXb = _r2PosC9X + (int)(_game->crtRandom() % 0xe) * 8;
			_r2DropYb = _r2PosC9Y - 100;
			_r2PosC9Y += 0xbe;
			_r2FloorB = r2FindFloor(_r2DropXb, _r2DropYb, _r2FloorB);
			cur = _r2sC9;
			break;
		case 0xcf:
			if (!_r2Open)
				next = _rooms[2].polled < 3 ? 0xcb : 0xd0;
			else
				next = 0xca;
			break;
		case 0xd0:
			next = 0xcc;
			break;
		case 0xd1:
			if (_r2HitsC9 > 2) {
				if (_r2Hits4 > 2) {
					_r2Open = 1;
					_rooms[2].exits[1] = 0xc;
				}
				next = 0xca;
			} else {
				next = 0xcb;
			}
			break;
		default:
			break;
		}
	}
	if (next != -2)
		cur = _r2sC9 = slotSwitch(next, cur);
	int d = 0;
	if (cur != -1) {
		d = place(cur, 0xc9, true);
		cur = _r2sC9;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// The floor under a drop: upwards from room 2's bottom in base map 0, above y
int16 AtticScene::r2FindFloor(int x, int y, int16 old) {
	for (int16 fy = (int16)_rooms[2].view[3]; y < fy; fy--) {
		if (solidAt(kBaseMaps, (int16)x, fy))
			return fy;
	}
	return old;
}

// ==== Room 3 (0x40acc0..0x40b050) ====

// 0x40ace0
void AtticScene::r3Loaded() {
	_rooms[3].stages++;
	_rooms[3].flags = (_rooms[3].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x40acc0
int AtticScene::r3Load() {
	return submit(0x498610, 6, 3);
}

// 0x40ad20
int AtticScene::r3Poll() {
	_rooms[3].polled++;
	_r3s2D = 0x2d;
	_r3sEB = 0xeb;
	_r3Started = 0;
	if (resWait(0x498610))
		return 5;
	showBitmap(1, 3, 1, 30000, true);
	bmpSet(0x4df7ac, _rooms[3].view[0], _rooms[3].view[1]);
	if (resWait(0x498634))
		return 5;
	showSlot(0x2d, 100, false);
	if (resWait(0x498658))
		return 5;
	showSlot(0x2e, 100, false);
	if (resWait(0x4986a0))
		return 5;
	showSlot(0xeb, 100, false);
	_rooms[3].flags |= kRoomActive;
	return 0;
}

// 0x40ae10
int AtticScene::r3Unload() {
	bmpVisible(0x4df7ac, false);
	visible(0x2d, false);
	visible(0x2e, false);
	visible(0xeb, false);
	_r3s2D = -1;
	_r3sEB = -1;
	stopSound(0x2e);
	stopSound(0xeb);
	release(0x498610, 6);
	_rooms[3].flags = (_rooms[3].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x40aed0
void AtticScene::r3Tick(uint32 now) {
	if (_r3sEB != -1 && _r3Started && slot(_r3sEB).deadline < now) {
		Slot &s = slot(_r3sEB);
		s.step++;
		if (s.sequenceLength <= s.step)
			s.step = s.sequenceLength - 1;
		int d = place(_r3sEB, 0xeb, true);
		setDeadline(_r3sEB, now + (int16)d * kMsPerStep);
	}
	if (_r3s2D != -1 && slot(_r3s2D).deadline < now) {
		slot(_r3s2D).step++;
		int16 cur = _r3s2D;
		if (slot(cur).sequenceLength <= slot(cur).step && cur > 0x2c && cur < 0x2f)
			cur = _r3s2D = slotSwitch(0x2d, cur);
		int d = place(cur, 0x2d, true);
		setDeadline(_r3s2D, now + (int16)d * kMsPerStep);
	}
}

// ==== Room 4 (0x40b070..0x40be30) ====

// 0x40b070
void AtticScene::r4Loaded() {
	Room &r = _rooms[4];
	r.stages++;
	if (r.stages == 1 || r.stages == 2) {
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
	} else if (r.stages == 3) {
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x40b0e0
int AtticScene::r4Load() {
	switch (_rooms[4].stages) {
	case 0:
		return submit(0x4986e8, 6, 4);
	case 1:
		return submit(0x4987c0, 4, 4);
	case 2:
		return submit(0x498850, 8, 4);
	default:
		return _rooms[4].stages - 2;
	}
}

// 0x40b140
int AtticScene::r4Unload() {
	int n = _rooms[4].stages;
	if (n > 0) {
		bmpVisible(0x4df93c, false);
		bmpVisible(0x4df940, false);
		visible(0x21, false);
		visible(0xb3, false);
		visible(0xb4, false);
		stopSound(0xb4);
	}
	if (n > 1) {
		visible(0x22, false);
		visible(0xb5, false);
		stopSound(0x22);
		stopSound(0xb5);
	}
	if (n > 2) {
		visible(0x24, false);
		visible(0x79, false);
		visible(0x7b, false);
		visible(0x26, false);
		slot(0x79).x = _r4Pos79X;
		slot(0x79).y = _r4Pos79Y;
		slot(0x7b).x = _r4Pos7BX;
		slot(0x7b).y = _r4Pos7BY;
		slot(0x26).x = _r4Pos26X;
		slot(0x26).y = _r4Pos26Y;
		stopSound(0x24);
		stopSound(0x79);
		stopSound(0x7b);
		stopSound(0x26);
	}
	_r4sB3 = -1;
	_r4s21 = -1;
	_r4s24 = -1;
	_r4sBall = -1;
	int count = n == 1 ? 6 : n == 2 ? 10 : n == 3 ? 0x12 : 0;
	release(0x4986e8, count);
	_rooms[4].flags = (_rooms[4].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x40b3a0
int AtticScene::r4Poll() {
	Room &r = _rooms[4];
	r.polled++;
	if (r.polled == 1) {
		bmpVisible(0x4df460, false);
		if (bmp(0x4df460) && bmp(0x4df460)->object)
			bmp(0x4df460)->object->setDrawOrder(29999);
		if (resWait(0x4986e8))
			return 5;
		showBitmap(1, 4, 1, 30000, true);
		bmpSet(0x4df93c, r.view[0], r.view[1]);
		bmpSet(0x4df940, 0x7ec, 0xb00);
		if (resWait(0x49870c))
			return 5;
		showBitmap(1, 4, 2, 3, false);
		bmpPlace(0x4df940);
		bmpVisible(0x4df940, true);
		if (resWait(0x498730))
			return 5;
		showSlot(0x21, 100, false);
		if (resWait(0x498754))
			return 5;
		showSlot(0xb3, 100, false);
		if (resWait(0x498778))
			return 5;
		showSlot(0xb4, 100, false);
		r.flags |= kRoomActive;
		_r4sB3 = 0xb3;
		_r4TimeB3 = 0;
		_r4s21 = 0x21;
	} else if (r.polled == 2) {
		if (resWait(0x4987c0))
			return 5;
		showSlot(0x22, 100, false);
		if (resWait(0x498808))
			return 5;
		showSlot(0xb5, 100, false);
		targetAdd(_r4sB3, true);
		targetAdd(_r4s21, true);
		return 0;
	} else if (r.polled == 3) {
		if (resWait(0x498850))
			return 5;
		showSlot(0x24, 100, false);
		if (resWait(0x498898))
			return 5;
		showSlot(0x79, 99, false);
		if (resWait(0x4988e0))
			return 5;
		showSlot(0x7b, 99, false);
		if (resWait(0x498928))
			return 5;
		showSlot(0x26, 99, false);
		if (slot(0x26).sprite)
			slot(0x26).flip = slot(0x26).sprite->applyEffect(kFxHorizFlip);
		_r4Pos79X = slot(0x79).x;
		_r4Pos79Y = slot(0x79).y;
		_r4Pos7BX = slot(0x7b).x;
		_r4Pos7BY = slot(0x7b).y;
		_r4Pos26X = slot(0x26).x;
		_r4Pos26Y = slot(0x26).y;
		_r4s24 = -1;
		_r4sBall = -1;
		_r4Climbed = 0;
		return 0;
	}
	return 0;
}

// 0x40b720
void AtticScene::r4Tick(uint32 now) {
	r4TickB3(now);
	r4Tick21(now);
	r4Tick24(now);
	r4TickBall(now);
}

static const int16 kR4Balls[3] = { 0x79, 0x7b, 0x26 }; // 0x498970

// 0x40b750
void AtticScene::r4TickB3(uint32 now) {
	if (_r4sB3 == -1 || now <= slot(_r4sB3).deadline)
		return;
	if (_rooms[4].polled > 2 && _r4sB3 == 0xb5 && slot(0xb5).step == 3) {
		_r4Index = (_r4Index + 1) % 3;
		_r4sBall = slotSwitch(kR4Balls[_r4Index], _r4sBall);
		playAt(_r4sBall);
	}
	slot(_r4sB3).step++;
	int16 cur = _r4sB3;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		targetRemove(_r4sB3);
		if (_r4sB3 == 0xb3) {
			if (_r4TimeB3 < now) {
				_r4sB3 = slotSwitch(0xb4, cur);
				playAt(0xb4);
			}
			slot(_r4sB3).step = 0;
			targetAdd(_r4sB3, true);
			cur = _r4sB3;
		} else if (_r4sB3 == 0xb4) {
			_r4sB3 = slotSwitchPre(0xb3, cur);
			slot(_r4sB3).step = 0;
			_r4TimeB3 = _game->crtRandom() % 5000 + now;
			targetAdd(_r4sB3, true);
			cur = _r4sB3;
		} else if (_r4sB3 == 0xb5) {
			_r4sB3 = slotSwitch(0xb5, cur);
			slot(_r4sB3).step = slot(_r4sB3).sequenceLength - 1;
			cur = _r4sB3;
		}
	}
	int d = place(cur, 0xb3, true);
	setDeadline(_r4sB3, now + (int16)d * kMsPerStep);
}

// 0x40ba00
void AtticScene::r4Tick21(uint32 now) {
	if (_r4s21 == -1 || now <= slot(_r4s21).deadline)
		return;
	slot(_r4s21).step++;
	int16 cur = _r4s21;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		targetRemove(_r4s21);
		if (cur == 0x21) {
			cur = _r4s21 = slotSwitch(0x21, cur);
			targetAdd(cur, true);
		} else if (cur == 0x22) {
			_r4s21 = slotSwitch(0x22, cur);
			slot(_r4s21).step = slot(_r4s21).sequenceLength - 1;
			cur = _r4s21;
		}
	}
	int d = place(cur, 0x21, true);
	setDeadline(_r4s21, now + (int16)d * kMsPerStep);
}

// 0x40bb30: Ned climbs (0x24)
void AtticScene::r4Tick24(uint32 now) {
	if (_r4s24 == -1 || slot(_r4s24).deadline >= now)
		return;
	if (_r4s24 == 0x24 && slot(0x24).step == 4)
		playAction(0x36);
	slot(_r4s24).step++;
	int16 cur = _r4s24;
	if (slot(cur).sequenceLength <= slot(cur).step && cur == 0x24) {
		cur = _r4s24 = slotSwitch(-1, cur);
		_inputLocked = 0;
	}
	int d = 0;
	if (cur != -1) {
		d = place(cur, 0x24, true);
		cur = _r4s24;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x40bc50: the thrown ball runs down until it leaves the room
void AtticScene::r4TickBall(uint32 now) {
	if (_r4sBall == -1 || slot(_r4sBall).deadline >= now)
		return;
	slot(_r4sBall).step++;
	int16 cur = _r4sBall;
	int16 ball = kR4Balls[_r4Index];
	if (slot(cur).sequenceLength <= slot(cur).step) {
		if (_rooms[4].view[3] < slot(ball).y) {
			cur = _r4sBall = slotSwitch(-1, cur);
		} else {
			_r4sBall = slotSwitch(ball, cur);
			playAt(_r4sBall, _r4sBall);
			cur = _r4sBall;
		}
	}
	int d = 0;
	if (cur != -1) {
		if (_r4Index == 2) {
			slot(kR4Balls[2]).x -= 0x10;
			slot(ball).y += 8;
		} else {
			slot(ball).x += 8;
			slot(ball).y += 6;
		}
		d = place(_r4sBall, ball, _r4Index != 2);
		cur = _r4sBall;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// ==== Room 5 (0x40be30..0x40e010) ====

// 0x40be30: loads only from rooms 3, 6 and 11
bool AtticScene::r5ShouldLoad() {
	return _room == 3 || _room == 6 || _room == 0xb;
}

// 0x40be60: like 0x467da0, but stays while Ned is in room 3
bool AtticScene::r5ShouldUnload(int r) {
	if (r == _room)
		return false;
	for (int i = 0; i < 4; i++) {
		if (_rooms[r].exits[i] == _room || _room == 3)
			return false;
	}
	if (_rooms[r].flags & (kRoomAbort | kRoomLoaded))
		return true;
	if (_rooms[r].flags & kRoomLoading)
		_rooms[r].flags |= kRoomCancel;
	return false;
}

// 0x40bed0
void AtticScene::r5Loaded() {
	Room &r = _rooms[5];
	int old = r.stages++;
	switch (old) {
	case 0:
		if (r.flags & kRoomCancel) {
			r.flags |= kRoomAbort;
			break;
		}
		if (!_r5Done) {
			r.flags |= kRoomMore;
			break;
		}
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
		break;
	case 1:
	case 2:
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
		break;
	case 3:
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
		break;
	default:
		break;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x40bf80
int AtticScene::r5Load() {
	switch (_rooms[5].stages) {
	case 0:
		return submit(0x498978, 8, 5);
	case 1:
		return submit(0x498a98, 4, 5);
	case 2:
		return submit(0x498b28, 0xe, 5);
	case 3:
		return submit(0x498d20, 0xc, 5);
	default:
		return _rooms[5].stages;
	}
}

// 0x40c010
int AtticScene::r5Poll() {
	Room &r = _rooms[5];
	int stage = r.polled++;
	switch (stage) {
	case 0:
		_r5Fight = 0;
		if (resWait(0x498978))
			return 5;
		showBitmap(1, 5, 1, 30000, true);
		bmpSet(0x4dfacc, r.view[0], r.view[1]);
		_r5sC2 = 0xc2;
		if (resWait(0x49899c))
			return 5;
		showSlot(0xc2, 0xaf, false);
		if (_r5Hit)
			removeRegion(_r5Region854);
		_r5Time3B = 0;
		_r5s3B = _r5Done ? 0x3d : 0x3b;
		if (resWait(0x4989c0))
			return 5;
		showSlot(0x3b, 0xaf, false);
		if (resWait(0x4989e4))
			return 5;
		showSlot(0x3c, 0xaf, false);
		if (resWait(0x498a2c))
			return 5;
		showSlot(0x3d, 0x8c, false);
		if (_r5Done)
			r.flags |= kRoomActive;
		_r5Down31 = 0;
		_r5Down86 = 0;
		_r5Down8A = 0;
		return 0;
	case 1:
		_r5s31 = 0x31;
		_r5Time31 = 0;
		if (resWait(0x498a98))
			return 5;
		showSlot(0x31, 0x96, false);
		_r5s86 = 0x86;
		_r5Time86 = 0;
		if (resWait(0x498abc))
			return 5;
		showSlot(0x86, 0x95, false);
		_r5s8A = 0x8a;
		_r5Time8A = 0;
		if (resWait(0x498ae0))
			return 5;
		showSlot(0x8a, 0x96, false);
		_r5s2F = 0x2f;
		_r5Var828 = 0;
		if (resWait(0x498b04))
			return 5;
		showSlot(0x2f, 100, false);
		targetAdd(_r5sC2, true);
		r.flags |= kRoomActive;
		return 0;
	case 2: {
		static const struct {
			uint32 entry;
			int id, z;
		} kStage[] = {
			{ 0x498b28, 0x83, 0x96 }, { 0x498b4c, 0x87, 0x95 }, { 0x498b70, 0x8b, 0x96 }, { 0x498b94, 0x30, 100 },
			{ 0x498bb8, 0xe8, 0x96 }, { 0x498bdc, 0xe9, 0x95 }, { 0x498c00, 0xea, 0x96 }
		};
		for (const auto &e : kStage) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, false);
		}
		_r5Pos31X = slot(0x31).x;
		_r5Pos31Y = slot(0x31).y;
		_r5Pos86X = slot(0x86).x;
		_r5Pos86Y = slot(0x86).y;
		_r5Pos8AX = slot(0x8a).x;
		_r5Pos8AY = slot(0x8a).y;
		return 0;
	}
	case 3: {
		static const struct {
			uint32 entry;
			int id, z;
		} kStage[] = {
			{ 0x498d20, 0x84, 0x96 }, { 0x498d44, 0x85, 0x96 }, { 0x498d68, 0x88, 0x95 }, { 0x498d8c, 0x89, 0x95 },
			{ 0x498db0, 0x8c, 0x96 }, { 0x498dd4, 0x8d, 0x96 }
		};
		for (const auto &e : kStage) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, false);
		}
		return 0;
	}
	default:
		return 0;
	}
}

// 0x40c700
int AtticScene::r5Unload() {
	int n = _rooms[5].stages;
	if (n > 0) {
		bmpVisible(0x4dfacc, false);
		visible(0xc2, false);
		visible(0x3b, false);
		visible(0x3c, false);
		visible(0x3d, false);
		stopSound(0x3c);
	}
	if (n > 1) {
		visible(0x31, false);
		visible(0x86, false);
		visible(0x8a, false);
		visible(0x2f, false);
	}
	if (n > 2) {
		static const int kHide[] = { 0x83, 0x87, 0x8b, 0x30, 0xe8, 0xe9, 0xea };
		for (int id : kHide)
			visible(id, false);
		static const int kStop[] = { 0x83, 0x87, 0x8b, 0x30, 0xe8, 0xe9, 0xea };
		for (int id : kStop)
			stopSound(id);
	}
	if (n > 3) {
		static const int kHide[] = { 0x84, 0x85, 0x88, 0x89, 0x8c, 0x8d };
		for (int id : kHide)
			visible(id, false);
		for (int id : kHide)
			stopSound(id);
	}
	if (_r5Fight) {
		_r5Hit = 0;
		slot(0x31).x = _r5Pos31X;
		slot(0x31).y = _r5Pos31Y;
		slot(0x86).x = _r5Pos86X;
		slot(0x86).y = _r5Pos86Y;
		slot(0x8a).x = _r5Pos8AX;
		slot(0x8a).y = _r5Pos8AY;
	}
	if (_r5Region854)
		removeRegion(_r5Region854);
	if (_r5Region31)
		removeRegion(_r5Region31);
	if (_r5Region86)
		removeRegion(_r5Region86);
	if (_r5Region8A)
		removeRegion(_r5Region8A);
	int count = n == 1 ? 8 : n == 2 ? 0xc : n == 3 ? 0x1a : n == 4 ? 0x26 : 0;
	_r5s31 = _r5s86 = _r5s8A = _r5s3B = _r5sC2 = _r5s2F = -1;
	release(0x498978, count);
	_rooms[5].flags = (_rooms[5].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x40c590
void AtticScene::r5Tick(uint32 now) {
	r5Tick3B(now);
	if (_r5sC2 != -1 && slot(_r5sC2).deadline < now) {
		targetRemove(_r5sC2);
		_r5sC2 = slotSwitch(0xc2, _r5sC2);
		slot(_r5sC2).step = (int16)_r5Hit;
		int d = place(_r5sC2, 0xc2, true);
		setDeadline(_r5sC2, now + (int16)d * kMsPerStep);
		if (!_r5Hit)
			targetAdd(_r5sC2, true);
	}
	if (_r5Down31 && _r5Down86 && _r5Down8A)
		_r5Fight = 0;
	if (_rooms[5].polled > 1) {
		r5TickFighter(now, 0);
		r5TickFighter(now, 1);
		r5TickFighter(now, 2);
		r5Tick2F(now);
	}
}

// 0x40cce0 / 0x40d180 / 0x40d5a0: the three fighters, each with an idle, a hit, an
// attack, a defeat and an entrance slot
void AtticScene::r5TickFighter(uint32 now, int which) {
	static const struct {
		int16 idle, hit, attack, down, enter;
		int16 attackStep, action, enterDy;
		bool enterPre;
	} kFighters[3] = {
		{ 0x31, 0x83, 0x84, 0x85, 0xe8, 4, 0x16, 6, false }, // 0x40cce0
		{ 0x86, 0x87, 0x88, 0x89, 0xe9, 5, 0x21, 6, true },  // 0x40d180
		{ 0x8a, 0x8b, 0x8c, 0x8d, 0xea, 3, 0x1c, 4, false }  // 0x40d5a0
	};
	const auto &f = kFighters[which];
	int16 &cur = which == 0 ? _r5s31 : which == 1 ? _r5s86 : _r5s8A;
	uint32 &time = which == 0 ? _r5Time31 : which == 1 ? _r5Time86 : _r5Time8A;
	int32 &region = which == 0 ? _r5Region31 : which == 1 ? _r5Region86 : _r5Region8A;

	if (cur == -1 || now <= slot(cur).deadline)
		return;
	slot(cur).step++;
	int16 s = cur;
	if (slot(s).step >= slot(s).sequenceLength) {
		if (_r5Fight)
			targetRemove(cur);
		bool restart = true;
		if (s == f.idle) {
			if (_r5Fight && time < now) {
				cur = slotSwitch(f.hit, s);
				playAt(f.hit);
				if (_r5Fight)
					targetAdd(cur, true);
				s = cur;
			} else {
				cur = slotSwitch(f.idle, s);
				if (_r5Fight)
					targetAdd(cur, true);
				s = cur;
			}
		} else if (s == f.hit) {
			cur = slotSwitch(f.idle, s);
			time = _game->crtRandom() % 3000 + 3000 + now;
			if (_r5Fight)
				targetAdd(cur, true);
			s = cur;
		} else if (s == f.attack) {
			cur = slotSwitch(f.idle, s);
			if (_r5Fight)
				targetAdd(cur, true);
			s = cur;
		} else if (s == f.down) {
			cur = s = slotSwitch(f.down, s);
			if (region) {
				removeRegion(region);
				s = cur;
			}
		} else if (s == f.enter) {
			if (f.enterPre) {
				cur = s = slotSwitchPre(f.idle, s);
			} else {
				cur = s = slotSwitch(f.idle, s);
			}
			if (which == 0) {
				_viewRoomX = -1;
				_inputLocked = 0;
				if (_nedPos.x < _rooms[5].view[0] + 100)
					autoWalk(kInputRight, 1, _rooms[5].view[0] + 100, 0);
				s = cur;
			}
		} else {
			restart = true;
		}
		if (restart) {
			if (s == f.down)
				slot(f.down).step = slot(f.down).sequenceLength - 1;
			else
				setStep(s, 0);
		}
		s = cur;
	}
	if (s == f.enter)
		slot(f.idle).y += f.enterDy;
	int frame = 0;
	int d = place(s, f.idle, true, &frame);
	if (cur == f.attack && slot(f.attack).step == f.attackStep) {
		ned()->enable(true);
		playAction(f.action);
	}
	if (region) {
		// The fighter's box 0 moves the region that stops Ned
		int32 rect[4];
		stepRect(cur, 0, rect, &frame);
		Slot &cs = slot(cur);
		int dummy = 0;
		if (cs.sequence)
			cs.sequence->getStep(cs.step, dummy, d);
		int pos[2];
		attachPos(cur, f.idle, pos, true);
		rect[2] += pos[0];
		rect[0] += pos[0];
		rect[3] += pos[1];
		rect[1] += pos[1];
		setRegion(region, rect, 0x20000);
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x40d9c0: the cage (0x2f), opened by the hit on 0xc2 (0x30), starts the fight
void AtticScene::r5Tick2F(uint32 now) {
	if (_r5s2F == -1 || slot(_r5s2F).deadline >= now)
		return;
	int32 zero[4] = { 0, 0, 0, 0 };
	slot(_r5s2F).step++;
	int16 cur = _r5s2F;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		if (cur == 0x2f) {
			cur = _r5s2F = slotSwitch(0x2f, cur);
		} else if (cur == 0x30) {
			_r5s2F = slotSwitch(-1, cur);
			_r5Fight = 1;
			_r5Region31 = addRegion(zero, 0x80000, 0x95);
			_r5Region86 = addRegion(zero, 0x80000, 0xea);
			_r5Region8A = addRegion(zero, 0x80000, 0xee);
			_world.setUser(&_userMap);
			static const struct {
				int16 enter, ref;
			} kEnter[3] = { { 0xe8, 0x31 }, { 0xe9, 0x86 }, { 0xea, 0x8a } };
			for (int i = 0; i < 3; i++) {
				int16 &s = i == 0 ? _r5s31 : i == 1 ? _r5s86 : _r5s8A;
				s = slotSwitch(kEnter[i].enter, s);
				setStep(s, 0);
				int d = place(s, kEnter[i].ref, true);
				setDeadline(s, _game->now() + (int16)d * kMsPerStep);
				targetAdd(s, true);
			}
			playAt(0xe8);
			playAt(0xe9);
			playAt(0xea);
			cur = _r5s2F;
		}
	}
	if (cur >= 0) {
		int d = place(cur, 0x2f, true);
		setDeadline(_r5s2F, now + (int16)d * kMsPerStep);
	}
}

// 0x40ddb0: the gate (0x3b closed, 0x3c rattling, 0x3d open once the three fighters are down)
void AtticScene::r5Tick3B(uint32 now) {
	if (_r5s3B == -1 || now <= slot(_r5s3B).deadline)
		return;
	slot(_r5s3B).step++;
	int16 cur = _r5s3B;
	if (slot(cur).step < slot(cur).sequenceLength)
		goto place;
	if (cur == 0x3b) {
		if (!_r5Down31 || !_r5Down86 || !_r5Down8A) {
			if (now <= _r5Time3B)
				goto restart;
			_r5s3B = slotSwitch(0x3c, cur);
			playAt(0x3c);
		} else {
			if (!_r5Done)
				r5AddMap3();
			_r5s3B = slotSwitch(0x3d, cur);
			playAt(0x3d);
		}
	} else if (cur == 0x3c) {
		_r5s3B = slotSwitch(0x3b, cur);
		_r5Time3B = _game->crtRandom() % 0xdac + now;
	} else {
		if (cur == 0x3d) {
			_r5s3B = slotSwitch(0x3d, cur);
			slot(_r5s3B).step = slot(_r5s3B).sequenceLength - 1;
			if (!_hint0Shown) {
				hint(0, facing() != 0);
				_hint0Shown = 1;
			}
		}
		goto place;
	}
restart:
	setStep(_r5s3B, 0);
place:
	cur = _r5s3B;
	int d = place(cur, cur == 0x3d ? 0x3d : 0x3b, true);
	setDeadline(_r5s3B, now + (int16)d * kMsPerStep);
}

// 0x40cba0
int AtticScene::r5AddMap3() {
	addBaseMap(3);
	_r5Done = 1;
	return 0;
}

// ==== Room 6 (0x40e100..0x40e220) ====

// 0x40e100
void AtticScene::r6Loaded() {
	_rooms[6].stages++;
	_rooms[6].flags = (_rooms[6].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x40e140
int AtticScene::r6Load() {
	return submit(0x498ee8, 1, 6);
}

// 0x40e1b0
int AtticScene::r6Poll() {
	_rooms[6].polled++;
	if (resWait(0x498ee8))
		return 5;
	showBitmap(1, 6, 1, 30000, true);
	bmpSet(0x4dfc5c, _rooms[6].view[0], _rooms[6].view[1]);
	_rooms[6].flags |= kRoomActive;
	return 0;
}

// 0x40e160
int AtticScene::r6Unload() {
	bmpVisible(0x4dfc5c, false);
	release(0x498ee8, 1);
	_rooms[6].flags = (_rooms[6].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// ==== Room 7 (0x40e230..0x40ecb0): the television ====

static const int16 kR7Shows[3] = { 0x8e, 0x91, 0x49 }; // 0x498f10, one per variant entry 0x49905c + v * 0x24

// 0x40e230
void AtticScene::r7Loaded() {
	Room &r = _rooms[7];
	int n = r.stages + 1;
	if (n == 1) {
		r.stages = n;
		if (!(r.flags & kRoomCancel)) {
			r.flags |= kRoomMore;
			goto done;
		}
	} else if (n == 2) {
		r.stages = n;
		if (!(r.flags & kRoomCancel)) {
			if (_r7Plays < 3)
				_r7Variant = _r7Plays;
			else
				_r7Variant = _game->crtRandom() % 3;
			r.flags |= kRoomMore;
			goto done;
		}
	} else {
		r.stages = n;
		if (n == 3)
			r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
		goto done;
	}
	r.flags |= kRoomAbort;
done:
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x40e2e0
int AtticScene::r7Load() {
	switch (_rooms[7].stages) {
	case 0:
		return submit(0x498f18, 6, 7);
	case 1:
		return submit(0x498ff0, 3, 7);
	case 2:
		return submit(0x49905c + _r7Variant * 0x24, 1, 7);
	default:
		return _rooms[7].stages - 2;
	}
}

// 0x40e4e0
int AtticScene::r7Poll() {
	Room &r = _rooms[7];
	r.polled++;
	if (r.polled == 1) {
		_r7s45 = 0x45;
		_r7s46 = 0x46;
		if (resWait(0x498f18))
			return 5;
		showBitmap(1, 7, 1, 30000, true);
		bmpSet(0x4dfdec, r.view[0], r.view[1]);
		bmpSet(0x4dfdf4, 0x1a43, 0xb87);
		if (resWait(0x498f3c))
			return 5;
		showBitmap(1, 7, 3, 3, false);
		bmpPlace(0x4dfdf4);
		bmpVisible(0x4dfdf4, true);
		if (resWait(0x498f60))
			return 5;
		if (!_r7MapAdded) {
			addMap(map(0x5257d4));
			_r7MapAdded = 1;
		}
		if (resWait(0x498f84))
			return 5;
		showSlot(0x45, 100, false);
		if (resWait(0x498fcc))
			return 5;
		showSlot(0x46, 100, false);
		r.flags |= kRoomActive;
	} else if (r.polled == 2) {
		if (resWait(0x498ff0))
			return 5;
		showSlot(0x8e, 100, false);
		if (resWait(0x499014))
			return 5;
		showSlot(0x91, 100, false);
		if (resWait(0x499038))
			return 5;
		showSlot(0x49, 100, false);
	}
	return 0;
}

// 0x40e350
int AtticScene::r7Unload() {
	int n = _rooms[7].stages;
	if (n > 0) {
		bmpVisible(0x4dfdec, false);
		bmpVisible(0x4dfdf4, false);
		visible(0x45, false);
		visible(0x46, false);
		_r7s45 = -1;
		_r7s46 = -1;
		if (_r7MapAdded) {
			removeMap(map(0x5257d4));
			_r7MapAdded = 0;
		}
		stopSound(0x45);
	}
	if (n > 1) {
		visible(0x8e, false);
		visible(0x91, false);
		visible(0x49, false);
	}
	if (n > 2) {
		stopSound(kR7Shows[_r7Variant]);
		release(0x49905c + _r7Variant * 0x24, 1);
	}
	int count = n == 1 ? 6 : (n > 1 && n < 4) ? 9 : 0;
	release(0x498f18, count);
	_rooms[7].flags = (_rooms[7].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x40e730
void AtticScene::r7Tick(uint32 now) {
	r7Tick46(now);
	if (_r7s45 != -1 && slot(_r7s45).deadline < now) {
		slot(_r7s45).step++;
		int16 cur = _r7s45;
		if (slot(cur).sequenceLength <= slot(cur).step && cur == 0x45)
			cur = _r7s45 = slotSwitch(0x45, cur);
		int d = place(cur, 0x45, true);
		setDeadline(_r7s45, now + (int16)d * kMsPerStep);
	}
}

// 0x40e820: the screen (0x46); a show plays on its own clock, catching up whole steps
void AtticScene::r7Tick46(uint32 now) {
	int16 cur = _r7s46;
	if (cur == -1 || now < slot(cur).deadline)
		return;
	if (cur == 0x46) {
		slot(0x46).step++;
	} else if (slot(cur).step != 0) {
		uint32 t = _r7Elapsed + _r7Start;
		while (t <= now) {
			slot(cur).step++;
			if (slot(_r7s46).sequenceLength <= slot(_r7s46).step) {
				slot(_r7s46).step = slot(_r7s46).sequenceLength;
				break;
			}
			int d = place(_r7s46, 0x46, true);
			_r7Elapsed += (int16)d * kMsPerStep;
			t = _r7Elapsed + _r7Start;
			cur = _r7s46;
		}
	} else {
		_r7Start = _game->now();
		_r7Elapsed = 0;
		slot(_r7s46).step++;
	}
	cur = _r7s46;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		targetRemove(cur);
		switch (_r7s46) {
		case 0x46:
		case 0x49:
		case 0x8e:
		case 0x91:
			_r7s46 = slotSwitch(0x46, cur);
			playAt(0x45, 0x45);
			cur = _r7s46;
			break;
		default:
			break;
		}
	}
	if (cur == 0x46) {
		targetAdd(0x46, true);
		cur = _r7s46;
	}
	int d = place(cur, 0x46, true);
	if (_r7s46 != 0x46)
		setDeadline(_r7s46, _r7Elapsed + _r7Start);
	else
		slot(0x46).deadline = now + (int16)d * kMsPerStep;
}

// 0x40ebb0: a show's sound ended; the next show loads (in order, then at random)
void AtticScene::r7SoundEnd() {
	release(0x49905c + _r7Variant * 0x24, 1);
	_r7Plays++;
	_r7Variant = _r7Plays > 2 ? (int)(_game->crtRandom() % 3) : _r7Plays;
	Room &r = _rooms[7];
	r.stages--;
	r.polled--;
	r.flags = (r.flags & ~kRoomLoaded) | kRoomLoading;
	if (r.stages == 0)
		submit(0x498f18, 6, 7);
	else if (r.stages == 1)
		submit(0x498ff0, 3, 7);
	else if (r.stages == 2)
		submit(0x49905c + _r7Variant * 0x24, 1, 7);
}

// ==== Room 8 (0x40ecb0..0x40f510) ====

// 0x40ecb0
void AtticScene::r8Loaded() {
	Room &r = _rooms[8];
	r.stages++;
	if (r.stages == 1 || r.stages == 2) {
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
	} else if (r.stages == 3) {
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x40ed20
int AtticScene::r8Load() {
	switch (_rooms[8].stages) {
	case 0:
		return submit(0x4990c8, 3, 8);
	case 1:
		return submit(0x499134, 3, 8);
	case 2:
		return submit(0x4991a0, 6, 8);
	default:
		return _rooms[8].stages - 2;
	}
}

// 0x40eef0
int AtticScene::r8Poll() {
	Room &r = _rooms[8];
	r.polled++;
	if (r.polled == 1) {
		if (resWait(0x4990c8))
			return 5;
		showBitmap(1, 8, 1, 30000, true);
		bmpSet(0x4dff7c, r.view[0], r.view[1]);
		bmpSet(0x4dff84, 0x541, 0x9bb);
		if (resWait(0x4990ec))
			return 5;
		showBitmap(1, 8, 3, 3, false);
		bmpPlace(0x4dff84);
		bmpVisible(0x4dff84, true);
		if (resWait(0x499110))
			return 5;
		showSlot(0xbf, 100, true);
		r.flags |= kRoomActive;
		_r8sBF = 0xbf;
		_r8State = 0;
	} else if (r.polled == 2) {
		bmpSet(0x4dff80, 0x3d8, 0x99e);
		if (resWait(0x499134))
			return 5;
		showBitmap(1, 8, 2, 5, false);
		bmpPlace(0x4dff80);
		if (resWait(0x499158))
			return 5;
		showSlot(0xc0, 100, false);
		return 0;
	} else if (r.polled == 3) {
		if (resWait(0x4991a0))
			return 5;
		showSlot(0x27, 4, false);
		if (resWait(0x4991c4))
			return 5;
		showSlot(0x28, 4, false);
		if (resWait(0x4991e8))
			return 5;
		showSlot(0x29, 4, false);
		_r8sCh = -1;
		return 0;
	}
	return 0;
}

// 0x40ed80
int AtticScene::r8Unload() {
	int n = _rooms[8].stages;
	if (n > 0) {
		bmpVisible(0x4dff7c, false);
		bmpVisible(0x4dff84, false);
		visible(0xbf, false);
	}
	if (n > 1) {
		bmpVisible(0x4dff80, false);
		visible(0xc0, false);
		stopSound(0xc0);
	}
	if (n > 2) {
		visible(0x27, false);
		visible(0x28, false);
		visible(0x29, false);
		stopSound(0x27);
		stopSound(0x28);
		stopSound(0x29);
	}
	_r8sBF = -1;
	_r8sCh = -1;
	int count = n == 1 ? 3 : n == 2 ? 6 : n == 3 ? 0xc : 0;
	release(0x4990c8, count);
	_rooms[8].flags = (_rooms[8].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x40f150
void AtticScene::r8Tick(uint32 now) {
	r8TickBF(now);
	r8TickChannel(now);
}

// 0x40f170: the switch (0xbf idle, 0xc0 pressed); each press turns the picture 0x4dff80 on or off
void AtticScene::r8TickBF(uint32 now) {
	if (_r8sBF == -1 || now <= slot(_r8sBF).deadline)
		return;
	slot(_r8sBF).step++;
	if (slot(_r8sBF).sequenceLength <= slot(_r8sBF).step) {
		targetRemove(_r8sBF);
		if (_r8sBF == 0xbf) {
			_r8sBF = slotSwitch(0xbf, _r8sBF);
			setStep(_r8sBF, 0);
			targetAdd(_r8sBF, true);
		} else if (_r8sBF == 0xc0) {
			if (_r8State == 0) {
				bmpVisible(0x4dff80, true);
				_r8State = 1;
				_r8sCh = -1;
			} else if (_r8State == 1) {
				bmpVisible(0x4dff80, false);
				_r8State = 0;
			} else if (_r8State == 2) {
				_inputLocked = 0;
				bmpVisible(0x4dff80, false);
				_r8sCh = slotSwitch(-1, _r8sCh);
				_r8State = 0;
			}
			_r8sBF = slotSwitch(0xbf, _r8sBF);
			setStep(_r8sBF, 0);
			targetAdd(_r8sBF, true);
		}
	}
	int d = place(_r8sBF, 0xbf, true);
	setDeadline(_r8sBF, now + (int16)d * kMsPerStep);
}

// 0x40f370: the channel picture (0x27 + channel), held on its last step
void AtticScene::r8TickChannel(uint32 now) {
	if (_r8sCh == -1 || slot(_r8sCh).deadline >= now)
		return;
	slot(_r8sCh).step++;
	int16 cur = _r8sCh;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		_r8sCh = slotSwitch(cur, cur);
		slot(_r8sCh).step = slot(_r8sCh).sequenceLength - 1;
		if (_r8State == 1) {
			_r8sBF = slotSwitch(0xc0, _r8sBF);
			int d = place(_r8sBF, 0xbf, true);
			setDeadline(_r8sBF, _game->now() + (int16)d * kMsPerStep);
			_r8State = 2;
			playAt(0xc0);
		}
		cur = _r8sCh;
	}
	if (cur != -1) {
		int d = place(cur, cur, true);
		setDeadline(_r8sCh, now + (int16)d * kMsPerStep);
	}
}

// ==== Room 9 (0x40f510..0x40fea0) ====

// 0x40f510
void AtticScene::r9Loaded() {
	Room &r = _rooms[9];
	r.stages++;
	if (r.stages == 1) {
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
	} else if (r.stages == 2) {
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x40f570
int AtticScene::r9Load() {
	if (_rooms[9].stages == 0)
		return submit(0x499278, 3, 9);
	if (_rooms[9].stages == 1)
		return submit(0x4992e4, 4, 9);
	return _rooms[9].stages - 1;
}

// 0x40f700
int AtticScene::r9Poll() {
	Room &r = _rooms[9];
	r.polled++;
	int32 zero[4] = { 0, 0, 0, 0 };
	if (r.polled == 1) {
		if (resWait(0x499278))
			return 5;
		showBitmap(1, 9, 1, 30000, true);
		bmpSet(0x4e010c, r.view[0], r.view[1]);
		if (resWait(0x49929c))
			return 5;
		showSlot(0x2b, 3, false);
		if (resWait(0x4992c0))
			return 5;
		showSlot(0x82, 100, false);
		_r9s2B = 0x2b;
		_r9s82 = 0x82;
		_r9Region = addRegion(zero, 0x80000, 0x8f);
		_world.setUser(&_userMap);
		r.flags |= kRoomActive;
		_toRoom25 = 0;
	} else if (r.polled == 2) {
		if (resWait(0x4992e4))
			return 5;
		showSlot(0x2c, 3, false);
		if (resWait(0x49932c))
			return 5;
		showSlot(0x2a, 100, false);
		targetAdd(_r9s2B, true);
		_r9Time = 0;
		return 0;
	}
	return 0;
}

// 0x40f5c0
int AtticScene::r9Unload() {
	int n = _rooms[9].stages;
	if (n > 0) {
		bmpVisible(0x4e010c, false);
		visible(0x2b, false);
		visible(0x82, false);
	}
	if (n > 1) {
		visible(0x2c, false);
		visible(0x2a, false);
	}
	_r9s2B = -1;
	_r9s82 = -1;
	if (_r9Region)
		removeRegion(_r9Region);
	stopSound(0x2c);
	stopSound(0x2a);
	int count = n == 1 ? 3 : n == 2 ? 7 : 0;
	release(0x499278, count);
	_rooms[9].flags = (_rooms[9].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x40f8a0: also brings Ned out in room 25 after he went down the hole (region 0x3f9)
void AtticScene::r9Tick(uint32 now) {
	r9Tick2B(now);
	r9Tick82(now);
	if (_toRoom25 && _rooms[25].polled > 0) {
		const Room &r25 = _rooms[25];
		_jumpX = (0x27f - _scrollX) - r25.view[2];
		_nedPos.x = r25.view[2] - 0x32;
		_cameraJump = 1;
		_jumpY = _scrollY + r25.view[1];
		for (int16 y = (int16)r25.view[3]; r25.view[1] <= y; y--) {
			if (solidAt(kBaseMaps, _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
		if (_rooms[25].polled > 0 && _r25s184 != 0x185) {
			targetRemove(_r25s184);
			_r25s184 = slotSwitch(0x185, _r25s184);
			int d = place(_r25s184, 0x184, true);
			setDeadline(_r25s184, _game->now() + (int16)d * kMsPerStep);
			_r25Time184 = slot(_r25s184).deadline + 6000;
			if (_r25Region)
				removeRegion(_r25Region);
			ned()->enable(true);
			autoWalk(kInputLeft, 0, _rooms[25].view[2] - 0x96, 0);
			_toRoom25 = 0;
			playAt(0x185, 0x2c);
		}
	}
}

// 0x40fad0: the hole cover (0x2b, 0x2c when hit); its box is region 0x8f
void AtticScene::r9Tick2B(uint32 now) {
	if (_r9s2B == -1 || now <= slot(_r9s2B).deadline)
		return;
	int32 zero[4] = { 0, 0, 0, 0 };
	slot(_r9s2B).step++;
	int16 cur = _r9s2B;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		targetRemove(_r9s2B);
		if (cur == 0x2b) {
			_r9s2B = slotSwitchPre(0x2b, cur);
			setStep(_r9s2B, 0);
			targetAdd(_r9s2B, true);
			if (!_r9Region) {
				_r9Region = addRegion(zero, 0x80000, 0x8f);
				_world.setUser(&_userMap);
			}
			cur = _r9s2B;
		} else if (cur == 0x2c) {
			if (_r9Time < now) {
				_r9s2B = slotSwitch(0x2b, cur);
			} else {
				_r9s2B = slotSwitch(0x2c, cur);
				playAt(0x2c);
			}
			setStep(_r9s2B, 0);
			targetAdd(_r9s2B, true);
			cur = _r9s2B;
		}
	}
	int d = 0;
	if (_r9Region)
		r9MoveRegion(_r9Region, cur, 0x2b);
	d = place(cur, 0x2b, true);
	setDeadline(_r9s2B, now + (int16)d * kMsPerStep);
}

// A sprite's box 0 (at its attached position against ref) moves region id, as type 0x20000 (0x487780)
void AtticScene::r9MoveRegion(int32 id, int16 s, int16 ref) {
	int32 rect[4];
	stepRect(s, 0, rect);
	int pos[2];
	attachPos(s, ref, pos, true);
	rect[0] += pos[0];
	rect[2] += pos[0];
	rect[3] += pos[1];
	rect[1] += pos[1];
	setRegion(id, rect, 0x20000);
}

// 0x40fda0: the flap (0x82, 0x2a when the region 0x3eb springs it)
void AtticScene::r9Tick82(uint32 now) {
	if (_r9s82 == -1 || slot(_r9s82).deadline >= now)
		return;
	slot(_r9s82).step++;
	int16 cur = _r9s82;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		if (cur == 0x2a) {
			_r9s82 = slotSwitch(0x2a, cur);
			slot(_r9s82).step = slot(_r9s82).sequenceLength - 1;
			cur = _r9s82;
		} else if (cur == 0x82) {
			cur = _r9s82 = slotSwitch(0x82, cur);
		}
	}
	int d = place(cur, 0x82, true);
	setDeadline(_r9s82, now + (int16)d * kMsPerStep);
}

// ==== Room 10 (0x40feb0..0x410f20) ====

// 0x40feb0
void AtticScene::r10Loaded() {
	Room &r = _rooms[10];
	r.stages++;
	if (r.stages == 1) {
		if (!(r.flags & kRoomCancel)) {
			if (!_r10Done) {
				r.flags |= kRoomMore;
				goto done;
			}
			r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
			goto done;
		}
	} else if (r.stages == 2) {
		if (!(r.flags & kRoomCancel)) {
			r.flags |= kRoomMore;
			goto done;
		}
	} else {
		if (r.stages == 3)
			r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
		goto done;
	}
	r.flags |= kRoomAbort;
done:
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x40ff40
int AtticScene::r10Load() {
	switch (_rooms[10].stages) {
	case 0:
		return submit(0x499378, 8, 10);
	case 1:
		return submit(0x499498, 6, 10);
	case 2:
		return submit(0x499570, 4, 10);
	default:
		return _rooms[10].stages - 2;
	}
}

// 0x4101b0
int AtticScene::r10Poll() {
	Room &r = _rooms[10];
	r.polled++;
	if (r.polled == 1) {
		if (resWait(0x499378))
			return 5;
		showBitmap(1, 10, 1, 30000, true);
		bmpSet(0x4e029c, r.view[0], r.view[1]);
		if (resWait(0x49939c))
			return 5;
		bmpSet(0x4e02a0, 0xaa4, 0xb13);
		showBitmap(1, 10, 2, 3, false);
		bmpPlace(0x4e02a0);
		if (resWait(0x4993c0))
			return 5;
		showSlot(0x1c, 100, true);
		if (resWait(0x499408))
			return 5;
		showSlot(0xf, 100, false);
		if (resWait(0x499450))
			return 5;
		showSlot(0x1e, 0x5a, false);
		_r10s1C = 0x1c;
		_r10Time1C = 0;
		if (_r10Done) {
			bmpVisible(0x4e02a0, true);
			_r10s1D = 0x1e;
			r.flags |= kRoomActive;
		}
	} else if (r.polled == 2) {
		if (resWait(0x499498))
			return 5;
		showSlot(0x1d, 0x78, true);
		if (resWait(0x4994bc))
			return 5;
		showSlot(0x16, 100, true);
		if (resWait(0x4994e0))
			return 5;
		showSlot(0x20, 0x6e, false);
		if (resWait(0x499528))
			return 5;
		showSlot(0x1b, 100, false);
		_r10s1D = 0x1d;
		_r10sA = 0x16;
		_r10Time16 = 0;
		targetAdd(_r10s1C, true);
		targetAdd(_r10sA, true);
		_r10sB = -1;
		r.flags |= kRoomActive;
		return 0;
	} else if (r.polled == 3) {
		if (resWait(0x499570))
			return 5;
		showSlot(0x10, 100, false);
		if (resWait(0x499594))
			return 5;
		showSlot(0x1a, 100, false);
		if (resWait(0x4995b8))
			return 5;
		showSlot(0x18, 100, false);
		_r10sB = -1;
		return 0;
	}
	_r10sB = -1;
	return 0;
}

// 0x40ffa0
int AtticScene::r10Unload() {
	int n = _rooms[10].stages;
	if (n > 0) {
		bmpVisible(0x4e029c, false);
		bmpVisible(0x4e02a0, false);
		visible(0x1c, false);
		visible(0xf, false);
		visible(0x1e, false);
		stopSound(0xf);
		stopSound(0x1e);
		stopSound(0x1c);
		if (!_r10Done) {
			_rooms[10].exits[1] = -1;
		} else {
			_rooms[10].exits[1] = 0xd;
			_rooms[2].exits[0] = 0xd;
		}
	}
	if (n > 1) {
		visible(0x1d, false);
		visible(0x16, false);
		visible(0x20, false);
		visible(0x1b, false);
		stopSound(0x20);
		stopSound(0x1b);
	}
	if (n > 2) {
		visible(0x10, false);
		visible(0x18, false);
		visible(0x1a, false);
		stopSound(0x18);
	}
	_r10s1C = _r10sA = _r10s1D = _r10sB = -1;
	int count = n == 1 ? 8 : n == 2 ? 0xe : n == 3 ? 0x12 : 0;
	release(0x499378, count);
	_rooms[10].flags = (_rooms[10].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x4104e0
void AtticScene::r10Tick(uint32 now) {
	r10Tick1C(now);
	r10TickB(now);
	r10TickA(now);
	r10Tick1D(now);
	if (_r10Return) {
		_r10s1C = slotSwitch(0xf, _r10s1C);
		int d = place(_r10s1C, 0x1c, true);
		setDeadline(_r10s1C, _game->now() + (int16)d * kMsPerStep);
		_r10Back = 1;
		_r10Return = 0;
		playAt(0xf, 0x1c);
	}
	if (_r10Enter && (_rooms[10].flags & kRoomActive)) {
		// Ned comes up through the floor of room 10 (regions 0x3f7 / 0x3fc)
		const Room &r10 = _rooms[10];
		_inputLocked = 1;
		ned()->enable(false);
		_nedPos.x = r10.view[0] + 300;
		_jumpX = -(_scrollX + r10.view[0]);
		setFacing(1);
		_jumpY = -(_scrollY + r10.view[1]);
		_cameraJump = 1;
		for (int16 y = (int16)r10.view[3]; r10.view[1] <= y; y--) {
			if (solidAt(kBaseMaps + 4 * ((_r10Open != 0) + 6), _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
		_r10Enter = 0;
		_r10Return = 1;
	}
}

// 0x4106d0: the trapdoor guard (0x1c, 0x1b, 0xf when hit)
void AtticScene::r10Tick1C(uint32 now) {
	if (_r10s1C == -1 || now <= slot(_r10s1C).deadline)
		return;
	if (!_r10Back) {
		if (_r10Done && _r10s1C == 0xf && slot(0xf).step == 3) {
			_r10sB = slotSwitch(0x10, _r10sB);
			setStep(_r10sB, 0);
		}
	} else if (_r10s1C == 0xf && slot(0xf).step == 3) {
		ned()->enable(true);
		Common::Point p = _nedPos;
		ned()->playAction(0x38, p, _scrollX, _scrollY);
		_r10Back = 0;
		_inputLocked = 0;
	}
	slot(_r10s1C).step++;
	int16 cur = _r10s1C;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		targetRemove(_r10s1C);
		if (cur == 0xf) {
			_r10s1C = slotSwitch(0x1c, cur);
			playAt(0x1c);
			targetAdd(_r10s1C, true);
			cur = _r10s1C;
		} else if (cur == 0x1b) {
			_r10s1C = slotSwitch(0x1c, cur);
			_r10Time1C = _game->crtRandom() % 5000 + 5000 + now;
			targetAdd(_r10s1C, true);
			playAt(0x1c);
			cur = _r10s1C;
		} else if (cur == 0x1c) {
			if (_r10Time1C < now && _rooms[10].polled > 1) {
				_r10s1C = slotSwitch(0x1b, cur);
				playAt(0x1b);
			} else {
				_r10s1C = slotSwitch(0x1c, cur);
				playAt(0x1c, 0x1c);
			}
			targetAdd(_r10s1C, true);
			cur = _r10s1C;
		}
	}
	int d = 0;
	if (cur != -1) {
		d = place(cur, 0x1c, true);
		cur = _r10s1C;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x4109d0: the creature 0x16 (0x20 and 0x17 its moves, 0x18 the pull)
void AtticScene::r10TickA(uint32 now) {
	if (_r10sA == -1 || now <= slot(_r10sA).deadline)
		return;
	if (_r10sA == 0x18 && slot(0x18).step == 9) {
		_r10sB = slotSwitch(-1, _r10sB);
		_r10s1D = slotSwitch(0x1e, _r10s1D);
		playAt(0x1e);
	}
	slot(_r10sA).step++;
	int16 cur = _r10sA;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		targetRemove(_r10sA);
		switch (_r10sA) {
		case 0x16:
			if (_r10Time16 < now && _rooms[10].polled > 1) {
				_r10sA = slotSwitch(0x20, cur);
				_r10s1D = slotSwitch(-1, _r10s1D);
				setDeadline(_r10s1D, now - 1);
				playAt(0x20);
			} else {
				_r10sA = slotSwitch(0x16, cur);
				_r10s1D = slotSwitch(0x1d, _r10s1D);
				setDeadline(_r10s1D, now - 1);
			}
			targetAdd(_r10sA, true);
			cur = _r10sA;
			break;
		case 0x17:
		case 0x20:
			_r10sA = slotSwitch(0x16, cur);
			_r10Time16 = _game->crtRandom() % 2000 + 2000 + now;
			targetAdd(_r10sA, true);
			_r10s1D = slotSwitch(0x1d, _r10s1D);
			setDeadline(_r10s1D, now - 1);
			cur = _r10sA;
			break;
		case 0x18:
			_r10sA = slotSwitch(0x18, cur);
			setStep(_r10sA, 10);
			targetAdd(_r10sA, true);
			playAt(0x18);
			cur = _r10sA;
			break;
		default:
			break;
		}
	}
	int d = 0;
	if (cur != -1) {
		d = place(cur, cur, true);
		cur = _r10sA;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x410d30: the floor flap (0x1d, 0x1e open); opening it swaps base maps 6 and 7
void AtticScene::r10Tick1D(uint32 now) {
	if (_r10s1D == -1 || slot(_r10s1D).deadline >= now)
		return;
	slot(_r10s1D).step++;
	int16 cur = _r10s1D;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		if (cur == 0x1d) {
			cur = _r10s1D = slotSwitch(0x1d, cur);
		} else if (cur == 0x1e) {
			_r10s1D = slotSwitch(0x1e, cur);
			slot(_r10s1D).step = slot(_r10s1D).sequenceLength - 1;
			if (!_r10Open) {
				removeBaseMap(6);
				addBaseMap(7);
				for (int16 y = (int16)_rooms[10].view[3]; _rooms[10].view[1] <= y; y--) {
					if (solidAt(0x52595c, _nedPos.x, y)) {
						_nedPos.y = y;
						break;
					}
				}
				bmpVisible(0x4e02a0, true);
				_r10Open = 1;
				if (!_hint5Shown) {
					_hint5Shown = 1;
					hint(5, facing() != 0);
				}
			}
			cur = _r10s1D;
			_viewRoomX = -1;
			_inputLocked = 0;
		}
	}
	int d = place(cur, 0x1d, true);
	setDeadline(_r10s1D, now + (int16)d * kMsPerStep);
}

// 0x410f20: 0x10 then 0x1a; on 0x10's step 2 the creature is pulled (0x18)
void AtticScene::r10TickB(uint32 now) {
	if (_r10sB == -1 || slot(_r10sB).deadline >= now)
		return;
	if (_r10sB == 0x10 && slot(0x10).step == 2) {
		_r10sA = slotSwitch(0x18, _r10sA);
		playAt(0x18);
	}
	slot(_r10sB).step++;
	int16 cur = _r10sB;
	if (slot(cur).sequenceLength <= slot(cur).step && (cur == 0x10 || cur == 0x1a))
		cur = _r10sB = slotSwitch(0x1a, cur);
	int d = 0;
	if (cur != -1) {
		d = place(cur, cur, true);
		cur = _r10sB;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// ==== Room 11 (0x411060..0x4111a0) ====

// 0x411060
void AtticScene::r11Loaded() {
	_rooms[11].stages++;
	_rooms[11].flags = (_rooms[11].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x4110a0
int AtticScene::r11Load() {
	return submit(0x499600, 2, 0xb);
}

// 0x411130
int AtticScene::r11Poll() {
	_rooms[11].polled++;
	if (resWait(0x499600))
		return 5;
	showBitmap(1, 0xb, 1, 30000, true);
	bmpSet(0x4e042c, _rooms[11].view[0], _rooms[11].view[1]);
	_r11Time = _game->now();
	_rooms[11].flags |= kRoomActive;
	return 0;
}

// 0x4110c0
int AtticScene::r11Unload() {
	bmpVisible(0x4e042c, false);
	stopSound(0x4d);
	release(0x499600, 2);
	_rooms[11].flags = (_rooms[11].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x4111a0: an ambient sound every 5.5 s
void AtticScene::r11Tick(uint32 now) {
	if (_r11Time < now) {
		playAt(0x4d);
		_r11Time = now + 0x15b4;
	}
}

// ==== Room 12 (0x4111e0..0x412930): below the trapdoor ====

// 0x4111e0
void AtticScene::r12Loaded() {
	Room &r = _rooms[12];
	int old = r.stages++;
	switch (old) {
	case 0:
	case 1:
	case 2:
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
		break;
	case 3:
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
		break;
	default:
		break;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x411270
int AtticScene::r12Load() {
	switch (_rooms[12].stages) {
	case 0:
		return submit(0x499668, 10, 0xc);
	case 1:
		return submit(0x4997d0, 5, 0xc);
	case 2:
		return submit(0x499884, 7, 0xc);
	case 3:
		return submit(0x499980, 8, 0xc);
	default:
		return _rooms[12].stages;
	}
}

// 0x411640
int AtticScene::r12Poll() {
	Room &r = _rooms[12];
	int stage = r.polled++;
	switch (stage) {
	case 0:
		if (resWait(0x499668))
			return 5;
		showBitmap(1, 0xc, 1, 30000, true);
		bmpSet(0x4e05bc, r.view[0], r.view[1]);
		bmpSet(0x4e05cc, 0xfd9, 0xc76);
		if (resWait(0x49968c))
			return 5;
		showBitmap(1, 0xc, 5, 3, false);
		bmpPlace(0x4e05cc);
		bmpVisible(0x4e05cc, true);
		bmpSet(0x4e05d0, 0xecb, 0xb76);
		if (resWait(0x4996b0))
			return 5;
		showBitmap(1, 0xc, 6, 3, false);
		bmpPlace(0x4e05d0);
		bmpVisible(0x4e05d0, true);
		if (resWait(0x499740))
			return 5;
		showSlot(0x32, 0x5a, true);
		_r12s32 = 0x32;
		_r12Time32 = 0;
		if (resWait(0x499764))
			return 5;
		showSlot(0x36, 0x6e, true);
		_r12s36 = 0x36;
		_r12Time36 = 0;
		_r12Open36 = 0;
		if (resWait(0x499788))
			return 5;
		showSlot(0x95, 100, true);
		_r12s95 = 0x95;
		_r12Time95 = 0;
		_r12Open95 = 0;
		if (resWait(0x4997ac))
			return 5;
		showSlot(0x39, 0x69, false);
		_r12s39 = 0x39;
		r.flags |= kRoomActive;
		_r12Time39 = 0;
		_r12Open39 = 0;
		_r12Touched = 0;
		_r12Armed = 1;
		_r12HintFlag = 0;
		return 0;
	case 1: {
		static const struct {
			uint32 entry;
			int id, z;
		} kStage[] = { { 0x4997d0, 0x33, 0x5a }, { 0x499818, 0x38, 0x6e }, { 0x49983c, 0x3e, 100 }, { 0x499860, 0x94, 0x69 } };
		for (const auto &e : kStage) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, false);
		}
		return 0;
	}
	case 2: {
		static const struct {
			uint32 entry;
			int id, z;
		} kStage[] = {
			{ 0x499884, 0x34, 0x5a }, { 0x4998cc, 0x35, 0x5a }, { 0x4998f0, 0x37, 0x6e }, { 0x499914, 0x98, 100 },
			{ 0x49995c, 0x3a, 0x69 }
		};
		for (const auto &e : kStage) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, false);
		}
		return 0;
	}
	case 3:
		_r12Count = 0;
		return 0;
	default:
		return 0;
	}
}

// 0x411300
int AtticScene::r12Unload() {
	int n = _rooms[12].stages;
	if (n > 0) {
		bmpVisible(0x4e05bc, false);
		bmpVisible(0x4e05cc, false);
		bmpVisible(0x4e05d0, false);
		visible(0x32, false);
		visible(0x36, false);
		visible(0x95, false);
		visible(0x39, false);
		if (_r12Open36 && _r12MapAAdded) {
			removeMap(map(0x525740));
			_r12MapAAdded = 0;
		}
		if (_r12Open39 && _r12MapBAdded) {
			removeMap(map(0x525744));
			_r12MapBAdded = 0;
		}
		if (_r12Open95 && _r12MapCAdded) {
			removeMap(map(0x525748));
			_r12MapCAdded = 0;
		}
	}
	if (n > 1) {
		visible(0x33, false);
		visible(0x38, false);
		visible(0x3e, false);
		visible(0x94, false);
		stopSound(0x33);
	}
	if (n > 2) {
		visible(0x34, false);
		visible(0x35, false);
		visible(0x37, false);
		visible(0x98, false);
		visible(0x3a, false);
		stopSound(0x34);
		stopSound(0x98);
	}
	if (n > 3) {
		for (int id = 0xf1; id < 0xf9; id++)
			stopSound(id);
	}
	_r12s32 = _r12s36 = _r12s95 = _r12s39 = -1;
	if (_r12RegionA)
		removeRegion(_r12RegionA);
	if (_r12RegionB)
		removeRegion(_r12RegionB);
	if (_r12RegionC)
		removeRegion(_r12RegionC);
	int count = n == 1 ? 10 : n == 2 ? 0xf : n == 3 ? 0x16 : n == 4 ? 0x1e : 0;
	release(0x499668, count);
	_rooms[12].flags = (_rooms[12].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x411a50
void AtticScene::r12Tick(uint32 now) {
	if (!_r12Armed && _r12Time700 < now) {
		// The three monsters wake up after Ned arrives
		if (!_r12Open36) {
			_r12Time36 = now + 0x1194;
			_r12Arm36 = 1;
		}
		if (!_r12Open39) {
			_r12Time39 = now + 4000;
			_r12Arm39 = 1;
		}
		if (!_r12Open95) {
			_r12Time95 = now + 0xdac;
			_r12Arm95 = 1;
		}
		_r12Armed = 1;
	}
	if (_r12Exit && (_rooms[2].flags & kRoomActive)) {
		// The way back up (region 0x3fd) comes out in room 2
		const Room &r2 = _rooms[2];
		_jumpX = (-0x189 - _scrollX) - r2.view[0];
		_cameraJump = 1;
		_jumpY = -(_scrollY + r2.view[1]);
		_nedPos.x = r2.view[2] - 300;
		setFacing(0);
		for (int16 y = (int16)r2.view[3]; r2.view[1] <= y; y--) {
			if (solidAt(kBaseMaps, _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
		_r12Exit = 0;
		_r2s14 = slotSwitch(0x14, _r2s14);
		int d = place(_r2s14, 0x14, true);
		setDeadline(_r2s14, _game->now() + (int16)d * kMsPerStep);
		if (!_r12Escaped) {
			_r12Escaped = 1;
			_r12HintFlag = 1;
		}
		_r12HintTime = now + 3000;
		_r12Hint = 1;
		_inputLocked = 1;
	}
	r12Tick32(now);
	r12Tick36(now);
	r12Tick39(now);
	r12Tick95(now);
}

// 0x411c70: the drummer (0x32; 0x33/0x35 rests, 0x34 a hit, then a drum sound from 0xf1..0xf8)
void AtticScene::r12Tick32(uint32 now) {
	if (_r12s32 == -1 || now <= slot(_r12s32).deadline)
		return;
	slot(_r12s32).step++;
	int16 cur = _r12s32;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		targetRemove(_r12s32);
		bool add = true;
		switch (_r12s32) {
		case 0x32:
			if (_r12Time32 < now && _rooms[12].polled > 1) {
				_r12s32 = slotSwitch(0x33, cur);
				playAt(0x33);
			} else {
				_r12s32 = slotSwitch(0x32, cur);
			}
			setStep(_r12s32, 0);
			break;
		case 0x33:
		case 0x35:
			_r12s32 = slotSwitch(0x32, cur);
			_r12Time32 = _game->crtRandom() % 7000 + now;
			break;
		case 0x34:
			if (now < _r12Time) {
				_r12s32 = slotSwitch(0x34, cur);
				playAt(0x34);
			} else {
				_r12s32 = slotSwitch(0x35, cur);
			}
			add = false;
			break;
		default:
			add = false;
			break;
		}
		if (add)
			targetAdd(_r12s32, true);
		cur = _r12s32;
	}
	int d = place(cur, 0x32, true);
	setDeadline(_r12s32, now + (int16)d * kMsPerStep);
}

// 0x411e70: the monster 0x36 (0x38 attacking, 0x37 when Ned's region touches it)
void AtticScene::r12Tick36(uint32 now) {
	if (_r12s36 == -1 || now <= slot(_r12s36).deadline)
		return;
	int32 zero[4] = { 0, 0, 0, 0 };
	slot(_r12s36).step++;
	int16 cur = _r12s36;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		bool idle = false;
		if (cur == 0x36) {
			if (_r12Arm36 && _r12Time36 <= now) {
				_r12s36 = slotSwitch(0x38, cur);
				int d = place(_r12s36, 0x36, true);
				setDeadline(_r12s36, _game->now() + (int16)d * kMsPerStep);
				if (!_r12RegionA) {
					_r12RegionA = addRegion(zero, 0x80000, 0x9a);
					_world.setUser(&_userMap);
				}
				cur = _r12s36;
				_r12Arm36 = 0;
			} else {
				idle = true;
			}
		} else if (cur == 0x37) {
			targetRemove(_r12s36);
			_r12s36 = slotSwitch(0x37, cur);
			targetAdd(_r12s36, true);
			cur = _r12s36;
		} else if (cur == 0x38) {
			if (!_r12Open36) {
				targetRemove(_r12s36);
				_r12s36 = slotSwitch(0x37, cur);
				targetAdd(_r12s36, true);
				cur = _r12s36;
			} else {
				idle = true;
			}
		}
		if (idle)
			cur = _r12s36 = slotSwitch(0x36, cur);
	}
	int d = place(cur, 0x36, true);
	if (_r12s36 == 0x37 && _r12Touched && !_r12Open36) {
		ned()->enable(true);
		playAction(0x39);
		_r12Touched = 0;
	}
	if (_r12RegionA)
		r12MoveRegion(_r12RegionA, _r12s36, 0x36, d);
	setDeadline(_r12s36, now + (int16)d * kMsPerStep);
}

// The box of s (its current step, attached against ref) becomes region id's rect (0x4877f0)
void AtticScene::r12MoveRegion(int32 id, int16 s, int16 ref, int &duration) {
	int32 rect[4];
	stepRect(s, 0, rect);
	Slot &cs = slot(s);
	int frame = 0;
	if (cs.sequence)
		cs.sequence->getStep(cs.step, frame, duration);
	int pos[2];
	attachPos(s, ref, pos, true);
	rect[0] += pos[0];
	rect[2] += pos[0];
	rect[3] += pos[1];
	rect[1] += pos[1];
	for (uint i = 0; i < _userMap.regions.size(); i++) {
		if (_userMap.regions[i].id == id) {
			Common::Rect &r = _userMap.regions[i].rect;
			r.left = rect[0];
			r.top = rect[1];
			r.right = rect[2];
			r.bottom = rect[3];
			break;
		}
	}
}

// 0x4121d0: the monster 0x39 (0x94 attacking, 0x3a)
void AtticScene::r12Tick39(uint32 now) {
	int32 zero[4] = { 0, 0, 0, 0 };
	if (_r12s39 == -1 || now <= slot(_r12s39).deadline)
		return;
	slot(_r12s39).step++;
	int16 cur = _r12s39;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		if (cur == 0x39) {
			if (!_r12Arm39 || now < _r12Time39) {
				cur = _r12s39 = slotSwitchPre(0x39, cur);
			} else {
				_r12s39 = slotSwitch(0x94, cur);
				int d = place(_r12s39, 0x39, true);
				setDeadline(_r12s39, _game->now() + (int16)d * kMsPerStep);
				if (!_r12RegionB) {
					_r12RegionB = addRegion(zero, 0x80000, 0x9d);
					_world.setUser(&_userMap);
				}
				cur = _r12s39;
				_r12Arm39 = 0;
			}
		} else if (cur == 0x3a) {
			targetRemove(_r12s39);
			_r12s39 = slotSwitchPre(0x3a, cur);
			targetAdd(_r12s39, true);
			cur = _r12s39;
		} else if (cur == 0x94) {
			if (_r12Open39) {
				cur = _r12s39 = slotSwitch(0x39, cur);
			} else {
				targetRemove(_r12s39);
				_r12s39 = slotSwitchPre(0x3a, cur);
				targetAdd(_r12s39, true);
				cur = _r12s39;
			}
		}
		setStep(cur, 0);
		cur = _r12s39;
	}
	int d = place(cur, 0x39, true);
	if (_r12s39 == 0x3a && _r12Touched && !_r12Open39) {
		ned()->enable(true);
		playAction(0x39);
		_r12Touched = 0;
	}
	if (_r12RegionB)
		r12MoveRegion(_r12RegionB, _r12s39, 0x39, d);
	setDeadline(_r12s39, now + (int16)d * kMsPerStep);
}

// 0x412560: the monster 0x95 (0x3e attacking, 0x98)
void AtticScene::r12Tick95(uint32 now) {
	int32 zero[4] = { 0, 0, 0, 0 };
	if (_r12s95 == -1 || now <= slot(_r12s95).deadline)
		return;
	slot(_r12s95).step++;
	int16 cur = _r12s95;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		if (cur == 0x3e) {
			if (!_r12Open95) {
				targetRemove(_r12s95);
				_r12s95 = slotSwitchPre(0x98, cur);
				targetAdd(_r12s95, true);
				playAt(0x98, 0x98);
			} else {
				_r12s95 = slotSwitchPre(0x95, cur);
			}
		} else if (cur == 0x95) {
			if (!_r12Arm95 || now < _r12Time95) {
				_r12s95 = slotSwitchPre(0x95, cur);
			} else {
				_r12s95 = slotSwitch(0x3e, cur);
				int d = place(_r12s95, 0x95, true);
				setDeadline(_r12s95, _game->now() + (int16)d * kMsPerStep);
				if (!_r12RegionC) {
					_r12RegionC = addRegion(zero, 0x80000, 0xf9);
					_world.setUser(&_userMap);
				}
				_r12Arm95 = 0;
			}
		} else if (cur == 0x98) {
			targetRemove(_r12s95);
			_r12s95 = slotSwitchPre(0x98, cur);
			targetAdd(_r12s95, true);
			playAt(0x98, 0x98);
		}
		cur = _r12s95;
		setStep(cur, 0);
	}
	int d = place(cur, 0x95, true);
	if (_r12s95 == 0x98 && _r12Touched && !_r12Open95) {
		ned()->enable(true);
		playAction(0x39);
		_r12Touched = 0;
	}
	if (_r12RegionC)
		r12MoveRegion(_r12RegionC, _r12s95, 0x95, d);
	setDeadline(_r12s95, now + (int16)d * kMsPerStep);
}

// ==== Room 13 (0x412930..0x413050): the cellar below room 10 ====

// 0x412930
void AtticScene::r13Loaded() {
	Room &r = _rooms[13];
	r.stages++;
	if (r.stages == 1) {
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
	} else if (r.stages == 2) {
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x412990
int AtticScene::r13Load() {
	if (_rooms[13].stages == 0)
		return submit(0x499aa0, 4, 0xd);
	if (_rooms[13].stages == 1)
		return submit(0x499b30, 7, 0xd);
	return _rooms[13].stages - 1;
}

// 0x412b10
int AtticScene::r13Poll() {
	Room &r = _rooms[13];
	r.polled++;
	if (r.polled == 1) {
		if (resWait(0x499aa0))
			return 5;
		showBitmap(1, 0xd, 1, 30000, true);
		bmpSet(0x4e074c, r.view[0], r.view[1]);
		if (resWait(0x499ac4))
			return 5;
		bmpSet(0x4e0750, 0xbae, 0xe14);
		showBitmap(1, 0xd, 2, 3, false);
		bmpPlace(0x4e0750);
		bmpVisible(0x4e0750, true);
		if (resWait(0x499ae8))
			return 5;
		bmpSet(0x4e0754, 0x9ee, 0xdfc);
		showBitmap(1, 0xd, 3, 4, false);
		bmpPlace(0x4e0754);
		bmpVisible(0x4e0754, true);
		if (resWait(0x499b0c))
			return 5;
		showSlot(0x47, 100, true);
		r.flags |= kRoomActive;
		_r13s47 = 0x47;
		_r13sBall = -1;
	} else if (r.polled == 2) {
		if (resWait(0x499b30))
			return 5;
		showSlot(0x48, 100, false);
		if (resWait(0x499b78))
			return 5;
		showSlot(1, 5, false);
		if (resWait(0x499bc0))
			return 5;
		showSlot(0x96, 5, false);
		if (resWait(0x499be4))
			return 5;
		showSlot(0x97, 3, false);
		_r13TimeBall = 0xffffffff;
		_r13BallHit = 0;
		return 0;
	}
	return 0;
}

// 0x4129e0
int AtticScene::r13Unload() {
	int n = _rooms[13].stages;
	if (n > 0) {
		bmpVisible(0x4e074c, false);
		bmpVisible(0x4e0750, false);
		bmpVisible(0x4e0754, false);
		visible(0x47, false);
	}
	if (n > 1) {
		visible(0x48, false);
		visible(1, false);
		visible(0x96, false);
		visible(0x97, false);
		stopSound(0x48);
		stopSound(1);
		stopSound(0x97);
	}
	int count = n == 1 ? 4 : n == 2 ? 0xb : 0;
	release(0x499aa0, count);
	_rooms[13].flags = (_rooms[13].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x412d80
void AtticScene::r13Tick(uint32 now) {
	if (_r13TimeBall == 0xffffffff && ned()->getState() != 0x12)
		_r13TimeBall = _game->crtRandom() % 8000 + now;
	r13Tick47(now);
	r13TickBall(now);
}

// Ned's box against a rect (IntersectRect, non-empty)
bool AtticScene::nedTouches(const int32 rect[4]) {
	int32 box[4];
	ned()->getBox(ned()->getCurSlot(), _nedPos, box);
	return MAX(box[0], rect[0]) < MIN(box[2], rect[2]) && MAX(box[1], rect[1]) < MIN(box[3], rect[3]);
}

// A slot's box k at its position attached against ref, in world coordinates
void AtticScene::attachedBox(int16 s, int16 ref, int k, int32 rect[4]) {
	stepRect(s, k, rect);
	int pos[2];
	attachPos(s, ref, pos, true);
	rect[0] += pos[0];
	rect[2] += pos[0];
	rect[3] += pos[1];
	rect[1] += pos[1];
}

// 0x412dd0: the roller (0x47; 0x48 rolls the ball); touching it knocks Ned over
void AtticScene::r13Tick47(uint32 now) {
	if (_r13s47 == -1 || now <= slot(_r13s47).deadline)
		return;
	slot(_r13s47).step++;
	int16 cur = _r13s47;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		targetRemove(_r13s47);
		if (cur == 0x47) {
			if (_r13Time47 < now && _rooms[13].polled > 1) {
				_r13s47 = slotSwitch(0x48, cur);
				playAt(0x48, 0x48);
			}
			setStep(_r13s47, 0);
			targetAdd(_r13s47, true);
			cur = _r13s47;
		} else if (cur == 0x48) {
			_r13s47 = slotSwitch(0x47, cur);
			_r13Time47 = _game->crtRandom() % 0x9c4 + 0x4e2 + now;
			targetAdd(_r13s47, true);
			cur = _r13s47;
		}
	}
	int32 rect[4];
	attachedBox(_r13s47, 0x47, 1, rect);
	if (nedTouches(rect))
		playAction(1);
	int d = place(_r13s47, 0x47, true);
	setDeadline(_r13s47, now + (int16)d * kMsPerStep);
}

// 0x413070: the ball (1) rolls left along the floor of base map 4; the roller bounces it (0x96, 0x97)
void AtticScene::r13TickBall(uint32 now) {
	if (_r13sBall == -1 && _r13TimeBall < now && _rooms[13].polled > 1) {
		slot(1).x = slot(0xbe).x;
		slot(1).y = slot(0xbe).y;
		_r13sBall = slotSwitch(1, _r13sBall);
	}
	if (_r13sBall == -1 || slot(_r13sBall).deadline >= now)
		return;
	slot(_r13sBall).step++;
	int16 cur = _r13sBall;
	const Room &r13 = _rooms[13];
	if (slot(cur).sequenceLength <= slot(cur).step) {
		if (cur == 1) {
			if (r13.view[2] < slot(1).x || slot(1).x < r13.view[0]) {
				_r13sBall = slotSwitch(-1, cur);
				_r13BallHit = 0;
				_r13TimeBall = _game->crtRandom() % 8000 + now;
			} else {
				_r13sBall = slotSwitch(1, cur);
				playAt(1, 1);
			}
		} else if (cur == 0x96) {
			_r13sBall = slotSwitch(0x97, cur);
			playAt(0x97, 0x97);
		} else if (cur == 0x97) {
			if (r13.view[3] < slot(0x97).y) {
				_r13sBall = slotSwitch(-1, cur);
				_r13TimeBall = _game->crtRandom() % 8000 + now;
			} else {
				_r13sBall = slotSwitch(0x97, cur);
				playAt(0x97, 0x97);
			}
		}
	}
	int d = 0;
	if (_r13sBall != -1) {
		int32 rect[4];
		if (r13.polled < 2) {
			rect[0] = r13.view[0];
			rect[2] = r13.view[0] + 8;
			rect[1] = r13.view[1];
			rect[3] = r13.view[1] + 8;
		} else {
			attachedBox(1, 1, 1, rect);
		}
		if (!_r13BallHit && nedTouches(rect)) {
			playAction(0x3a);
			_r13BallHit = 1;
		}
		if (r13.polled > 1 && _r13sBall == 1) {
			int32 roller[4];
			attachedBox(_r13s47, 0x47, 1, roller);
			if (MAX(rect[0], roller[0]) < MIN(rect[2], roller[2]) && MAX(rect[1], roller[1]) < MIN(rect[3], roller[3])) {
				_r13sBall = slotSwitch(0x96, _r13sBall);
				slot(0x96).x = slot(1).x;
				slot(0x96).y = slot(1).y - 0x40;
				slot(0x97).x = slot(1).x;
				slot(0x97).y = slot(1).y;
			}
		}
		if (_r13sBall == 1) {
			slot(1).x -= 0x14;
			for (int16 y = (int16)r13.view[3]; r13.view[1] <= y; y--) {
				if (solidAt(0x525950, (int16)slot(1).x, y)) {
					slot(1).y = y - 0x40;
					break;
				}
			}
		} else if (_r13sBall == 0x97) {
			slot(0x97).y += 4;
		}
		d = place(_r13sBall, _r13sBall, true);
	}
	setDeadline(_r13sBall, now + (int16)d * kMsPerStep);
}

// ==== Room 15 (0x4135d0..0x413780) ====

// 0x4135d0
void AtticScene::r15Loaded() {
	_rooms[15].stages++;
	_rooms[15].flags = (_rooms[15].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x413610
int AtticScene::r15Load() {
	return submit(0x499c30, 2, 0xf);
}

// 0x4136a0
int AtticScene::r15Poll() {
	_rooms[15].polled++;
	if (resWait(0x499c30))
		return 5;
	showBitmap(1, 0xf, 1, 30000, true);
	bmpSet(0x4e0a6c, _rooms[15].view[0], _rooms[15].view[1]);
	bmpSet(0x4e0a70, 0x109d, 0x568);
	if (resWait(0x499c54))
		return 5;
	showBitmap(1, 0xf, 2, 3, false);
	bmpPlace(0x4e0a70);
	bmpVisible(0x4e0a70, true);
	_rooms[15].flags |= kRoomActive;
	return 0;
}

// 0x413630
int AtticScene::r15Unload() {
	bmpVisible(0x4e0a6c, false);
	bmpVisible(0x4e0a70, false);
	release(0x499c30, 2);
	_rooms[15].flags = (_rooms[15].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// ==== Room 16 (0x413790..0x4138b0) ====

// 0x413790
void AtticScene::r16Loaded() {
	_rooms[16].stages++;
	_rooms[16].flags = (_rooms[16].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x4137d0
int AtticScene::r16Load() {
	return submit(0x499c78, 1, 0x10);
}

// 0x413840
int AtticScene::r16Poll() {
	_rooms[16].polled++;
	if (resWait(0x499c78))
		return 5;
	showBitmap(1, 0x10, 1, 30000, true);
	bmpSet(0x4e0bfc, _rooms[16].view[0], _rooms[16].view[1]);
	_rooms[16].flags |= kRoomActive;
	return 0;
}

// 0x4137f0
int AtticScene::r16Unload() {
	bmpVisible(0x4e0bfc, false);
	release(0x499c78, 1);
	_rooms[16].flags = (_rooms[16].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// ==== Room 17 (0x4138c0..0x414400) ====

// 0x4138c0
void AtticScene::r17Loaded() {
	_rooms[17].stages++;
	_rooms[17].flags = (_rooms[17].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x413900
int AtticScene::r17Load() {
	return submit(0x499ca0, 0x13, 0x11);
}

// 0x413ad0
int AtticScene::r17Poll() {
	Room &r = _rooms[17];
	r.polled++;
	if (resWait(0x499ca0))
		return 5;
	showBitmap(1, 0x11, 1, 30000, true);
	bmpSet(0x4e0d8c, r.view[0], r.view[1]);
	static const struct {
		uint32 va, entry;
		int16 x, y;
		int id;
	} kBitmaps[] = {
		{ 0x4e0d90, 0x499cc4, 0x143c, 0x6d5, 2 }, { 0x4e0d94, 0x499ce8, 0x154f, 0x6b8, 3 }, { 0x4e0d98, 0x499d0c, 0x124e, 0x68a, 4 }
	};
	for (const auto &b : kBitmaps) {
		bmpSet(b.va, b.x, b.y);
		if (resWait(b.entry))
			return 5;
		showBitmap(1, 0x11, b.id, 3, false);
		bmpPlace(b.va);
		bmpVisible(b.va, true);
	}
	_r17s59 = 0x59;
	_r17Time5A = 0;
	if (resWait(0x499d30))
		return 5;
	showSlot(0x59, 100, false);
	if (resWait(0x499d54))
		return 5;
	showSlot(0x5a, 100, false);
	if (resWait(0x499d9c))
		return 5;
	showSlot(0x5b, 100, false);
	_r17s61 = 0x61;
	_r17Time61 = _game->now();
	if (resWait(0x499de4))
		return 5;
	showSlot(0x61, 100, false);
	_r17s5C = _r18Done ? -1 : 0x5c;
	static const struct {
		uint32 entry;
		int id;
		bool on;
	} kSlots[] = {
		{ 0x499e2c, 0x5c, false }, { 0x499e74, 0xf9, true }, { 0x499ebc, 0xfa, true }, { 0x499f04, 0xfb, true },
		{ 0x499f28, 0xfc, true }
	};
	for (const auto &e : kSlots) {
		if (resWait(e.entry))
			return 5;
		showSlot(e.id, 100, e.on);
	}
	for (int i = 0; i < 4; i++)
		_r17sF[i] = 0xf9 + i;
	_r17TimeF = _game->now();
	_r17TimeSound = _game->crtRandom() % 7000 + _r17TimeF;
	targetAdd(_r17s59, true);
	r.flags |= kRoomActive;
	return 0;
}

// 0x413920
int AtticScene::r17Unload() {
	bmpVisible(0x4e0d8c, false);
	bmpVisible(0x4e0d90, false);
	bmpVisible(0x4e0d94, false);
	bmpVisible(0x4e0d98, false);
	static const int kHide[] = { 0x59, 0x5a, 0x5b, 0x61, 0x5c, 0xf9, 0xfa, 0xfb, 0xfc };
	for (int id : kHide)
		visible(id, false);
	_r17s61 = -1;
	_r17s59 = -1;
	_r17s5C = -1;
	static const int kStop[] = { 0x5a, 0x5b, 0x5c, 0x61, 0xf9, 0xfa };
	for (int id : kStop)
		stopSound(id);
	release(0x499ca0, 0x13);
	_rooms[17].flags = (_rooms[17].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x413eb0
void AtticScene::r17Tick(uint32 now) {
	r17Tick59(now);
	r17Tick61(now);
	r17Tick5C(now);
	r17TickF(now);
}

// 0x413ee0
void AtticScene::r17Tick59(uint32 now) {
	if (_r17s59 == -1 || now <= slot(_r17s59).deadline)
		return;
	slot(_r17s59).step++;
	int16 cur = _r17s59;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		targetRemove(_r17s59);
		if (cur == 0x59) {
			if (_r17Time5A < now) {
				_r17s59 = slotSwitch(0x5a, cur);
				playAt(0x5a);
			}
			setStep(_r17s59, 0);
			targetAdd(_r17s59, true);
			cur = _r17s59;
		} else if (cur == 0x5a) {
			_r17s59 = slotSwitch(0x59, cur);
			_r17Time5A = _game->crtRandom() % 2000 + now;
			targetAdd(_r17s59, true);
			cur = _r17s59;
		} else if (cur == 0x5b) {
			cur = _r17s59 = slotSwitch(-1, cur);
		}
	}
	int d = 0;
	if (cur != -1) {
		d = place(cur, 0x59, true);
		cur = _r17s59;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x4140b0
void AtticScene::r17Tick61(uint32 now) {
	if (_r17s61 == -1)
		return;
	if (_r17Time61 <= now) {
		playAt(_r17s61);
		_r17Time61 = now + 0xf85;
	}
	if (slot(_r17s61).deadline < now) {
		slot(_r17s61).step++;
		int16 cur = _r17s61;
		if (slot(cur).sequenceLength <= slot(cur).step && cur == 0x61)
			cur = _r17s61 = slotSwitch(0x61, cur);
		int d = place(cur, 0x61, true);
		setDeadline(_r17s61, now + (int16)d * kMsPerStep);
	}
}

// 0x4141c0
void AtticScene::r17Tick5C(uint32 now) {
	if (_r17s5C == -1 || slot(_r17s5C).deadline >= now)
		return;
	slot(_r17s5C).step++;
	int16 cur = _r17s5C;
	if (slot(cur).sequenceLength <= slot(cur).step && cur == 0x5c) {
		_r17s5C = slotSwitch(0x5c, cur);
		setStep(_r17s5C, 0);
		playAt(_r17s5C, _r17s5C);
		cur = _r17s5C;
	}
	int d = place(cur, 0x5c, true);
	setDeadline(_r17s5C, now + (int16)d * kMsPerStep);
}

// 0x4142d0: four looping sprites and two ambient sounds
void AtticScene::r17TickF(uint32 now) {
	if (_r17TimeF <= now) {
		playAt(0xf9);
		_r17TimeF = now + 0x79a;
	}
	if (_r17TimeSound <= now) {
		playAt(0xfa);
		_r17TimeSound = _game->crtRandom() % 7000 + now;
	}
	for (int i = 0; i < 4; i++) {
		int16 s = _r17sF[i];
		if (s == -1 || slot(s).deadline >= now)
			continue;
		slot(s).step++;
		if (slot(_r17sF[i]).sequenceLength <= slot(_r17sF[i]).step)
			_r17sF[i] = slotSwitch(i + 0xf9, _r17sF[i]);
		int d = place(_r17sF[i], _r17sF[i], true);
		setDeadline(_r17sF[i], now + (int16)d * kMsPerStep);
	}
}

// ==== Room 18 (0x414400..0x415320) ====

// 0x414400
void AtticScene::r18Loaded() {
	_rooms[18].stages++;
	_rooms[18].flags = (_rooms[18].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x414440
int AtticScene::r18Load() {
	return submit(0x499f50, 0x14, 0x12);
}

// 0x499fe0's map (DAT_00525290) joins the world once the ladder is down
void AtticScene::r18AddMap() {
	if (resWait(0x499fe0) == 0) {
		if (!_r18MapAdded) {
			addMap(map(0x525290));
			_r18MapAdded = 1;
		}
		_r18Done = 1;
	}
}

// 0x4146c0
int AtticScene::r18Poll() {
	Room &r = _rooms[18];
	r.polled++;
	if (resWait(0x499f50))
		return 5;
	showBitmap(1, 0x12, 1, 30000, true);
	bmpSet(0x4e0f1c, r.view[0], r.view[1]);
	static const struct {
		uint32 va, entry;
		int16 x, y;
		int id;
	} kBitmaps[] = {
		{ 0x4e0f20, 0x499f74, 0x109d, 0x620, 2 }, { 0x4e0f24, 0x499f98, 0x10f2, 0x527, 3 }, { 0x4e0f28, 0x499fbc, 0x11aa, 0x672, 4 }
	};
	for (const auto &b : kBitmaps) {
		bmpSet(b.va, b.x, b.y);
		if (resWait(b.entry))
			return 5;
		showBitmap(1, 0x12, b.id, 3, false);
		bmpPlace(b.va);
		bmpVisible(b.va, true);
	}
	static const struct {
		uint32 entry;
		int id, z;
	} kSlots[] = {
		{ 0x49a004, 0x5e, 5 }, { 0x49a04c, 0xaa, 5 }, { 0x49a094, 0x5f, 5 }, { 0x49a0dc, 0x60, 5 }, { 0x49a124, 0x5d, 5 },
		{ 0x49a16c, 0x62, 100 }, { 0x49a1b4, 99, 100 }, { 0x49a1fc, 0xac, 100 }
	};
	for (const auto &e : kSlots) {
		if (resWait(e.entry))
			return 5;
		showSlot(e.id, e.z, false);
	}
	if (!_r18Done) {
		_r18s62 = 0x62;
		_r18Pulled = 0;
		_r18s5E = 0x5e;
		_r18s5D = -1;
		targetAdd(0x5e, true);
		targetAdd(_r18s62, true);
	} else {
		_r18s62 = -1;
		_r18Pulled = 1;
		_r18s5E = 0x60;
		_r18s5D = 0x5d;
		r18AddMap();
	}
	r.flags |= kRoomActive;
	return 0;
}

// 0x414460
int AtticScene::r18Unload() {
	bmpVisible(0x4e0f1c, false);
	bmpVisible(0x4e0f20, false);
	bmpVisible(0x4e0f24, false);
	bmpVisible(0x4e0f28, false);
	static const int kHide[] = { 0x5e, 0xaa, 0x5f, 0x60, 0x5d, 0x62, 99, 0xac };
	for (int id : kHide)
		visible(id, false);
	_r18s5E = -1;
	_r18s5D = -1;
	_r18s62 = -1;
	static const int kStop[] = { 0x5e, 0xaa, 0x5f, 0x60, 0x5d, 0x62, 99 };
	for (int id : kStop)
		stopSound(id);
	if (_r18Done && _r18MapAdded) {
		removeMap(map(0x525290));
		_r18MapAdded = 0;
	}
	release(0x499f50, 0x14);
	_rooms[18].flags = (_rooms[18].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x414ae0
void AtticScene::r18Tick(uint32 now) {
	r18Tick5E(now);
	r18Tick62(now);
}

// 0x414b00: the ladder (0x5e up, 0xaa hit, 0x5f falling, 0x60 down) and its top (0x5d)
void AtticScene::r18Tick5E(uint32 now) {
	if (_r18s5E != -1) {
		if (_r18s5E == 0x5f && slot(0x5f).step == 1) {
			_r18s62 = slotSwitch(0xac, _r18s62);
			int d = place(_r18s62, 0x62, true);
			setDeadline(_r18s62, now + (int16)d * kMsPerStep);
		}
		if (now > slot(_r18s5E).deadline) {
			slot(_r18s5E).step++;
			int16 cur = _r18s5E;
			if (slot(cur).sequenceLength <= slot(cur).step) {
				targetRemove(_r18s5E);
				switch (_r18s5E) {
				case 0x5e:
					cur = _r18s5E = slotSwitch(0x5e, cur);
					targetAdd(cur, true);
					playAt(0x5e, 0x5e);
					break;
				case 0x5f:
					_r18s5E = slotSwitch(0x60, cur);
					playAt(0x60, 0x5f);
					if (_r18s5D == -1) {
						_r18s5D = slotSwitch(0x5d, -1);
						int d = place(_r18s5D, 0x5d, true);
						setDeadline(_r18s5D, now + (int16)d * kMsPerStep);
						if (!_r18Done)
							r18AddMap();
						playAt(0x5d);
						_r17s5C = slotSwitch(-1, _r17s5C);
					}
					cur = _r18s5E;
					break;
				case 0x60:
					_r18s5E = slotSwitch(0x60, cur);
					playAt(0x60, 0x60);
					cur = _r18s5E;
					break;
				case 0xaa:
					cur = _r18s5E = slotSwitch(0x5e, cur);
					targetAdd(cur, true);
					playAt(0x5e, 0xaa);
					break;
				default:
					break;
				}
			}
			int d = place(cur, 0x5e, true);
			setDeadline(_r18s5E, now + (int16)d * kMsPerStep);
		}
	}
	if (_r18s5D != -1 && slot(_r18s5D).deadline < now) {
		slot(_r18s5D).step++;
		int16 cur = _r18s5D;
		if (slot(cur).sequenceLength <= slot(cur).step && cur == 0x5d) {
			_r18s5D = slotSwitch(0x5d, cur);
			slot(_r18s5D).step = slot(_r18s5D).sequenceLength - 1;
			if (!_hint4Shown) {
				hint(4, true);
				_hint4Shown = 1;
			}
		}
		int d = place(_r18s5D, 0x5d, true);
		setDeadline(_r18s5D, now + (int16)d * kMsPerStep);
	}
}

// 0x415030: the rope (0x62; 99 pulled, 0xac slack)
void AtticScene::r18Tick62(uint32 now) {
	if (_r18s62 == -1 || slot(_r18s62).deadline >= now)
		return;
	if (_r18s62 == 99 && slot(99).step == 4)
		playAt(99, 0x62);
	slot(_r18s62).step++;
	int16 cur = _r18s62;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		if (cur == 0x62) {
			_r18s62 = slotSwitch(0x62, cur);
			playAt(_r18s62);
		} else if (cur == 99) {
			_r18s62 = slotSwitch(99, cur);
			slot(_r18s62).step = slot(_r18s62).sequenceLength - 1;
			if (!_r18Pulled) {
				targetRemove(_r18s5E);
				_r18s5E = slotSwitch(0x5f, _r18s5E);
				int d = place(_r18s5E, 0x5e, true);
				setDeadline(_r18s5E, _game->now() + (int16)d * kMsPerStep);
				playAt(0x5f);
				_r18Pulled = 1;
			}
		} else if (cur == 0xac) {
			_r18s62 = slotSwitch(0xac, cur);
			slot(_r18s62).step = slot(_r18s62).sequenceLength - 1;
		}
	}
	int d = place(_r18s62, 0x62, true);
	setDeadline(_r18s62, now + (int16)d * kMsPerStep);
}

// ==== Room 19 (0x415320..0x416a90) ====

// 0x415320
void AtticScene::r19Loaded() {
	Room &r = _rooms[19];
	r.stages++;
	if (r.stages == 1 || r.stages == 2) {
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
	} else if (r.stages == 3) {
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x415390
int AtticScene::r19Load() {
	switch (_rooms[19].stages) {
	case 0:
		return submit(0x49a228, 0xd, 0x13);
	case 1:
		return submit(0x49a3fc, 9, 0x13);
	case 2:
		return submit(0x49a540, 6, 0x13);
	default:
		return _rooms[19].stages - 2;
	}
}

// 0x4156a0
int AtticScene::r19Poll() {
	Room &r = _rooms[19];
	r.polled++;
	int32 zero[4] = { 0, 0, 0, 0 };
	if (r.polled == 1) {
		if (resWait(0x49a228))
			return 5;
		showBitmap(1, 0x13, 1, 30000, true);
		bmpSet(0x4e10ac, r.view[0], r.view[1]);
		if (resWait(0x49a24c))
			return 5;
		bmpSet(0x4e10b0, 0x14f4, 0x4fc);
		showBitmap(1, 0x13, 2, 3, false);
		bmpPlace(0x4e10b0);
		bmpVisible(0x4e10b0, true);
		if (resWait(0x49a270))
			return 5;
		bmpSet(0x4e10b4, 0x1738, 0x590);
		showBitmap(1, 0x13, 3, 0xbe, false);
		bmpPlace(0x4e10b4);
		bmpVisible(0x4e10b4, true);
		if (_r19Climbed) {
			if (resWait(0x49a294))
				return 5;
			if (!_r19MapAdded) {
				addMap(map(0x525278));
				_r19MapAdded = 1;
			}
		}
		_r19sEF = 0xef;
		if (resWait(0x49a2b8))
			return 5;
		showSlot(0xef, 0x96, true);
		_r19s67 = 0x67;
		_r19Time67 = 0;
		if (resWait(0x49a2dc))
			return 5;
		showSlot(0x67, 200, false);
		_r19s65 = 0x65;
		_r19Time65 = 0;
		_r19Toggle = 0;
		if (resWait(0x49a300))
			return 5;
		showSlot(0x65, 200, true);
		_r19s6D = 0x6d;
		static const struct {
			uint32 entry;
			int id, z;
		} kSlots[] = { { 0x49a348, 0x6d, 100 }, { 0x49a36c, 0x6a, 100 }, { 0x49a390, 0x6b, 100 }, { 0x49a3d8, 0xf0, 0x96 } };
		for (const auto &e : kSlots) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, false);
		}
		if (!_r19Done) {
			_r19HitEF = 0;
			targetAdd(_r19sEF, true);
			_r19sAny = 0x6a;
		} else {
			_r19sAny = 0x6b;
			_r19HitEF = 2;
		}
		_r19NedHit = 0;
		r.flags |= kRoomActive;
	} else if (r.polled == 2) {
		static const struct {
			uint32 entry;
			int id, z;
		} kSlots[] = { { 0x49a3fc, 0xb1, 200 }, { 0x49a444, 0x66, 200 }, { 0x49a48c, 0xb2, 200 }, { 0x49a4d4, 0x6f, 3 }, { 0x49a51c, 0x71, 3 } };
		for (const auto &e : kSlots) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, false);
		}
		return 0;
	} else if (r.polled == 3) {
		static const struct {
			uint32 entry;
			int id, z;
		} kSlots[] = { { 0x49a540, 0x68, 200 }, { 0x49a588, 0x6e, 100 }, { 0x49a5d0, 0x69, 200 } };
		for (const auto &e : kSlots) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, false);
		}
		_r19Time6D = 0;
		_r19Region = addRegion(zero, 0x80000, 0xd2);
		_world.setUser(&_userMap);
		return 0;
	}
	return 0;
}

// 0x4153f0
int AtticScene::r19Unload() {
	int n = _rooms[19].stages;
	if (n > 0) {
		bmpVisible(0x4e10ac, false);
		bmpVisible(0x4e10b0, false);
		bmpVisible(0x4e10b4, false);
		static const int kHide[] = { 0xef, 0x67, 0x65, 0x6d, 0x6a, 0x6b, 0xf0 };
		for (int id : kHide)
			visible(id, false);
		stopSound(0x65);
		stopSound(0x6b);
		if (_r19Done && _r19MapAdded) {
			removeMap(map(0x525278));
			_r19MapAdded = 0;
		}
	}
	if (n > 1) {
		static const int kHide[] = { 0xb1, 0x66, 0xb2, 0x6f, 0x71 };
		for (int id : kHide)
			visible(id, false);
		stopSound(0xb1);
		stopSound(0x66);
		stopSound(0xb2);
		stopSound(0x6f);
	}
	if (n > 2) {
		visible(0x68, false);
		visible(0x6e, false);
		visible(0x69, false);
		stopSound(0x6e);
		stopSound(0x69);
	}
	_r19s67 = _r19s65 = _r19s6D = _r19sAny = _r19sEF = -1;
	int count = n == 1 ? 0xd : n == 2 ? 0x16 : n == 3 ? 0x1c : 0;
	release(0x49a228, count);
	_rooms[19].flags = (_rooms[19].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x415bf0
void AtticScene::r19Tick(uint32 now) {
	r19Tick67(now);
	r19Tick65(now);
	r19Tick6D(now);
	r19TickAny(now);
	r19TickEF(now);
}

// 0x415c30: the sleeper (0x67; 0x68 / 0xb1 its moves, 0x69 waking up)
void AtticScene::r19Tick67(uint32 now) {
	if (_r19s67 == -1)
		return;
	if (_r19s67 == 0x69) {
		if (slot(0x69).step == 9) {
			_r19sEF = slotSwitch(0xf0, _r19sEF);
			int d = place(_r19sEF, 0xef, true);
			setDeadline(_r19sEF, _game->now() + (int16)d * kMsPerStep);
		}
		if (slot(_r19s67).step == 0xd) {
			_r19sAny = slotSwitch(0x6b, _r19sAny);
			int d = place(_r19sAny, 0x6b, true);
			setDeadline(_r19sAny, _game->now() + (int16)d * kMsPerStep);
			playAt(0x6b);
		}
	}
	if (slot(_r19s67).deadline >= now)
		return;
	slot(_r19s67).step++;
	int16 cur = _r19s67;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		switch (cur) {
		case 0x67:
			if (_r19Time67 < now && _rooms[19].polled > 2) {
				if (!_r19Done) {
					_r19s67 = slotSwitch(0x68, cur);
					playAt(0x68);
				} else {
					_r19s67 = slotSwitch(0xb1, cur);
					playAt(0xb1);
				}
			} else {
				_r19s67 = slotSwitch(0x67, cur);
			}
			cur = _r19s67;
			break;
		case 0x68:
		case 0xb1:
			_r19s67 = slotSwitch(0x67, cur);
			_r19Time67 = _game->crtRandom() % 3000 + 3000 + now;
			cur = _r19s67;
			break;
		case 0x69:
			_r19s67 = slotSwitch(0x67, cur);
			_r19Done = 1;
			cur = _r19s67;
			break;
		default:
			break;
		}
	}
	int d = place(cur, 0x67, true);
	setDeadline(_r19s67, now + (int16)d * kMsPerStep);
}

// 0x415fd0: the creature 0x65 (0x66 / 0xb2 alternating once the sleeper woke); 0x66 hits Ned in costume 8
void AtticScene::r19Tick65(uint32 now) {
	if (_r19s65 == -1 || slot(_r19s65).deadline >= now)
		return;
	if (_r19s65 == 0x66 && slot(0x66).step == 8)
		playAt(0x66);
	slot(_r19s65).step++;
	int16 cur = _r19s65;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		if (cur == 0x65) {
			if (_r19Time65 < now) {
				if (!_r19Done || _rooms[19].polled < 2) {
					_r19s65 = slotSwitch(0x65, cur);
					playAt(0x65);
				} else {
					_r19s65 = slotSwitch(_r19Toggle ? 0x66 : 0xb2, cur);
					_r19Toggle = 1 - _r19Toggle;
					if (_r19s65 == 0xb2)
						playAt(0xb2);
				}
			} else {
				_r19s65 = slotSwitch(0x65, cur);
			}
			_r19Time65 = _game->crtRandom() % 3000 + 3000 + now;
		} else if (cur == 0x66 || cur == 0xb2) {
			_r19s65 = slotSwitch(_r19Toggle ? 0x66 : 0xb2, cur);
			_r19Toggle = 1 - _r19Toggle;
			if (_r19s65 == 0xb2)
				playAt(0xb2);
		}
	}
	if (ned()->getCostume() == 8 && !_r19NedHit && _r19s65 == 0x66) {
		int32 rect[4];
		attachedBox(0x66, 0x65, 1, rect);
		if (nedTouches(rect)) {
			playAction(0x44);
			_r19NedHit = 1;
		}
	}
	int d = place(_r19s65, 0x65, true);
	setDeadline(_r19s65, now + (int16)d * kMsPerStep);
}

// 0x4163a0: the bed (0x6d; 0x6e creaking, 0x6f/0x71 Ned climbing in to sleep, costume 8)
void AtticScene::r19Tick6D(uint32 now) {
	if (_r19s6D == -1 || now <= slot(_r19s6D).deadline)
		return;
	slot(_r19s6D).step++;
	int16 cur = _r19s6D;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		switch (cur) {
		case 0x6d:
			if (now <= _r19Time6D || _rooms[19].polled < 3) {
				cur = _r19s6D = slotSwitch(0x6d, cur);
				goto place;
			}
			_r19s6D = slotSwitch(0x6e, cur);
			playAt(_r19s6D, 0x6e);
			break;
		case 0x6e:
			_r19s6D = slotSwitch(0x6d, cur);
			_r19Time6D = _game->crtRandom() % 0x5dc + 0x5dc + now;
			break;
		case 0x6f:
			_r19s6D = slotSwitch(0x71, cur);
			playAction(0x3a);
			if (_r19Region)
				removeRegion(_r19Region);
			break;
		case 0x71:
			_r19s6D = slotSwitch(-1, cur);
			ned()->setCostume(8);
			removeBaseMap(8);
			if (_rooms[22].flags & kRoomActive)
				bmpVisible(0x4e1560, true);
			_inputLocked = 0;
			break;
		default:
			goto place;
		}
		cur = _r19s6D;
	}
place:
	int d = 0;
	if (cur != -1) {
		d = place(cur, 0x6d, true);
		cur = _r19s6D;
	}
	if (_r19Region && cur >= 0) {
		r9MoveRegion(_r19Region, cur, 0x6d);
		Slot &cs = slot(cur);
		int frame = 0;
		if (cs.sequence)
			cs.sequence->getStep(cs.step, frame, d);
		cur = _r19s6D;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x4166c0: 0x6a, then 0x6b once the sleeper woke (its map 0x525278 joins)
void AtticScene::r19TickAny(uint32 now) {
	if (_r19sAny == -1 || now <= slot(_r19sAny).deadline)
		return;
	slot(_r19sAny).step++;
	int16 cur = _r19sAny;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		targetRemove(_r19sAny);
		if (cur == 0x6a) {
			_r19sAny = slotSwitch(0x6a, cur);
			slot(_r19sAny).step = slot(_r19sAny).sequenceLength - 1;
			targetAdd(_r19sAny, true);
			cur = _r19sAny;
		} else if (cur == 0x6b) {
			_r19sAny = slotSwitch(0x6b, cur);
			slot(_r19sAny).step = slot(_r19sAny).sequenceLength - 1;
			if (!_r19Climbed) {
				if (!_r19MapAdded) {
					addMap(map(0x525278));
					_r19MapAdded = 1;
				}
				_r19Climbed = 1;
			}
			cur = _r19sAny;
		}
	}
	int d = place(cur, cur, true);
	setDeadline(_r19sAny, now + (int16)d * kMsPerStep);
}

// 0x416890: the alarm clock (0xef); once hit it rings and wakes the sleeper (0x69)
void AtticScene::r19TickEF(uint32 now) {
	if (_r19sEF == -1 || slot(_r19sEF).deadline >= now)
		return;
	slot(_r19sEF).step++;
	if (_r19sEF == 0xef) {
		if (_r19HitEF == 0) {
			slot(0xef).step = 0;
		} else if (_r19HitEF == 2) {
			slot(0xef).step = slot(0xef).sequenceLength - 1;
		} else if (slot(0xef).sequenceLength <= slot(0xef).step) {
			_r19HitEF = 2;
			targetRemove(0xef);
			_r19sEF = slotSwitch(0xef, _r19sEF);
			slot(_r19sEF).step = slot(_r19sEF).sequenceLength - 1;
			_r19s67 = slotSwitch(0x69, _r19s67);
			int d = place(_r19s67, 0x67, true);
			setDeadline(_r19s67, _game->now() + (int16)d * kMsPerStep);
			playAt(0x69);
		}
	} else if (_r19sEF == 0xf0) {
		slot(0xf0).step = slot(0xf0).sequenceLength - 1;
	}
	int d = place(_r19sEF, 0xef, true);
	setDeadline(_r19sEF, now + (int16)d * kMsPerStep);
}

// ==== Room 21 (0x416a90..0x417c70) ====

// 0x416a90
void AtticScene::r21Loaded() {
	Room &r = _rooms[21];
	r.stages++;
	if (r.stages == 1 || r.stages == 2) {
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
	} else if (r.stages == 3) {
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x416b00
int AtticScene::r21Load() {
	switch (_rooms[21].stages) {
	case 0:
		return submit(0x49a618, 0xe, 0x15);
	case 1:
		return submit(0x49a810, 8, 0x15);
	case 2:
		return submit(0x49a930, 0xc, 0x15);
	default:
		return _rooms[21].stages - 2;
	}
}

// 0x416eb0
int AtticScene::r21Poll() {
	Room &r = _rooms[21];
	r.polled++;
	if (r.polled == 1) {
		if (resWait(0x49a618))
			return 5;
		showBitmap(1, 0x15, 1, 30000, true);
		bmpSet(0x4e13cc, r.view[0], r.view[1]);
		_r21sB0 = 0x186;
		_r21Var220 = 0;
		_r21TimeAE = 0;
		static const struct {
			uint32 entry;
			int id, z;
			bool on;
		} kSlots1[] = { { 0x49a63c, 0xb0, 100, false }, { 0x49a660, 0x186, 100, true }, { 0x49a6a8, 0x187, 100, false },
			{ 0x49a6f0, 0xae, 100, false } };
		for (const auto &e : kSlots1) {
			if (resWait(e.entry))
				return 5;
			showSlot(e.id, e.z, e.on);
		}
		_r21s208 = -1;
		if (resWait(0x49a738))
			return 5;
		showSlot(0xee, 99, false);
		if (resWait(0x49a75c))
			return 5;
		showSlot(0xed, 99, false);
		_r21Ready = 0;
		_r21s218 = -1;
		if (resWait(0x49a780))
			return 5;
		showSlot(0xec, 99, false);
		if (resWait(0x49a7c8))
			return 5;
		showSlot(0x194, 99, false);
		_r21Pos[0] = slot(0xec).x;
		_r21PosY[0] = slot(0xec).y;
		_r21Pos[1] = slot(0x194).x;
		_r21PosY[1] = slot(0x194).y;
		r.flags |= kRoomActive;
		_r21Throws = -1;
		_r21Count = 0;
	} else if (r.polled == 2) {
		static const uint32 kEntries[] = { 0x49a810, 0x49a858, 0x49a8a0, 0x49a8e8 };
		for (int i = 0; i < 4; i++) {
			if (resWait(kEntries[i]))
				return 5;
			showSlot(0x188 + i, 100, false);
		}
		return 0;
	} else if (r.polled == 3) {
		static const uint32 kEntries[] = { 0x49a930, 0x49a978, 0x49a9c0, 0x49aa08, 0x49aa50, 0x49aa98 };
		for (int i = 0; i < 6; i++) {
			if (resWait(kEntries[i]))
				return 5;
			showSlot(0x18c + i, 100, false);
		}
		return 0;
	}
	return 0;
}

// 0x416b60
int AtticScene::r21Unload() {
	int n = _rooms[21].stages;
	if (n > 0) {
		bmpVisible(0x4e13cc, false);
		static const int kHide[] = { 0xb0, 0x186, 0x187, 0xae, 0xee, 0xed, 0xec, 0x194 };
		for (int id : kHide)
			visible(id, false);
		static const int kStop[] = { 0x186, 0x187, 0xae, 0xec, 0x194 };
		for (int id : kStop)
			stopSound(id);
		_r21sB0 = -1;
		slot(0xec).x = _r21Pos[0];
		slot(0xec).y = _r21PosY[0];
		slot(0x194).x = _r21Pos[1];
		slot(0x194).y = _r21PosY[1];
	}
	if (n > 1) {
		for (int id = 0x188; id < 0x18c; id++)
			visible(id, false);
		for (int id = 0x188; id < 0x18c; id++)
			stopSound(id);
	}
	if (n > 2) {
		for (int id = 0x18c; id < 0x192; id++)
			visible(id, false);
		for (int id = 0x18c; id < 0x192; id++)
			stopSound(id);
	}
	int count = n == 1 ? 0xe : n == 2 ? 0x16 : n == 3 ? 0x22 : 0;
	release(0x49a618, count);
	_rooms[21].flags = (_rooms[21].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x4172b0: after the drop in room 23 (region 0x3ff) Ned falls into room 21
void AtticScene::r21Tick(uint32 now) {
	if (_r21Drop && (_rooms[21].flags & kRoomActive)) {
		_jumpX = -(_scrollX + _rooms[21].view[0]);
		_jumpY = -(_scrollY + _rooms[21].view[1]);
		_cameraJump = 1;
		_viewRoomX = -1;
		_viewRoomY = -1;
		_r21Drop = 0;
		_inputLocked = 0;
		ned()->enable(true);
		ned()->startFall();
	}
	r21TickB0(now);
	r21Tick208(now);
	r21Tick218(now);
}

// 0x417350: the juggler (0xae; on step 4 a ball (0xed, every third throw 0xee) appears; then
// a routine 0x186..0x191 chosen by _r21Count)
void AtticScene::r21TickB0(uint32 now) {
	if (_r21sB0 == -1 || now <= slot(_r21sB0).deadline)
		return;
	if (_r21sB0 == 0xae) {
		int16 step = slot(0xae).step;
		if (step == 4) {
			if (!_r21Done) {
				_r21Throws++;
				_r21Ready = (_r21Throws % 3) == 2;
			} else {
				_r21Ready = 0;
			}
			_r21s208 = slotSwitch(_r21Ready ? 0xee : 0xed, _r21s208);
			place(_r21s208, _r21s208, true);
			setStep(_r21s208, 1);
			setDeadline(_r21s208, now - 1);
		} else if (step == 0x28) {
			_r21s208 = slotSwitch(-1, _r21s208);
		}
		if (_r21sB0 == 0xae) {
			int16 s = slot(0xae).step;
			if (s == 5 || s == 0x11 || s == 0x1d)
				playAt(0xae);
		}
	}
	slot(_r21sB0).step++;
	int16 cur = _r21sB0;
	if (slot(cur).step >= slot(cur).sequenceLength) {
		targetRemove(_r21sB0);
		int next = -2, sound = -1;
		switch (cur) {
		case 0xae:
			if (now <= _r21TimeAE) {
				next = 0xae;
				break;
			}
			if (_r21Count == 0) {
				next = sound = 0x186;
			} else if (_r21Count == 1) {
				next = sound = _rooms[21].polled < 2 ? 0x186 : 0x188;
			} else if (_r21Count == 2) {
				next = sound = _rooms[21].polled < 2 ? 0x186 : 0x18c;
			}
			_r21Count = (_r21Count + 1) % 3;
			break;
		case 0x186:
			next = sound = 0x187;
			break;
		case 0x187:
		case 0x18b:
		case 0x191:
			_r21sB0 = slotSwitch(0xae, cur);
			_r21TimeAE = _game->crtRandom() % 5000 + 5000 + now;
			cur = _r21sB0;
			break;
		case 0x188:
		case 0x189:
		case 0x18a:
		case 0x18c:
		case 0x18d:
		case 0x18e:
		case 399:
		case 400:
			next = sound = cur + 1;
			break;
		default:
			break;
		}
		if (next != -2) {
			cur = _r21sB0 = slotSwitch(next, cur);
			if (sound >= 0)
				playAt(sound);
		}
	}
	if (cur == 0xae) {
		targetAdd(0xae, true);
		cur = _r21sB0;
	}
	int d = place(cur, 0xb0, true);
	setDeadline(_r21sB0, now + (int16)d * kMsPerStep);
}

// 0x4179f0: the ball held up (0xed / 0xee)
void AtticScene::r21Tick208(uint32 now) {
	if (_r21s208 == -1 || now <= slot(_r21s208).deadline)
		return;
	slot(_r21s208).step++;
	int16 cur = _r21s208;
	if (slot(cur).sequenceLength <= slot(cur).step && (cur == 0xed || cur == 0xee))
		cur = _r21s208 = slotSwitch(cur, cur);
	int d = place(cur, cur, true);
	setDeadline(_r21s208, now + (int16)d * kMsPerStep);
}

// 0x417ad0: the hit ball flies on an arc from its start to 0x200 right, 0xf0 up
void AtticScene::r21Tick218(uint32 now) {
	if (_r21s218 == -1 || slot(_r21s218).deadline >= now)
		return;
	slot(_r21s218).step++;
	int16 cur = _r21s218;
	_r21Step++;
	if (slot(cur).sequenceLength <= slot(cur).step)
		cur = _r21s218 = slotSwitch(_r21Step < _r21Steps ? cur : -1, cur);
	if (cur == -1)
		return;
	slot(cur).x = ((_r21X2 - _r21X) * _r21Step) / _r21Steps + _r21X;
	slot(_r21s218).y = ((_r21Y2 - _r21Y) * _r21Step) / _r21Steps + _r21Y;
	slot(_r21s218).y -= (int)(sin((double)_r21Step * M_PI / _r21Steps) * _r21Arc);
	int d = place(_r21s218, _r21s218, true);
	setDeadline(_r21s218, now + (int16)d * kMsPerStep);
}

// ==== Room 22 (0x417c70..0x418130) ====

// 0x417c70
void AtticScene::r22Loaded() {
	_rooms[22].stages++;
	_rooms[22].flags = (_rooms[22].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x417cb0
int AtticScene::r22Load() {
	return submit(0x49aae0, 6, 0x16);
}

// 0x417db0
int AtticScene::r22Poll() {
	Room &r = _rooms[22];
	r.polled++;
	_r22Spin = 0;
	_r22Teleport = 0;
	_r22s183 = 0x183;
	if (resWait(0x49aae0))
		return 5;
	showBitmap(1, 0x16, 1, 30000, true);
	bmpSet(0x4e155c, r.view[0], r.view[1]);
	if (resWait(0x49ab04))
		return 5;
	bmpSet(0x4e1560, 0x190e, 0x5a2);
	showBitmap(1, 0x16, 2, 3, false);
	bmpPlace(0x4e1560);
	if (resWait(0x49ab28))
		return 5;
	if (!_r22MapAAdded) {
		addMap(map(0x5251e0));
		_r22MapAAdded = 1;
	}
	if (resWait(0x49ab4c))
		return 5;
	if (!_r22MapBAdded) {
		addMap(map(0x5251e8));
		_r22MapBAdded = 1;
	}
	if (resWait(0x49ab70))
		return 5;
	showSlot(0x183, 100, true);
	r.flags |= kRoomActive;
	if (ned()->getCostume() == 8)
		bmpVisible(0x4e1560, true);
	return 0;
}

// 0x417cd0
int AtticScene::r22Unload() {
	bmpVisible(0x4e155c, false);
	bmpVisible(0x4e1560, false);
	visible(0x183, false);
	_r22s183 = -1;
	stopSound(0x183);
	if (_r22MapAAdded) {
		removeMap(map(0x5251e0));
		_r22MapAAdded = 0;
	}
	if (_r22MapBAdded) {
		removeMap(map(0x5251e8));
		_r22MapBAdded = 0;
	}
	release(0x49aae0, 6);
	_rooms[22].flags = (_rooms[22].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x417fd0: the whirligig (0x183, spun by region 0x3f4); it throws Ned up into room 23
void AtticScene::r22Tick(uint32 now) {
	if (_r22Teleport) {
		Common::Point p = ned()->getSpritePosition(ned()->getCurSlot());
		if (p.y < -0x96) {
			_nedPos.x -= 100;
			for (int16 y = (int16)_rooms[23].view[3]; _rooms[23].view[1] <= y; y--) {
				if (solidAt(0x5251e8, _nedPos.x, y)) {
					_nedPos.y = y;
					ned()->resetIdle();
					_r22Teleport = 0;
					break;
				}
			}
		}
	}
	if (_r22s183 != -1 && slot(_r22s183).deadline < now) {
		Slot &s = slot(_r22s183);
		if (!_r22Spin)
			s.step = 0;
		else
			s.step++;
		if (_r22Spin && s.sequenceLength <= s.step) {
			_r22Spin = 0;
			s.step = 0;
		}
		int d = place(_r22s183, 0x183, true);
		setDeadline(_r22s183, now + (int16)d * kMsPerStep);
	}
}

// ==== Room 23 (0x418150..0x418510) ====

// 0x418150: loads only from room 22
bool AtticScene::r23ShouldLoad() {
	return _room == 0x16;
}

// 0x418160
void AtticScene::r23Loaded() {
	Room &r = _rooms[23];
	r.stages++;
	if (r.stages == 1)
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x4181a0
int AtticScene::r23Load() {
	return submit(0x49abb8, 4, 0x17);
}

// 0x418250
int AtticScene::r23Poll() {
	Room &r = _rooms[23];
	r.polled++;
	if (r.polled != 1)
		return 0;
	if (resWait(0x49abb8))
		return 5;
	showBitmap(1, 0x17, 1, 30000, true);
	bmpSet(0x4e16ec, r.view[0], r.view[1]);
	if (resWait(0x49abdc))
		return 5;
	bmpSet(0x4e16f0, 0x1a30, 0x1c6);
	showBitmap(1, 0x17, 2, 3, false);
	bmpPlace(0x4e16f0);
	bmpVisible(0x4e16f0, true);
	if (resWait(0x49ac00))
		return 5;
	bmpSet(0x4e16f4, 0x18de, 0x1a4);
	showBitmap(1, 0x17, 3, 3, false);
	bmpPlace(0x4e16f4);
	bmpVisible(0x4e16f4, true);
	if (resWait(0x49ac24))
		return 5;
	if (_r21Done) {
		_r23Time = _game->now() + 0x963;
		playAt(0xe7);
	}
	r.flags |= kRoomActive;
	return 0;
}

// 0x4181c0
int AtticScene::r23Unload() {
	if (_rooms[23].stages > 0) {
		bmpVisible(0x4e16ec, false);
		bmpVisible(0x4e16f0, false);
		bmpVisible(0x4e16f4, false);
		stopSound(0xe7);
	}
	release(0x49abb8, 4);
	_rooms[23].flags = (_rooms[23].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x418420: the end of the attic (the view reaches room 20: scene 1), and the drop into room 21
void AtticScene::r23Tick(uint32 now) {
	if (_endPan && -_rooms[20].view[0] == _scrollX && -_rooms[20].view[1] == _scrollY) {
		stopSound(0xe7);
		_pendingScene = 1;
	}
	if (_r23Drop) {
		_inputLocked = 1;
		ned()->show(false);
		ned()->enable(false);
		_nedPos.x = _rooms[21].view[0] + 200;
		setFacing(1);
		_nedPos.y = _rooms[21].view[1];
		_r23Drop = 0;
		_r21Drop = 1;
	}
	if (_r21Done && _r23Time < now) {
		_r23Time = now + 0x963;
		playAt(0xe7, 0xe7);
	}
}

// ==== Room 24 (0x418510..0x418630) ====

// 0x418510
void AtticScene::r24Loaded() {
	_rooms[24].stages++;
	_rooms[24].flags = (_rooms[24].flags & ~kRoomLoading) | kRoomLoaded;
}

// 0x418550
int AtticScene::r24Load() {
	return submit(0x49ac48, 1, 0x18);
}

// 0x4185c0
int AtticScene::r24Poll() {
	_rooms[24].polled++;
	if (resWait(0x49ac48))
		return 5;
	showBitmap(1, 0x18, 1, 30000, true);
	bmpSet(0x4e187c, _rooms[24].view[0], _rooms[24].view[1]);
	_rooms[24].flags |= kRoomActive;
	return 0;
}

// 0x418570
int AtticScene::r24Unload() {
	bmpVisible(0x4e187c, false);
	release(0x49ac48, 1);
	_rooms[24].flags = (_rooms[24].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// ==== Room 25 (0x418640..0x419120) ====

// 0x418640
void AtticScene::r25Loaded() {
	Room &r = _rooms[25];
	r.stages++;
	if (r.stages == 1 || r.stages == 2) {
		if (!(r.flags & kRoomCancel))
			r.flags |= kRoomMore;
		else
			r.flags |= kRoomAbort;
	} else if (r.stages == 3) {
		r.flags = (r.flags & ~kRoomLoading) | kRoomLoaded;
	}
	if (!(r.flags & kRoomAbort))
		r.flags |= kRoomArrived;
}

// 0x4186b0
int AtticScene::r25Load() {
	switch (_rooms[25].stages) {
	case 0:
		return submit(0x49ac90, 5, 0x19);
	case 1:
		return submit(0x49ad44, 2, 0x19);
	case 2:
		return submit(0x49ad8c, 2, 0x19);
	default:
		return _rooms[25].stages - 2;
	}
}

// 0x418870
int AtticScene::r25Poll() {
	Room &r = _rooms[25];
	r.polled++;
	int32 zero[4] = { 0, 0, 0, 0 };
	if (r.polled == 1) {
		if (resWait(0x49ac90))
			return 5;
		showBitmap(1, 0x19, 1, 30000, true);
		bmpSet(0x4e1a0c, r.view[0], r.view[1]);
		_r25Region = addRegion(zero, 0x80000, 0x1e8);
		_world.setUser(&_userMap);
		_r25s184 = 0x184;
		_r25Time184 = 0;
		_toRoom9 = 0;
		if (resWait(0x49acb4))
			return 5;
		showSlot(0x184, 3, false);
		if (resWait(0x49acd8))
			return 5;
		showSlot(0x185, 3, false);
		targetAdd(_r25s184, true);
		if (resWait(0x49ad20))
			return 5;
		showSlot(0x40, 100, true);
		r.flags |= kRoomActive;
		_r25s = 0x40;
		return 0;
	}
	if (r.polled == 2) {
		if (resWait(0x49ad44))
			return 5;
		showSlot(0x41, 100, false);
		_r25Time = _game->now();
		_r25Count = -1;
		return 0;
	}
	if (r.polled == 3) {
		if (resWait(0x49ad68))
			return 5;
		showSlot(0x43, 100, false);
	}
	return 0;
}

// 0x418710
int AtticScene::r25Unload() {
	int n = _rooms[25].stages;
	if (n > 0) {
		bmpVisible(0x4e1a0c, false);
		visible(0x40, false);
		visible(0x184, false);
		visible(0x185, false);
		_r25s184 = -1;
	}
	if (n > 1) {
		visible(0x41, false);
		stopSound(0x41);
	}
	if (n > 2) {
		visible(0x43, false);
		stopSound(0x43);
	}
	_r25s = -1;
	if (_r25Region)
		removeRegion(_r25Region);
	int count = n == 1 ? 5 : n == 2 ? 7 : n == 3 ? 9 : 0;
	release(0x49ac90, count);
	_rooms[25].flags = (_rooms[25].flags & ~kRoomUnloading) | kRoomIdle;
	return 0;
}

// 0x418a50: also brings Ned out in room 9 after he went into the hole of region 0x3fa
void AtticScene::r25Tick(uint32 now) {
	r25Tick184(now);
	r25TickPump(now);
	if (_r6Look && -_rooms[6].view[1] == _scrollY) {
		hint(1, facing() != 0);
		_inputLocked = 0;
		_r6Look = 0;
	}
	if (_toRoom9 && _rooms[9].polled > 1) {
		const Room &r9 = _rooms[9];
		_nedPos.x = r9.view[0] + 200;
		_jumpX = r9.view[0] - _scrollX;
		_cameraJump = 1;
		_jumpY = _scrollY + r9.view[1];
		for (int16 y = (int16)r9.view[3]; r9.view[1] <= y; y--) {
			if (solidAt(kBaseMaps, _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
		if (_rooms[9].polled > 1 && _r9s2B != 0x2c) {
			targetRemove(_r9s2B);
			_r9s2B = slotSwitch(0x2c, _r9s2B);
			int d = place(_r9s2B, 0x2b, true);
			setDeadline(_r9s2B, _game->now() + (int16)d * kMsPerStep);
			_r9Time = slot(_r9s2B).deadline + 6000;
			if (_r9Region)
				removeRegion(_r9Region);
			ned()->enable(true);
			autoWalk(kInputRight, 1, r9.view[0] + 0x140, 0);
			_toRoom9 = 0;
			playAt(0x2c, 0x185);
		}
	}
}

// 0x418cd0: the hole lid (0x184, 0x185 when hit); its box is region 0x1e8
void AtticScene::r25Tick184(uint32 now) {
	if (_r25s184 == -1 || now <= slot(_r25s184).deadline)
		return;
	int32 zero[4] = { 0, 0, 0, 0 };
	slot(_r25s184).step++;
	int16 cur = _r25s184;
	if (slot(cur).sequenceLength <= slot(cur).step) {
		targetRemove(_r25s184);
		if (cur == 0x184) {
			cur = _r25s184 = slotSwitch(0x184, cur);
			targetAdd(cur, true);
			if (!_r25Region) {
				_r25Region = addRegion(zero, 0x80000, 0x1e8);
				_world.setUser(&_userMap);
			}
		} else if (cur == 0x185) {
			if (_r25Time184 < now) {
				_r25s184 = slotSwitch(0x184, cur);
			} else {
				_r25s184 = slotSwitch(0x185, cur);
				playAt(0x185);
			}
			setStep(_r25s184, 0);
			targetAdd(_r25s184, true);
			cur = _r25s184;
		}
	}
	if (_r25Region)
		r9MoveRegion(_r25Region, cur, 0x184);
	int d = place(cur, 0x184, true);
	setDeadline(_r25s184, now + (int16)d * kMsPerStep);
}

// 0x418fa0: the pump (0x40 idle; 0x41 pumped by regions 0x3e9 / 0x3ea, 0x43 the last stroke)
void AtticScene::r25TickPump(uint32 now) {
	if (_r25s == -1 || now <= slot(_r25s).deadline)
		return;
	slot(_r25s).step++;
	int16 cur = _r25s;
	const ExeImage &exe = _game->exe();
	if (cur == 0x40) {
		if (slot(0x40).step >= slot(0x40).sequenceLength) {
			_r25s = slotSwitch(0x40, cur);
			setStep(_r25s, 0);
			cur = _r25s;
		}
	} else if (cur == 0x41) {
		int16 end = (int16)exe.readSint32(kR25Ends + _r25Count * 4);
		if (slot(0x41).step >= end) {
			_r25s = slotSwitch(0x41, cur);
			setStep(_r25s, end - 1);
			cur = _r25s;
		}
	} else if (cur == 0x43 && slot(0x43).step >= slot(0x43).sequenceLength) {
		_r25s = slotSwitch(0x41, cur);
		setStep(_r25s, (int16)exe.readSint32(kR25Ends + 8) - 1);
		cur = _r25s;
	}
	int d = 0;
	if (cur != -1) {
		d = place(cur, cur, true);
		cur = _r25s;
	}
	setDeadline(cur, now + (int16)d * kMsPerStep);
}

// 0x475180(which, facing, Ned's x, y, the room's scroll xform 0x404ef0 and copies, unused)
void AtticScene::hint(int which, bool facingRight) {
	_game->hints().trigger(which, facingRight, _nedPos.x, _nedPos.y, xform, this);
}

// The completion callbacks of the request lists (+0x1c of an entry)
void AtticScene::listCallback(uint32 fn) {
	switch (fn) {
	case 0x409450: r2Loaded(); break;
	case 0x40ace0: r3Loaded(); break;
	case 0x40b070: r4Loaded(); break;
	case 0x40bed0: r5Loaded(); break;
	case 0x40e100: r6Loaded(); break;
	case 0x40e230: r7Loaded(); break;
	case 0x40ecb0: r8Loaded(); break;
	case 0x40f510: r9Loaded(); break;
	case 0x40feb0: r10Loaded(); break;
	case 0x411060: r11Loaded(); break;
	case 0x4111e0: r12Loaded(); break;
	case 0x412930: r13Loaded(); break;
	case 0x4135d0: r15Loaded(); break;
	case 0x413790: r16Loaded(); break;
	case 0x4138c0: r17Loaded(); break;
	case 0x414400: r18Loaded(); break;
	case 0x415320: r19Loaded(); break;
	case 0x416a90: r21Loaded(); break;
	case 0x417c70: r22Loaded(); break;
	case 0x418160: r23Loaded(); break;
	case 0x418510: r24Loaded(); break;
	case 0x418640: r25Loaded(); break;
	default:
		warning("Attic: unknown list callback %x", fn);
		break;
	}
}

// +0x3c
int AtticScene::roomLoad(int r) {
	switch (r) {
	case 2: return r2Load();
	case 3: return r3Load();
	case 4: return r4Load();
	case 5: return r5Load();
	case 6: return r6Load();
	case 7: return r7Load();
	case 8: return r8Load();
	case 9: return r9Load();
	case 10: return r10Load();
	case 11: return r11Load();
	case 12: return r12Load();
	case 13: return r13Load();
	case 15: return r15Load();
	case 16: return r16Load();
	case 17: return r17Load();
	case 18: return r18Load();
	case 19: return r19Load();
	case 21: return r21Load();
	case 22: return r22Load();
	case 23: return r23Load();
	case 24: return r24Load();
	case 25: return r25Load();
	default: return -1;
	}
}

// +0x40
int AtticScene::roomPoll(int r) {
	switch (r) {
	case 2: return r2Poll();
	case 3: return r3Poll();
	case 4: return r4Poll();
	case 5: return r5Poll();
	case 6: return r6Poll();
	case 7: return r7Poll();
	case 8: return r8Poll();
	case 9: return r9Poll();
	case 10: return r10Poll();
	case 11: return r11Poll();
	case 12: return r12Poll();
	case 13: return r13Poll();
	case 15: return r15Poll();
	case 16: return r16Poll();
	case 17: return r17Poll();
	case 18: return r18Poll();
	case 19: return r19Poll();
	case 21: return r21Poll();
	case 22: return r22Poll();
	case 23: return r23Poll();
	case 24: return r24Poll();
	case 25: return r25Poll();
	default: return -1;
	}
}

// +0x44 (rooms 6, 15, 16 and 24 do nothing)
void AtticScene::roomTick(int r, uint32 now) {
	switch (r) {
	case 2: r2Tick(now); break;
	case 3: r3Tick(now); break;
	case 4: r4Tick(now); break;
	case 5: r5Tick(now); break;
	case 7: r7Tick(now); break;
	case 8: r8Tick(now); break;
	case 9: r9Tick(now); break;
	case 10: r10Tick(now); break;
	case 11: r11Tick(now); break;
	case 12: r12Tick(now); break;
	case 13: r13Tick(now); break;
	case 17: r17Tick(now); break;
	case 18: r18Tick(now); break;
	case 19: r19Tick(now); break;
	case 21: r21Tick(now); break;
	case 22: r22Tick(now); break;
	case 23: r23Tick(now); break;
	case 25: r25Tick(now); break;
	default: break;
	}
}

// +0x4c
int AtticScene::roomUnload(int r) {
	switch (r) {
	case 2: return r2Unload();
	case 3: return r3Unload();
	case 4: return r4Unload();
	case 5: return r5Unload();
	case 6: return r6Unload();
	case 7: return r7Unload();
	case 8: return r8Unload();
	case 9: return r9Unload();
	case 10: return r10Unload();
	case 11: return r11Unload();
	case 12: return r12Unload();
	case 13: return r13Unload();
	case 15: return r15Unload();
	case 16: return r16Unload();
	case 17: return r17Unload();
	case 18: return r18Unload();
	case 19: return r19Unload();
	case 21: return r21Unload();
	case 22: return r22Unload();
	case 23: return r23Unload();
	case 24: return r24Unload();
	case 25: return r25Unload();
	default: return -1;
	}
}

} // End of namespace NightmareNed
