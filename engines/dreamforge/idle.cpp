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

#include "dreamforge/actor.h"
#include "dreamforge/detection.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/logic.h"
#include "dreamforge/scene.h"

namespace DreamForge {

// 0x166e0
bool DreamForgeEngine::actorBusy(int g) const {
	const Actor *a = getActor(g);
	return a && (a->isWalking() || a->_busy);
}

// 0x1bb30: the next fidget comes 300..380 ticks on, if the puppy stays put until then
void DreamForgeEngine::resetFidget(int g) {
	for (int n = 1; n <= 2; n++) {
		if (g != n && g != 3)
			continue;
		const Actor *a = _actors[n - 1];
		_fidgetAt[n - 1] = ticks() + (getRandom15() * 5 >> 15) * 20 + 300;
		_fidgetFrom[n - 1] = Common::Point(a->_x, a->_y);
		_fidgetDir[n - 1] = a->_dir;
	}
}

// 0x1bbc0: a random idle animation for the posture
void DreamForgeEngine::fidget(int g) {
	Actor *a = _actors[g - 1];
	// 0x1bac0: only a puppy that hasn't moved since the timer started
	if (actorBusy(g) || a->_dir != _fidgetDir[g - 1] || a->_x != _fidgetFrom[g - 1].x || a->_y != _fidgetFrom[g - 1].y ||
		a->_hidden) {
		resetFidget(g);
		return;
	}
	static const int8 kStandingFidgets[10] = { 16, 20, 6, 3, 24, 15, 12, 23, 8, 13 };
	static const int8 kLyingFidgets[2] = { 28, 21 };
	int slot;
	if (a->_posture == kSitting)
		slot = kSlotStandUp;
	else if (a->_posture == kLying)
		slot = kLyingFidgets[getRandom15() >> 14];
	else
		slot = kStandingFidgets[getRandom15() * 10 >> 15];
	// Some only have frames for some directions
	int dir = a->_dir;
	bool ok = true;
	if (slot == 3)
		ok = dir >= 0 && dir <= 4;
	else if (slot == 15)
		ok = dir == 5 || dir == 7;
	else if (slot == 21)
		ok = dir == 0 || dir == 4;
	debugC(1, kDebugScript, "puppy %d fidgets: slot %d%s", g, slot, ok ? "" : " (not this way)");
	int saved = _currentActor;
	_currentActor = g;
	if (ok)
		actorPlay(*a, slot);
	a->setDirection(dir);
	a->idle();
	_currentActor = saved;
	resetFidget(g);
}

// 0x20b00: every 3600 ticks (about 200 s) in some rooms, a bit of idle banter
void DreamForgeEngine::idleBanter() {
	if (ticks() <= _idleBanterAt || _logic->var(0x647B0) != 0)
		return;
	if (!actVeto(2) || !actVeto(1))
		return;
	// 0x484f0
	static const int8 kRooms[][2] = {
		{ 1, 3 }, { 1, 5 }, { 1, 10 }, { 2, 5 }, { 2, 7 }, { 2, 8 }, { 2, 10 }, { 2, 11 }, { 3, 1 }
	};
	bool here = false;
	if (_scene->_act == 1)
		for (auto &r : kRooms)
			here |= r[0] == _scene->_area && r[1] == _scene->_room;
	if (here) {
		setFlag(0x1A, true);
		banter(7);
		setFlag(0x1A, false);
	}
	_idleBanterAt = ticks() + 0xE10;
}

// The act's fidget veto (0x88370): true if puppy g may fidget or wander
bool DreamForgeEngine::actVeto(int g) {
	return callActHandler(0x88370, g, 0, 0, 1) != 0;
}

// 0x1c1f0: puppy 2 on its spot faces puppy 1
void DreamForgeEngine::faceGuy1() {
	Actor *a = _actors[1];
	int dir = a->directionTo(_actors[0]->_y, _actors[0]->_x);
	if (dir >= 0 && dir != a->_dir && !isFlagSet(0x18) && !isFlagSet(0x14)) {
		int saved = _currentActor;
		_currentActor = 2;
		actorTurn(*a, dir);
		a->idle();
		_currentActor = saved;
	}
}

// 0x1b620 / 0x1b780: puppy 2 wanders to one of the room's spots and waits there
void DreamForgeEngine::wander() {
	Actor *a = _actors[1];
	if (a->isWalking() || a->_busy || a->_hidden || !actVeto(2))
		return;
	int empty = 0;
	for (int i = 0; i < 5; i++) {
		const Scene::Spot &s = _scene->_spots[i];
		if (s.y == -1) {
			if (i != 4)
				empty++;
		} else if (a->_y == s.y && a->_x == s.x) {
			faceGuy1();
			return;
		}
	}
	if (empty == 4)
		return;
	// The spots it may go to are worked out once per room, from where it stood then
	if (_wanderSpots.empty()) {
		for (int i = 0; i < 4; i++) {
			const Scene::Spot &s = _scene->_spots[i];
			if (s.y != -1 && a->_y != s.y && a->_x != s.x && s.type >= 2 && s.type <= 4)
				_wanderSpots.push_back(i);
		}
	}
	if (_wanderSpots.empty())
		return;
	const Scene::Spot &s = _scene->_spots[_wanderSpots[getRandom15() * _wanderSpots.size() >> 15]];
	debugC(1, kDebugScript, "puppy 2 wanders to (%d, %d)", s.x, s.y);
	// Flag 0x18 keeps the walking direction at the end (0x1b4b0)
	setFlag(0x18, true);
	int saved = _currentActor;
	_currentActor = 2;
	actorWalk(2, s.y, s.x, -1);
	_currentActor = saved;
	setFlag(0x18, false);
}

// 0x10890, after each step outside scripts: the act's idle hook, fidgets and puppy 2
void DreamForgeEngine::idleStep() {
	if (!_scene)
		return;
	if (!_newGame && !isFlagSet(0x1A) && !actorBusy(1) && !actorBusy(2)) {
		idleBanter();
		callActHandler(0x6DE38, 0, 0, 0, 0);
	}
	if (!isFlagSet(0x11)) {
		if (actorBusy(1) || actorBusy(2))
			resetFidget(1);
		else if (ticks() >= _fidgetAt[0]) {
			if (actVeto(1))
				fidget(1);
			else
				resetFidget(1);
		}
		if (actorBusy(1) || actorBusy(2))
			resetFidget(2);
		else if (ticks() >= _fidgetAt[1]) {
			if (actVeto(2))
				fidget(2);
			else
				resetFidget(2);
		}
	} else {
		resetFidget(3);
	}
	if (!isFlagSet(0x1B) && !isFlagSet(0x1A) && !actorBusy(1))
		wander();
}

} // End of namespace DreamForge
