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
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene2.h"
#include "nightmarened/slothelpers.h"

namespace NightmareNed {

Scene2Board::Scene2Board(Game *game, Scene2 *world) : Scene2Part(game, 2, world) {
	const ExeImage &exe = game->exe();
	for (int i = 0; i < 8; i++) {
		_pieces[i].used = 0;
		_pieces[i].slot = 0;
	}
	_scroll[0] = _scroll[1] = 0;
	_highSound = exe.readSint32(0x4b6610);
	_lowSound = exe.readSint32(0x4b6614);
	_spinSound = exe.readSint32(0x4b6618);
	_jumpTo = 0;
	_creature = -1;
	_at = _goal = kHome;
	_scrollMode = _scrollModeWas = 0;
	_pieceCount = 0;
	_meterState = _meterWanted = 0;
	_nedSlot = -1;
	_jumpStep = _jumpSteps = 0;
	_jumpFrom[0] = _jumpFrom[1] = 0;
	_jumpTarget[0] = _jumpTarget[1] = 0;
	_jumpHeight = 0;
	_jumpPhase = 0;
	_misses = 0;
	_waitTime = 0;
	_atSlot = 0x198;
	_walking = 0;
	_meterSlot = -1;
	_nedState = _wanted = 0;
	_spin = 0;
	_done = _loaded = 0;
	_scrollTime = 0;
	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = nullptr; // 0x484ad0 returns 0 and leaves the target unset
	_hooks.hitTarget = hookHit;
}

// m2 0x482ef0
int Scene2Board::load(int fromScene, int fromSub) {
	_done = 0;
	if (!_game->getNaf(kNaf))
		return 5;
	_loaded = 0;
	ned()->listLoad(0xc);
	if (submit(kMainList, 0x2f, 3))
		return 5;
	_loaded = 1;   // 0x482ed0, the callback on the last entry
	return 0;
}

// m3 0x482f70
int Scene2Board::enter(int fromScene, int fromSub) {
	uint32 now = _game->now();
	ned()->listShow(0xc);
	attachList(kMainList, 0x2f);
	byte pal[256 * 3];
	memset(pal, 0, sizeof(pal));
	if (palette(kPalette))
		memcpy(pal, palette(kPalette), sizeof(pal));
	ned()->copyPalette(pal);
	_game->display().applyPalette(pal, kPalAll);

	BitmapRec *bg = bmp(kBackdrop);
	int w = bg ? bg->bitmap.surface.w : 0x280;
	_scroll[0] = -((w - 0x280) / 2);
	_scroll[1] = 0;
	if (bg && bg->object) {
		bg->object->setPosition(_scroll[0], _scroll[1]);
		bg->object->setDrawOrder(0x68);
		bg->object->setVisibility(true);
	}
	BitmapRec *layer = bmp(kLayer);
	if (layer && layer->object) {
		layer->object->setPosition(_scroll[0], _scroll[1]);
		layer->object->setDrawOrder(4);
		layer->object->setVisibility(true);
		setBitmapKey(layer, 10);
	}
	BitmapRec *box = bmp(kBox);
	if (box && box->object) {
		box->object->setPosition(_scroll[0] + 0xb7, 0x282);
		box->object->setDrawOrder(0x65);
		box->object->setVisibility(true);
		setBitmapKey(box, 10);
	}
	BitmapRec *shelf = bmp(kShelf);
	if (shelf && shelf->object) {
		shelf->object->setPosition(_scroll[0] + 0x18b, 0x155);
		shelf->object->setDrawOrder(0x65);
		shelf->object->setVisibility(true);
		setBitmapKey(shelf, 10);
	}
	_at = kHome;
	_goal = kHome;
	_atSlot = 0x198;
	_creature = switchPlace(kArea, 0x40, -1, 0x198, now, false);
	for (int i = 0; i < 8; i++) {
		if (slot(kArea, 400 + i).sprite)
			slot(kArea, 400 + i).sprite->setDrawOrder(3);
	}
	static const int16 kCreatureSlots[] = { 0x40, 0x41, 0x42, 0xba, 0x43, 0xbb };
	for (int16 id : kCreatureSlots) {
		if (slot(kArea, id).sprite)
			slot(kArea, id).sprite->setDrawOrder(3);
	}
	if (slot(kArea, 0x5b).sprite)
		slot(kArea, 0x5b).sprite->setDrawOrder(5);
	slotShow(_game, kArea, 0xbc, 0x67, true);
	slotShow(_game, kArea, 0x5f, 0x66, true);
	slotShow(_game, kArea, 0xe1, 0x66, false);
	switchPlace(kArea, 0x5f, -1, 0x5f, now, false);

	// The critters: as many as the ride's grabs, in random jars
	for (int i = 0; i < 8; i++) {
		_pieces[i].used = 0;
		_pieces[i].slot = (int16)_game->exe().readUint16(kPieceSlots + 2 * i);
	}
	_pieceCount = _s2->_caught;
	for (int n = _pieceCount; n > 0; n--) {
		int k;
		do {
			k = (int)_game->crtRandom() % 8;
		} while (_pieces[k].used);
		_pieces[k].used = 1;
		slotShow(_game, kArea, _pieces[k].slot, 100, true);
		switchPlace(kArea, _pieces[k].slot, -1, _pieces[k].slot, now, true);
	}
	for (int id = 0x48; id <= 0x4a; id++) {
		if (slot(kArea, id).sprite)
			slot(kArea, id).sprite->setDrawOrder(4);
	}
	if (!loadSst(2, _sst))
		return 5;
	applySst(_sst, _scroll);
	_nedSlot = switchPlace(kNedArea, 0xa8, -1, kNedRef, now, false);
	_nedState = 0;
	_wanted = 0;
	_waitTime = now;
	_misses = 0;
	_scrollModeWas = 0;
	_scrollMode = 0;
	_meterState = 0;
	_meterWanted = 0;
	_meterSlot = -1;
	_walking = 0;
	_world = CollisionWorld();
	CollisionMap *m = map(kMap);
	if (m)
		_world.addMap(m, m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom);
	slot(kNedArea, kNedRef).x = _scroll[0] + 0x11a;
	slot(kNedArea, kNedRef).y = _scroll[1] + 0xa0;
	_game->media().playMusic(kBoardMusic);
	ned()->show(false);
	ned()->enable(false);
	return 0;
}

// m4 0x4834d0
int Scene2Board::leave(int toScene, int toSub) {
	stopAllSounds();
	ned()->show(false);
	ned()->enable(false);
	ned()->setWrap1(false, 0, nullptr, nullptr);
	_sst.clear();
	release(kMainList, 0x2f);
	ned()->listFree(0xc);
	if (_s2->_next == 5)
		_game->media().loadClip(0x18);
	if (toScene != 2)
		_game->closeNaf(kNaf);
	_world = CollisionWorld();
	return 0;
}

// m5 0x4834a0: the TV clip into scene 5 ("87240001.wav")
int Scene2Board::unload(int toScene, int toSub) {
	if (_s2->_next == 5)
		_game->media().playClipWithMusic(1, 0x18);
	return 0;
}

// 0x454920 with the board's world, scroll and hooks
void Scene2Board::nedTick(uint32 input, uint32 now) {
	int f = 0;
	ned()->tick(_world, input, f, _s2->_nedPos, now, _scroll[0], _scroll[1], &_hooks);
}

// 0x4848a0
void Scene2Board::xform(void *user, int &x, int &y) {
	Scene2Board *s = (Scene2Board *)user;
	x += s->_scroll[0];
	y += s->_scroll[1];
}

// 0x484ab0 (+0x00)
int Scene2Board::hookUse(void *user, int x, int y, int16 *state) {
	return 1;
}

// 0x484aa0 (+0x04)
int Scene2Board::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	return 0;
}

// 0x484ac0 (+0x0c)
int Scene2Board::hookHit(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	return 1;
}

// 0x484ae0 / 0x484af0 / 0x484b00
void Scene2Board::soundEnded(uint32 callback) {
	if (callback == kCbPiecesHigh)
		_highSound = 1;
	else if (callback == kCbPiecesLow)
		_lowSound = 1;
	else if (callback == kCbSpin)
		_spinSound = 1;
}

// 0x483a80: Ned's states: 0 waits, 1 spins up, 2 spins, 3 slows down, 4 points at a shelf,
// 5/6 shrugs, 7 leaves; once the board is cleared he walks (ned_tick)
void Scene2Board::nedAnimTick(uint32 input, uint32 now) {
	if (_walking) {
		nedTick(input, now);
		return;
	}
	if (_spinSound && (_nedState == 2 || _nedState == 1)) {
		_spinSound = 0;
		playSoundNotify(kArea, 0x62, kCbSpin);
	} else {
		_spinSound = 0;
	}
	if (_nedSlot == -1)
		return;
	if (_nedState != _wanted) {
		if (_nedState == 0)
			_nedSlot = switchPlace(kNedArea, 0x19b, _nedSlot, kNedRef, now, false);
		switch (_wanted) {
		case 1:
			_misses++;
			_spin = 0xa6;
			playSoundNotify(kArea, 0x62, kCbSpin);
			break;
		case 3:
			_spin = 0x29;
			playSound(kArea, 0x63);
			break;
		case 5:
		case 6:
			_spin = 0x7d;
			break;
		default:
			_spin = 1;
			break;
		}
		slot(kNedArea, _nedSlot).deadline = _spin + now;
		_nedState = _wanted;
	}

	if (_nedState == 0 || _nedState == 7) {
		if (_nedSlot == 0xaa) {
			if (_pieceCount == 0) {
				_game->display().slotSwitch(kArea, 0xe1, -1);
				place(kArea, 0xe1, 0xe1);
			} else if (slot(kNedArea, 0xaa).step == 6 && _meterState == 2) {
				_meterWanted = 3;
				visible(kNedArea, 0xaa, false);
				switchPlace(kArea, 0x5b, -1, 0x5b, now, false);
				_nedSlot = -1;
				return;
			}
		}
		if (stepSlot(kNedArea, _nedSlot, kNedRef, now))
			return;
		switch (_nedSlot) {
		case 0xa8:
			_nedSlot = switchPlace(kNedArea, 0xa8, 0xa8, kNedRef, now, false);
			break;
		case 0xa9:
			_nedSlot = switchPlace(kNedArea, 0xa8, 0xa9, kNedRef, now, false);
			_waitTime = now;
			break;
		case 0xaa: {
			// Ned drops to the floor and walks; the attic exit opens
			visible(kNedArea, 0xaa, false);
			switchPlace(kArea, 0x5b, -1, 0x5b, now, false);
			_nedSlot = -1;
			_s2->_nedPos.x = 0x136 - _scroll[0];
			_walking = 1;
			_scrollMode = 3;
			BitmapRec *bg = bmp(kBackdrop);
			CollisionMap *m = map(kMap);
			for (int y = bg ? bg->bitmap.surface.h : 0; y >= 0; y--) {
				if (m && m->isSolid((int16)_s2->_nedPos.x, (int16)y)) {
					_s2->_nedPos.y = y;
					break;
				}
			}
			ned()->setBasePosition(_s2->_nedPos.x, _s2->_nedPos.y);
			_game->res().getSlot(kNedArea, 4).offsets[0] = 6;
			ned()->setFacing(1);
			_nedSlot = 1;
			ned()->setCurSlot(1);
			ned()->setWrap1(false, bg ? bg->bitmap.surface.w : 0, xform, this);
			ned()->show(true);
			ned()->enable(true);
			nedTick(input, now);
			_s2->openExit();
			_game->hints().trigger(3, ned()->getFacing() != 0, _s2->_nedPos.x, _s2->_nedPos.y, xform, this);
			break;
		}
		default:
			break;
		}
		return;
	}

	Slot &s = slot(kNedArea, _nedSlot);
	if (now <= s.deadline) {
		_wanted = _nedState;
		return;
	}
	switch (_nedState) {
	case 1:
		if (++s.step >= s.sequenceLength)
			s.step = 0;
		_spin -= 10;
		if (_spin <= 0x29) {
			_spin = 0x29;
			_nedState = 2;
			s.deadline = now + 0x29;
		} else {
			s.deadline = now + _spin;
		}
		break;
	case 2:
		if (++s.step >= s.sequenceLength)
			s.step = 0;
		s.deadline = now + _spin;
		break;
	case 3:
		if (++s.step >= s.sequenceLength)
			s.step = 0;
		_spin += 10;
		if (_spin >= 0xa6) {
			_spin = 0xa6;
			if (!(s.step & 1)) {
				_nedState = 4;
				_waitTime = now;
			}
		}
		s.deadline = now + _spin;
		break;
	case 4:
		s.deadline = now + 9999;
		break;
	case 5:
		if (++s.step >= s.sequenceLength) {
			s.step = 0;
			_nedSlot = switchPlace(kNedArea, 0xa8, _nedSlot, kNedRef, now, false);
			_waitTime = now;
			_nedState = 0;
			_wanted = 0;
			return;
		}
		s.deadline = now + 0x7d;
		break;
	case 6:
		if (--s.step < 0) {
			s.step = 0;
			_nedSlot = switchPlace(kNedArea, 0xa8, _nedSlot, kNedRef, now, false);
			_waitTime = now;
			_nedState = 0;
			_wanted = 0;
			return;
		}
		s.deadline = now + 0x7d;
		break;
	default:
		break;
	}
	place(kNedArea, _nedSlot, kNedRef);
	_wanted = _nedState;
}

// 0x4840a0: the creature sits at home (8), hops to the shelf Ned points at and eats the
// piece there, or hops to Ned (9) when the shelf is empty
void Scene2Board::creatureTick(uint32 now) {
	if (_done)
		return;
	int16 c = _creature;
	if (c > 0xba) {
		if (c == 0xbb) {
			// To Ned: he is caught on step 8
			if (slot(kArea, c).step == 0)
				playSound(kArea, 0x43);
			if (slot(kArea, _creature).step == 8) {
				_nedSlot = switchPlace(kNedArea, 0xa9, _nedSlot, kNedRef, now, false);
				playSound(kNedArea, 0xa9);
			}
			if (stepSlot(kArea, _creature, _atSlot, now))
				return;
			_creature = switchPlace(kArea, _atSlot, _creature, _atSlot, now, false);
			_goal = kHome;
			return;
		}
		if (c < 0x190 || c > 0x199)
			return;
		// Sitting at a position: hop when the goal changed, else back to 0x40
		if (_at != _goal) {
			_creature = switchPlace(kArea, 0x41, c, _atSlot, now, false);
			_jumpPhase = 0;
		} else {
			_creature = switchPlace(kArea, 0x40, c, _atSlot, now, false);
		}
		return;
	}
	if (c == 0x43) {
		if (slot(kArea, c).step == 0)
			playSound(kArea, 0x43);
		if (slot(kArea, _creature).step == 8) {
			_nedSlot = switchPlace(kNedArea, 0xa9, _nedSlot, kNedRef, now, false);
			playSound(kNedArea, 0xa9);
		}
		if (stepSlot(kArea, _creature, _atSlot, now))
			return;
		_creature = switchPlace(kArea, _atSlot, _creature, _atSlot, now, false);
		_goal = kHome;
		return;
	}
	if (c == 0xba || c == 0x42) {
		// Eats the piece on step 4
		if (slot(kArea, c).step == 4 && _at >= 0 && _at < 8 && _pieces[_at].used) {
			playSound(kArea, 0x42);
			_pieces[_at].used = 0;
			visible(kArea, _pieces[_at].slot, false);
			_pieceCount--;
		}
		if (stepSlot(kArea, _creature, _atSlot, now))
			return;
		_creature = switchPlace(kArea, _atSlot, _creature, _atSlot, now, false);
		_goal = 9;
		return;
	}
	if (c == 0x40) {
		if (slot(kArea, c).step == 0)
			playSound(kArea, 0x40);
		if (_at == _goal && stepSlot(kArea, c, _atSlot, now))
			return;
		_creature = switchPlace(kArea, _atSlot, _creature, _atSlot, now, false);
		return;
	}
	if (c != 0x41)
		return;

	// The hop: an arc from the position to the goal's, in max(|dx|, |dy|) / 7 steps
	if (_jumpPhase == 0) {
		stepSlot(kArea, c, _atSlot, now);
		if (slot(kArea, _creature).step != 2)
			return;
		Slot &from = slot(kArea, _atSlot);
		Slot &to = slot(kArea, _goal + 400);
		_jumpFrom[0] = from.x;
		_jumpFrom[1] = from.y;
		_jumpTarget[0] = to.x;
		_jumpTarget[1] = to.y;
		int d = MAX(ABS(_jumpFrom[0] - _jumpTarget[0]), ABS(_jumpFrom[1] - _jumpTarget[1]));
		_jumpStep = 1;
		_jumpSteps = (int16)(d / 7);
		_jumpHeight = d / 3;
		_jumpPhase++;
		return;
	}
	if (_jumpPhase == 1) {
		Slot &j = slot(kArea, 0x41);
		if (j.deadline >= now)
			return;
		int n = _jumpSteps ? _jumpSteps : 1;
		j.x = (_jumpTarget[0] - _jumpFrom[0]) * _jumpStep / n + _jumpFrom[0];
		int y = (_jumpTarget[1] - _jumpFrom[1]) * _jumpStep / n + _jumpFrom[1];
		j.y = y - (int)(sin(_jumpStep * 3.1415926535 / n) * _jumpHeight);   // 0x4954a0
		if (_jumpStep == _jumpSteps / 4)
			slot(kArea, _creature).step++;
		if (_jumpStep == _jumpSteps / 2)
			slot(kArea, _creature).step++;
		place(kArea, 0x41, 0x41);
		_jumpStep++;
		if (_jumpStep >= _jumpSteps) {
			_at = _goal;
			_atSlot = (int16)(_goal + 400);
			_jumpPhase++;
		}
		j.deadline = now + 1;
		return;
	}
	if (_jumpPhase == 2 && !stepSlot(kArea, c, _atSlot, now)) {
		int16 next;
		if (_at == kHome) {
			next = _atSlot;
		} else if (_at == kNedSpot) {
			next = _jumpTo != 0xba ? 0x43 : 0xbb;
		} else {
			int e = _pieces[CLIP<int>(_at, 0, 7)].slot - 0x19a;
			next = e >= 4 ? 0x42 : 0xba;
			_jumpTo = next;
		}
		_creature = switchPlace(kArea, next, _creature, _atSlot, now, false);
	}
}

// 0x484680: the pieces loop; one of each half hums at a time
void Scene2Board::piecesTick(uint32 now) {
	if (_highSound) {
		_highSound = 0;
		for (int i = 0; i < 8; i++) {
			int16 s = _pieces[i].slot;
			if (_pieces[i].used && s >= 0x19e && s <= 0x1a1) {
				playSoundNotify(kArea, 0x19e, kCbPiecesHigh);
				break;
			}
		}
	}
	if (_lowSound) {
		_lowSound = 0;
		for (int i = 0; i < 8; i++) {
			int16 s = _pieces[i].slot;
			if (_pieces[i].used && s >= 0x19a && s <= 0x19d) {
				playSoundNotify(kArea, 0x19a, kCbPiecesLow);
				break;
			}
		}
	}
	for (int i = 0; i < 8; i++) {
		if (_pieces[i].used && !stepSlot(kArea, _pieces[i].slot, _pieces[i].slot, now))
			switchPlace(kArea, _pieces[i].slot, _pieces[i].slot, _pieces[i].slot, now, false);
	}
}

// 0x4847a0: the exit sign (0x49 lights, then 0x48; 0x4a once Ned is out)
void Scene2Board::meterTick(uint32 now) {
	if (_meterState != _meterWanted) {
		_meterState = _meterWanted;
		if (_meterWanted == 1) {
			_meterSlot = switchPlace(kArea, 0x49, _meterSlot, 0x49, now, false);
			return;
		}
		if (_meterWanted == 3) {
			_meterSlot = switchPlace(kArea, 0x4a, _meterSlot, 0x48, now, false);
			return;
		}
	}
	if (_meterState == 1) {
		if (!stepSlot(kArea, _meterSlot, _meterSlot, now)) {
			_meterSlot = switchPlace(kArea, 0x48, _meterSlot, 0x48, now, false);
			_meterWanted = 2;
		}
	} else if (_meterState == 3) {
		if (!stepSlot(kArea, _meterSlot, 0x48, now)) {
			_meterState = 4;
			_meterWanted = 4;
		}
	}
}

// 0x4848c0: once Ned walks, the room scrolls to keep him near x 200 facing right, 440 left
void Scene2Board::camera(uint32 now, int d[2]) {
	d[0] = d[1] = 0;
	int16 state = ned()->getState();
	if (state == 10)
		return;
	int x = _s2->_nedPos.x + _scroll[0];
	if (ned()->getFacing() == 0) {
		if (state == 7) {
			if (now > _scrollTime + 0x12) {
				d[0] = 1;
				d[1] = 1;
			}
		} else if (state == 8) {
			if (now > _scrollTime + 0xc) {
				d[0] = 1;
				d[1] = -1;
			}
		} else if (state == 1 && x > 0x1b7) {
			if (now > _scrollTime + 0xc)
				d[0] = 1;
		} else if (x <= 0x1b7) {
			int dist = 0x1b8 - x;
			d[0] = dist < 7 ? 1 : (dist > 0xc ? 4 : 2);
		}
	} else {
		if (state == 7) {
			if (now > _scrollTime + 0x12) {
				d[0] = -1;
				d[1] = 1;
			}
		} else if (state == 8) {
			if (now > _scrollTime + 0xc) {
				d[0] = -1;
				d[1] = -1;
			}
		} else if (state == 1 && x < 0xc9) {
			if (now > _scrollTime + 0xc)
				d[0] = -1;
		} else if (x >= 0xc9) {
			int dist = x - 200;
			d[0] = dist < 7 ? -1 : (dist > 0xc ? -4 : -2);
		}
	}
	if (d[0] || d[1])
		_scrollTime = _game->now();
}

// 0x479fb0
void Scene2Board::moveSlot(int area, int id, int dx, int dy) {
	slotMoveBy(_game, area, id, dx, dy);
}

// m7 0x483570
int Scene2Board::tick(uint32 input, uint32 now) {
	if (wait(now))
		return 0;
	pollSounds();
	if ((slot(kArea, 0x5f).step >> 2) < _misses - 1) {
		stepSlot(kArea, 0x5f, 0x5f, now);
		playSound(kArea, 0x5f);
	}
	if (_nedState != 7) {
		if (_nedState == 2 && (input & kInputButton2))
			_wanted = 3;
		if (_nedState == 4 && now >= _waitTime + 2000 && _at == kHome && _goal == kHome) {
			int16 k = (int16)(slot(kNedArea, _nedSlot).step >> 1);
			// Ned's pointing loop has 16 steps; the table has 8 entries
			bool used = k >= 0 && k < 8 && _pieces[k].used;
			if (!used && _misses < 8) {
				_wanted = 1;
			} else {
				if (used)
					_goal = k;
				_wanted = (k < 4) + 5;
			}
		}
		if (_nedState == 0 && _wanted == 0 && _at == kHome && _goal == kHome) {
			if (_misses < 8 && _pieceCount != 0) {
				if (now >= _waitTime + 2000)
					_wanted = 1;
			} else if (_scrollModeWas == 0) {
				_scrollMode = 1;
				BitmapRec *box = bmp(kBox);
				if (_pieceCount != 0 && box && box->object)
					box->object->setVisibility(false);
			} else if (_scrollModeWas == 2) {
				_done = 1;
				if (_pieceCount == 0 || _meterState != 0) {
					_nedState = 7;
					_wanted = 7;
					_nedSlot = switchPlace(kNedArea, 0xaa, _nedSlot, kNedRef, now, false);
					playSound(kNedArea, 0xaa);
				} else {
					_meterWanted = 1;
				}
			}
		}
	}

	_scrollModeWas = _scrollMode;
	if (_scrollMode == 1 || _scrollMode == 3) {
		int d[2] = { 0, -4 };
		if (_scrollMode == 3)
			camera(now, d);
		BitmapRec *bg = bmp(kBackdrop);
		int bw = bg ? bg->bitmap.surface.w : 0x280;
		int bh = bg ? bg->bitmap.surface.h : 0x1e0;
		if (d[0] + _scroll[0] > 0)
			d[0] = -_scroll[0];
		if (d[0] + _scroll[0] < 0x280 - bw)
			d[0] = 0x280 - bw - _scroll[0];
		if (d[1] + _scroll[1] < 0x1e0 - bh) {
			d[1] = 0x1e0 - bh - _scroll[1];
			if (_scrollModeWas == 1)
				_scrollMode = 2;
		}
		moveSlot(kNedArea, kNedRef, d[0], d[1]);
		moveSlot(kArea, 0x198, d[0], d[1]);
		for (int i = 0; i < 4; i++) {
			moveSlot(kArea, 0x19a + i, d[0], d[1]);
			moveSlot(kArea, 0x19e + i, d[0], d[1]);
		}
		moveSlot(kArea, 0x48, d[0], d[1]);
		moveSlot(kArea, 0x49, d[0], d[1]);
		moveSlot(kArea, 0x5b, d[0], d[1]);
		moveSlot(kArea, 0xbc, d[0], d[1]);
		moveSlot(kArea, 0x5f, d[0], d[1]);
		static const uint32 kLayers[] = { kBackdrop, kLayer, kBox, kShelf };
		for (uint32 va : kLayers) {
			BitmapRec *rec = bmp(va);
			if (rec && rec->object)
				rec->object->adjPosition(d[0], d[1]);
		}
		if (_walking)
			ned()->adjPosition(d[0], d[1]);
		_scroll[0] += d[0];
		_scroll[1] += d[1];
	}

	nedAnimTick(input, now);
	piecesTick(now);
	creatureTick(now);
	meterTick(now);

	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	_game->display().setCollisionOverlay(maps, Common::Point(_scroll[0], _scroll[1]));
	debugC(3, kDebugGraphics, "Scene 2:2: input %x Ned %d,%d slot %d state %d/%d creature %d at %d goal %d pieces %d misses %d scroll %d,%d mode %d meter %d",
		input, _s2->_nedPos.x, _s2->_nedPos.y, _nedSlot, _nedState, _wanted, _creature, _at, _goal, _pieceCount,
		_misses, _scroll[0], _scroll[1], _scrollMode, _meterState);
	if (_walking) {
		BitmapRec *bg = bmp(kBackdrop);
		if (_s2->_nedPos.x <= 0) {
			_s2->_next = 0;
		} else if (_s2->_nedPos.x >= (bg ? bg->bitmap.surface.w : 0x280)) {
			_s2->_next = 5;
		} else {
			_game->display().draw();
			return 0;
		}
		ned()->show(false);
		_game->display().draw();
		_game->switchScene(2, -1);
		return 0;
	}
	if (_meterState == 4) {
		_s2->_next = 0;
		_game->switchScene(2, -1);
		return 0;
	}
	_game->display().draw();
	return 0;
}

} // End of namespace NightmareNed
