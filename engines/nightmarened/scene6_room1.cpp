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
#include "nightmarened/scene6_room1.h"

namespace NightmareNed {

Scene6Room1::Scene6Room1(Game *game) : Scene6Room(game, 1) {
	const ExeImage &exe = _game->exe();
	_chose = exe.readSint32(0x4b011c);
	_knockNew = exe.readSint32(0x4b0120);
	_sound57 = exe.readSint32(0x4b0124);
	_sound54 = exe.readSint32(0x4b0128);
	_sound5d = exe.readSint32(0x4b012c);
	_closeNew = exe.readSint32(0x4b0130);
	_soundClose = exe.readSint32(0x4b0134);
	_flyNew = exe.readSint32(0x4b0138);
	_sound58 = exe.readSint32(0x4b013c);
	_vehicleNew = exe.readSint32(0x4b0140);
	_monster = -1;
	_action = 0;
	_ready = _box = _close = _open = _won = _leaving = _fly = _held = _toss = _spin = _hit = _busy = _done = 0;
	_set52 = _set57 = _set53 = _set54 = _setSpin = _setFly = _set17f = _unused854 = 0;
	_set55 = _set56 = _set58 = _set6e = _set6f = _set17d = 0;
	_spins = 0;
	_falling = 0;
	_flyDX = _flyDY = _flyX = _flyY = _aimX = _aimY = _flyHomeX = _flyHomeY = 0;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

uint32 Scene6Room1::entry(int id) const {
	return ((Scene6 *)_parent)->entryFor(id, kList, kCount);
}

bool Scene6Room1::loaded(int id) const {
	return ((Scene6 *)_parent)->isLoaded(entry(id));
}

// 0x4342b0 with the list's entries
void Scene6Room1::swap(int16 id) {
	hub()->swapShown(id, _monster, entry(id), _monster >= 0 ? entry(_monster) : 0);
	_monster = id;
}

void Scene6Room1::attachOnce(int &flag, int id, int z) {
	if (flag)
		return;
	hub()->attach(id, z);
	slot(id).step = -1;
	flag = 1;
}

// m2 0x45a610
int Scene6Room1::load(int fromScene, int fromSub) {
	_done = 0;
	if (fromScene != 6)
		hub()->prepareRoomDirect();
	return hub()->submit(kList, kCount, kNafScene);
}

// m3 0x45a640
int Scene6Room1::enter(int fromScene, int fromSub) {
	Scene6 *h = hub();
	int r = setupWorld(1, kList, kMapFloor);
	if (r)
		return r;
	RdxSurface *primary = _game->display().getPrimary();
	BitmapRec *room = h->bmp(kBmpRoom);
	if (room && room->object) {
		room->object->setDrawOrder(200);
		room->object->setDestination(primary);
		room->object->setVisibility(true);
	}
	// Unlike room 0's, the front bitmap goes to its record's position
	BitmapRec *front = h->bmp(kBmpFront);
	if (front && front->object) {
		front->object->setDrawOrder(Scene6::kNedZ - 1);
		front->object->setDestination(primary);
		front->object->setPosition(front->x, front->y);
		front->object->setVisibility(true);
	}
	if (h->palette(kPalette))
		_game->display().applyPalette(h->palette(kPalette), kPalUpper);
	showEntry(kList, kCount, 0x51, Scene6::kNedZ + 1, false);
	showEntry(kList, kCount, 0x52, Scene6::kNedZ + 1, false);
	showEntry(kList, kCount, 0x59, Scene6::kNedZ + 1, true);
	h->place(0x59, 0x59);
	showEntry(kList, kCount, 0x5d, Scene6::kNedZ + 2, true);
	slot(0x5d).step = 0;
	h->place(0x5d, 0x5d);
	if (!loaded(0x12f))
		return 5;
	h->attach(0x12f, Scene6::kNedZ + 1);
	slot(0x12f).step = -1;
	_action = 3;
	_targets.clear();
	Scene6::targetAdd(_targets, 0x5d);
	slot(0x12e).step = -1;
	if (slot(0x12f).sprite) {
		slot(0x12f).sprite->setPosition(0x1c7, 6);
		slot(0x12f).sprite->setCurrentImage(0);
	}
	slot(0x12f).step = -1;
	if (slot(0x12e).sprite)
		slot(0x12e).sprite->setCurrentImage(0);
	slot(0x12e).step = -1;
	if (slot(0x132).sprite)
		slot(0x132).sprite->setCurrentImage(0x15);
	if (slot(0x136).sprite)
		slot(0x136).sprite->setCurrentImage(0);
	if (slot(0x137).sprite)
		slot(0x137).sprite->setCurrentImage(0);
	h->visible(0x12f, true);
	h->visible(0x12e, true);
	h->visible(0x132, true);
	h->visible(0x136, false);
	h->visible(0x137, false);
	h->_timeUp = h->_beaten = 0;
	h->timerTick(0, true);

	h->_saved[Scene6::kWon1] = 0;
	_won = _leaving = _open = _close = _box = 0;
	_fly = _held = _toss = _spin = _hit = _busy = 0;
	_monster = -1;
	_set52 = _set57 = _set53 = _set54 = _setSpin = _setFly = _set17f = _unused854 = 0;
	_set55 = _set56 = _set58 = 0;
	h->_effect133 = h->_effect134 = 0;
	_set6e = _set6f = _set17d = 0;
	ned()->resetIdle();
	ned()->show(true);
	ned()->enable(true);
	int f = 0;
	// The scroll arguments are what the registers held (0x12f's position); nothing draws before the next tick
	ned()->tick(_world, 0, f, nedPos(), 0, 0x1c7, 6, &_hooks);
	h->openWipe();
	h->freeWipe();
	_game->_optionsAllowed = false;
	return 0;
}

// m4 0x45acd0
int Scene6Room1::leave(int toScene, int toSub) {
	return leaveRoom(toScene, Scene6::kVisited1);
}

// m5 0x45ad20
int Scene6Room1::unload(int toScene, int toSub) {
	return unloadRoom(toScene, kList, kCount);
}

// m7 0x45af40 (throttled like the playroom's)
int Scene6Room1::tick(uint32 input, uint32 now) {
	if (throttle(now))
		return 0;
	Scene6 *h = hub();
	heldTick(now);
	tossTick(now);
	spinTick(now);
	flyTick(now);
	boxTick(now);
	closeTick(now);
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
		debugC(3, kDebugGraphics, "Scene 6:1: input %x Ned %d,%d state %d slot %d action %d monster %d", input,
			nedPos().x, nedPos().y, ned()->getState(), ned()->getCurSlot(), _action, _monster);
	}
	if ((h->_beaten || h->_timeUp) && !_leaving)
		_action = 0xb;
	return 0;
}

// 0x45ae20: after a cycle the monster idles once, then picks an attack at random
void Scene6Room1::chooseAction() {
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
		if (_set53)
			hub()->switchSlotPre(0x53, _monster);
		break;
	case 6:
		if (_set52)
			hub()->switchSlotPre(0x52, _monster);
		break;
	case 7:
		if (_set55)
			hub()->switchSlotPre(0x55, _monster);
		break;
	case 8:
		if (_set54)
			hub()->switchSlotPre(0x54, _monster);
		break;
	default:
		break;
	}
}

// 0x45b1d0
void Scene6Room1::actionTick(uint32 now) {
	switch (_action) {
	case 0:
		_done = 1;
		hub()->switchTo(6, -1);
		return;
	case 1:
		swap(0x57);
		knockTick(now);
		return;
	case 2:
		swap(0x58);
		deathTick(now);
		return;
	case 3:
		swap(0x52);
		idleTick(now);
		hub()->visible(0x31, true);
		_action = 10;
		return;
	case 4:
		if (!hub()->_saved[Scene6::kWon1]) {
			swap(0x52);
			idleTick(now);
		}
		_monster = 0x52;
		_ready = 0;
		_action = 0;
		return;
	case 5:
		swap(0x53);
		attackTick(now);
		return;
	case 6:
		swap(0x52);
		idleTick(now);
		return;
	case 7:
		swap(0x55);
		throwTick(now);
		return;
	case 8:
		swap(0x54);
		attack2Tick(now);
		return;
	case 9:
		swap(0x56);
		openTick(now);
		return;
	case 10:
		swap(0x52);
		idleTick(now);
		vehicleIn(now);
		return;
	case 0xb:
		if (!hub()->_saved[Scene6::kWon1]) {
			swap(0x52);
			idleTick(now);
		}
		hub()->visible(0x191, false);
		_monster = 0x52;
		vehicleOut(now);
		return;
	case 0xc:
		swap(0x6f);
		inTick(now);
		return;
	case 0xd:
		swap(0x6e);
		outTick(now);
		return;
	default:
		return;
	}
}

// 0x45b1d0 case 10: the vehicle 0x31 leaves to the right, 25 pixels a step
void Scene6Room1::vehicleIn(uint32 now) {
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

// 0x45c9a0: the vehicle comes back from the right, backwards, and takes Ned home
void Scene6Room1::vehicleOut(uint32 now) {
	_ready = 0;
	int16 cur = ned()->getCurSlot();
	if (cur == 0x7e || cur == 0x36 || cur == 0x7f || cur == 0x105)
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

// 0x45b7f0: the idle cycle; its end lets the monster pick again, unless a throw is still out
void Scene6Room1::idleTick(uint32 now) {
	_ready = 0;
	attachOnce(_set52, 0x52, Scene6::kNedZ + 1);
	Slot &s = slot(0x52);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		s.deadline = 0;
		if (!_busy) {
			s.step = -1;
			s.deadline = 0;
			_ready = 1;
		}
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x52, 0x51) * kMsPerStep;
}

// 0x45b8c0: knocked back (0x57, with 0x191 over 0x5d); then open again (while _open), or 0x5d closes
void Scene6Room1::knockTick(uint32 now) {
	Scene6 *h = hub();
	_ready = 0;
	Slot &s = slot(0x57);
	Slot &over = slot(0x191);
	if (!_set57) {
		h->attach(0x57, Scene6::kNedZ + 1);
		h->attach(0x191, Scene6::kNedZ + 2);
		s.step = -1;
		over.step = -1;
		_set57 = 1;
	}
	if (_sound57) {
		h->playSound(0x57);
		_sound57 = 0;
	}
	if (_knockNew) {
		h->visible(0x5d, false);
		// 0x191 is at step -1 here, so this places nothing
		h->place(0x191, 0x5b);
		h->visible(0x191, true);
		s.step = -1;
		over.step = -1;
		_knockNew = 0;
	}
	if (s.deadline >= now)
		return;
	s.step++;
	over.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		over.step = -1;
		_ready = 1;
		_knockNew = 1;
		_sound57 = 1;
		if (_open) {
			h->visible(0x5d, true);
			h->visible(0x191, false);
			_action = 9;
			_ready = 0;
		} else {
			h->visible(0x5d, false);
			h->visible(0x191, false);
			h->visible(0x192, true);
			_close = 1;
			_box = 0;
		}
	}
	if (s.step != -1) {
		h->place(0x57, 0x51);
		int duration = h->place(0x191, 0x5d);
		s.deadline = now + (int16)duration * kMsPerStep;
	}
	if (_won) {
		h->visible(0x192, true);
		h->visible(0x191, false);
		_action = 2;
		_ready = 0;
		_close = 1;
		_box = 0;
	}
}

// 0x45bb10: the first attack; its sound plays when it catches Ned (0x45cdd0)
void Scene6Room1::attackTick(uint32 now) {
	_ready = 0;
	attachOnce(_set53, 0x53, Scene6::kNedZ + 1);
	Slot &s = slot(0x53);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x53, 0x51) * kMsPerStep;
}

// 0x45bbd0: open (0x56), a target from now on; it loops while _open, then goes back in (0x6f)
void Scene6Room1::openTick(uint32 now) {
	_ready = 0;
	if (!_set56) {
		hub()->attach(0x56, Scene6::kNedZ + 1);
		Scene6::targetAdd(_targets, 0x56);
		slot(0x56).step = -1;
		_set56 = 1;
	}
	Slot &s = slot(0x56);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		if (!_open) {
			s.step = 0;
			s.deadline = 0;
			_ready = 0;
			_action = 0xc;
		}
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x56, 0x51) * kMsPerStep;
}

// 0x45bd00: back in (0x6f)
void Scene6Room1::inTick(uint32 now) {
	_ready = 0;
	attachOnce(_set6f, 0x6f, Scene6::kNedZ + 1);
	Slot &s = slot(0x6f);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x6f, 0x51) * kMsPerStep;
}

// 0x45bdc0: coming out (0x6e); then open
void Scene6Room1::outTick(uint32 now) {
	_ready = 0;
	attachOnce(_set6e, 0x6e, Scene6::kNedZ + 1);
	Slot &s = slot(0x6e);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_action = 9;
		_open = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x6e, 0x51) * kMsPerStep;
}

// 0x45be90: the second attack; its sound at step 6
void Scene6Room1::attack2Tick(uint32 now) {
	_ready = 0;
	attachOnce(_set54, 0x54, Scene6::kNedZ + 1);
	Slot &s = slot(0x54);
	if (s.deadline >= now)
		return;
	s.step++;
	if (_sound54 && s.step == 6) {
		hub()->playSound(0x54);
		_sound54 = 0;
	}
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
		_sound54 = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x54, 0x51) * kMsPerStep;
}

// 0x45bf90: 0x5d plays once zapped; its frame 9 brings the monster out (0x6e)
void Scene6Room1::boxTick(uint32 now) {
	if (!_box)
		return;
	if (_sound5d) {
		hub()->playSound(0x5d);
		_sound5d = 0;
	}
	Slot &s = slot(0x5d);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		s.deadline = 0;
		_ready = 0;
		_box = 0;
		_close = 1;
		_open = 0;
		_sound5d = 1;
	}
	s.deadline = now + (int16)hub()->place(0x5d, 0x5d) * kMsPerStep;
	int frame = -1, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	if (frame == 9) {
		_action = 0xd;
		_open = 1;
	}
}

// 0x45c080: 0x192 plays over 0x5d (not while knocked back), then 0x5d shows again
void Scene6Room1::closeTick(uint32 now) {
	Scene6 *h = hub();
	if (!_close || _monster == 0x57) {
		_closeNew = 1;
		return;
	}
	Slot &s = slot(0x192);
	if (_closeNew) {
		h->attach(0x192, Scene6::kNedZ + 2);
		h->visible(0x192, true);
		h->visible(0x5d, false);
		s.step = -1;
		_closeNew = 0;
	}
	h->visible(0x5d, false);
	if (_soundClose) {
		h->playSound(0x5d);
		_soundClose = 0;
	}
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.deadline = 0;
		_ready = 0;
		_close = 0;
		s.step = 0;
		h->place(0x5d, 0x5d);
		h->visible(0x5d, true);
		h->visible(0x192, false);
		_closeNew = 1;
		_soundClose = 1;
	}
	s.deadline = now + (int16)h->place(0x192, 0x5d) * kMsPerStep;
}

// 0x45c1e0: 0x17e spins in place four times, then flies
void Scene6Room1::spinTick(uint32 now) {
	if (!_spin || !loaded(0x17e))
		return;
	attachOnce(_setSpin, 0x17e, Scene6::kNedZ + 1);
	Slot &s = slot(0x17e);
	if (s.deadline >= now)
		return;
	s.step++;
	hub()->visible(0x17e, true);
	if (s.sequenceLength <= s.step) {
		_spins++;
		s.step = 0;
		if (_spins > 3) {
			_spins = 0;
			_spin = 0;
			_fly = 1;
		}
	}
	s.deadline = now + (int16)hub()->place(0x17e, 0x17e) * kMsPerStep;
}

// 0x45c2f0: 0x17e flies 4 pixels a tick towards Ned's x at the throw, then drops; off screen or on Ned it goes home
void Scene6Room1::flyTick(uint32 now) {
	if (!_fly)
		return;
	Scene6 *h = hub();
	Slot &s = slot(0x17e);
	if (!_setFly) {
		h->attach(0x17e, Scene6::kNedZ + 1);
		_setFly = 1;
	}
	h->visible(0x17e, true);
	if (s.deadline < now) {
		s.step++;
		if (s.sequenceLength <= s.step)
			s.step = 0;
		s.deadline = now + (int16)h->place(0x17e, 0x17e) * kMsPerStep;
	}
	if (_flyNew) {
		_flyDX = 4;
		_flyDY = 4;
		_aimX = nedPos().x;
		_aimY = nedPos().y;
		_flyHomeX = s.x;
		_flyHomeY = s.y;
		// 0x430410's position out is where the sprite went
		h->place(0x17e, 0x17e);
		Common::Point p = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
		if (_aimX - p.x < 0)
			_flyDX = -_flyDX;
		_flyX = p.x;
		_flyY = p.y;
	}
	if (!_falling)
		_flyX += _flyDX;
	else
		_flyY += _flyDY;
	if (!_flyNew) {
		s.x = _flyX;
		s.y = _flyY;
		h->place(0x17e, 0x17e);
	}
	_flyNew = 0;
	if ((_flyDX > 0 && _flyX >= _aimX) || (_flyDX < 0 && _flyX <= _aimX))
		_falling = 1;
	if (_hit || _flyX > 0x280 || _flyX < 0 || _flyY > 0x1e0 || _flyY < 0) {
		_flyNew = 1;
		_hit = 0;
		_fly = 0;
		h->visible(0x17e, false);
		s.x = _flyHomeX;
		s.y = _flyHomeY;
		h->place(0x17e, 0x59);
		if (!_box)
			_ready = 1;
		_busy = 0;
		_falling = 0;
	}
}

// 0x45c570: 0x59, or 0x17f on it while the monster holds the thing
void Scene6Room1::heldTick(uint32 now) {
	if (!loaded(0x17f))
		return;
	Scene6 *h = hub();
	h->place(0x59, 0x59);
	attachOnce(_set17f, 0x17f, Scene6::kNedZ + 1);
	if (_box) {
		_held = 0;
		_busy = 0;
	}
	if (!_held) {
		h->visible(0x17f, false);
		h->visible(0x59, true);
		return;
	}
	h->visible(0x17f, true);
	h->visible(0x59, false);
	Slot &s = slot(0x17f);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step)
		s.step = 0;
	s.deadline = now + (int16)h->place(0x17f, 0x59) * kMsPerStep;
}

// 0x45c6b0: the toss (0x17d on 0x59); then 0x17e spins
void Scene6Room1::tossTick(uint32 now) {
	if (!_toss || !loaded(0x17d))
		return;
	_held = 0;
	attachOnce(_set17d, 0x17d, Scene6::kNedZ + 1);
	Slot &s = slot(0x17d);
	if (s.deadline >= now)
		return;
	s.step++;
	hub()->visible(0x17d, true);
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_toss = 0;
		hub()->visible(0x17d, false);
		_spin = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x17d, 0x59) * kMsPerStep;
}

// 0x45c7c0: the throw (0x55) picks the thing up; its end tosses it
void Scene6Room1::throwTick(uint32 now) {
	_ready = 0;
	_busy = 1;
	_held = 1;
	attachOnce(_set55, 0x55, Scene6::kNedZ + 1);
	Slot &s = slot(0x55);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_toss = 1;
		_action = 6;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x55, 0x51) * kMsPerStep;
}

// 0x45c8a0: dying; the room is won
void Scene6Room1::deathTick(uint32 now) {
	_ready = 0;
	attachOnce(_set58, 0x58, Scene6::kNedZ + 1);
	if (_sound58) {
		hub()->playSound(0x58);
		_sound58 = 0;
	}
	Slot &s = slot(0x58);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		hub()->_saved[Scene6::kWon1] = 1;
		_won = 1;
		_sound58 = 1;
		_action = 0xb;
		hub()->visible(0x58, false);
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x58, 0x51) * kMsPerStep;
}

static bool intersect(const int32 a[4], const int32 b[4]) {
	return MAX(a[0], b[0]) < MIN(a[2], b[2]) && MAX(a[1], b[1]) < MIN(a[3], b[3]);
}

// 0x45cdd0: the monster's box 1 (or the flying 0x17e's) against Ned's
void Scene6Room1::hitChecks(uint32 now) {
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
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x105 || ned()->getCurSlot() == 0x36)
			break;
		h->playSound(0x53);
		action = 0x16;
		break;
	case 6:
	case 9: {
		if (!_fly)
			break;
		Slot &s = slot(0x17e);
		h->stepRect(0x17e, s.step, 1, rect);
		ned()->getBox(cur, nedPos(), box);
		Common::Point sp = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
		rect[0] += sp.x;
		rect[2] += sp.x;
		rect[3] += sp.y;
		rect[1] += sp.y;
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x7f || ned()->getCurSlot() == 0x36)
			break;
		ned()->playAction(0xa, nedPos(), 0, 0);
		_hit = 1;
		h->damageStep();
		break;
	}
	case 8:
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x7e || ned()->getCurSlot() == 0x36)
			break;
		action = 9;
		break;
	default:
		break;
	}
	if (action != -1) {
		ned()->playAction(action, nedPos(), 0, 0);
		h->damageStep();
	}
	if (h->_beaten && ned()->getState() != 0xf && ned()->getCurSlot() != 0x19c) {
		ned()->playAction(0x1e, nedPos(), 0, 0);
		h->_beaten = 0;
	}
}

// ---- Hooks (DAT_0051bfd0) ----

// 0x45b050 (+0x00): button 2 aims the zap at the target in front of Ned (0x45cbf0)
int Scene6Room1::hookUse(void *user, int x, int y, int16 *state) {
	Scene6Room1 *r = (Scene6Room1 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	r->findTarget(dir, found, rect);
	r->ned()->setUseSlot(dir);
	return 1;
}

// 0x45b040 (+0x04)
int Scene6Room1::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	*state = 0xe;
	return 1;
}

// 0x45b1a0 (+0x08)
void Scene6Room1::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene6Room1 *r = (Scene6Room1 *)user;
	*a = 2;
	int16 dir = 0;
	r->findTarget(dir, *b, rect);
}

// 0x45b0a0 (+0x0c): 0x5d plays; the open monster (at any step) is hit
int Scene6Room1::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene6Room1 *r = (Scene6Room1 *)user;
	Scene6 *h = r->hub();
	if (r->_done || h->_timeUp || r->_won)
		return 1;
	if (*b == 0x56) {
		r->_action = 1;
		r->_ready = 0;
		Slot &m = r->slot(0x12f);
		m.step++;
		if (m.step == m.sequenceLength - 2)
			h->_effect133 = 1;
		if (m.sequenceLength - 1 <= m.step) {
			m.deadline = 0;
			m.step = m.sequenceLength - 1;
			h->_saved[Scene6::kWon1] = 1;
			r->_won = 1;
			h->_effect133 = 0;
			h->_effect134 = 1;
		}
		if (m.step != -1)
			h->place(0x12f, 0x12f);
	} else if (*b == 0x5d) {
		r->_box = 1;
	}
	return 1;
}

} // End of namespace NightmareNed
