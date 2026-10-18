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
#include "nightmarened/scene6_room2.h"

namespace NightmareNed {

Scene6Room2::Scene6Room2(Game *game) : Scene6Room(game, 2) {
	const ExeImage &exe = _game->exe();
	_leaving = exe.readSint32(0x4b5d64);
	_chose = exe.readSint32(0x4b5d68);
	_sound39 = exe.readSint32(0x4b5d6c);
	_sound3e = exe.readSint32(0x4b5d70);
	_lungeStep = (int16)exe.readUint16(0x4b5d74);
	_lungeNew = exe.readSint32(0x4b5d78);
	_flightNew = exe.readSint32(0x4b5d7c);
	_place40 = exe.readSint32(0x4b5d80);
	_sound41 = exe.readSint32(0x4b5d84);
	_sound44 = exe.readSint32(0x4b5d88);
	_sound3f = exe.readSint32(0x4b5d8c);
	_vehicleNew = exe.readSint32(0x4b5d90);
	_monster = -1;
	_action = 0;
	_homeX = _ready = _box = _won = _flying = _glow = _burst = _spin = _shotHit = _throwing = 0;
	_set39 = _set3e = _set3b = _set3a = _set184a = _set184b = _set41 = _set183 = _set3c = _set3d = _set44 = _set3f = 0;
	_done = _lungeHeld = _falling = 0;
	_spins = 0;
	_shotX = _shotY = _shotHomeX = _shotHomeY = _shotVY = _shotVX = _shotTargetX = _shotTargetY = 0;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

uint32 Scene6Room2::entry(int id) const {
	return ((Scene6 *)_parent)->entryFor(id, kList, kCount);
}

bool Scene6Room2::loaded(int id) const {
	return ((Scene6 *)_parent)->isLoaded(entry(id));
}

// 0x4342b0 with the list's entries
void Scene6Room2::swap(int16 id) {
	hub()->swapShown(id, _monster, entry(id), _monster >= 0 ? entry(_monster) : 0);
	_monster = id;
}

void Scene6Room2::attachOnce(int &flag, int id, int z) {
	if (flag)
		return;
	hub()->attach(id, z);
	slot(id).step = -1;
	flag = 1;
}

// m2 0x47d7f0
int Scene6Room2::load(int fromScene, int fromSub) {
	if (fromScene != 6)
		hub()->prepareRoomDirect();
	int r = hub()->submit(kList, kCount, kNafScene);
	if (r == 0)
		_done = 0;
	return r;
}

// m3 0x47d820
int Scene6Room2::enter(int fromScene, int fromSub) {
	Scene6 *h = hub();
	int r = setupWorld(2, kList, kMapFloor);
	if (r)
		return r;
	RdxSurface *primary = _game->display().getPrimary();
	BitmapRec *room = h->bmp(kBmpRoom);
	if (room && room->object) {
		room->object->setDrawOrder(200);
		room->object->setDestination(primary);
		room->object->setVisibility(true);
	}
	// Bitmap 2 (area 10) goes where its record says
	BitmapRec *front = h->bmp(kBmpFront);
	if (front && front->object) {
		front->object->setDrawOrder(Scene6::kNedZ - 1);
		front->object->setDestination(primary);
		front->object->setPosition(front->x, front->y);
		front->object->setVisibility(true);
	}
	if (h->palette(kPalette))
		_game->display().applyPalette(h->palette(kPalette), kPalUpper);
	showEntry(kList, kCount, 0x38, Scene6::kNedZ + 1, false);
	// 0x39's visibility call still takes 0x38's sprite (the register held it)
	if (loaded(0x39)) {
		h->attach(0x39, Scene6::kNedZ + 1);
		h->visible(0x38, false);
	}
	showEntry(kList, kCount, 0x40, Scene6::kNedZ + 2, true);
	h->place(0x40, 0x40);
	showEntry(kList, kCount, 0x44, Scene6::kNedZ + 1, false);
	Slot &box = slot(0x44);
	box.step = 0;
	int boxX = box.x, boxY = box.y;
	if (!loaded(0x130))
		return 5;
	h->attach(0x130, Scene6::kNedZ + 1);
	h->visible(0x130, false);
	slot(0x12e).step = -1;
	_action = 3;
	_targets.clear();
	Scene6::targetAdd(_targets, 0x44);
	if (box.sprite)
		box.sprite->setPosition(boxX, boxY);
	h->visible(0x44, true);
	if (slot(0x130).sprite) {
		slot(0x130).sprite->setPosition(0x1c7, 6);
		slot(0x130).sprite->setCurrentImage(0);
	}
	slot(0x130).step = -1;
	if (slot(0x12e).sprite)
		slot(0x12e).sprite->setCurrentImage(0);
	slot(0x12e).step = -1;
	if (slot(0x132).sprite)
		slot(0x132).sprite->setCurrentImage(0x15);
	if (slot(0x136).sprite)
		slot(0x136).sprite->setCurrentImage(0);
	if (slot(0x137).sprite)
		slot(0x137).sprite->setCurrentImage(0);
	h->visible(0x130, true);
	h->visible(0x12e, true);
	h->visible(0x132, true);
	h->visible(0x136, false);
	h->visible(0x137, false);
	h->switchSlot(0x39, -1);
	h->place(0x39, 0x38);
	h->timerTick(0, true);

	h->_saved[Scene6::kWon2] = 0;
	_won = _box = 0;
	h->_timeUp = h->_beaten = 0;
	_leaving = 0;
	_monster = -1;
	_flying = _glow = _burst = _spin = _shotHit = _throwing = 0;
	_set39 = _set3e = _set3b = _set3a = _set184a = _set184b = _set41 = _set183 = _set3c = _set3d = _set44 = _set3f = 0;
	h->_effect133 = h->_effect134 = 0;
	ned()->resetIdle();
	ned()->show(true);
	ned()->enable(true);
	int f = 0;
	// The scroll arguments are what the registers held (0x130's x, 0x44's y); nothing draws before the next tick
	ned()->tick(_world, 0, f, nedPos(), 0, 0x1c7, boxY, &_hooks);
	h->openWipe();
	h->freeWipe();
	_game->_optionsAllowed = false;
	return 0;
}

// m4 0x47ded0
int Scene6Room2::leave(int toScene, int toSub) {
	return leaveRoom(toScene, Scene6::kVisited2);
}

// m5 0x47df20
int Scene6Room2::unload(int toScene, int toSub) {
	return unloadRoom(toScene, kList, kCount);
}

// m7 0x47e140 (throttled like the playroom's)
int Scene6Room2::tick(uint32 input, uint32 now) {
	if (throttle(now))
		return 0;
	Scene6 *h = hub();
	glowTick(now);
	burstTick(now);
	spinTick(now);
	flightTick(now);
	boxTick(now);
	h->effect134Tick(now);
	h->effect133Tick(now);
	if (_action != 3)
		h->timerTick(now, false);
	int f = 0;
	ned()->tick(_world, input, f, nedPos(), now, 0, 0, &_hooks);
	if (!_leaving)
		chooseAction();
	hitChecks(now);
	int r = actionTick(now);
	// After case 0 the room is gone: the exe's last check below only sets _action, which m3 resets
	if (_done)
		return 0;
	if (!r)
		draw();
	debugC(3, kDebugGraphics, "Scene 6:2: input %x Ned %d,%d state %d slot %d action %d monster %d", input,
		nedPos().x, nedPos().y, ned()->getState(), ned()->getCurSlot(), _action, _monster);
	if ((h->_beaten || h->_timeUp) && !_leaving)
		_action = 0xb;
	return 0;
}

// 0x47e020: after a cycle the monster idles once, then picks an attack at random
void Scene6Room2::chooseAction() {
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
		if (_set3b)
			hub()->switchSlotPre(0x3b, _monster);
		break;
	case 6:
		if (_set39)
			hub()->switchSlotPre(0x39, _monster);
		break;
	case 7:
		if (_set3a)
			hub()->switchSlotPre(0x3a, _monster);
		break;
	case 8:
		if (_set3c)
			hub()->switchSlotPre(0x3c, _monster);
		break;
	default:
		break;
	}
}

// 0x47e3d0: returns 1 when it switched back to the playroom
int Scene6Room2::actionTick(uint32 now) {
	switch (_action) {
	case 0:
		_done = 1;
		ned()->show(false);
		hub()->switchTo(6, -1);
		return 1;
	case 1:
		swap(0x3e);
		knockTick(now);
		return 0;
	case 2:
		swap(0x3f);
		deathTick(now);
		return 0;
	case 3:
		swap(0x39);
		idleTick(now);
		hub()->visible(0x31, true);
		_action = 10;
		return 0;
	case 4:
		if (!hub()->_saved[Scene6::kWon2]) {
			swap(0x39);
			idleTick(now);
		}
		_monster = 0x39;
		_action = 0;
		return 0;
	case 5:
		swap(0x3b);
		lungeTick(now);
		return 0;
	case 6:
		swap(0x39);
		idleTick(now);
		return 0;
	case 7:
		swap(0x3a);
		swipeTick(now);
		return 0;
	case 8:
		swap(0x3c);
		throwTick(now);
		return 0;
	case 9:
		swap(0x3d);
		openTick(now);
		return 0;
	case 10:
		swap(0x39);
		idleTick(now);
		vehicleIn(now);
		return 0;
	case 0xb:
		if (!hub()->_saved[Scene6::kWon2]) {
			swap(0x39);
			idleTick(now);
		}
		_monster = 0x39;
		vehicleOut(now);
		return 0;
	default:
		return 0;
	}
}

// 0x47e3d0 case 10: the vehicle 0x31 leaves to the right, 25 pixels a step
void Scene6Room2::vehicleIn(uint32 now) {
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

// 0x47f7c0: the vehicle comes back from the right, backwards, and takes Ned home
void Scene6Room2::vehicleOut(uint32 now) {
	_ready = 0;
	int16 cur = ned()->getCurSlot();
	if (cur == 0x7a || cur == 0x36 || cur == 0x7b || cur == 7)
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

// 0x47e920: the idle cycle; it loops while a shot is out, and its end lets the monster pick again
void Scene6Room2::idleTick(uint32 now) {
	_ready = 0;
	attachOnce(_set39, 0x39, Scene6::kNedZ + 1);
	if (_sound39) {
		hub()->playSound(0x39);
		_sound39 = 0;
	}
	Slot &s = slot(0x39);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		s.deadline = 0;
		if (!_throwing) {
			s.step = -1;
			s.deadline = 0;
			_ready = 1;
			_sound39 = 1;
		}
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x39, 0x38) * kMsPerStep;
}

// 0x47ea10: knocked back; then open again (while the box plays), or dying once won
void Scene6Room2::knockTick(uint32 now) {
	_ready = 0;
	attachOnce(_set3e, 0x3e, Scene6::kNedZ + 1);
	if (_sound3e) {
		hub()->playSound(0x3e);
		_sound3e = 0;
	}
	Slot &s = slot(0x3e);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		// Step 0, not -1: it shows its first step again until the next action
		s.step = 0;
		s.deadline = 0;
		_ready = 1;
		_sound3e = 1;
		if (_box) {
			_action = 9;
			_ready = 0;
		}
		if (_won) {
			_action = 2;
			_ready = 0;
		}
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x3e, 0x38) * kMsPerStep;
}

// 0x47eb20: the lunge; at frame 6 the anchor 0x38 slides 20 pixels a tick to x 0x96
// and back (pushing Ned when he is caught, slot 0x7b), then the cycle goes on
void Scene6Room2::lungeTick(uint32 now) {
	_ready = 0;
	attachOnce(_set3b, 0x3b, Scene6::kNedZ + 1);
	Slot &anchor = slot(0x38);
	if (_lungeNew) {
		_homeX = anchor.x;
		_lungeNew = 0;
	}
	Slot &s = slot(0x3b);
	int frame = -1, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	if (frame == 6 && !_lungeHeld) {
		anchor.x += _lungeStep;
		if (anchor.x <= 0x96 || anchor.x > _homeX) {
			_lungeStep = -_lungeStep;
			if (anchor.x >= _homeX) {
				anchor.x = _homeX;
				_lungeHeld = 1;
			}
			return;
		}
		hub()->place(0x3b, 0x38);
		if (_lungeStep < 0 && ned()->getCurSlot() == 0x7b)
			nedPos().x += _lungeStep;
		return;
	}
	_lungeHeld = 0;
	_lungeNew = 1;
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x3b, 0x38) * kMsPerStep;
}

// 0x47ecd0: the swipe
void Scene6Room2::swipeTick(uint32 now) {
	_ready = 0;
	attachOnce(_set3a, 0x3a, Scene6::kNedZ + 1);
	Slot &s = slot(0x3a);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_ready = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x3a, 0x38) * kMsPerStep;
}

// 0x47f370: the throw; it lights 0x41, and its end bursts 0x183
void Scene6Room2::throwTick(uint32 now) {
	_ready = 0;
	_throwing = 1;
	_glow = 1;
	attachOnce(_set3c, 0x3c, Scene6::kNedZ + 1);
	Slot &s = slot(0x3c);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_burst = 1;
		_action = 6;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x3c, 0x38) * kMsPerStep;
}

// 0x47f100: 0x41 (on 0x40) loops with its sound while _glow; otherwise hidden and silent
void Scene6Room2::glowTick(uint32 now) {
	Scene6 *h = hub();
	if (_place40) {
		h->place(0x40, 0x40);
		_place40 = 0;
	}
	if (!loaded(0x41))
		return;
	attachOnce(_set41, 0x41, Scene6::kNedZ + 1);
	if (_box) {
		_glow = 0;
		_throwing = 0;
	}
	Slot &s = slot(0x41);
	if (!_glow) {
		h->visible(0x41, false);
		_place40 = 1;
		_game->stopSlotSound(s);   // 0x455930
		_sound41 = 1;
		return;
	}
	h->visible(0x41, true);
	if (_sound41) {
		h->playSound(0x41);
		_sound41 = 0;
	}
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		_sound41 = 1;
	}
	s.deadline = now + (int16)h->place(0x41, 0x40) * kMsPerStep;
}

// 0x47f260: the burst 0x183 (on 0x40) plays once, then 0x184 spins
void Scene6Room2::burstTick(uint32 now) {
	if (!_burst || !loaded(0x183))
		return;
	_glow = 0;
	attachOnce(_set183, 0x183, Scene6::kNedZ + 1);
	Slot &s = slot(0x183);
	if (s.deadline >= now)
		return;
	s.step++;
	hub()->visible(0x183, true);
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_burst = 0;
		hub()->visible(0x183, false);
		_spin = 1;
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x183, 0x40) * kMsPerStep;
}

// 0x47ed90: 0x184 spins in place four times, then flies
void Scene6Room2::spinTick(uint32 now) {
	if (!_spin || !loaded(0x184))
		return;
	attachOnce(_set184a, 0x184, Scene6::kNedZ + 1);
	hub()->visible(0x184, true);
	Slot &s = slot(0x184);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		_spins++;
		s.step = 0;
		if (_spins > 3) {
			_spins = 0;
			_spin = 0;
			_flying = 1;
		}
	}
	s.deadline = now + (int16)hub()->place(0x184, 0x184) * kMsPerStep;
}

// 0x47eea0: 0x184 flies 3 pixels a tick towards Ned's x at the start, then drops; off
// the screen (or on Ned) it goes back where it was and the monster may act again
void Scene6Room2::flightTick(uint32 now) {
	if (!_flying)
		return;
	Scene6 *h = hub();
	Slot &s = slot(0x184);
	if (!_set184b) {
		h->attach(0x184, Scene6::kNedZ + 1);
		_set184b = 1;
	}
	h->visible(0x184, true);
	if (s.deadline < now) {
		s.step++;
		if (s.sequenceLength <= s.step)
			s.step = 0;
		s.deadline = now + (int16)h->place(0x184, 0x184) * kMsPerStep;
	}
	if (_flightNew) {
		_shotTargetX = nedPos().x;
		_shotVX = 3;
		_shotVY = 3;
		_shotTargetY = nedPos().y;
		_shotHomeX = s.x;
		_shotHomeY = s.y;
		// 0x430410's position out: where the sprite was put
		h->place(0x184, 0x184);
		Common::Point p = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
		if (_shotTargetX - p.x < 0)
			_shotVX = -_shotVX;
		_shotX = p.x;
		_shotY = p.y;
	}
	if (!_falling)
		_shotX += _shotVX;
	else
		_shotY += _shotVY;
	if (!_flightNew) {
		s.x = _shotX;
		s.y = _shotY;
		h->place(0x184, 0x184);
	}
	_flightNew = 0;
	if ((_shotX >= _shotTargetX && _shotVX > 0) || (_shotX <= _shotTargetX && _shotVX < 0))
		_falling = 1;
	if (_shotHit || _shotX > 0x280 || _shotX < 0 || _shotY > 0x1e0 || _shotY < 0) {
		_flightNew = 1;
		_shotHit = 0;
		_flying = 0;
		h->visible(0x184, false);
		s.x = _shotHomeX;
		s.y = _shotHomeY;
		h->place(0x184, 0x40);
		if (!_box)
			_ready = 1;
		_throwing = 0;
		_falling = 0;
	}
}

// 0x47f450: open (0x3d), a target from its first use; it loops while the box plays
void Scene6Room2::openTick(uint32 now) {
	_ready = 0;
	if (!_set3d) {
		hub()->attach(0x3d, Scene6::kNedZ + 1);
		Scene6::targetAdd(_targets, 0x3d);
		slot(0x3d).step = -1;
		_set3d = 1;
	}
	Slot &s = slot(0x3d);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.step == 3)
		hub()->playSound(0x3d);
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		if (!_box) {
			s.step = -1;
			s.deadline = 0;
			_ready = 1;
		}
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x3d, 0x38) * kMsPerStep;
}

// 0x47f590: the box 0x44 plays once (sound at the start and at step 6) and opens the
// monster unless it is lunging, knocked back or dying, or the room is ending
void Scene6Room2::boxTick(uint32 now) {
	if (!_box)
		return;
	Scene6 *h = hub();
	attachOnce(_set44, 0x44, Scene6::kNedZ + 1);
	if (_monster != 0x3b && _monster != 0x3e && _monster != 0x3f && !h->_beaten &&
		_action != 0xb && _action != 4 && _action != 0)
		_action = 9;
	if (_sound44) {
		h->playSound(0x44);
		_sound44 = 0;
	}
	Slot &s = slot(0x44);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.step == 6)
		h->playSound(0x44);
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		s.deadline = 0;
		_box = 0;
		_sound44 = 1;
	}
	s.deadline = now + (int16)h->place(0x44, 0x44) * kMsPerStep;
}

// 0x47f6c0: dying; the room is won
void Scene6Room2::deathTick(uint32 now) {
	_ready = 0;
	attachOnce(_set3f, 0x3f, Scene6::kNedZ + 1);
	if (_sound3f) {
		hub()->playSound(0x3f);
		_sound3f = 0;
	}
	Slot &s = slot(0x3f);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = -1;
		s.deadline = 0;
		_action = 0xb;
		hub()->_saved[Scene6::kWon2] = 1;
		_won = 1;
		_sound3f = 1;
		hub()->visible(0x3f, false);
	}
	if (s.step != -1)
		s.deadline = now + (int16)hub()->place(0x3f, 0x38) * kMsPerStep;
}

static bool intersect(const int32 a[4], const int32 b[4]) {
	return MAX(a[0], b[0]) < MIN(a[2], b[2]) && MAX(a[1], b[1]) < MIN(a[3], b[3]);
}

// 0x47fbf0: the monster's box 1 (or the flying 0x184's) against Ned's
void Scene6Room2::hitChecks(uint32 now) {
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
	bool hit = false;
	switch (_action) {
	case 5:
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x7b)
			break;
		h->playSound(0x3b);
		ned()->playAction(0xb, nedPos(), 0, 0);
		hit = true;
		break;
	case 6:
	case 9: {
		if (!_flying)
			break;
		Slot &s = slot(0x184);
		h->stepRect(0x184, s.step, 1, rect);
		ned()->getBox(cur, nedPos(), box);
		Common::Point sp = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
		rect[0] += sp.x;
		rect[2] += sp.x;
		rect[3] += sp.y;
		rect[1] += sp.y;
		if (!intersect(rect, box) || ned()->getCurSlot() == 7)
			break;
		ned()->playAction(0x19, nedPos(), 0, 0);
		_shotHit = 1;
		hit = true;
		break;
	}
	case 7:
		if (!intersect(rect, box) || ned()->getCurSlot() == 0x7a)
			break;
		h->playSound(0x3a);
		ned()->playAction(8, nedPos(), 0, 0);
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

// ---- Hooks (DAT_005206f0) ----

// 0x47e240 (+0x00): button 2 aims the zap at the target in front of Ned (0x47fa10)
int Scene6Room2::hookUse(void *user, int x, int y, int16 *state) {
	Scene6Room2 *r = (Scene6Room2 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	r->findTarget(dir, found, rect);
	r->ned()->setUseSlot(dir);
	return 1;
}

// 0x47e230 (+0x04)
int Scene6Room2::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	*state = 0xe;
	return 1;
}

// 0x47e3a0 (+0x08)
void Scene6Room2::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene6Room2 *r = (Scene6Room2 *)user;
	*a = 2;
	int16 dir = 0;
	r->findTarget(dir, *b, rect);
}

// 0x47e290 (+0x0c): the box opens the monster; the open monster is hit at any step
int Scene6Room2::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene6Room2 *r = (Scene6Room2 *)user;
	Scene6 *h = r->hub();
	if (r->_done || h->_timeUp || r->_won)
		return 1;
	if (*b == 0x3d) {
		r->_action = 1;
		r->_ready = 0;
		Slot &m = r->slot(0x130);
		m.step++;
		if (m.step == m.sequenceLength - 2)
			h->_effect133 = 1;
		if (m.sequenceLength - 1 <= m.step) {
			m.deadline = 0;
			m.step = m.sequenceLength - 1;
			h->_saved[Scene6::kWon2] = 1;
			r->_won = 1;
			h->_effect133 = 0;
			h->_effect134 = 1;
		}
		if (m.step != -1)
			h->place(0x130, 0x130);
	} else if (*b == 0x44) {
		r->_box = 1;
		r->_ready = 0;
	}
	return 1;
}

} // End of namespace NightmareNed
