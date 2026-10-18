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

#include <math.h>

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene6_room3.h"

namespace NightmareNed {

Scene6Room3::Scene6Room3(Game *game) : Scene6Room(game, 3) {
	const ExeImage &exe = _game->exe();
	_intro = exe.readSint32(0x4a5514);
	_chose = exe.readSint32(0x4a5518);
	_swoopNew = exe.readSint32(0x4a551c);
	_spillNew = exe.readSint32(0x4a5520);
	_sound69 = exe.readSint32(0x4a5524);
	_doorOpens = exe.readSint32(0x4a5528);
	_sound67 = exe.readSint32(0x4a552c);
	_vehicleNew = exe.readSint32(0x4a5530);
	_monster = -1;
	_action = 0;
	_unused98 = _ready = _won = _leaving = _box = _open = _done = _spill = 0;
	_set5f = _set64 = _set60 = _set61 = _setF5 = _setF6 = _set181 = _set69 = _set67 = _set62 = _set63 = _set65 = 0;
	_nedHit = 0;
	_swoopNedX = _swoopNedY = 0;
	_arcIndex = 0;
	_anchorX = _anchorY = 0;
	_arcStep = 0;
	_swoopBack = _swoopTurned = _swoopReached = 0;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

uint32 Scene6Room3::entry(int id) const {
	return ((Scene6 *)_parent)->entryFor(id, kList, kCount);
}

bool Scene6Room3::loaded(int id) const {
	return ((Scene6 *)_parent)->isLoaded(entry(id));
}

// 0x4342b0 with the list's entries
void Scene6Room3::swap(int16 id) {
	hub()->swapShown(id, _monster, entry(id), _monster >= 0 ? entry(_monster) : 0);
	_monster = id;
}

void Scene6Room3::attachOnce(int &flag, int id, int z) {
	if (flag)
		return;
	hub()->attach(id, z);
	slot(id).step = -1;
	flag = 1;
}

// m2 0x44a240
int Scene6Room3::load(int fromScene, int fromSub) {
	_done = 0;
	if (fromScene != 6)
		hub()->prepareRoomDirect();
	return hub()->submit(kList, kCount, kNafScene);
}

// m3 0x44a270
int Scene6Room3::enter(int fromScene, int fromSub) {
	Scene6 *h = hub();
	int r = setupWorld(4, kList, kMapFloor);
	if (r)
		return r;
	RdxSurface *primary = _game->display().getPrimary();
	BitmapRec *room = h->bmp(kBmpRoom);
	if (room && room->object) {
		room->object->setDrawOrder(200);
		room->object->setDestination(primary);
		room->object->setVisibility(true);
	}
	// The front layer is shown only while 0x69 plays (0x44c270)
	BitmapRec *front = h->bmp(kBmpFront);
	if (front && front->object) {
		front->object->setDrawOrder(199);
		front->object->setDestination(primary);
		front->object->setVisibility(false);
	}
	if (h->palette(kPalette))
		_game->display().applyPalette(h->palette(kPalette), kPalUpper);
	showEntry(kList, kCount, 0x5e, Scene6::kNedZ + 1, false);
	showEntry(kList, kCount, 0x5f, Scene6::kNedZ + 3, false);
	slot(0x5f).step = -1;
	showEntry(kList, kCount, 0x68, Scene6::kNedZ + 1, true);
	showEntry(kList, kCount, 0x66, Scene6::kNedZ + 1, true);
	h->place(0x66, 0x66);
	showEntry(kList, kCount, 0x180, Scene6::kNedZ + 2, false);
	slot(0x180).step = -1;
	if (!loaded(0x135))
		return 5;
	h->attach(0x135, Scene6::kNedZ + 1);
	slot(0x12e).step = -1;
	_action = 3;
	_targets.clear();
	Scene6::targetAdd(_targets, 0x66);
	if (slot(0x135).sprite) {
		slot(0x135).sprite->setPosition(0x1c7, 6);
		slot(0x135).sprite->setCurrentImage(0);
	}
	slot(0x135).step = -1;
	if (slot(0x12e).sprite)
		slot(0x12e).sprite->setCurrentImage(0);
	slot(0x12e).step = -1;
	if (slot(0x132).sprite)
		slot(0x132).sprite->setCurrentImage(0x15);
	if (slot(0x136).sprite)
		slot(0x136).sprite->setCurrentImage(0);
	if (slot(0x137).sprite)
		slot(0x137).sprite->setCurrentImage(0);
	h->visible(0x135, true);
	h->visible(0x12e, true);
	h->visible(0x132, true);
	h->visible(0x136, false);
	h->visible(0x137, false);
	h->timerTick(0, true);

	h->_saved[Scene6::kWon3] = 0;
	_won = _leaving = _box = _unused98 = 0;
	h->_timeUp = h->_beaten = 0;
	_open = _spill = 0;
	_intro = 1;
	_monster = -1;
	h->_effect133 = h->_effect134 = 0;
	_set5f = _set64 = _set60 = _set61 = _setF5 = _setF6 = _set181 = _set69 = _set67 = _set62 = _set63 = _set65 = 0;
	_nedHit = 0;
	ned()->resetIdle();
	ned()->show(true);
	ned()->enable(true);
	int f = 0;
	// The scroll arguments are what the registers held (0x135's position); nothing draws before the next tick
	ned()->tick(_world, 0, f, nedPos(), 0, 0x1c7, 6, &_hooks);
	h->openWipe();
	h->freeWipe();
	_game->_optionsAllowed = false;
	return 0;
}

// m4 0x44a920
int Scene6Room3::leave(int toScene, int toSub) {
	return leaveRoom(toScene, Scene6::kVisited3);
}

// m5 0x44a970
int Scene6Room3::unload(int toScene, int toSub) {
	return unloadRoom(toScene, kList, kCount);
}

// m7 0x44ab90 (throttled like the playroom's)
int Scene6Room3::tick(uint32 input, uint32 now) {
	if (throttle(now))
		return 0;
	Scene6 *h = hub();
	boxTick(now);
	doorTick(now);
	introTick(now);
	spillTick(now);
	h->effect134Tick(now);
	h->effect133Tick(now);
	if (_action != 3)
		h->timerTick(now, false);
	if (!_leaving)
		chooseAction();
	int f = 0;
	ned()->tick(_world, input, f, nedPos(), now, 0, 0, &_hooks);
	hitChecks(now);
	actionTick(now);
	if (!_done) {
		draw();
		debugC(3, kDebugGraphics, "Scene 6:3: input %x Ned %d,%d state %d slot %d action %d monster %d", input,
			nedPos().x, nedPos().y, ned()->getState(), ned()->getCurSlot(), _action, _monster);
	}
	if ((h->_beaten || h->_timeUp) && !_leaving)
		_action = 0xb;
	return 0;
}

// 0x44ab90 (the same code as the dead 0x44bfa0): 0x180 plays its sequence once, shown
void Scene6Room3::introTick(uint32 now) {
	if (!_intro)
		return;
	hub()->visible(0x180, true);
	Slot &s = slot(0x180);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_intro = 0;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x180, 0x180) * kMsPerStep;
}

// 0x44aa70: after a cycle the monster idles once, then picks an attack at random
void Scene6Room3::chooseAction() {
	if (_chose && _ready && _action != 2) {
		_action = 6;
		_chose = 0;
		return;
	}
	_game->crtSeed(_game->now());
	if (!_ready || _action == 2)
		return;
	_chose = 1;
	_action = (int16)((int32)_game->crtRandom() % 4 + 5);
	switch (_action) {
	case 5:
		if (_set60)
			hub()->switchSlotPre(0x60, _monster);
		break;
	case 6:
		if (_set5f)
			hub()->switchSlotPre(0x5f, _monster);
		break;
	case 7:
		// Tests 0x61's flag, not 0xf5's
		if (_set61)
			hub()->switchSlotPre(0xf5, _monster);
		break;
	case 8:
		if (_set62)
			hub()->switchSlotPre(0x62, _monster);
		break;
	default:
		break;
	}
}

// 0x44ae90
void Scene6Room3::actionTick(uint32 now) {
	switch (_action) {
	case 0:
		_done = 1;
		hub()->switchTo(6, -1);
		return;
	case 1:
		swap(0x64);
		knockTick(now);
		return;
	case 2:
		swap(0x65);
		deathTick(now);
		return;
	case 3:
		swap(0x5f);
		idleTick(now);
		hub()->visible(0x31, true);
		_action = 10;
		return;
	case 4:
		if (!hub()->_saved[Scene6::kWon3]) {
			swap(0x5f);
			idleTick(now);
		}
		_monster = 0x5f;
		_action = 0;
		return;
	case 5:
		swap(0x60);
		swoopTick(now);
		return;
	case 6:
		swap(0x5f);
		idleTick(now);
		return;
	case 7:
		swap(0xf5);
		grabTick(now);
		return;
	case 8:
		swap(0x62);
		summonTick(now);
		return;
	case 9: {
		int16 old = _monster;
		swap(0x63);
		if (old != 0x63 && old >= 0)
			_game->stopSlotSound(slot(old));
		openTick(now);
		return;
	}
	case 10:
		swap(0x5f);
		idleTick(now);
		vehicleIn(now);
		return;
	case 0xb:
		// A swoop or a carry plays to its end first
		if (!hub()->_saved[Scene6::kWon3]) {
			if (_monster == 0x60) {
				swap(0x60);
				swoopTick(now);
			} else if (_monster == 0x61) {
				swap(0x61);
				carryTick(now);
			}
		}
		// Only the record changes: the last monster sprite stays shown
		if (_ready)
			_monster = 0x5f;
		vehicleOut(now);
		return;
	case 0xc:
		swap(0x61);
		carryTick(now);
		return;
	case 0xd:
		swap(0xf6);
		grabMissTick(now);
		return;
	default:
		return;
	}
}

// 0x44ae90 case 10: the vehicle 0x31 leaves to the right, 25 pixels a step
void Scene6Room3::vehicleIn(uint32 now) {
	_ready = 0;
	Slot &v = slot(0x31);
	if (v.deadline >= now)
		return;
	v.step++;
	if (v.sequenceLength <= v.step)
		v.step = 0;
	v.deadline = now + (int16)hub()->place(0x31, 0x2c) * kMsPerStep;
	Slot &home = slot(0x2c);
	home.x += 0x19;
	if (home.x > 0x280) {
		home.x = hub()->_vehicleHomeX;
		_action = 6;
	}
}

// 0x44c590: the vehicle comes back from the right, backwards, and takes Ned home
void Scene6Room3::vehicleOut(uint32 now) {
	_ready = 0;
	int16 cur = ned()->getCurSlot();
	if (cur == 0x7e || cur == 0x36 || cur == 0x81 || _monster == 0x61)
		return;
	if (cur == 0x19c && ned()->getSlotStep(0x19c) != 0x1f)
		return;
	Slot &v = slot(0x31);
	Slot &home = slot(0x2c);
	hub()->visible(0x31, true);
	if (_vehicleNew) {
		v.step = v.sequenceLength;
		home.x = 0x28d;
		hub()->place(0x31, 0x2c);
		hub()->visible(0x31, true);
		_vehicleNew = 0;
		Scene6::targetRemove(_targets, 0x66);
	}
	if (v.deadline >= now)
		return;
	v.step--;
	if (v.step < 0)
		v.step = v.sequenceLength - 1;
	v.deadline = now + (int16)hub()->place(0x31, 0x2c) * kMsPerStep;
	home.x -= 0x19;
	if (home.x <= hub()->_vehicleHomeX) {
		_vehicleNew = 1;
		_leaving = 1;
		home.x = hub()->_vehicleHomeX;
		_action = 4;
	}
}

// 0x44b590: the idle cycle (on 0x5e); its end lets the monster pick again
void Scene6Room3::idleTick(uint32 now) {
	_ready = 0;
	attachOnce(_set5f, 0x5f, Scene6::kNedZ + 1);
	Slot &s = slot(0x5f);
	if (s.step == -1)
		hub()->playSound(0x5f);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		// The original repeats these three under !_spill
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x5f, 0x5e) * kMsPerStep;
}

// 0x44b680: knocked back; then open again, or dying once won (checked on every step)
void Scene6Room3::knockTick(uint32 now) {
	_ready = 0;
	attachOnce(_set64, 0x64, Scene6::kNedZ + 1);
	Slot &s = slot(0x64);
	if (s.step == -1)
		hub()->playSound(0x64);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_action = 9;
		_ready = 0;
	}
	if (_won) {
		_action = 2;
		_ready = 0;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x64, 0x5e) * kMsPerStep;
}

// 0x441580 (only this room calls it)
void Scene6Room3::buildArc(const int32 rect[4], int reverse) {
	const ExeImage &exe = _game->exe();
	uint64 bits = ((uint64)exe.readUint32(kDegToRad + 4) << 32) | exe.readUint32(kDegToRad);
	double degToRad;
	memcpy(&degToRad, &bits, sizeof(degToRad));
	bits = ((uint64)exe.readUint32(kHalf + 4) << 32) | exe.readUint32(kHalf);
	double half;
	memcpy(&half, &bits, sizeof(half));
	int hw = (rect[2] - rect[0]) / 2;
	int hh = (rect[3] - rect[1]) / 2;
	double cx = rect[0] + hw;
	double cy = rect[1] + hh;
	double rx = (double)(hw * 2) * half;
	double ry = (double)(hh * 2) * half;
	// 0x48c500 malloc(0xb40), never freed; only the first 179 pairs are written
	_arc.clear();
	_arc.resize(kArcSize * 2);
	int angle = reverse ? 0xb5 : 0x168;
	for (int i = 0; i < kArcSize; i++) {
		double a = angle * degToRad;
		_arc[i * 2] = (int16)(int32)(cos(a) * rx + cx);
		_arc[i * 2 + 1] = (int16)(int32)(sin(a) * ry + cy);
		if (reverse) {
			if (++angle == 0x168)
				break;
		} else {
			if (--angle == 0xb5)
				break;
		}
	}
}

// 0x44b770: the swoop 0x60. The anchor 0x5e rides an arc from where it stands to 0x78
// left of Ned (holding step 4 until it passes Ned's x), then back along it, reversed.
void Scene6Room3::swoopTick(uint32 now) {
	Slot &a = slot(0x5e);
	Slot &s = slot(0x60);
	_ready = 0;
	if (!_set60) {
		hub()->attach(0x60, Scene6::kNedZ - 1);
		_set60 = 1;
		s.step = -1;
		_swoopNew = 1;
	}
	if (_swoopNew) {
		_swoopNew = 0;
		_arcIndex = 0;
		_arcStep = 0x14;
		_anchorX = a.x;
		_anchorY = a.y;
		_swoopNedX = nedPos().x;
		_swoopNedY = nedPos().y;
		int32 rect[4];
		rect[0] = nedPos().x - 0x78;
		rect[2] = a.x;
		rect[1] = a.y - a.h / 2;
		rect[3] = rect[1] + (a.h / 2) * 3;
		buildArc(rect, 0);
	}
	if (s.deadline >= now)
		return;
	if (!_swoopBack) {
		if (s.step != 4 || _swoopReached)
			s.step++;
		if (a.x - a.w / 2 <= _swoopNedX)
			_swoopReached = 1;
		if (s.step > s.sequenceLength) {
			s.step = s.sequenceLength;
			s.deadline = 0;
			_swoopReached = 0;
			_swoopTurned = 1;
			_arcStep = -_arcStep;
			_swoopBack = 1;
		}
		if (s.step == s.sequenceLength)
			return;
	} else {
		s.step--;
		if (s.step < 0) {
			s.step = -1;
			s.deadline = 0;
			_ready = 1;
			if (_open) {
				_action = 9;
				_ready = 0;
			}
			_swoopNew = 1;
			_swoopTurned = 0;
			a.x = _anchorX;
			a.y = _anchorY;
			_swoopBack = 0;
			_swoopReached = 0;
		}
		if (s.step == -1) {
			s.step = 0;
			hub()->place(0x60, 0x5e);
			s.step = -1;
			return;
		}
	}
	_arcIndex = (int16)(_arcIndex + _arcStep);
	if (_arcIndex > kArcLast)
		_arcIndex = kArcLast;
	if (_arcIndex < 0)
		_arcIndex = 0;
	a.x = _arc[_arcIndex * 2];
	a.y = _arc[_arcIndex * 2 + 1];
	s.deadline = now + (int16)hub()->place(0x60, 0x5e) * kMsPerStep;
}

// 0x44baa0: the grab; then 0x61 if it caught Ned, else 0xf6
void Scene6Room3::grabTick(uint32 now) {
	_ready = 0;
	attachOnce(_setF5, 0xf5, Scene6::kNedZ + 1);
	Slot &s = slot(0xf5);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 0;
		_action = _nedHit ? 0xc : 0xd;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0xf5, 0x5e) * kMsPerStep;
}

// 0x44bb80
void Scene6Room3::grabMissTick(uint32 now) {
	_ready = 0;
	attachOnce(_setF6, 0xf6, Scene6::kNedZ + 1);
	Slot &s = slot(0xf6);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0xf6, 0x5e) * kMsPerStep;
}

// 0x44bc50: Ned, hidden, is carried off; he is dropped 0xaa left of the anchor. Its last
// step rewinds to 0, not -1, so it is placed again.
void Scene6Room3::carryTick(uint32 now) {
	_ready = 0;
	attachOnce(_set61, 0x61, Scene6::kNedZ + 1);
	ned()->show(false);
	Slot &s = slot(0x61);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.step == 5)
		hub()->playSound(0x61);
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		s.deadline = 0;
		_ready = 1;
		_nedHit = 0;
		nedPos().x = slot(0x5e).x - 0xaa;
		ned()->setBasePosition(nedPos().x, nedPos().y);
		ned()->show(true);
		if (_open) {
			_action = 9;
			_ready = 0;
		}
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x61, 0x5e) * kMsPerStep;
}

// 0x44bd80: its end sets off 0x181
void Scene6Room3::summonTick(uint32 now) {
	_ready = 0;
	attachOnce(_set62, 0x62, Scene6::kNedZ + 1);
	Slot &s = slot(0x62);
	if (s.step == -1)
		hub()->playSound(0x62);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_spill = 1;
		_action = 6;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x62, 0x5e) * kMsPerStep;
}

// 0x44be60: 0x181 plays on 0x180 (moved down 0x41 meanwhile), then 0x180 replays
void Scene6Room3::spillTick(uint32 now) {
	if (!_spill || !loaded(0x181))
		return;
	attachOnce(_set181, 0x181, Scene6::kNedZ + 2);
	hub()->visible(0x181, true);
	hub()->visible(0x180, false);
	Slot &ref = slot(0x180);
	if (_spillNew) {
		ref.y += 0x41;
		_spillNew = 0;
	}
	Slot &s = slot(0x181);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_spillNew = 1;
		ref.y -= 0x41;
		_spill = 0;
		_intro = 1;
		hub()->visible(0x181, false);
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x181, 0x180) * kMsPerStep;
}

// 0x44c040: open (0x63), a target from now on; it holds step 7 while 0x69 plays
void Scene6Room3::openTick(uint32 now) {
	_ready = 0;
	if (!_set63) {
		hub()->attach(0x63, Scene6::kNedZ + 1);
		Scene6::targetAdd(_targets, 0x63);
		slot(0x63).step = -1;
		_set63 = 1;
	}
	Slot &s = slot(0x63);
	if (s.step == -1)
		hub()->playSound(0x63);
	if (s.deadline >= now)
		return;
	if (s.step != 7 || !_open)
		s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x63, 0x5e) * kMsPerStep;
}

// 0x44c180: dying; the room is won
void Scene6Room3::deathTick(uint32 now) {
	_ready = 0;
	attachOnce(_set65, 0x65, Scene6::kNedZ + 1);
	Slot &s = slot(0x65);
	if (s.step == -1)
		hub()->playSound(0x65);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_action = 0xb;
		hub()->_saved[Scene6::kWon3] = 1;
		_won = 1;
		hub()->visible(0x65, false);
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x65, 0x5e) * kMsPerStep;
}

// 0x44c270: while _open, 0x69 replaces 0x68 and the front layer shows; once per run it
// opens the monster unless it is busy, dying or leaving. It loops back to step 0.
void Scene6Room3::doorTick(uint32 now) {
	if (!_open || !loaded(0x69))
		return;
	Scene6 *h = hub();
	_ready = 0;
	if (!_set69) {
		h->attach(0x69, Scene6::kNedZ + 1);
		h->visible(0x69, true);
		slot(0x69).step = -1;
		h->visible(0x68, false);
		_set69 = 1;
		_sound69 = 1;
		_doorOpens = 1;
	}
	if (_doorOpens && _monster != 0x60 && _monster != 0x64 && _monster != 0x61 && _monster != 0x65 &&
			!h->_beaten && _action != 0xb && _action != 4 && _action != 0) {
		_action = 9;
		_doorOpens = 0;
	}
	BitmapRec *front = h->bmp(kBmpFront);
	if (front && front->object)
		front->object->setVisibility(true);
	if (_sound69) {
		h->playSound(0x69);
		_sound69 = 0;
	}
	Slot &s = slot(0x69);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		_open = 0;
		_sound69 = 1;
		_doorOpens = 1;
		if (front && front->object)
			front->object->setVisibility(false);
	}
	s.deadline = now + (int16)h->place(0x69, 0x68) * kMsPerStep;
}

// 0x44c420: the box 0x66, replaced by 0x67 while it plays; its step 5 sets _open
void Scene6Room3::boxTick(uint32 now) {
	if (!loaded(0x67))
		return;
	Scene6 *h = hub();
	attachOnce(_set67, 0x67, Scene6::kNedZ + 1);
	if (!_box) {
		h->place(0x66, 0x66);
		h->visible(0x67, false);
		h->visible(0x66, true);
		return;
	}
	h->visible(0x67, true);
	h->visible(0x66, false);
	if (_sound67) {
		h->playSound(0x67);
		_sound67 = 0;
	}
	Slot &s = slot(0x67);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_box = 0;
		_sound67 = 1;
	}
	if (s.step == 5)
		_open = 1;
	if (s.step != -1)
		s.deadline = now + (int16)h->place(0x67, 0x66) * kMsPerStep;
}

static bool intersect(const int32 a[4], const int32 b[4]) {
	return MAX(a[0], b[0]) < MIN(a[2], b[2]) && MAX(a[1], b[1]) < MIN(a[3], b[3]);
}

// 0x44c9d0: the monster's box 1 (or 0x181's, while it plays) against Ned's
void Scene6Room3::hitChecks(uint32 now) {
	Scene6 *h = hub();
	if (_done || _monster == -1)
		return;
	int16 cur = ned()->getCurSlot();
	Slot &m = slot(_monster);
	int step = m.step;
	if (step == -1)
		step = 0;
	if (m.sequenceLength <= step)
		step = m.sequenceLength - 1;
	int32 rect[4], box[4];
	h->stepRect(_monster, step, 1, rect);
	ned()->getBox(cur, nedPos(), box);
	Common::Point p = m.sprite ? m.sprite->getPosition() : Common::Point(0, 0);
	rect[0] += p.x;
	rect[2] += p.x;
	rect[3] += p.y;
	rect[1] += p.y;
	bool hit = false;
	switch (_action) {
	case 5:
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x81 || ned()->getCurSlot() == 0x36)
			break;
		ned()->playAction(0x13, nedPos(), 0, 0);
		h->playSound(0x60);
		hit = true;
		break;
	case 6: {
		if (!_spill)
			break;
		Slot &s = slot(0x181);
		h->stepRect(0x181, s.step, 1, rect);
		ned()->getBox(cur, nedPos(), box);
		Common::Point sp = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
		rect[2] += sp.x;
		rect[0] += sp.x;
		rect[3] += sp.y;
		rect[1] += sp.y;
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x7e)
			break;
		ned()->playAction(9, nedPos(), 0, 0);
		hit = true;
		break;
	}
	case 7:
		// The grab plays no action on Ned; 0x61 hides and carries him
		if (!intersect(rect, box) || _nedHit)
			break;
		_nedHit = 1;
		hit = true;
		break;
	default:
		break;
	}
	if (hit)
		h->damageStep();
	if (h->_beaten && ned()->getState() != 0xf && ned()->getCurSlot() != 0x19c) {
		ned()->playAction(0x1e, nedPos(), 0, 0);
		h->_beaten = 0;
	}
}

// ---- Hooks (DAT_004c4c68) ----

// 0x44ad10 (+0x00): button 2 aims the zap at the target in front of Ned (0x44c7f0)
int Scene6Room3::hookUse(void *user, int x, int y, int16 *state) {
	Scene6Room3 *r = (Scene6Room3 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	r->findTarget(dir, found, rect);
	r->ned()->setUseSlot(dir);
	return 1;
}

// 0x44ad00 (+0x04)
int Scene6Room3::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	*state = 0xe;
	return 1;
}

// 0x44ae60 (+0x08)
void Scene6Room3::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene6Room3 *r = (Scene6Room3 *)user;
	*a = 2;
	int16 dir = 0;
	r->findTarget(dir, *b, rect);
}

// 0x44ad60 (+0x0c): the box plays; the open monster is hit at any step
int Scene6Room3::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene6Room3 *r = (Scene6Room3 *)user;
	Scene6 *h = r->hub();
	if (r->_done || h->_timeUp || r->_won)
		return 1;
	if (*b == 0x63) {
		r->_action = 1;
		r->_ready = 0;
		Slot &m = r->slot(0x135);
		m.step++;
		if (m.step == m.sequenceLength - 2)
			h->_effect133 = 1;
		if (m.sequenceLength - 1 <= m.step) {
			m.deadline = 0;
			m.step = m.sequenceLength - 1;
			h->_saved[Scene6::kWon3] = 1;
			r->_won = 1;
			h->_effect133 = 0;
			h->_effect134 = 1;
		}
		if (m.step != -1)
			h->place(0x135, 0x135);
	} else if (*b == 0x66) {
		r->_box = 1;
	}
	return 1;
}

} // End of namespace NightmareNed
