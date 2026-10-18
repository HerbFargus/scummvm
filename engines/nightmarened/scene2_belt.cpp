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
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene2.h"
#include "nightmarened/slothelpers.h"

namespace NightmareNed {

Scene2Belt::Scene2Belt(Game *game, Scene2 *world) : Scene2Part(game, 1, world) {
	for (int i = 0; i < kLanes; i++) {
		_lanes[i].slot = _lanes[i].pending = _lanes[i].row = -1;
		_rowY[i] = 0;
		_soundPick[i] = 0;
	}
	_loaded = 0;
	_target = _row = 1;
	_endSlot = -1;
	_catching = 0;
	_lastInput = 0;
	_nedSlot = -1;
	_spawned = 0;
	_phase = 0;
	_soundIndex = 0;
}

// 0x441400: four different sounds of the ten, loaded as a list of their own
void Scene2Belt::buildSounds() {
	_game->crtSeed(_game->now());
	for (int i = 0; i < 4; i++)
		_soundPick[i] = 0;
	for (int n = 0; n < 4; n++) {
		for (;;) {
			int pick = (int)_game->crtRandom() % 10;
			bool used = false;
			for (int k = 0; k < n && !used; k++)
				used = _soundPick[k] == pick;
			if (!used) {
				_soundPick[n] = pick;
				break;
			}
		}
	}
	_sounds.clear();
	for (int i = 0; i < 4; i++) {
		Request r;
		r.scene = 1;
		r.area = kArea;
		r.id = _game->exe().readSint32(kSoundIds + 4 * _soundPick[i]);
		r.type = Request::kSound;
		_sounds.push_back(r);
	}
	submitBuilt(kSoundList, _sounds, 1);
}

// m2 0x43ffc0
int Scene2Belt::load(int fromScene, int fromSub) {
	if (!_game->getNaf(kNaf))
		return 5;
	ned()->listLoad(0xb);
	_loaded = 0;
	if (submit(kMainList, 0x1a, 1))
		return 5;
	if (submit(kEndList, 3, 2))
		return 5;
	_loaded = 1;   // 0x43ffb0, the callback on kEndSprite
	buildSounds();
	return 0;
}

// m3 0x440060
int Scene2Belt::enter(int fromScene, int fromSub) {
	_game->_optionsAllowed = false;
	ned()->listShow(0xb);
	attachList(kMainList, 0x1a);
	byte pal[256 * 3];
	memset(pal, 0, sizeof(pal));
	if (palette(kPalette))
		memcpy(pal, palette(kPalette), sizeof(pal));
	ned()->copyPalette(pal);
	_game->display().applyPalette(pal, kPalAll);
	if (!loadSst(1, _sst))
		return 5;
	applySst(_sst, nullptr);
	BitmapRec *belt = bmp(kBelt);
	if (belt && belt->object) {
		belt->object->setDrawOrder(0x67);
		belt->object->setVisibility(true);
	}
	BitmapRec *front = bmp(kFront);
	if (front && front->object) {
		front->object->setDrawOrder(1);
		front->object->setVisibility(true);
		setBitmapKey(front, 10);
	}
	_phase = 0;
	for (int i = 0; i < kLanes; i++) {
		if (slot(kArea, 300 + i).sprite)
			slot(kArea, 300 + i).sprite->setDrawOrder(0x65);
		_rowY[i] = (int16)slot(kArea, 300 + i).y;
	}
	if (slot(kArea, kCatch).sprite)
		slot(kArea, kCatch).sprite->setDrawOrder(0x65);
	if (slot(kArea, kMiss).sprite)
		slot(kArea, kMiss).sprite->setDrawOrder(0x65);
	for (int i = 0; i < 12; i++) {
		if (slot(kArea, 304 + i).sprite)
			slot(kArea, 304 + i).sprite->setDrawOrder(0x66);
	}
	_nedSlot = (int16)_game->display().slotSwitch(kNedArea, kNed, -1, true); // 0x430930
	_row = 1;
	_target = 1;
	_catching = 0;
	slot(kNedArea, kNed).x = 0x96;
	slot(kNedArea, kNed).y = 0x115;
	_lastInput = 0;
	_s2->_caught = 0;
	_spawned = 0;
	spawn();
	spawn();
	spawn();
	spawn();
	_game->media().playMusic(kBeltMusic);
	slotPlayLoop(_game, kNedArea, kNed);
	return 0;
}

// m4 0x440290
int Scene2Belt::leave(int toScene, int toSub) {
	stopAllSounds();
	_game->stopSlotSound(slot(kNedArea, kNed));
	visible(kNedArea, _nedSlot, false);
	_sst.clear();
	release(kMainList, 0x1a);
	release(kEndList, 3);
	release(kSoundList, 4);
	ned()->listFree(0xb);
	ned()->show(false);
	ned()->enable(false);
	if (toScene != 2)
		_game->closeNaf(kNaf);
	return 0;
}

// m5 0x440280
int Scene2Belt::unload(int toScene, int toSub) {
	return 0;
}

// 0x440350: the next door of lane (n & 3), with a surgeon in the row the table gives (-1: none)
void Scene2Belt::spawn() {
	if (_spawned < 0x24) {
		int lane = _spawned & 3;
		int16 row = (int16)_game->exe().readUint16(kItems + 2 * _spawned);
		visible(kArea, 300 + lane, false);
		visible(kArea, 304 + lane, false);
		visible(kArea, 308 + lane, false);
		visible(kArea, 312 + lane, false);
		if (_s2->_caught == 8)
			row = -1;
		if (row < 0) {
			_lanes[lane].slot = -1;
		} else {
			visible(kArea, 304 + lane + row * 4, true);
			visible(kArea, 300 + lane, true);
			slot(kArea, 300 + lane).y = _rowY[row];
			_lanes[lane].slot = (int16)(300 + lane);
		}
		_lanes[lane].row = row;
		_lanes[lane].pending = -1;
		_spawned++;
	}
	if ((_spawned > 0x1f || _s2->_caught == 8) && _phase < 1) {
		_phase = 1;   // 0x482ee0 does nothing
	}
}

// 0x4410a0: kind (the grab or the miss) moves to lane; any other lane playing it stops
void Scene2Belt::laneTake(int lane, int16 kind, uint32 now) {
	for (int i = 0; i < kLanes; i++) {
		if (i == lane)
			continue;
		if (_lanes[i].pending == kind)
			_lanes[i].pending = -1;
		if (_lanes[i].slot != kind)
			continue;
		if (kind == kCatch && _catching)
			_catching &= ~(1 << i);
		switchPlace(kArea, 300 + i, kind, 300 + i, now, false);
		_lanes[i].slot = (int16)(300 + i);
		visible(kArea, 300 + i, false);
		_lanes[i].slot = -1;
	}
	_lanes[lane].pending = kind;
}

// 0x440cb0: a surgeon reaching Ned grabs him if in his row, else misses; the lanes animate
void Scene2Belt::laneTick(uint32 now) {
	int16 nedX = (int16)slot(kNedArea, kNed).x;
	for (int i = 0; i < kLanes; i++) {
		Lane &l = _lanes[i];
		if (l.slot == -1)
			continue;
		if (l.pending == -1 && _s2->_caught < 8) {
			int16 d = (int16)((int16)slot(kArea, 300 + i).x - nedX - 0x10e);
			if (d >= 0 && d < 8) {
				if (l.row == _row) {
					playSound(kArea, kCatch);
					_catching |= (uint16)(1 << i);
					laneTake(i, kCatch, now);
					_s2->_caught++;
					if (_s2->_caught > 8)
						_s2->_caught = 8;
				} else {
					playSound(kArea, kMiss);
					laneTake(i, kMiss, now);
				}
			}
		}
		int16 cur = l.slot;
		if (cur == 300 + i) {
			if (l.pending == -1) {
				l.slot = switchPlace(kArea, 300 + i, cur, 300 + i, now, false);
			} else {
				l.slot = switchPlace(kArea, l.pending, cur, 300 + i, now, false);
				l.pending = -1;
			}
			continue;
		}
		if (cur == kCatch && _catching && slot(kArea, kCatch).step == 4) {
			int16 c = (int16)_game->exe().readUint16(kCatchSlots + 2 * _row);
			_nedSlot = switchPlace(kNedArea, c, _nedSlot, kNed, now, false);
			playSound(kNedArea, c);
			_catching &= ~(1 << i);
		}
		Slot &s = slot(kArea, l.slot);
		if (s.deadline < now) {
			s.step++;
			if (s.step < s.sequenceLength) {
				int duration = place(kArea, l.slot, 300 + i);
				s.deadline = now + (int16)duration * kMsPerStep;
			} else {
				switchPlace(kArea, 300 + i, l.slot, 300 + i, now, false);
				l.slot = (int16)(300 + i);
				visible(kArea, 300 + i, false);
				l.slot = -1;
			}
		} else {
			place(kArea, l.slot, 300 + i);
		}
	}
}

// 0x4408c0: Ned changes row one step at a time; in phase 3 he rides off to the right
int Scene2Belt::nedTick(uint32 now) {
	if (_nedSlot == -1)
		return 0;
	if (_phase == 3) {
		slot(kNedArea, kNed).x += 8;
		if (slot(kNedArea, kNed).x > 0x28a) {
			_s2->_next = 2;
			_game->switchScene(2, -1);
			return 1;
		}
	}
	bool go = _row != _target && !_catching && (_nedSlot == 8 || _nedSlot == kNed || _nedSlot == 9);
	if (!go) {
		Slot &s = slot(kNedArea, _nedSlot);
		if (s.deadline < now) {
			s.step++;
			if (s.step < s.sequenceLength) {
				int duration = place(kNedArea, _nedSlot, kNed);
				s.deadline = now + (int16)duration * kMsPerStep;
			} else {
				go = true;
			}
		} else {
			place(kNedArea, _nedSlot, kNed);
		}
	}
	if (!go)
		return 0;
	if (_row != _target && !_catching) {
		playSound(kNedArea, 8);
		int16 move;
		if (_target <= _row) {
			move = _row != 2 ? 0xa5 : 0x19d;
			_row--;
		} else {
			move = _row != 0 ? 0xa4 : 0x19e;
			_row++;
		}
		_nedSlot = switchPlace(kNedArea, move, _nedSlot, kNed, now, false);
		return 0;
	}
	int16 idle = (int16)_game->exe().readUint16(kRowSlots + 2 * _row);
	_nedSlot = switchPlace(kNedArea, idle, _nedSlot, kNed, now, false);
	return 0;
}

// m7 0x4404b0
int Scene2Belt::tick(uint32 input, uint32 now) {
	if (wait(now))
		return 0;
	uint32 lo = input & 0xff;
	uint32 pressed = (lo ^ _lastInput) & lo;
	if ((pressed & kInputUp) && ++_target > 2)
		_target = 2;
	if ((pressed & kInputDown) && --_target < 0)
		_target = 0;
	_lastInput = lo;
	if (now % 1000 == 1) {
		if (_soundIndex > 3)
			_soundIndex = 0;
		playSound(kArea, _game->exe().readSint32(kSoundIds + 4 * _soundPick[_soundIndex]));
		_soundIndex++;
	}

	BitmapRec *belt = bmp(kBelt);
	int arrived = 0;
	if (_phase <= 2) {
		if (belt && belt->object)
			belt->object->adjView(8, 0, 0, 0);
		int w = belt ? belt->bitmap.surface.w : 0;
		for (int i = 0; i < kLanes; i++) {
			Slot &s = slot(kArea, 300 + i);
			s.x -= 8;
			for (int k = 0; k < 3; k++) {
				if (slot(kArea, 304 + i + 4 * k).sprite)
					slot(kArea, 304 + i + 4 * k).sprite->adjPosition(-8, 0);
			}
			if (s.x < -(w >> 1)) {
				s.x += w;
				for (int k = 0; k < 3; k++) {
					if (slot(kArea, 304 + i + 4 * k).sprite)
						slot(kArea, 304 + i + 4 * k).sprite->adjPosition(w, 0);
				}
				spawn();
			}
		}
	}
	BitmapRec *end = bmp(kEnd);
	if (_phase == 2 && end && end->object) {
		Common::Point p = end->object->getPosition();
		end->object->setPosition(p.x - 8, p.y);
		if (slot(kArea, 0x61).sprite)
			slot(kArea, 0x61).sprite->adjPosition(-8, 0);   // 0x430a30
		if (p.x - 8 <= 0) {
			_phase = 3;
			arrived = 1;
			if (belt && belt->object)
				belt->object->setVisibility(false);
		}
	}
	if (_phase == 1 && belt && belt->object) {
		Common::Rect view = belt->object->getView();
		if (view.left == view.width() - 0x280) {
			showBitmap(kEndList, 100, true);
			showSprite(kEndSprite, 1, true);
			if (end && end->object)
				end->object->setPosition(0x280, 0);
			_phase = 2;
			_endSlot = (int16)_game->display().slotSwitch(kArea, 0x61, -1);
			place(kArea, 0x61, 0x61);
			arrived = 0;
		}
	}
	BitmapRec *front = bmp(kFront);
	if (front && front->object) {
		if (_phase >= 1) {
			int x = front->object->getView().left;
			if (x >= 0x199 && x <= 0x321)
				front->object->setVisibility(false);
		}
		front->object->adjView(0xc, 0, 0, 0);
	}
	// Only on the tick the end arrives (the flag is a local that is cleared every tick)
	if (_phase == 3 && arrived) {
		if (slot(kArea, 0x61).step == 0)
			playSound(kArea, 0x61);
		stepSlot(kArea, 0x61, 0x61, now);
	}
	if (nedTick(now))
		return 0;
	laneTick(now);
	_game->display().setCollisionOverlay(Common::Array<CollisionMap *>(), Common::Point(0, 0));
	_game->display().draw();
	debugC(3, kDebugGraphics, "Scene 2:1: input %x row %d/%d Ned %d caught %d spawned %d phase %d", input,
		_row, _target, _nedSlot, _s2->_caught, _spawned, _phase);
	return 0;
}

} // End of namespace NightmareNed
