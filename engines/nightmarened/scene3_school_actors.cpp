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
#include "common/util.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene3_school.h"

namespace NightmareNed {

// ---- The strips and the camera ----

// 0x424500: the 24 strips in a ring, from x 0 at y -480
void Scene3School::stripsInit() {
	for (int i = 0; i < kStripCount; i++) {
		Strip &s = _strips[i];
		s.next = (i + 1) % kStripCount;
		s.prev = (i + kStripCount - 1) % kStripCount;
		s.index = i;
		s.worldX = i * kStripW;
		s.screenX = i * kStripW;
		s.screenY = -0x1e0;
	}
	_stripsUnused = 0;
}

// 0x424580: every loaded strip moves; y stays within -480..0 (which corrects dy for the
// rest) and x wraps by the level width outside -640..1280
void Scene3School::stripsMove(int &dx, int &dy) {
	int i = 0;
	for (int k = kStripCount; k > 0; k--) {
		Strip &s = _strips[i];
		BitmapRec *rec = bmp(kStripBitmaps + 4 * s.index);
		if (rec && rec->object) {
			s.screenX += dx;
			s.screenY += dy;
			if (s.screenY < -0x1e0) {
				dy += -0x1e0 - s.screenY;
				s.screenY = -0x1e0;
			}
			if (s.screenY > 0) {
				dy -= s.screenY;
				s.screenY = 0;
			}
			if (s.screenX < -0x280)
				s.screenX += kWorldW;
			else if (s.screenX > 0x500)
				s.screenX -= kWorldW;
			rec->object->setPosition(s.screenX, s.screenY);
		}
		i = s.next;
	}
}

// The camera's catch-up speed for a distance d
static int cameraSpeed(int d) {
	if (d <= 6)
		return 1;
	if (d <= 0xc)
		return 2;
	if (d <= 0x18)
		return 4;
	return d > 0x30 ? 0x12 : 0xc;
}

// 0x423f00: keeps Ned near x 200 facing right and 440 facing left (320 hanging, by the way
// he moves), and shows the half of the level he is in; everything placed this tick moves along
void Scene3School::camera(uint32 now) {
	int16 state = ned()->getState();
	if (state == 10)
		return;
	int dx = 0, dy = 0;
	int32 box[4];
	ned()->getBox(ned()->getCurSlot(), _nedPos, box);
	int16 h = (int16)(box[3] - box[1]);
	int sx = stripScreenX(_nedPos.x);
	int sy = _nedPos.y + _scrollY;
	bool slow = _cameraTime + 0x12 < now;
	bool fast = _cameraTime + 0xc < now;

	if (!ned()->isHanging()) {
		if (ned()->getFacing() != 0) {
			if (state == 7) {
				if (slow)
					dx = -1;
			} else if (state == 8) {
				if (fast)
					dx = -1;
			} else if (state != 5 && state != 6) {
				if (sx <= 200) {
					if (state == 1 && fast)
						dx = -1;
				} else {
					dx = -cameraSpeed(sx - 200);
				}
			}
		} else {
			if (state == 7) {
				if (slow)
					dx = 1;
			} else if (state == 8) {
				if (fast)
					dx = 1;
			} else if (state != 5 && state != 6) {
				if (sx >= 0x1b8) {
					if (state == 1 && fast)
						dx = 1;
				} else {
					dx = cameraSpeed(0x1b8 - sx);
				}
			}
		}
	} else if (sx < _lastScreenX) {
		if (state == 7) {
			if (slow)
				dx = 1;
		} else if (state == 8) {
			if (fast)
				dx = 1;
		} else if (state != 5 && state != 6) {
			if (sx >= 0x140) {
				if (state == 1 && fast)
					dx = 1;
			} else {
				dx = cameraSpeed(0x140 - sx);
			}
		}
	} else {
		if (state == 7) {
			if (slow)
				dx = -1;
		} else if (state == 8) {
			if (fast)
				dx = -1;
		} else if (state != 5 && state != 6) {
			if (sx <= 0x140) {
				if (state == 1 && fast)
					dx = -1;
			} else {
				dx = -cameraSpeed(sx - 0x140);
			}
		}
	}

	if (_nedPos.y - h > 0x1e1 || sy + 8 >= 0x1e0)
		dy = -2;
	else if (_nedPos.y < 0x1e0 || sy - h - 8 <= 0)
		dy = 2;

	stripsMove(dx, dy);
	_scrollY = _strips[0].screenY;
	_scrollX = _strips[0].screenX;
	static const int16 kPlatforms[] = { kSlide, 0x118, 0x119, 0x11a, 0x11b };
	for (int16 id : kPlatforms) {
		if (slot(id).sprite)
			slot(id).sprite->adjPosition(dx, dy);
	}
	nudge(_trapA, dx, dy);
	nudge(_trapB, dx, dy);
	nudge(_trapC, dx, dy);
	nudge(_spawnerA, dx, dy);
	nudge(_spawnerB, dx, dy);
	nudge(_doorStart, dx, dy);
	nudge(_doorExit, dx, dy);
	nudge(kSwitch, dx, dy);
	if (_ghost != -1)
		nudge(_ghost, dx, dy);
	ned()->adjPosition(dx, dy);
	_lastScreenX = sx;
	_lastScreenY = sy;
	if (dx != 0 || dy != 0)
		_cameraTime = _game->now();
}

// ---- The traps ----

// 0x424650 / 0x424830 / 0x424a10: a trap's slots are base (still), +1 (bumping Ned, who
// plays actionA at stepA), +2 (zapped, holds its last step), +3 (zapped and bumping,
// actionB at stepB) and +4 (zapped twice, holds)
void Scene3School::trapTick(int16 &cur, int base, int stepA, int actionA, int stepB, int actionB, uint32 now) {
	if (cur == -1)
		return;
	Slot *s = &slot(cur);
	if (now <= s->deadline)
		return;
	if (cur == base + 1) {
		s->step++;
		if (s->step < s->sequenceLength) {
			if (s->step == stepA)
				ned()->playAction(actionA, _nedPos, _scrollX, _scrollY);
		} else {
			cur = switchSlot(base, cur);
		}
	} else if (cur == base + 2 || cur == base + 4) {
		if (s->step < s->sequenceLength - 1)
			s->step++;
	} else if (cur == base + 3) {
		s->step++;
		if (s->step < s->sequenceLength) {
			if (s->step == stepB)
				ned()->playAction(actionB, _nedPos, _scrollX, _scrollY);
		} else {
			cur = switchSlotPre(base + 2, cur);
			slot(cur).step = slot(cur).sequenceLength - 1;
		}
	}
	if (cur != -1) {
		int duration = place(cur, base);
		slot(cur).deadline = now + (int16)duration * kMsPerStep;
	}
}

// 0x422fc0 (cases 100, 0x6e, 0x78): walking into a trap
int Scene3School::trapRegion(int16 &cur, int base) {
	int id;
	if (cur == base) {
		id = base + 1;
	} else if (cur == base + 2 && slot(base + 2).step == slot(base + 2).sequenceLength - 1) {
		id = base + 3;
	} else {
		return 0;
	}
	if (showSprite(id, 10, false))
		return 5;
	cur = switchSlot(id, cur);
	if (!isLoaded(entryFor(id, Request::kSound)))
		return 5;
	playSound(id);
	int duration = place(cur, base);
	slot(cur).deadline = _game->now() + (int16)duration * kMsPerStep;
	return 0;
}

// 0x423700 (cases 0xde/0xe0, 0xe3/0xe5, 0xe8/0xea): zapping a trap
int Scene3School::trapHit(int16 &cur, int base, int16 found) {
	int id;
	if (found == base) {
		id = base + 2;
	} else {
		if (slot(cur).step != slot(cur).sequenceLength - 1)
			return 1;
		id = base + 4;
	}
	targetRemove(found);
	if (showSprite(id, 10, false))
		return 5;
	targetAdd(id);
	cur = switchSlot(id, cur);
	if (!isLoaded(entryFor(id, Request::kSound)))
		return 5;
	playSound(id);
	int duration = place(cur, base);
	slot(cur).deadline = _game->now() + (int16)duration * kMsPerStep;
	return 1;
}

// ---- The dispensers and the creature ----

// 0x425200 (one half each): an opened dispenser (slot 0xf0, shared by both) lets the
// creature out at step 12 beside it
void Scene3School::spawnerTick(int16 &cur, int base, uint32 now) {
	if (slot(cur).deadline >= now) {
		place(cur, base);
		return;
	}
	if (cur == kSpawnerOpen) {
		slot(kSpawnerOpen).step++;
		int16 step = slot(cur).step;
		if (step < slot(cur).sequenceLength) {
			if (step == 0xc) {
				showSprite(kGhost, 6, false);
				_ghost = switchSlot(kGhost, -1);
				slot(_ghost).x = slot(base).x + 0x2b;
				slot(_ghost).y = slot(base).y + 0x7c;
				place(_ghost, _ghost);
				_ghostPeriodY = 1;
				_ghostPeriodX = 1;
				_ghostPhaseY = 0;
				_ghostPhaseX = 0;
				targetAdd(kGhost);
				playLoop(kGhost);
			} else if (step == 0xb || step == 4) {
				playSound(kSpawnerOpen);
			}
		} else {
			cur = switchSlot(base, cur);
		}
	}
	int duration = place(cur, base);
	slot(cur).deadline = now + (int16)duration * kMsPerStep;
}

// 0x425640: the creature loops (and moves each step); zapped (0xed) it plays once and is gone
void Scene3School::ghostTick(uint32 now) {
	if (_ghost == -1)
		return;
	Slot &s = slot(_ghost);
	if (s.deadline >= now)
		return;
	s.step++;
	int16 cur = _ghost;
	if (s.sequenceLength <= s.step) {
		if (_ghost == kGhostGone)
			cur = _ghost = switchSlot(-1, kGhostGone);
		else
			s.step = 0;
	}
	if (cur == -1)
		return;
	if (cur == kGhost)
		ghostMove();
	int duration = place(_ghost, kGhost);
	slot(_ghost).deadline = now + (int16)duration * kMsPerStep;
}

// 0x425730: the creature floats on two random sine swings that drift towards Ned, and
// knocks him over (action 0x31) when it touches the top of his box
void Scene3School::ghostMove() {
	int32 nedBox[4];
	ned()->getBox(ned()->getCurSlot(), _nedPos, nedBox);
	Slot &g = slot(_ghost);
	int frame = 0, duration = 0;
	if (g.sequence)
		g.sequence->getStep(g.step, frame, duration);
	int pos[2];
	attachPos(_ghost, _ghost, pos, true);
	int32 r[4];
	boxRect(_ghost, frame, 0, r);
	r[0] += pos[0];
	r[2] += pos[0];
	r[1] += pos[1];
	r[3] += pos[1];
	nedBox[3] = nedBox[1] + 4;
	int16 state = ned()->getState();
	if ((state == 4 || state == 1 || state == 3) && !ned()->isHanging() &&
		MAX(nedBox[0], r[0]) < MIN(nedBox[2], r[2]) && MAX(nedBox[1], r[1]) < MIN(nedBox[3], r[3]))
		ned()->playAction(0x31, _nedPos, _scrollX, _scrollY);

	_ghostNedY = nedBox[1];
	_ghostNedX = _nedPos.x;
	_ghostX = g.x;
	_ghostY = g.y;
	_ghostPhaseX += 4;
	if (_ghostPhaseX >= _ghostPeriodX) {
		_ghostAmpX = 0;
		_ghostPeriodX = (int)_game->crtRandom() % 0x15e + 10;
		_ghostBaseX = _ghostX;
		_ghostPhaseX = _ghostPeriodX - (int)_game->crtRandom() % _ghostPeriodX;
	}
	_ghostPhaseY += 4;
	if (_ghostPhaseY >= _ghostPeriodY) {
		_ghostAmpY = 0;
		_ghostPeriodY = (int)_game->crtRandom() % 0x15e + 10;
		_ghostBaseY = _ghostY;
		_ghostPhaseY = _ghostPeriodY - (int)_game->crtRandom() % _ghostPeriodY;
	}
	int swingX = _sinTable[CLIP(_ghostPhaseX, 0, (int)kTableSize - 1)] * _ghostAmpX;
	int swingY = _cosTable[CLIP(_ghostPhaseY, 0, (int)kTableSize - 1)] * _ghostAmpY;
	_ghostAmpX += (0x5a - _ghostAmpX) / 5;
	_ghostAmpY += (0x5a - _ghostAmpY) / 5;
	int distX = ABS(_ghostX - _ghostNedX) + 1;
	int distY = ABS(_ghostY - _ghostNedY) + 1;
	swingX >>= 9;
	swingY >>= 9;
	if (ABS(distX - distY) > 5 && _ghostAmpX % 3 != 0) {
		if (distX > distY / 2) {
			if (_ghostNedX < _ghostX && swingX > 0)
				_ghostBaseX--;
			if (_ghostX < _ghostNedX && swingX < 0)
				_ghostBaseX++;
		}
		if (distY > distX / 2) {
			if (_ghostNedY < _ghostY && swingY > 0)
				_ghostBaseY--;
			if (_ghostY < _ghostNedY && swingY < 0)
				_ghostBaseY++;
		}
	}
	_ghostX = CLIP(swingX + _ghostBaseX, 0x1e, 0x762);
	_ghostY = CLIP(swingY + _ghostBaseY, 0, 0x3bf);
	g.x = _ghostX;
	g.y = _ghostY;
}

// ---- The doors ----

// 0x424bf0: the start door opens (0xd7, 0x116, 0xd9), takes Ned in and puts him back at
// the start, walking right
void Scene3School::doorStartTick(uint32 now) {
	int16 cur = _doorStart;
	if (!_doorStartOn || slot(cur).deadline >= now) {
		if (_doorStart != -1)
			place(_doorStart, kDoorStart);
		return;
	}
	if (cur == kDoorStart) {
		slot(kDoorStart).step++;
		if (slot(cur).sequenceLength <= slot(cur).step) {
			showSprite(kDoorOpening, 10, false);
			cur = _doorStart = switchSlot(kDoorOpening, cur);
		}
	} else if (cur == kDoorOpen) {
		slot(kDoorOpen).step++;
		if (slot(cur).step < slot(cur).sequenceLength) {
			if (slot(cur).step == 1) {
				ned()->show(false);
				ned()->enable(false);
			}
		} else {
			cur = _doorStart = switchSlot(kDoorStart, cur);
			_scrollX = _startScrollX;
			_doorStartOn = 0;
			_scrollY = _startScrollY;
			_nedPos = _startPos;
			ned()->show(true);
			ned()->enable(true);
			nedTick(kInputRight, now);
			cur = _doorStart;
		}
	} else if (cur == kDoorOpening) {
		slot(kDoorOpening).step++;
		if (slot(cur).sequenceLength <= slot(cur).step) {
			showSprite(kDoorOpen, 10, false);
			cur = _doorStart = switchSlot(kDoorOpen, cur);
			playSound(kDoorOpen);
		}
	}
	if (cur == -1)
		return;
	int duration = place(cur, kDoorStart);
	slot(_doorStart).deadline = now + (int16)duration * kMsPerStep;
}

// 0x424ea0: the exit door opens with the start door's slots (0x11c, 0x116, 0xd9), takes
// Ned in and returns to the hub. True when the scene has changed
bool Scene3School::doorExitTick(uint32 now) {
	int16 cur = _doorExit;
	if (!_doorExitOn || now <= slot(cur).deadline) {
		if (_doorExit != -1)
			place(_doorExit, kDoorExit);
		return false;
	}
	if (cur == kDoorOpen) {
		slot(kDoorOpen).step++;
		if (slot(_doorExit).sequenceLength <= slot(_doorExit).step) {
			_doorExit = switchSlot(kDoorExit, _doorExit);
			_doorExitOn = 0;
			regionEnable(map(kMapLevel), 0xdc, false);
			return switchTo(3) == 0;
		}
		if (slot(_doorExit).step == 1) {
			ned()->show(false);
			ned()->enable(false);
		}
	} else if (cur == kDoorOpening) {
		slot(kDoorOpening).step++;
		if (slot(_doorExit).sequenceLength <= slot(_doorExit).step) {
			showSprite(kDoorOpen, 10, false);
			// The original hides the start door's slot here, not its own
			_doorExit = switchSlot(kDoorOpen, _doorStart);
			playSound(kDoorOpen);
		}
	} else if (cur == kDoorExit) {
		slot(kDoorExit).step++;
		if (slot(_doorExit).sequenceLength <= slot(_doorExit).step) {
			showSprite(kDoorOpening, 10, false);
			_doorExit = switchSlot(kDoorOpening, _doorExit);
		}
	}
	if (_doorExit == -1)
		return false;
	int duration = place(_doorExit, kDoorExit);
	if (_doorExitOn)
		slot(_doorExit).deadline = now + (int16)duration * kMsPerStep;
	return false;
}

// ---- The platforms ----

// 0x422a20: the platforms' own loops, then their moves
void Scene3School::liftsTick(uint32 now) {
	static const int16 kLoops[] = { kSlide, 0x118, 0x119, 0x11a, 0x11b };
	for (int16 id : kLoops) {
		Slot &s = slot(id);
		if (s.deadline >= now || s.sequenceLength <= 0)
			continue;
		s.step = (int16)((s.step + 1) % s.sequenceLength);
		int frame = 0, duration = 0;
		if (!s.sequence || !s.sequence->getStep(s.step, frame, duration))
			continue;
		if (s.sprite)
			s.sprite->setCurrentImage(frame);
		s.deadline = now + (int16)duration * kMsPerStep;
	}
	slideMove(now);
	liftMove(kMapLift1, now, 1, 0x118);
	liftMove(kMapLift2, now, 2, 0x119);
	liftMove(kMapLift3, now, 3, 0x11a);
	liftMove(kMapLift4, now, 4, 0x11b);
}

// 0x4228e0: the sliding platform moves 8 every 83 ms between its limits, carrying Ned
void Scene3School::slideMove(uint32 now) {
	if (now <= _liftLast[0] + 0x53)
		return;
	_liftLast[0] = now;
	CollisionMap *m = map(kMapSlide);
	if (!m)
		return;
	Common::Rect b = m->bounds;
	int step;
	if (_liftDir[0]) {
		step = 8;
		if (_liftMax[0] < b.left + 8) {
			_liftDir[0] = 0;
			step = -0x10;
		}
	} else {
		step = -8;
		if (b.left - 8 < _liftMin[0]) {
			_liftDir[0] = 1;
			step = 0x10;
		}
	}
	if (m->isSolid((int16)_nedPos.x, (int16)_nedPos.y)) {
		ned()->adjPosition(step, 0);
		_nedPos.x += step;
	}
	b.right += step;
	b.left += step;
	if (slot(kSlide).sprite)
		slot(kSlide).sprite->setPosition(stripScreenX(b.left), b.top + _scrollY);
	_world.moveMap(m, b);
}

// 0x4226a0: a lift moves 4 (0x11a: 8) every 83 ms, carrying Ned, and waits 1 s at each end
void Scene3School::liftMove(uint32 va, uint32 now, int index, int16 id) {
	int step = id != 0x11a ? 4 : 8;
	if (now <= _liftPause[index])
		return;
	// The original also returns here for index 0x118 in state 10, which never happens
	_liftPause[index] = 0;
	if (now <= _liftLast[index] + 0x53)
		return;
	_liftLast[index] = now;
	CollisionMap *m = map(va);
	if (!m)
		return;
	Common::Rect b = m->bounds;
	if (!_liftDir[index])
		step = -step;
	if (m->isSolid((int16)_nedPos.x, (int16)_nedPos.y)) {
		ned()->adjPosition(0, step);
		_nedPos.y += step;
	}
	b.top += step;
	b.bottom += step;
	int x = stripScreenX(b.left);
	int y = _scrollY + b.top;
	if (_liftDir[index]) {
		if (_liftMax[index] < b.top) {
			_liftDir[index] = 0;
			_liftPause[index] = now + 1000;
		}
	} else if (b.top < _liftMin[index]) {
		_liftDir[index] = 1;
		_liftPause[index] = now + 1000;
	}
	if (slot(id).sprite)
		slot(id).sprite->setPosition(x, y);
	_world.moveMap(m, b);
}

// ---- Hooks ----

// 0x422db0: the target in front of Ned, as a world rect (box 0 of its current frame)
int Scene3School::findTarget(int16 &dir, int16 &found, int32 rect[4]) {
	found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, dir, unused, found))
		return 1;
	int ref = -1;
	switch (found) {
	case 0xde:
	case 0xe0:
		ref = 0xde;
		break;
	case 0xe3:
	case 0xe5:
		ref = 0xe3;
		break;
	case 0xe8:
	case 0xea:
		ref = 0xe8;
		break;
	case kGhost:
		ref = kGhost;
		break;
	case kSwitch:
		ref = kSwitch;
		break;
	default:
		break;
	}
	// For the other targets (0xe2, 0xe7, 0xec) the original adds uninitialised locals
	int pos[2] = { 0, 0 };
	if (ref != -1)
		attachPos(found, ref, pos, true);
	Slot &s = slot(found);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	boxRect(found, frame, 0, rect);
	rect[0] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	rect[2] += pos[0];
	return 1;
}

// 0x4236b0 (+0x00): button 2 uses the direction towards the target
int Scene3School::hookUse(void *user, int x, int y, int16 *state) {
	Scene3School *s = (Scene3School *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(dir, found, rect);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x422fc0 (+0x04)
int Scene3School::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	return ((Scene3School *)user)->region(id, flags);
}

// 0x423e80 (+0x08): the area is 4; the direction 0x422db0 finds is dropped
void Scene3School::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene3School *s = (Scene3School *)user;
	*a = 4;
	int16 dir = 4;
	s->findTarget(dir, *b, rect);
}

// 0x423700 (+0x0c)
int Scene3School::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	return ((Scene3School *)user)->hitTarget(*a, *b);
}

// 0x422fc0: regions that take effect when they carry flag 0x200; Ned's state never changes
int Scene3School::region(int32 id, uint32 flags) {
	if (!(flags & 0x200))
		return 0;
	switch (id) {
	case 100:
		return trapRegion(_trapA, kTrapA);
	case 0x6e:
		return trapRegion(_trapB, kTrapB);
	case 0x78:
		return trapRegion(_trapC, kTrapC);
	case 0x96:
		_doorStartOn = 1;
		if (!isLoaded(entryFor(kDoorStart, Request::kSound)))
			return 5;
		playSound(kDoorStart);
		return 0;
	case 200:
		if (_ghost == -1) {
			if (showSprite(kSpawnerOpen, 10, false))
				return 5;
			_spawnerA = switchSlot(kSpawnerOpen, _spawnerA);
			place(_spawnerA, kSpawnerA);
		}
		return 0;
	case 0xd2:
		if (_ghost == -1) {
			if (showSprite(kSpawnerOpen, 10, false))
				return 5;
			_spawnerB = switchSlot(kSpawnerOpen, _spawnerB);
			place(_spawnerB, kSpawnerB);
		}
		return 0;
	case 0xdc:
		_doorExitOn = 1;
		if (!isLoaded(entryFor(kDoorStart, Request::kSound)))
			return 5;
		playSound(kDoorStart);
		return 0;
	default:
		return 0;
	}
}

// 0x423700: Ned's zap reached a target
int Scene3School::hitTarget(int16 a, int16 found) {
	if (a != 4)
		return 1;
	switch (found) {
	case 0xde:
	case 0xe0:
		return trapHit(_trapA, kTrapA, found);
	case 0xe3:
	case 0xe5:
		return trapHit(_trapB, kTrapB, found);
	case 0xe8:
	case 0xea:
		return trapHit(_trapC, kTrapC, found);
	case kGhost: {
		stopSound(kGhost);
		targetRemove(_ghost);
		showSprite(kGhostGone, 6, true);
		_ghost = switchSlot(kGhostGone, _ghost);
		int duration = place(_ghost, kGhost);
		slot(_ghost).deadline = _game->now() + (int16)duration * kMsPerStep;
		return 1;
	}
	case kSwitch:
		_switchOn = 1;
		return 1;
	default:
		return 1;
	}
}

} // End of namespace NightmareNed
