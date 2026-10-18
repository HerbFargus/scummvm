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
#include "nightmarened/scene6_room0.h"

namespace NightmareNed {

Scene6Room0::Scene6Room0(Game *game) : Scene6Room(game, 0) {
	const ExeImage &exe = _game->exe();
	_chose = exe.readSint32(0x49e0b0);
	_sound46 = exe.readSint32(0x49e0b4);
	_sound4a = exe.readSint32(0x49e0b8);
	_sound50 = exe.readSint32(0x49e0bc);
	_sound23 = exe.readSint32(0x49e0c0);
	_sound17c = exe.readSint32(0x49e0c4);
	_sound4b = exe.readSint32(0x49e0c8);
	_sound47 = exe.readSint32(0x49e0cc);
	_sound48 = exe.readSint32(0x49e0d0);
	_shotNew = exe.readSint32(0x49e0d4);
	_sound4e = exe.readSint32(0x49e0d8);
	_sound4c = exe.readSint32(0x49e0dc);
	_vehicleNew = exe.readSint32(0x49e0e0);
	_monster = -1;
	_action = 0;
	_jar = _tip = _ready = _won = _leaving = _box = _unused90 = _open = _done = _shot = 0;
	_set46 = _set4b = _set50 = _set23 = _set47 = _setF8 = _setF7 = _set48 = _set4e = _set49 = _set4a = _set4c = 0;
	_nedHit = 0;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

uint32 Scene6Room0::entry(int id) const {
	return ((Scene6 *)_parent)->entryFor(id, kList, kCount);
}

bool Scene6Room0::loaded(int id) const {
	return ((Scene6 *)_parent)->isLoaded(entry(id));
}

// 0x4342b0 with the list's entries
void Scene6Room0::swap(int16 id) {
	hub()->swapShown(id, _monster, entry(id), _monster >= 0 ? entry(_monster) : 0);
	_monster = id;
}

void Scene6Room0::attachOnce(int &flag, int id, int z) {
	if (flag)
		return;
	hub()->attach(id, z);
	slot(id).step = -1;
	flag = 1;
}

// m2 0x425ae0
int Scene6Room0::load(int fromScene, int fromSub) {
	_done = 0;
	if (fromScene != 6)
		hub()->prepareRoomDirect();
	return hub()->submit(kList, kCount, kNafScene);
}

// m3 0x425b10
int Scene6Room0::enter(int fromScene, int fromSub) {
	Scene6 *h = hub();
	int r = setupWorld(3, kList, kMapFloor);
	if (r)
		return r;
	RdxSurface *primary = _game->display().getPrimary();
	BitmapRec *room = h->bmp(kBmpRoom);
	if (room && room->object) {
		room->object->setDrawOrder(200);
		room->object->setDestination(primary);
		room->object->setVisibility(true);
	}
	BitmapRec *front = h->bmp(kBmpFront);
	if (front && front->object) {
		front->object->setDrawOrder(Scene6::kNedZ - 1);
		front->object->setDestination(primary);
		front->object->setVisibility(true);
	}
	if (h->palette(kPalette))
		_game->display().applyPalette(h->palette(kPalette), kPalUpper);
	showEntry(kList, kCount, 0x45, Scene6::kNedZ + 1, false);
	showEntry(kList, kCount, 0x46, Scene6::kNedZ + 1, false);
	showEntry(kList, kCount, 0x4d, Scene6::kNedZ + 2, true);
	h->place(0x4d, 0x4d);
	showEntry(kList, kCount, 0x4f, Scene6::kNedZ + 1, true);
	h->place(0x4f, 0x4f);
	showEntry(kList, kCount, 0x1f, Scene6::kNedZ + 1, true);
	h->place(0x1f, 0x1f);
	showEntry(kList, kCount, 0x17c, Scene6::kNedZ + 1, true);
	h->place(0x17c, 0x17c);
	if (!loaded(0x131))
		return 5;
	h->attach(0x131, Scene6::kNedZ + 1);
	slot(0x131).step = -1;
	slot(0x12e).step = -1;
	_action = 3;
	_targets.clear();
	Scene6::targetAdd(_targets, 0x17c);
	if (slot(0x131).sprite) {
		slot(0x131).sprite->setPosition(0x1c7, 6);
		slot(0x131).sprite->setCurrentImage(0);
	}
	if (slot(0x12e).sprite)
		slot(0x12e).sprite->setCurrentImage(0);
	if (slot(0x132).sprite)
		slot(0x132).sprite->setCurrentImage(0x15);
	if (slot(0x136).sprite)
		slot(0x136).sprite->setCurrentImage(0);
	if (slot(0x137).sprite)
		slot(0x137).sprite->setCurrentImage(0);
	h->visible(0x131, true);
	slot(0x131).step = -1;
	h->visible(0x12e, true);
	slot(0x12e).step = -1;
	h->visible(0x132, true);
	h->visible(0x136, false);
	h->visible(0x137, false);
	h->timerTick(0, true);

	h->_saved[Scene6::kWon0] = 0;
	_won = _leaving = _box = _unused90 = 0;
	h->_timeUp = h->_beaten = 0;
	_monster = -1;
	_open = _shot = 0;
	h->_effect133 = h->_effect134 = 0;
	_set46 = _set4b = _set50 = _set23 = _set47 = _setF8 = _setF7 = _set48 = _set4e = _set49 = _set4a = _set4c = 0;
	_nedHit = 0;
	ned()->resetIdle();
	ned()->show(true);
	ned()->enable(true);
	int f = 0;
	// The scroll arguments are what the registers held (0x131's position); nothing draws before the next tick
	ned()->tick(_world, 0, f, nedPos(), 0, 0x1c7, 6, &_hooks);
	h->openWipe();
	h->freeWipe();
	_game->_optionsAllowed = false;
	return 0;
}

// m4 0x426260
int Scene6Room0::leave(int toScene, int toSub) {
	return leaveRoom(toScene, Scene6::kVisited0);
}

// m5 0x4262b0
int Scene6Room0::unload(int toScene, int toSub) {
	return unloadRoom(toScene, kList, kCount);
}

// m7 0x4264d0 (throttled like the playroom's)
int Scene6Room0::tick(uint32 input, uint32 now) {
	if (throttle(now))
		return 0;
	Scene6 *h = hub();
	if (!_box) {
		h->place(0x17c, 0x17c);
	} else {
		if (_sound17c) {
			h->playSound(0x17c);
			_sound17c = 0;
		}
		Slot &s = slot(0x17c);
		if (s.deadline < now) {
			s.step++;
			if (s.sequenceLength <= s.step) {
				s.step = 0;
				s.deadline = 0;
				_box = 0;
				_sound17c = 1;
			}
			s.deadline = now + (int16)h->place(0x17c, 0x17c) * kMsPerStep;
		}
	}
	h->effect134Tick(now);
	h->effect133Tick(now);
	if (_action != 3)
		h->timerTick(now, false);
	shotTick(now);
	tipTick(now);
	jarTick(now);
	if (!_leaving)
		chooseAction();
	int f = 0;
	ned()->tick(_world, input, f, nedPos(), now, 0, 0, &_hooks);
	hitChecks(now);
	actionTick(now);
	if (_done)
		return 0;
	draw();
	debugC(3, kDebugGraphics, "Scene 6:0: input %x Ned %d,%d state %d slot %d action %d monster %d step %d meter %d", input,
		nedPos().x, nedPos().y, ned()->getState(), ned()->getCurSlot(), _action, _monster, _monster >= 0 ? slot(_monster).step : -1,
		slot(0x131).step);
	if ((h->_beaten || h->_timeUp) && !_leaving)
		_action = 0xb;
	return 0;
}

// 0x4263b0: after a cycle the monster idles once, then picks an attack at random
void Scene6Room0::chooseAction() {
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
		if (_set47)
			hub()->switchSlotPre(0xf7, _monster);
		break;
	case 6:
		if (_set46)
			hub()->switchSlotPre(0x46, _monster);
		break;
	case 7:
		if (_set48)
			hub()->switchSlotPre(0x48, _monster);
		break;
	case 8:
		if (_set49)
			hub()->switchSlotPre(0x49, _monster);
		break;
	default:
		break;
	}
}

// 0x426810
void Scene6Room0::actionTick(uint32 now) {
	switch (_action) {
	case 0:
		_done = 1;
		hub()->switchTo(6, -1);
		return;
	case 1:
		swap(0x4b);
		knockTick(now);
		return;
	case 2:
		swap(0x4c);
		deathTick(now);
		return;
	case 3:
		swap(0x46);
		idleTick(now);
		hub()->visible(0x31, true);
		_action = 10;
		return;
	case 4:
		if (!hub()->_saved[Scene6::kWon0]) {
			swap(0x46);
			idleTick(now);
		}
		_monster = 0x46;
		_action = 0;
		return;
	case 5:
		swap(0xf7);
		attackTick(now);
		return;
	case 6:
		swap(0x46);
		idleTick(now);
		return;
	case 7:
		swap(0x48);
		attack2Tick(now);
		return;
	case 8:
		swap(0x49);
		throwTick(now);
		return;
	case 9:
		swap(0x4a);
		openTick(now);
		return;
	case 10:
		swap(0x46);
		idleTick(now);
		vehicleIn(now);
		return;
	case 0xb:
		if (!hub()->_saved[Scene6::kWon0]) {
			swap(0x46);
			idleTick(now);
		}
		_monster = 0x46;
		vehicleOut(now);
		return;
	case 0xc:
		swap(0x47);
		attackHitTick(now);
		return;
	case 0xd:
		swap(0xf8);
		attackEndTick(now);
		return;
	default:
		return;
	}
}

// 0x426810 case 10: the vehicle 0x31 leaves to the right, 25 pixels a step
void Scene6Room0::vehicleIn(uint32 now) {
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

// 0x427b40: the vehicle comes back from the right, backwards, and takes Ned home
void Scene6Room0::vehicleOut(uint32 now) {
	_ready = 0;
	int16 cur = ned()->getCurSlot();
	if (cur == 0x7e || cur == 0x36 || cur == 0x7d || cur == 0x7c)
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

// 0x426e20: the idle cycle; its end lets the monster pick again
void Scene6Room0::idleTick(uint32 now) {
	_ready = 0;
	attachOnce(_set46, 0x46, Scene6::kNedZ + 1);
	if (_sound46) {
		hub()->playSound(0x46);
		_sound46 = 0;
	}
	Slot &s = slot(0x46);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
		_sound46 = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x46, 0x45) * kMsPerStep;
}

// 0x426f10: open (0x4a), a target while it plays
void Scene6Room0::openTick(uint32 now) {
	_ready = 0;
	_open = 1;
	if (!_set4a) {
		hub()->attach(0x4a, Scene6::kNedZ + 1);
		Scene6::targetAdd(_targets, 0x4a);
		slot(0x4a).step = -1;
		_set4a = 1;
	}
	if (_sound4a) {
		hub()->playSound(0x4a);
		_sound4a = 0;
	}
	Slot &s = slot(0x4a);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.step == 0xc)
		hub()->playSound(0x4a);
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
		_open = 0;
		_sound4a = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x4a, 0x45) * kMsPerStep;
}

// 0x427060: the jar 0x50 (on 0x4f) rocks when Ned is hit; at its step 3 it tips 0x23
void Scene6Room0::jarTick(uint32 now) {
	if (!loaded(0x50))
		return;
	Slot &s = slot(0x50);
	if (!_jar) {
		s.step = 0;
		hub()->place(0x50, 0x4f);
		return;
	}
	if (!_set50) {
		hub()->attach(0x50, Scene6::kNedZ + 1);
		hub()->visible(0x4f, false);
		_set50 = 1;
	}
	if (_sound50) {
		hub()->playSound(0x50);
		_sound50 = 0;
	}
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.step == 3)
		_tip = 1;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		s.deadline = 0;
		_jar = 0;
		_sound50 = 1;
	}
	s.deadline = now + (int16)hub()->place(0x50, 0x4f) * kMsPerStep;
}

// 0x427190: 0x23 (on 0x1f); its sound is the jar's (slot 0x50)
void Scene6Room0::tipTick(uint32 now) {
	if (!loaded(0x23))
		return;
	Slot &s = slot(0x23);
	if (!_tip) {
		s.step = 0;
		hub()->place(0x23, 0x1f);
		return;
	}
	if (!_set23) {
		hub()->attach(0x23, Scene6::kNedZ + 1);
		hub()->visible(0x1f, false);
		_set23 = 1;
	}
	if (_sound23) {
		hub()->playSound(0x50);
		_sound23 = 0;
	}
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		s.deadline = 0;
		_tip = 0;
		_sound23 = 1;
	}
	s.deadline = now + (int16)hub()->place(0x23, 0x1f) * kMsPerStep;
}

// 0x427390: knocked back; then open again (while _open), or dying once won
void Scene6Room0::knockTick(uint32 now) {
	_ready = 0;
	attachOnce(_set4b, 0x4b, Scene6::kNedZ + 1);
	if (_sound4b) {
		hub()->playSound(0x4b);
		_sound4b = 0;
	}
	Slot &s = slot(0x4b);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_sound4b = 1;
		if (_open)
			_action = 9;
		_ready = _open == 0;
		if (_won) {
			_action = 2;
			_ready = 0;
		}
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x4b, 0x45) * kMsPerStep;
}

// 0x4274b0: the first attack; then 0x47 if it caught Ned, else 0xf8
void Scene6Room0::attackTick(uint32 now) {
	_ready = 0;
	attachOnce(_setF7, 0xf7, Scene6::kNedZ + 1);
	Slot &s = slot(0xf7);
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
		s.deadline = now + (int16)hub()->place(0xf7, 0x45) * kMsPerStep;
}

// 0x427590
void Scene6Room0::attackEndTick(uint32 now) {
	_ready = 0;
	attachOnce(_setF8, 0xf8, Scene6::kNedZ + 1);
	Slot &s = slot(0xf8);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0xf8, 0x45) * kMsPerStep;
}

// 0x427660 (drawn behind Ned)
void Scene6Room0::attackHitTick(uint32 now) {
	_ready = 0;
	attachOnce(_set47, 0x47, Scene6::kNedZ - 1);
	if (_sound47) {
		hub()->playSound(0x47);
		_sound47 = 0;
	}
	Slot &s = slot(0x47);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
		_nedHit = 0;
		_sound47 = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x47, 0x45) * kMsPerStep;
}

// 0x427750
void Scene6Room0::attack2Tick(uint32 now) {
	_ready = 0;
	attachOnce(_set48, 0x48, Scene6::kNedZ + 1);
	if (_sound48) {
		hub()->playSound(0x48);
		_sound48 = 0;
	}
	Slot &s = slot(0x48);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
		_sound48 = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x48, 0x45) * kMsPerStep;
}

// 0x427840: the throw; its end lets the shot fly
void Scene6Room0::throwTick(uint32 now) {
	_ready = 0;
	attachOnce(_set49, 0x49, Scene6::kNedZ + 1);
	Slot &s = slot(0x49);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_shot = 1;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x49, 0x45) * kMsPerStep;
}

// 0x427910: the shot 0x4e
void Scene6Room0::shotTick(uint32 now) {
	if (!_shot || !loaded(0x4e))
		return;
	attachOnce(_set4e, 0x4e, Scene6::kNedZ + 1);
	if (_shotNew) {
		hub()->switchSlotPre(0x4e, -1);
		_shotNew = 0;
	}
	if (_sound4e) {
		hub()->playSound(0x4e);
		_sound4e = 0;
	}
	Slot &s = slot(0x4e);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		s.deadline = 0;
		_shot = 0;
		hub()->visible(0x4e, false);
		_shotNew = 1;
		_sound4e = 1;
	}
	s.deadline = now + (int16)hub()->place(0x4e, 0x4e) * kMsPerStep;
}

// 0x427a40: dying; the room is won
void Scene6Room0::deathTick(uint32 now) {
	_ready = 0;
	attachOnce(_set4c, 0x4c, Scene6::kNedZ + 1);
	if (_sound4c) {
		hub()->playSound(0x4c);
		_sound4c = 0;
	}
	Slot &s = slot(0x4c);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_action = 0xb;
		hub()->_saved[Scene6::kWon0] = 1;
		_won = 1;
		hub()->visible(0x4c, false);
		_sound4c = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x4c, 0x45) * kMsPerStep;
}

static bool intersect(const int32 a[4], const int32 b[4]) {
	return MAX(a[0], b[0]) < MIN(a[2], b[2]) && MAX(a[1], b[1]) < MIN(a[3], b[3]);
}

// 0x427f70: the monster's box 1 (or the shot's) against Ned's
void Scene6Room0::hitChecks(uint32 now) {
	Scene6 *h = hub();
	if (_done || _monster == -1)
		return;
	int16 cur = ned()->getCurSlot();
	Slot &m = slot(_monster);
	if (m.step == -1)
		m.step = 0;
	int32 rect[4], box[4];
	h->stepRect(_monster, m.step, 1, rect);
	ned()->getBox(cur, nedPos(), box);
	Common::Point p = m.sprite ? m.sprite->getPosition() : Common::Point(0, 0);
	rect[0] += p.x;
	rect[2] += p.x;
	rect[3] += p.y;
	rect[1] += p.y;
	int action = -1;
	switch (_action) {
	case 5:
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x7c)
			break;
		ned()->playAction(0xe, nedPos(), 0, 0);
		_jar = 1;
		_nedHit = 1;
		h->damageStep();
		break;
	case 6:
	case 8:
	case 9: {
		if (!_shot)
			break;
		Slot &s = slot(0x4e);
		h->stepRect(0x4e, s.step, 1, rect);
		ned()->getBox(cur, nedPos(), box);
		Common::Point sp = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
		rect[2] += sp.x;
		rect[0] += sp.x;
		rect[3] += sp.y;
		rect[1] += sp.y;
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x7e)
			break;
		action = 9;
		break;
	}
	case 7:
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x7d || ned()->getCurSlot() == 0x7e)
			break;
		action = ned()->getFacing() ? 0xd : 9;
		break;
	default:
		break;
	}
	if (action != -1) {
		ned()->playAction(action, nedPos(), 0, 0);
		_jar = 1;
		h->damageStep();
	}
	if (h->_beaten && ned()->getState() != 0xf && ned()->getCurSlot() != 0x19c) {
		ned()->playAction(0x1e, nedPos(), 0, 0);
		h->_beaten = 0;
	}
}

// ---- Hooks (DAT_004bca48) ----

// 0x426670 (+0x00): button 2 aims the zap at the target in front of Ned (0x427d90)
int Scene6Room0::hookUse(void *user, int x, int y, int16 *state) {
	Scene6Room0 *r = (Scene6Room0 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	r->findTarget(dir, found, rect);
	r->ned()->setUseSlot(dir);
	return 1;
}

// 0x426660 (+0x04)
int Scene6Room0::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	*state = 0xe;
	return 1;
}

// 0x4267e0 (+0x08)
void Scene6Room0::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene6Room0 *r = (Scene6Room0 *)user;
	*a = 2;
	int16 dir = 0;
	r->findTarget(dir, *b, rect);
	debugC(2, kDebugGraphics, "Scene 6:0: target %d at %d,%d,%d,%d", *b, rect[0], rect[1], rect[2], rect[3]);
}

// 0x4266c0 (+0x0c): the box opens the monster; the open monster at step 11 is hit
int Scene6Room0::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene6Room0 *r = (Scene6Room0 *)user;
	Scene6 *h = r->hub();
	debugC(2, kDebugGraphics, "Scene 6:0: hit %d", *b);
	if (r->_done || h->_timeUp || r->_won)
		return 1;
	if (*b == 0x4a) {
		if (r->slot(0x4a).step != 0xb)
			return 1;
		r->_action = 1;
		r->_ready = 0;
		Slot &m = r->slot(0x131);
		m.step++;
		if (m.step == m.sequenceLength - 2)
			h->_effect133 = 1;
		if (m.sequenceLength - 1 <= m.step) {
			m.deadline = 0;
			m.step = m.sequenceLength - 1;
			h->_saved[Scene6::kWon0] = 1;
			r->_won = 1;
			h->_effect133 = 0;
			h->_effect134 = 1;
		}
		if (m.step != -1)
			h->place(0x131, 0x131);
	} else if (*b == 0x17c) {
		r->_box = 1;
		r->_ready = 0;
		r->_action = 9;
	}
	return 1;
}

} // End of namespace NightmareNed
