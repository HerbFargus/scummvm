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

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene3.h"

namespace NightmareNed {

// ---- Room 0 (0x468960..0x468ec0): the entrance hall, three doors and a grabber ----

// 0x468960
int Scene3::r0Load() {
	_doors[0] = 1;
	_doors[3] = 1;
	_doors[1] = 1;
	CollisionMap *m0 = map(0x51f684);
	door(_doors[0], m0, 0x15f, -1, -1);
	door(_doors[1], m0, 0x165, -1, -1);
	door(_doors[3], m0, -1, 6, 7);
	_r0Grabber = -1;
	_r0Entrance = -1;
	return submit(0x4b2580, 0xc);
}

// 0x468b10
int Scene3::r0Poll() {
	int polled = _rooms[0].polled++;
	if (polled == 0) {
		_rooms[0].flags |= kRoomActive;
		bmpPlace(0x4e0a68, _rooms[0].view[0], _rooms[0].view[1]);
		showBitmap(0x4b2580);
		showSprite(0x4b25a4, 0xb);
		switchSlot(0x15e, -1);
		place(0x15e, 0x15e, true);
		showSprite(0x4b25c8, 0xb);
		switchSlot(0x168, -1);
		place(0x168, 0x168, true);
		showSprite(0x4b25ec, _frontZ);
		switchSlot(0x19d, -1);
		place(0x19d, 0x19d, true);
		showSprite(0x4b2610, 4);
		switchSlot(0x19f, -1);
		place(0x19f, 0x19f, true, nullptr, xformRight);
		showSprite(0x4b2634, 9);
		_r0Grabber = switchSlotPre(0x21, -1);
		showSprite(0x4b267c, 0xc);
		showSprite(0x4b26a0, 0xc);
		switchSlotPre(0x11d, -1);
		showSprite(0x4b26c4, 0xc);
		switchSlotPre(0x11e, -1);
		showSprite(0x4b26e8, 0xc);
		switchSlotPre(0x120, -1);
		showSprite(0x4b270c, 6);
		_r0Entrance = switchSlotPre(0x179, -1);
	} else if (polled == 1) {
		showSprite(0x4b2730, 6);
		showSprite(0x4b2754, 6);
		targetAdd(0x11d, false);
		targetAdd(0x15f, false);
		targetAdd(0x11e, false);
		targetAdd(0x165, false);
		targetAdd(0x120, false);
		targetAdd(0x169, false);
	}
	return 0;
}

// 0x468ec0
void Scene3::r0Tick(uint32 now) {
	r0Grabber(now);
	place(0x15e, 0x15e, true);
	place(0x168, 0x168, true);
	doorTick(now, 0x15f, _doors[0], 0x15e, 0x11d);
	doorTick(now, 0x165, _doors[1], 0x168, 0x11e);
	doorTick(now, 0x169, _doors[3], 0x168, 0x120);
	r0Entrance(now);
}

// 0x468a20
int Scene3::r0Unload() {
	stopSound(0x21);
	targetRemove(0x11d);
	targetRemove(0x15f);
	targetRemove(0x11e);
	targetRemove(0x165);
	targetRemove(0x120);
	targetRemove(0x169);
	_r0Grabber = -1;
	_r0Entrance = -1;
	if (release(0x4b2580, 0x12) == 0) {
		_rooms[0].flags = (_rooms[0].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	_frontZ = 9;
	return -1;
}

// 0x46cdf0: the thing by the doors grabs Ned when he walks into it (action 0x2d)
void Scene3::r0Grabber(uint32 now) {
	int16 cur = _r0Grabber;
	if (cur == -1 || now <= slot(cur).deadline)
		return;
	Slot &s = slot(cur);
	s.step++;
	if (s.sequenceLength <= s.step)
		s.step = 0;
	if (s.step == 0)
		playNear(cur, cur, 0x21);
	int frame = 0;
	int duration = place(cur, 0x21, true, &frame);
	s.deadline = now + (int16)duration * kMsPerStep;
	if (ned()->getState() == 15)
		return;
	int32 rect[4];
	boxRect(cur, frame, 0, rect);
	Common::Point p = spritePos(cur);
	rect[0] += p.x;
	rect[2] += p.x;
	rect[1] += p.y;
	rect[3] += p.y;
	if (nedBoxHits(rect)) {
		nedSlot(0x183).offsets[0] = -10;
		ned()->setFinishCallback(grabFinished, this);
		ned()->playAction(0x2d, _nedPos, _scrollX, _scrollY);
	}
}

// 0x46d190: the entrance door opens while Ned walks in from the quilt, then closes
void Scene3::r0Entrance(uint32 now) {
	int16 cur = _r0Entrance;
	if (cur == -1 || now <= slot(cur).deadline)
		return;
	Slot &s = slot(cur);
	if (!_walkIn) {
		s.step--;
		if (s.step < 0) {
			s.step = 0;
			setDrawOrder(cur, 0xc);
		}
	} else {
		s.step++;
		if (s.sequenceLength <= s.step) {
			ned()->enable(true);
			_walkInDoor = 1;
			s.step = s.sequenceLength - 1;
		}
	}
	int duration = place(cur, 0x179, true);
	s.deadline = now + (int16)duration * kMsPerStep;
}

// 0x46e170: a door. Opening, the shut slot gives way to the animation (and the frame slot
// goes in front); shutting, it plays back and the shut slot returns.
void Scene3::doorTick(uint32 now, int16 anim, int open, int16 frame, int16 shut) {
	if (anim == -1)
		return;
	int16 cur = isVisible(shut) ? shut : anim;
	Slot &c = slot(cur);
	if (now <= c.deadline)
		return;
	if (!open) {
		if (cur != shut && c.step == c.sequenceLength - 1)
			playNear(anim, -1, shut);
		c.step--;
		if (c.step == c.sequenceLength - 2 && cur == anim) {
			if (frame != -1)
				setDrawOrder(frame, 0xb);
			setDrawOrder(cur, 0xc);
		}
		if (c.step < 0) {
			if (cur == anim) {
				switchSlot(shut, anim);
				cur = shut;
			} else {
				c.step = 0;
			}
		}
	} else {
		c.step++;
		if (c.step == 1) {
			if (cur == shut) {
				switchSlot(anim, shut);
				playNear(anim, -1, shut);
				cur = anim;
			}
			if (frame != -1)
				setDrawOrder(frame, 5);
			setDrawOrder(cur, 6);
		}
		Slot &n = slot(cur);
		if (n.sequenceLength <= n.step)
			n.step = n.sequenceLength - 1;
	}
	// The deadline always goes to the animation slot
	int duration = place(cur, shut, true);
	slot(anim).deadline = now + (int16)duration * kMsPerStep;
}

// ---- Room 1 (0x468fa0..0x469450): a creature that flees from the zap, two doors ----

// 0x468fa0
int Scene3::r1Load() {
	_doors[2] = 1;
	_doors[4] = 1;
	_r1Hit = 0;
	_r1Creature = -1;
	door(_doors[2], mapAt(3), 0x167, -1, -1);
	door(_doors[4], map(0x51f684), 0x16b, -1, -1);
	return submit(0x4b2808, 7);
}

// 0x469120
int Scene3::r1Poll() {
	int polled = _rooms[1].polled++;
	if (polled == 0) {
		_rooms[1].flags |= kRoomActive;
		bmpPlace(0x4e0a6c, _rooms[1].view[0], _rooms[1].view[1]);
		showBitmap(0x4b2808);
		BitmapRec *first = bmp(0x4e0a6c);
		bmpPlace(0x4e0a70, (first ? first->bitmap.surface.w : 0) + _rooms[1].view[0], _rooms[1].view[1]);
		showBitmap(0x4b282c);
		if (!_r1Gone) {
			showSprite(0x4b2850, 9);
			_r1Creature = switchSlotPre(1, -1);
		}
		showSprite(0x4b2874, 0xb);
		switchSlot(0x166, -1);
		place(0x166, 0x166, true);
		showSprite(0x4b2898, 0xb);
		switchSlot(0x16a, -1);
		place(0x16a, 0x16a, true);
		showSprite(0x4b28bc, 0xc);
		switchSlotPre(0x11f, -1);
		showSprite(0x4b28e0, 0xc);
		switchSlotPre(0x121, -1);
	} else if (polled == 1) {
		showSprite(0x4b2904, 9);
		showSprite(0x4b294c, 7);
		showSprite(0x4b2994, 0xc);
		showSprite(0x4b29b8, 0xc);
		targetAdd(0x11f, false);
		targetAdd(0x167, false);
		targetAdd(0x121, false);
		targetAdd(0x16b, false);
	}
	return 0;
}

// 0x469450
void Scene3::r1Tick(uint32 now) {
	prowler(now, _r1Creature, 1, 2, 3, _r1Hit, _r1Gone, 1);
	doorTick(now, 0x167, _doors[2], 0x166, 0x11f);
	doorTick(now, 0x16b, _doors[4], 0x16a, 0x121);
	place(0x166, 0x166, true);
	place(0x16a, 0x16a, true);
}

// 0x469050
int Scene3::r1Unload() {
	targetRemove(0x11f);
	targetRemove(0x167);
	targetRemove(0x121);
	targetRemove(0x16b);
	stopSound(2);
	stopSound(3);
	_r1Hit = 0;
	_r1Creature = -1;
	if (release(0x4b2808, 0xf) == 0) {
		_rooms[1].flags = (_rooms[1].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	return -1;
}

// 0x46bf10 (room 1: slots 1, 2, 3) and 0x46c2a0 (room 4: 4, 5, 0x127): a creature idles,
// then loops once its room is loaded, following the floor below the idle slot; touched
// (region hook 6 / 4) it flees left until it is off the screen, knocking Ned back
// (action 4) if it runs into him
void Scene3::prowler(uint32 now, int16 &cur, int16 idle, int16 loop, int16 flee, int &hit, int &gone, int room) {
	int16 old = cur;
	if (cur == -1 || now <= slot(cur).deadline)
		return;
	Slot &s = slot(cur);
	s.step++;
	if (s.sequenceLength <= s.step) {
		if (cur == idle && (_rooms[room].flags & kRoomLoaded)) {
			cur = switchSlot(loop, cur);
		} else if (cur == idle || cur == loop) {
			s.step = 0;
		} else if (cur == flee) {
			if (spritePos(flee).x < -slot(flee).w) {
				visible(flee, false);
				cur = -1;
			} else {
				s.step = 0;
				playNear(cur, cur, idle);
			}
		}
	}
	if (cur == -1)
		return;
	Slot &ref = slot(idle);
	int x = ref.x, y = ref.y, h = ref.h;
	for (int d = 0; d < 100; d++) {
		if (_world.isSolid((int16)x, (int16)(h + y + d))) {
			ref.y = ref.y + d;
			break;
		}
		if (_world.isSolid((int16)x, (int16)(h + y - d))) {
			ref.y = ref.y - d;
			break;
		}
	}
	if (slot(cur).step == 0)
		playNear(cur, old, idle);
	int frame = 0;
	int duration = place(cur, idle, true, &frame);
	if (!hit && cur == flee) {
		int32 rect[4];
		boxRect(flee, frame, 0, rect);
		Common::Point p = spritePos(cur);
		rect[0] += p.x;
		rect[2] += p.x;
		rect[1] += p.y;
		rect[3] += p.y;
		if (nedBoxHits(rect)) {
			ned()->playAction(4, _nedPos, _scrollX, _scrollY);
			gone = 1;
			hit = 1;
		}
	}
	if (cur == flee)
		ref.x -= 0x10;
	slot(cur).deadline = now + (int16)duration * kMsPerStep;
}

// ---- Room 2 (0x469500..0x4697a0): a creature that swallows Ned ----

// 0x469500
int Scene3::r2Load() {
	_r2Creature = -1;
	return submit(0x4b2a28, 4);
}

// 0x469620
int Scene3::r2Poll() {
	int polled = _rooms[2].polled++;
	if (polled == 0) {
		_rooms[2].flags |= kRoomActive;
		bmpPlace(0x4e0a74, _rooms[2].view[0], _rooms[2].view[1]);
		showBitmap(0x4b2a28);
		showSprite(0x4b2a4c, 9);
		_r2Creature = switchSlotPre(0xc, -1);
	} else if (polled == 1) {
		showSprite(0x4b2a70, 9);
		showSprite(0x4b2ab8, 9);
		showSprite(0x4b2b00, 9);
		targetAdd(_r2Creature, false);
		_r2Region = addRegion(4, 0x271d);
	}
	return 0;
}

// 0x469550
int Scene3::r2Unload() {
	stopSound(13);
	stopSound(14);
	stopSound(15);
	targetRemove(_r2Creature);
	_r2Creature = -1;
	removeRegion(_r2Region);
	if (release(0x4b2a28, 8) == 0) {
		_rooms[2].flags = (_rooms[2].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	return -1;
}

// 0x46e3c0: 0xc idles, 0xd looks out every 1..3 s, 0xe has Ned (region 0x271d) and spits
// him out (action 0x18), 0xf is the zapped creature, gone for good
void Scene3::r2Creature(uint32 now) {
	int16 cur = _r2Creature;
	if (cur == -1 || now <= slot(cur).deadline)
		return;
	Slot &s = slot(cur);
	s.step++;
	if (s.step == 2) {
		if (cur == 0xd)
			playNear(0xd, 0xd, 0xd);
	} else if (s.step == 0xb && cur == 0xe) {
		playNear(0xe, 0xe, 0xd);
	}
	if (s.sequenceLength <= s.step) {
		switch (cur) {
		case 0xc:
			if (_r2Time < now && (_rooms[2].flags & kRoomLoaded)) {
				targetRemove(cur);
				_r2Creature = switchSlot(0xd, cur);
				targetAdd(_r2Creature, false);
			} else {
				s.step = 0;
			}
			break;
		case 0xd:
			targetRemove(cur);
			_r2Creature = switchSlot(0xc, cur);
			targetAdd(_r2Creature, false);
			_r2Time = now + (_game->crtRandom() % 3 + 1) * 1000;
			break;
		case 0xe:
			targetRemove(cur);
			_r2Creature = switchSlot(0xd, cur);
			slot(_r2Creature).step = 0;
			targetAdd(_r2Creature, false);
			ned()->enable(true);
			ned()->playAction(0x18, _nedPos, _scrollX, _scrollY);
			break;
		case 0xf:
			if (_r2Region) {
				targetRemove(cur);
				removeRegion(_r2Region);
				setDrawOrder(cur, 9);
			}
			s.step = s.sequenceLength - 1;
			break;
		default:
			break;
		}
	}
	cur = _r2Creature;
	int frame = 0;
	int duration = place(cur, 0xd, true, &frame);
	if (_r2Region)
		moveRegionTo(_r2Region, cur, 0xd, frame, 1);
	slot(cur).deadline = now + (int16)duration * kMsPerStep;
}

// ---- Room 3 (0x4697b0..0x469b90): two creatures that scare Ned off, and run when zapped ----

// 0x4697b0
int Scene3::r3Load() {
	_r3a = -1;
	_r3b = -1;
	return submit(0x4b2b48, 3);
}

// 0x469940
int Scene3::r3Poll() {
	int polled = _rooms[3].polled++;
	if (polled == 0) {
		_rooms[3].flags |= kRoomActive;
		bmpPlace(0x4e0a78, _rooms[3].view[0], _rooms[3].view[1]);
		showBitmap(0x4b2b48);
		showSprite(0x4b2b6c, 7);
		showSprite(0x4b2b90, 7);
		if (!_r3Done) {
			_r3a = switchSlotPre(0x24, -1);
			_r3b = switchSlotPre(0x28, -1);
		}
	} else if (polled == 1) {
		showSprite(0x4b2bb4, 7);
		showSprite(0x4b2bfc, 0xa);
		showSprite(0x4b2c44, 6);
		showSprite(0x4b2c8c, 7);
		showSprite(0x4b2cb0, 0xa);
		showSprite(0x4b2cd4, 0xa);
		if (!_r3Done) {
			targetAdd(_r3a, false);
			targetAdd(_r3b, false);
			_r3aRegion = addRegion(4, 0x2734);
			_r3bRegion = addRegion(4, 0x2738);
		}
	}
	return 0;
}

// 0x469810
int Scene3::r3Unload() {
	stopSound(37);
	stopSound(38);
	stopSound(39);
	targetRemove(_r3a);
	targetRemove(_r3b);
	_r3a = -1;
	_r3b = -1;
	removeRegion(_r3aRegion);
	removeRegion(_r3bRegion);
	if (release(0x4b2b48, 0xc) == 0) {
		_rooms[3].flags = (_rooms[3].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	return -1;
}

// 0x46c650: 0x24 idles, 0x25 rises every 1..3 s, 0x26 scares Ned (region 0x2734: he turns
// right and jumps back, action 0xb), 0x27 runs off right once zapped
void Scene3::r3CreatureA(uint32 now) {
	int16 old = _r3a;
	if (old == -1)
		return;
	if (_r3aTouch) {
		_r3aTouch = 0;
		ned()->setFacing(1);
		nedSlot(0x7b).offsets[0] = -30;
		targetRemove(_r3a);
		_r3a = switchSlotPre(0x26, _r3a);
		ned()->playAction(0xb, _nedPos, _scrollX, _scrollY);
	}
	if (now <= slot(_r3a).deadline)
		return;
	Slot &s = slot(_r3a);
	s.step++;
	if (s.sequenceLength <= s.step) {
		switch (_r3a) {
		case 0x24:
			if (_r3aTime < now && (_rooms[3].flags & kRoomLoaded)) {
				targetRemove(_r3a);
				_r3a = switchSlot(0x25, _r3a);
				targetAdd(_r3a, false);
			} else {
				slot(0x24).step = 0;
			}
			break;
		case 0x25:
			targetRemove(_r3a);
			_r3a = switchSlot(0x24, _r3a);
			targetAdd(_r3a, false);
			_r3aTime = now + (_game->crtRandom() % 3 + 1) * 1000;
			break;
		case 0x26:
			targetRemove(0x26);
			_r3a = switchSlot(0x24, _r3a);
			targetAdd(_r3a, false);
			break;
		case 0x27:
			slot(0x27).step = 0;
			break;
		default:
			break;
		}
	}
	if (slot(_r3a).step == 0)
		playNear(_r3a, old, 0x24);
	int frame = 0, duration;
	if (_r3a == 0x27) {
		slot(0x24).x += 0x14;
		duration = place(_r3a, 0x24, true, &frame);
		if (0x500 < spritePos(_r3a).x) {
			_r3a = switchSlot(-1, _r3a);
			removeRegion(_r3aRegion);
		}
	} else {
		duration = place(_r3a, 0x24, true, &frame);
	}
	if (_r3aRegion)
		moveRegionTo(_r3aRegion, _r3a, 0x24, frame, 1);
	if (_r3a != -1)
		slot(_r3a).deadline = now + (int16)duration * kMsPerStep;
}

// 0x46ca20: the same for 0x28..0x2b (region 0x2738; it turns Ned left and runs off left)
void Scene3::r3CreatureB(uint32 now) {
	if (_r3b == -1)
		return;
	if (_r3bTouch) {
		_r3bTouch = 0;
		ned()->setFacing(0);
		nedSlot(0x7b).offsets[0] = -30;
		targetRemove(_r3b);
		playNear(0x2a, _r3b, 0x28);
		_r3b = switchSlot(0x2a, _r3b);
		ned()->playAction(0xb, _nedPos, _scrollX, _scrollY);
	}
	if (now <= slot(_r3b).deadline)
		return;
	Slot &s = slot(_r3b);
	s.step++;
	if (s.sequenceLength <= s.step) {
		switch (_r3b) {
		case 0x28:
			if (_r3bTime < now && (_rooms[3].flags & kRoomLoaded)) {
				targetRemove(_r3b);
				_r3b = switchSlot(0x29, _r3b);
				targetAdd(_r3b, false);
			} else {
				s.step = 0;
			}
			break;
		case 0x29:
			targetRemove(_r3b);
			_r3b = switchSlot(0x28, _r3b);
			targetAdd(_r3b, false);
			_r3bTime = now + (_game->crtRandom() % 3 + 1) * 1000;
			break;
		case 0x2a:
			targetRemove(_r3b);
			_r3b = switchSlot(0x28, _r3b);
			targetAdd(_r3b, false);
			break;
		case 0x2b:
			slot(0x2b).step = 0;
			break;
		default:
			break;
		}
	}
	int frame = 0, duration;
	if (_r3b == 0x2b) {
		slot(0x28).x -= 0x14;
		duration = place(_r3b, 0x28, true, &frame);
		if (spritePos(_r3b).x < -300) {
			_r3b = switchSlot(-1, _r3b);
			removeRegion(_r3bRegion);
		}
	} else {
		duration = place(_r3b, 0x28, true, &frame);
	}
	if (_r3bRegion)
		moveRegionTo(_r3bRegion, _r3b, 0x28, frame, 1);
	if (_r3b != -1)
		slot(_r3b).deadline = now + (int16)duration * kMsPerStep;
}

// ---- Room 4 (0x469bb0..0x469fa0): a creature that flees, and one that pulls Ned through the wall ----

// 0x469bb0
int Scene3::r4Load() {
	_r4a = -1;
	_r4b = -1;
	_r4aHit = 0;
	_r4Time = 0;
	return submit(0x4b2cf8, 5);
}

// 0x469ce0
int Scene3::r4Poll() {
	int polled = _rooms[4].polled++;
	if (polled == 0) {
		_rooms[4].flags |= kRoomActive;
		bmpPlace(0x4e0a7c, _rooms[4].view[0], _rooms[4].view[1]);
		showBitmap(0x4b2cf8);
		showSprite(0x4b2d1c, 9);
		if (!_r4Gone)
			_r4a = switchSlotPre(4, -1);
		showSprite(0x4b2d40, 9);
		slot(0x19).x += slot(0x18).w;
		if (!_saved[1])
			_r4b = switchSlotPre(0x18, -1);
		showSprite(0x4b2d64, 9);
		applyFlip(0x18, false);
		applyFlip(0x19, false);
	} else if (polled == 1) {
		showSprite(0x4b2dac, 9);
		showSprite(0x4b2df4, 9);
		showSprite(0x4b2e3c, 9);
		applyFlip(0x1a, false);
		applyFlip(0x1b, false);
		showSprite(0x4b2e84, 7);
		if (!_saved[1])
			targetAdd(_r4b, true);
	}
	return 0;
}

// 0x469c10
int Scene3::r4Unload() {
	targetRemove(_r4b);
	_r4aHit = 0;
	_r4Time = 0;
	_r4a = -1;
	_r4b = -1;
	stopSound(5);
	stopSound(25);
	stopSound(26);
	stopSound(27);
	stopSound(7);
	if (release(0x4b2cf8, 0xd) == 0) {
		_rooms[4].flags = (_rooms[4].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	return -1;
}

// 0x46d750: 0x18 idles facing Ned (flipped once he is 640 px past it), 0x19 turns, 0x1a
// reaches through the wall (sometimes, while Ned is in region 0x1e61) and carries Ned to the
// other side (action 3), 0x1b is zapped and gives vision 5 the first time
void Scene3::r4Creature(uint32 now) {
	if (_r4b == -1)
		return;
	if (nedIn1e61() && _game->crtRandom() % 0x1e == 2 && _r4b != 0x1a && _r4b != 0x1b &&
		(_rooms[4].flags & kRoomLoaded)) {
		targetRemove(_r4b);
		_r4b = switchSlotPre(0x1a, _r4b);
	}
	if (now <= slot(_r4b).deadline)
		return;
	bool fx = flipActive(0x18);
	bool loaded = _rooms[4].flags & kRoomLoaded;
	if (slot(0x18).x + 0x280 < _nedPos.x) {
		if (!fx && loaded) {
			for (int id = 0x18; id <= 0x1b; id++)
				applyFlip(id, true);
			r4Order();
			fx = true;
		}
	} else if (_nedPos.x < slot(0x18).x - 0x280 && fx && loaded) {
		for (int id = 0x18; id <= 0x1b; id++)
			applyFlip(id, false);
		r4Order();
		fx = false;
	}
	Slot &s = slot(_r4b);
	s.step++;
	if (s.sequenceLength <= s.step) {
		targetRemove(_r4b);
		switch (_r4b) {
		case 0x18:
			if (_r4Time < now && loaded)
				_r4b = switchSlot(0x19, _r4b);
			else
				s.step = 0;
			targetAdd(_r4b, true);
			break;
		case 0x19:
		case 0x1a:
			_r4b = switchSlot(0x18, _r4b);
			_r4Time = _game->crtRandom() % 3000 + 3000 + now;
			targetAdd(_r4b, true);
			break;
		case 0x1b:
			s.step = s.sequenceLength - 1;
			if (!_saved[1]) {
				_saved[1] = 1;
				hint(5, ned()->getFacing() != 0);
			}
			break;
		default:
			break;
		}
	}
	int16 cur = _r4b;
	bool sound;
	if (cur == 0x1a)
		sound = slot(0x1a).step == 5;
	else if (cur == 0x19)
		sound = slot(0x19).step == 1;
	else
		sound = slot(cur).step == 0;
	if (sound)
		playNear(cur, cur, 0x18);
	int16 ref = fx ? 0x19 : 0x18;
	int frame = 0;
	int duration = place(cur, ref, !fx, &frame);
	if (cur == 0x1a) {
		int32 rect[4];
		boxRect(0x1a, frame, fx ? 4 : 1, rect);
		int pos[2];
		attachPos(0x1a, ref, pos, !fx);
		rect[0] += pos[0];
		rect[2] += pos[0];
		rect[1] += pos[1];
		rect[3] += pos[1];
		int32 box[4];
		ned()->getBox(ned()->getCurSlot(), _nedPos, box);
		if (intersect(box, rect) && slot(cur).step == 4) {
			ned()->show(false);
			ned()->resetIdle();
			ned()->enable(false);
		}
		if (slot(cur).step == 6 && !ned()->isEnabled()) {
			sideStep(fx ? 8 : -8);
			ned()->enable(true);
			ned()->playAction(3, _nedPos, _scrollX, _scrollY);
		}
	}
	slot(cur).deadline = now + (int16)duration * kMsPerStep;
}

// 0x46d750: the draw orders it sets on turning (it tests room 8's creature, not its own)
void Scene3::r4Order() {
	if (_r8Creature == -1)
		return;
	setDrawOrder(0x18, 7);
	setDrawOrder(0x19, 7);
	if (_rooms[4].flags & kRoomLoaded)
		setDrawOrder(0x1a, 7);
}

// PtInRect(DAT_0051f5f0, Ned)
bool Scene3::nedIn1e61() const {
	return _region1e61[0] <= _nedPos.x && _nedPos.x < _region1e61[2] &&
		_region1e61[1] <= _nedPos.y && _nedPos.y < _region1e61[3];
}

// ---- Room 5 (0x469fc0..0x46a390): a creature that hides Ned, a door ----

// 0x469fc0
int Scene3::r5Load() {
	_r5Creature = -1;
	_r5Slot = -1;
	_r5Touch = 0;
	_doors[9] = 1;
	door(_doors[9], map(0x51f684), 0x175, 5, 4);
	return submit(0x4b2ed0, 6);
}

// 0x46a140
int Scene3::r5Poll() {
	int polled = _rooms[5].polled++;
	if (polled == 0) {
		_rooms[5].flags |= kRoomActive;
		bmpPlace(0x4e0a80, _rooms[5].view[0], _rooms[5].view[1]);
		showBitmap(0x4b2ed0);
		showSprite(0x4b2ef4, 0xc);
		switchSlotPre(0x126, -1);
		showSprite(0x4b2f18, 0xb);
		switchSlot(0x174, -1);
		place(0x174, 0x174, true);
		showSprite(0x4b2f3c, 9);
		_r5Creature = switchSlotPre(0x15, -1);
		showSprite(0x4b2f84, 10);
		_r5Slot = switchSlot(400, -1);
	} else if (polled == 1) {
		showSprite(0x4b2fa8, 0xc);
		showSprite(0x4b2fcc, 9);
		showSprite(0x4b3014, 9);
		targetAdd(0x126, false);
		targetAdd(0x175, false);
		targetAdd(_r5Creature, false);
		_r5Region = addRegion(4, 0x2725);
	}
	return 0;
}

// 0x46a390
void Scene3::r5Tick(uint32 now) {
	doorTick(now, 0x175, _doors[9], 0x174, 0x126);
	place(0x174, 0x174, true);
	r5Creature(now);
	if (_r5Slot != -1)
		place(_r5Slot, _r5Slot, true);
}

// 0x46a040
int Scene3::r5Unload() {
	targetRemove(0x126);
	targetRemove(0x175);
	targetRemove(_r5Creature);
	_r5Touch = 0;
	_r5Creature = -1;
	_r5Slot = -1;
	stopSound(21);
	stopSound(22);
	stopSound(23);
	removeRegion(_r5Region);
	if (release(0x4b2ed0, 0xc) == 0) {
		_rooms[5].flags = (_rooms[5].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	return -1;
}

// 0x46e790: 0x15 idles, 0x16 has Ned (region 0x2725) and lets him go, 0x17 is zapped
void Scene3::r5Creature(uint32 now) {
	if (_r5Creature == -1)
		return;
	if (_r5Touch) {
		_r5Touch = 0;
		targetRemove(_r5Creature);
		_r5Creature = switchSlotPre(0x16, _r5Creature);
		targetAdd(_r5Creature, false);
		nedHide();
	}
	if (now <= slot(_r5Creature).deadline)
		return;
	Slot &s = slot(_r5Creature);
	s.step++;
	if (s.sequenceLength <= s.step) {
		if (_r5Creature == 0x15) {
			slot(0x15).step = 0;
		} else if (_r5Creature == 0x16) {
			targetRemove(_r5Creature);
			_r5Creature = switchSlot(0x15, _r5Creature);
			targetAdd(_r5Creature, false);
			ned()->enable(true);
		} else if (_r5Creature == 0x17) {
			if (_r5Region) {
				targetRemove(_r5Creature);
				removeRegion(_r5Region);
			}
			s.step = s.sequenceLength - 1;
		}
	}
	int16 cur = _r5Creature;
	if (slot(cur).step == 0)
		playNear(cur, cur, 0x15);
	int frame = 0;
	int duration = place(cur, 0x15, true, &frame);
	// The original tests the slot number here, not the region (always true)
	if (cur != 0)
		moveRegionTo(_r5Region, cur, 0x15, frame, 0);
	slot(cur).deadline = now + (int16)duration * kMsPerStep;
}

// ---- Room 6 (0x46a420..0x46a710): a creature on the stairs, a door ----

// 0x46a420
int Scene3::r6Load() {
	slot(0x1c).offsets[0] = 4;
	_r6Creature = -1;
	_doors[5] = 1;
	door(_doors[5], map(0x51f684), 0x16d, 1, 0xf);
	return submit(0x4b3080, 3);
}

// 0x46a550
int Scene3::r6Poll() {
	int polled = _rooms[6].polled++;
	if (polled == 0) {
		_rooms[6].flags |= kRoomActive;
		CollisionMap *m0 = map(0x51f684);
		if (m0) {
			for (const CollisionMap::Region &r : m0->regions) {
				if (r.id == 0xc) {
					_r6MinX = r.rect.left;
					_r6MaxX = r.rect.right;
				}
			}
		}
		bmpPlace(0x4e0a84, _rooms[6].view[0], _rooms[6].view[1]);
		showBitmap(0x4b3080);
		showSprite(0x4b30a4, 7);
		showSprite(0x4b30c8, 0xc);
		switchSlotPre(0x122, -1);
		if (!_saved[0])
			_r6Creature = switchSlotPre(0x1c, -1);
	} else if (polled == 1) {
		showSprite(0x4b30ec, 7);
		showSprite(0x4b3134, 0xc);
		targetAdd(0x122, false);
		targetAdd(0x16d, false);
	}
	return 0;
}

// 0x46a710
void Scene3::r6Tick(uint32 now) {
	r6Creature(now);
	doorTick(now, 0x16d, _doors[5], -1, 0x122);
	if (_r6Enter) {
		ned()->enable(true);
		_r6Enter = 0;
		ned()->setCostume(5);
		ned()->playAction(0x2b, _nedPos, _scrollX, _scrollY);
		_upstairs = 1;
		setDrawOrder(0x176, 5);
		regionEnable(map(0x51f684), 0x176, false);
	}
}

// 0x46a4b0
int Scene3::r6Unload() {
	targetRemove(0x122);
	targetRemove(0x16d);
	targetRemove(_r6Creature);
	_r6Creature = -1;
	stopSound(29);
	if (release(0x4b3080, 7) == 0) {
		_rooms[6].flags = (_rooms[6].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	return -1;
}

// 0x46dd50: 0x1c sleeps, 0x1d paces region 0xc after Ned, following the floor; it knocks
// Ned back (action 4) at most every 4 s. Zapped, it vanishes (vision 4)
void Scene3::r6Creature(uint32 now) {
	int16 cur = _r6Creature;
	if (cur == -1 || now <= slot(cur).deadline)
		return;
	Slot &s = slot(cur);
	s.step++;
	Slot &base = slot(0x1c);
	if (s.sequenceLength <= s.step) {
		if (cur == 0x1c) {
			if (_rooms[6].flags & kRoomLoaded) {
				targetRemove(cur);
				_r6Creature = switchSlot(0x1d, cur);
				targetAdd(_r6Creature, true);
			} else {
				slot(0x1c).step = 0;
			}
		} else if (cur == 0x1d) {
			if (base.x < _r6MinX)
				base.offsets[0] = 8;
			else if (_r6MaxX < base.x)
				base.offsets[0] = -8;
			else if (_r6Time < now)
				base.offsets[0] = (_game->crtRandom() & 1) ? -8 : 8;
			else if (_nedPos.x < (_r6MaxX - _r6MinX) / 2 + _r6MinX)
				base.offsets[0] = 0x10;
			else
				base.offsets[0] = -0x10;
			slot(cur).step = 0;
		}
	}
	cur = _r6Creature;
	if (slot(cur).step == 0 && cur == 0x1d && _room == 6)
		playNear(0x1d, 0x1d, 0x1c);
	int x = base.w / 2 + base.x, y = base.y, h = base.h;
	for (int d = 0; d < 100; d++) {
		if (_world.isSolid((int16)x, (int16)(h + y + d))) {
			base.y = base.y + d;
			break;
		}
		if (_world.isSolid((int16)x, (int16)(h + y - d))) {
			base.y = base.y - d;
			break;
		}
	}
	int frame = 0;
	int duration = place(cur, 0x1c, true, &frame);
	base.x += base.offsets[0];
	slot(cur).deadline = now + (int16)duration * kMsPerStep;
	int32 rect[4];
	boxRect(cur, frame, 1, rect);
	Common::Point p = spritePos(cur);
	rect[0] += p.x;
	rect[2] += p.x;
	rect[1] += p.y;
	rect[3] += p.y;
	if (nedBoxHits(rect)) {
		if (ned()->getState() != 15) {
			ned()->dropToFloor(_world, _nedPos, 0x28, 3);
			ned()->playAction(4, _nedPos, _scrollX, _scrollY);
		}
		_r6Time = now + 4000;
	}
}

// ---- Room 7 (0x46a7c0..0x46ab10): the stairs' foot, two doors ----

// 0x46a7c0
int Scene3::r7Load() {
	_doors[6] = 1;
	_doors[7] = 1;
	CollisionMap *m0 = map(0x51f684);
	door(_doors[6], m0, -1, 8, 0xe);
	door(_doors[7], m0, -1, 9, 0xd);
	return submit(0x4b3180, 5);
}

// 0x46a8d0
int Scene3::r7Poll() {
	int polled = _rooms[7].polled++;
	if (polled == 0) {
		_rooms[7].flags |= kRoomActive;
		bmpPlace(0x4e0a88, _rooms[7].view[0], _rooms[7].view[1]);
		showBitmap(0x4b3180);
		showSprite(0x4b31a4, 0xc);
		switchSlotPre(0x123, -1);
		showSprite(0x4b31c8, 0xc);
		switchSlotPre(0x124, -1);
		showSprite(0x4b31ec, 0xb);
		switchSlot(0x16e, -1);
		place(0x16e, 0x16e, true);
		showSprite(0x4b3210, 0xb);
		switchSlot(0x176, -1);
		place(0x176, 0x176, true);
	} else if (polled == 1) {
		showSprite(0x4b3234, 0xc);
		showSprite(0x4b3258, 0xc);
		targetAdd(0x123, false);
		targetAdd(0x16f, false);
		targetAdd(0x124, false);
		targetAdd(0x171, false);
	}
	return 0;
}

// 0x46ab10
void Scene3::r7Tick(uint32 now) {
	doorTick(now, 0x16f, _doors[6], 0x16e, 0x123);
	doorTick(now, 0x171, _doors[7], 0x16e, 0x124);
	place(0x16e, 0x16e, true);
	place(0x176, 0x176, true);
	if (_r7Enter) {
		ned()->enable(true);
		_r7Enter = 0;
		ned()->setCostume(1);
		ned()->playAction(0x2a, _nedPos, _scrollX, _scrollY);
		setDrawOrder(0x176, 0xb);
		_upstairs = 0;
		regionEnable(map(0x51f684), 0x176, true);
	}
}

// 0x46a840
int Scene3::r7Unload() {
	targetRemove(0x123);
	targetRemove(0x16f);
	targetRemove(0x124);
	targetRemove(0x171);
	if (release(0x4b3180, 9) == 0) {
		_rooms[7].flags = (_rooms[7].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	return -1;
}

// ---- Room 8 (0x46ac30..0x46b210): a creature that swaps sides, a ride, a door ----

// 0x46ac30
int Scene3::r8Load() {
	_r8Slot = -1;
	_r8Creature = -1;
	_r8Time = 0;
	_r8Turn = 0;
	_doors[8] = 1;
	door(_doors[8], mapAt(11), 0x173, 0xb, 0xc);
	return submit(0x4b32c8, 9);
}

// 0x46add0
int Scene3::r8Poll() {
	int polled = _rooms[8].polled++;
	if (polled == 0) {
		_rooms[8].flags |= kRoomActive;
		BitmapRec *rec = bmp(0x4e0a8c);
		// Seen from the first rooms, the last one is left of x 0
		int x = (_room < 4) ? -(rec ? rec->bitmap.surface.w : 0) : _rooms[8].view[0];
		bmpPlace(0x4e0a8c, x, _rooms[8].view[1]);
		showBitmap(0x4b32c8);
		showSprite(0x4b32ec, 0xc);
		switchSlotPre(0x125, -1);
		showSprite(0x4b3310, 0xb);
		switchSlot(0x172, -1);
		place(0x172, 0x172, true);
		showSprite(0x4b3334, 10);
		switchSlot(0x1a0, -1);
		place(0x1a0, 0x1a0, true);
		showSprite(0x4b3358, 0xb);
		if (!_saved[2])
			_r8Slot = switchSlotPre(0xd1, -1);
		showSprite(0x4b337c, 9);
		if (!_r8Gone) {
			_r8Creature = switchSlotPre(8, -1);
			targetAdd(_r8Creature, true);
		}
		applyFlip(8, false);
		slot(9).x = slot(8).x + slot(8).w;
		slot(9).y = slot(8).y;
		showSprite(0x4b33a0, 9);
		applyFlip(9, false);
		showSprite(0x4b33e8, 0xb);
	} else if (polled == 1) {
		showSprite(0x4b340c, 9);
		applyFlip(10, false);
		showSprite(0x4b3454, 9);
		applyFlip(11, false);
		showSprite(0x4b34c0, 0xc);
		targetAdd(0x125, true);
		targetAdd(0x173, true);
	}
	return 0;
}

// 0x46b210
void Scene3::r8Tick(uint32 now) {
	r8Slot(now);
	r8Creature(now);
	doorTick(now, 0x173, _doors[8], 0x172, 0x125);
	place(0x172, 0x172, true);
	place(0x1a0, 0x1a0, true);
	if (_r8RideOn) {
		_r8RideOn = 0;
		ned()->setCostume(6);
		_r8Ride = 1;
	}
	if (_r8RideOff) {
		_r8RideOff = 0;
		ned()->setCostume(1);
		_r8Ride = 0;
	}
	if (_r8Action) {
		_r8Action = 0;
		ned()->setCostume(1);
		ned()->setFinishCallback(actionFinished, this);
		_game->_optionsAllowed = false;
		ned()->playAction(0x2c, _nedPos, _scrollX, _scrollY);
		_saved[2] = 1;
	}
}

// 0x46acc0
int Scene3::r8Unload() {
	targetRemove(_r8Creature);
	targetRemove(0x125);
	targetRemove(0x173);
	_r8Time = 0;
	_r8Turn = 0;
	_r8Slot = -1;
	_r8Creature = -1;
	stopSound(9);
	stopSound(10);
	stopSound(11);
	stopSound(210);
	if (release(0x4b32c8, 0x10) == 0) {
		_rooms[8].flags = (_rooms[8].flags & ~kRoomUnloading) | kRoomIdle;
		return 0;
	}
	return -1;
}

// 0x46d2d0: 8 waits facing Ned (flipped near the level's ends), 9 snaps every 3..6 s, 10 has
// Ned (regions 8 and 0x10, from behind) and drops him on its other side, 0xb turns round
void Scene3::r8Creature(uint32 now) {
	if (_r8Creature == -1)
		return;
	bool fx = flipActive(8);
	bool loaded = _rooms[8].flags & kRoomLoaded;
	if (_nedPos.x < 0x280 || slot(8).x + 0x280 < _nedPos.x) {
		if (!fx && loaded) {
			for (int id = 8; id <= 11; id++)
				applyFlip(id, true);
			fx = true;
		}
	} else if (_nedPos.x < slot(8).x - 0x280 && fx && loaded) {
		for (int id = 8; id <= 11; id++)
			applyFlip(id, false);
		fx = false;
	}
	if (_r8Zap) {
		_r8Zap = 0;
		targetRemove(_r8Creature);
		_r8Creature = switchSlotPre(10, _r8Creature);
		targetAdd(_r8Creature, false);
		ned()->show(false);
		ned()->resetIdle();
		ned()->enable(false);
	} else if (_r8Turn) {
		targetRemove(_r8Creature);
		playNear(0xb, _r8Creature, 8);
		_r8Creature = switchSlotPre(0xb, _r8Creature);
		targetAdd(_r8Creature, true);
		_r8Turn = 0;
	}
	if (now <= slot(_r8Creature).deadline)
		return;
	Slot &s = slot(_r8Creature);
	s.step++;
	if (s.sequenceLength <= s.step) {
		switch (_r8Creature) {
		case 8:
			if (_r8Time < now && loaded) {
				targetRemove(_r8Creature);
				_r8Creature = switchSlot(9, _r8Creature);
				targetAdd(_r8Creature, true);
			}
			slot(_r8Creature).step = 0;
			break;
		case 9:
			targetRemove(_r8Creature);
			// fall through
		case 0xb:
			_r8Creature = switchSlot(8, _r8Creature);
			_r8Time = _game->crtRandom() % 3000 + 3000 + now;
			targetAdd(_r8Creature, true);
			break;
		case 10:
			targetRemove(_r8Creature);
			_r8Creature = switchSlot(8, _r8Creature);
			targetAdd(_r8Creature, true);
			_r8Time = _game->crtRandom() % 3000 + 3000 + now;
			ned()->enable(true);
			if (!fx)
				_nedPos.x = slot(8).x + 0x83;
			else
				_nedPos.x = slot(9).x - 0x92;
			break;
		default:
			break;
		}
	}
	int16 cur = _r8Creature;
	if (cur == 9 ? slot(9).step == 5 : slot(cur).step == 0)
		playNear(cur, cur, 8);
	int duration = place(cur, fx ? 9 : 8, !fx);
	slot(cur).deadline = now + (int16)duration * kMsPerStep;
}

// 0x46d080: the ride's lever (0xd1, pulled: 0xd2)
void Scene3::r8Slot(uint32 now) {
	int16 cur = _r8Slot;
	if (cur == -1 || now <= slot(cur).deadline)
		return;
	Slot &s = slot(cur);
	s.step++;
	if (cur == 0xd2 && slot(0xd2).step == 3)
		playNear(0xd2, 0xd2, 0xd2);
	if (s.sequenceLength <= s.step) {
		if (cur == 0xd1)
			slot(0xd1).step = 0;
		else if (cur == 0xd2)
			_r8Slot = cur = switchSlot(-1, cur);
	}
	if (cur != -1) {
		int duration = place(cur, 0xd2, true);
		slot(cur).deadline = now + (int16)duration * kMsPerStep;
	}
}

// 0x46b3f0 / 0x46b450 / 0x46b510 (unreferenced draw-order helpers) are not ported

// ---- Hooks ----

// 0x4703b0 (+0x00): button 2. Behind the walls Ned only knocks (slot 0x181); zapping room
// 8's creature from behind makes it turn round.
int Scene3::hookUse(void *user, int x, int y, int16 *state) {
	Scene3 *s = (Scene3 *)user;
	int16 dir = 0x2f, found = -1;
	if (!s->_behind) {
		int32 rect[4];
		s->findTarget(dir, found, rect);
		if (found == s->_r8Creature && (s->_rooms[8].flags & kRoomActive)) {
			bool fx = s->flipActive(8);
			int f = s->ned()->getFacing();
			if ((f != 0 && !fx) || (f == 0 && fx))
				s->_r8Turn = 1;
		}
	} else {
		dir = 0x181;
	}
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x46fce0 (+0x04)
int Scene3::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	return ((Scene3 *)user)->region(id, state);
}

// 0x470e00 (+0x08): the area is 4
void Scene3::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene3 *s = (Scene3 *)user;
	*a = 4;
	int16 dir = 4;
	s->findTarget(dir, *b, rect);
}

// 0x4705b0 (+0x0c)
int Scene3::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	return ((Scene3 *)user)->hitTarget(*b);
}

// 0x46f1f0: the target in front of Ned, as a world rect (box 0 of its current frame). The
// doors only count while Ned stands in their region of map 0 (map 3 for 0x11f/0x167);
// otherwise the rect is far away. Targets of rooms not shown give 0.
int Scene3::findTarget(int16 &dir, int16 &found, int32 rect[4]) {
	found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, dir, unused, found))
		return 1;
	int16 id = found, ref = found;
	bool noMirror = true;
	int room = -1;
	int32 region = -1;
	CollisionMap *regions = map(0x51f684);
	switch (id) {
	case 8:
	case 9:
		room = 8;
		noMirror = !flipActive(9);
		ref = noMirror ? 8 : 9;
		break;
	case 0xc:
	case 0xd:
		room = 2;
		ref = 0xd;
		break;
	case 0x18:
	case 0x19:
		room = 4;
		noMirror = !flipActive(0x19);
		ref = noMirror ? 0x18 : 0x19;
		break;
	case 0x1c:
	case 0x1d:
		room = 6;
		ref = 0x1c;
		break;
	case 0x25:
		room = 3;
		ref = 0x24;
		break;
	case 0x29:
		room = 3;
		ref = 0x28;
		break;
	case 0x11d:
	case 0x15f:
		room = 0;
		region = 0x547;
		ref = 0x11d;
		break;
	case 0x11e:
	case 0x165:
		room = 0;
		region = 0x54d;
		ref = 0x11e;
		break;
	case 0x11f:
	case 0x167:
		room = 1;
		region = 0x54f;
		regions = mapAt(3);
		ref = 0x11f;
		break;
	case 0x120:
	case 0x169:
		room = 0;
		region = 0x551;
		ref = 0x120;
		break;
	case 0x121:
	case 0x16b:
		room = 1;
		ref = 0x121;
		break;
	case 0x122:
	case 0x16d:
		room = 6;
		region = 0x555;
		ref = 0x122;
		break;
	case 0x123:
	case 0x16f:
		room = 7;
		region = 0x557;
		ref = 0x123;
		break;
	case 0x124:
	case 0x171:
		room = 7;
		region = 0x559;
		ref = 0x124;
		break;
	case 0x125:
	case 0x173:
		room = 8;
		region = 0x55b;
		ref = 0x125;
		break;
	case 0x126:
	case 0x175:
		room = 5;
		region = 0x55d;
		ref = 0x126;
		break;
	default:
		break;
	}
	if (room != -1 && !(_rooms[room].flags & kRoomActive))
		return 0;
	if (region != -1 || id == 0x121 || id == 0x16b)
		dir = 0x2f;
	int pos[2] = { -25000, -25000 };
	bool inRegion = true;
	if (region != -1) {
		int32 rc[4];
		inRegion = regionRect(regions, region, rc) && rc[0] <= _nedPos.x && _nedPos.x < rc[2] &&
			rc[1] <= _nedPos.y && _nedPos.y < rc[3];
	}
	if (inRegion)
		attachPos(id, ref, pos, noMirror);
	int frame = currentFrame(id);
	boxRect(id, frame, 0, rect);
	rect[0] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	rect[2] += pos[0];
	return 1;
}

// 0x46fce0: the regions Ned walks into
int Scene3::region(int32 id, int16 *state) {
	uint32 now = _game->now();
	int f = ned()->getFacing();
	if (id == 0x1e61)
		return 0;
	if (id == _r2Region && id) {
		if (_r2Creature != 0xe && (_rooms[2].flags & kRoomLoaded)) {
			targetRemove(_r2Creature);
			_r2Creature = switchSlot(0xe, _r2Creature);
			int duration = place(_r2Creature, 0xd, true);
			slot(_r2Creature).deadline = now + (int16)duration * kMsPerStep;
			nedHide();
		}
		return 0;
	}
	if (id == 6) {
		if ((_rooms[1].flags & kRoomLoaded) && _r1Creature != 3 && _r1Creature != -1) {
			playNear(3, _r1Creature, 1);
			_r1Creature = switchSlot(3, _r1Creature);
			int duration = place(3, 1, true);
			slot(3).deadline = now + (int16)duration * kMsPerStep;
		}
		return 0;
	}
	if (id == 4) {
		if ((_rooms[4].flags & kRoomLoaded) && _r4a != 0x127 && _r4a != -1) {
			playNear(0x127, _r4a, 4);
			_r4a = switchSlot(0x127, _r4a);
			int duration = place(0x127, 4, true);
			slot(0x127).deadline = now + (int16)duration * kMsPerStep;
		}
		return 0;
	}
	if (id == 8) {
		if ((_rooms[8].flags & kRoomLoaded) && _r8Creature != -1 && !flipActive(8) && f != 0)
			_r8Zap = 1;
		return 0;
	}
	if (id == 0x10) {
		if ((_rooms[8].flags & kRoomLoaded) && _r8Creature != -1 && flipActive(8) && f == 0)
			_r8Zap = 1;
		return 0;
	}
	switch (id) {
	case 0x93f:
		if (f != 0) {
			_passRight = 1;
			_frontZ = 7;
			nedHide();
		}
		return 0;
	case 0x92f:
		if (f == 0) {
			_passLeft = 1;
			_frontZ = 9;
			nedHide();
		}
		return 0;
	default:
		break;
	}
	if (id == _r3aRegion && id) {
		if ((_rooms[3].flags & kRoomLoaded) && !_r3Done)
			_r3aTouch = 1;
		return 0;
	}
	if (id == _r3bRegion && id) {
		if (!_r3Done && (_rooms[3].flags & kRoomLoaded))
			_r3bTouch = 1;
		return 0;
	}
	if (id == _r5Region && id) {
		if (_rooms[5].flags & kRoomLoaded)
			_r5Touch = 1;
		return 0;
	}
	switch (id) {
	case 0x945:
		_exit = 8;
		return 0;
	case 0x937:
		_exit = 3;
		return 0;
	case 0x93d:
		_exit = 6;
		return 0;
	case 0x939:
		_exit = 4;
		return 0;
	case 0x93b:
		_exit = 5;
		return 0;
	case 0x173:
		*state = _r8Ride ? 4 : 0xe;
		return 1;
	case 0x941:
		if (f == 0) {
			_passUp = 1;
			nedHide();
		}
		return 0;
	case 0x946:
		if (f != 0) {
			_passDown = 1;
			nedHide();
		}
		return 0;
	case 0x935:
		if (f != 0)
			_exit = 7;
		return 0;
	case 0xd2b:
		if ((_rooms[8].flags & kRoomLoaded) && !_saved[2]) {
			if (_r8Slot == 0xd1) {
				_r8Slot = switchSlot(0xd2, 0xd1);
				int duration = place(_r8Slot, 0xd2, true);
				slot(_r8Slot).deadline = now + (int16)duration * kMsPerStep;
			}
			if (f == 0) {
				if (_r8Ride)
					_r8RideOff = 1;
			} else if (!_r8Ride) {
				_r8RideOn = 1;
			}
		}
		return 0;
	case 0x1113:
		if (_r8Slot != 0xd1 && f != 0) {
			if (!_saved[2])
				_r8Action = 1;
			regionEnable(mapAt(11), 0x1113, false);
			regionEnable(mapAt(11), 0xd2b, false);
		}
		return 0;
	case 0x55e:
		if (!_upstairs) {
			if (f != 0)
				_r6Enter = 1;
		} else if (f == 0) {
			_r7Enter = 1;
		}
		return 0;
	default:
		return 0;
	}
}

// 0x4705b0: what the zap does
int Scene3::hitTarget(int16 found) {
	uint32 now = _game->now();
	int f = ned()->getFacing();
	switch (found) {
	case 0xc:
	case 0xd: {
		targetRemove(_r2Creature);
		_r2Creature = switchSlot(0xf, _r2Creature);
		playNear(0xf, _r2Creature, 0xd);
		int duration = place(_r2Creature, 0xd, true);
		slot(_r2Creature).deadline = now + (int16)duration * kMsPerStep;
		removeMap(mapAt(2));
		addMap(mapAt(3));
		return 1;
	}
	case 8:
	case 9: {
		bool fx = flipActive(9);
		int z = slot(9).sprite ? slot(9).sprite->getDrawOrder() : 9;
		int at;
		if (f != 0 && fx)
			at = 9;
		else if (f == 0 && !fx)
			at = 8;
		else
			return 1;
		targetRemove(_r8Creature);
		_r8Creature = switchSlot(-1, _r8Creature);
		_puff.start(kArea, at, z, _game->now(), xform, this);
		_r8Gone = 1;
		return 1;
	}
	case 0x18:
	case 0x19: {
		targetRemove(_r4b);
		playNear(0x1b, _r4b, 0x18);
		_r4b = switchSlot(0x1b, _r4b);
		bool fx = flipActive(0x18);
		int duration = place(_r4b, fx ? 0x19 : 0x18, !fx);
		slot(_r4b).deadline = _game->now() + (int16)duration * kMsPerStep;
		return 1;
	}
	case 0x15:
		targetRemove(_r5Creature);
		playNear(0x17, _r5Creature, 0x15);
		_r5Creature = switchSlotPre(0x17, _r5Creature);
		r5Creature(0xffffffff);
		return 1;
	case 0x25:
	case 0x29: {
		targetRemove(_r3a);
		playNear(0x27, _r3a, 0x24);
		_r3a = switchSlot(0x27, _r3a);
		int duration = place(_r3a, 0x24, true);
		slot(_r3a).deadline = _game->now() + (int16)duration * kMsPerStep;
		targetRemove(_r3b);
		playNear(0x2b, _r3b, 0x28);
		_r3b = switchSlot(0x2b, _r3b);
		duration = place(_r3b, 0x28, true);
		slot(_r3b).deadline = _game->now() + (int16)duration * kMsPerStep;
		_r3Done = 1;
		return 1;
	}
	case 0x1c:
	case 0x1d:
		_r6Creature = switchSlot(-1, _r6Creature);
		_puff.start(kArea, 0x1c, 7, _game->now(), xform, this);
		_saved[0] = 1;
		hint(4, false);
		return 1;
	case 0x11d:
	case 0x15f:
		door(_doors[0], map(0x51f684), 0x15f, -1, -1);
		return 1;
	case 0x11e:
	case 0x165:
		door(_doors[1], map(0x51f684), 0x165, -1, -1);
		return 1;
	case 0x11f:
	case 0x167:
		door(_doors[2], mapAt(3), 0x167, -1, -1);
		return 1;
	case 0x120:
	case 0x169:
		door(_doors[3], map(0x51f684), -1, 6, 7);
		return 1;
	case 0x121:
	case 0x16b:
		door(_doors[4], map(0x51f684), 0x16b, -1, -1);
		return 1;
	case 0x122:
	case 0x16d:
		door(_doors[5], map(0x51f684), 0x16d, 1, 0xf);
		return 1;
	case 0x123:
	case 0x16f:
		door(_doors[6], map(0x51f684), -1, 8, 0xe);
		return 1;
	case 0x124:
	case 0x171:
		door(_doors[7], map(0x51f684), -1, 9, 0xd);
		return 1;
	case 0x125:
	case 0x173: {
		door(_doors[8], mapAt(11), 0x173, 0xb, 0xc);
		// Open, the creature goes behind the doorway (slot 0x1a0); shut, in front again
		int z = _doors[8] == 1 ? 3 : 9;
		if (_r8Creature != -1) {
			setDrawOrder(8, z);
			setDrawOrder(9, z);
			if (_rooms[8].flags & kRoomLoaded) {
				setDrawOrder(10, z);
				setDrawOrder(11, z);
			}
		}
		setDrawOrder(0x1a0, z + 1);
		return 1;
	}
	case 0x126:
	case 0x175:
		door(_doors[9], map(0x51f684), 0x175, 5, 4);
		return 1;
	default:
		return 1;
	}
}

} // End of namespace NightmareNed
