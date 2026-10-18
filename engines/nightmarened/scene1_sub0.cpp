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

#include "common/config-manager.h"
#include "common/debug.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/media.h"
#include "nightmarened/menus.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_sub0.h"

namespace NightmareNed {

// The sprites of the main list with their draw orders (0x47ffe0), hidden again by 0x480770
static const int16 kSub0Sprites[][2] = {
	{ 0x45, 900 }, { 0x36, 900 }, { 0x37, 900 }, { 0x38, 1000 }, { 0x3e, 1000 },
	{ 0x3a, 0x3b6 }, { 0x3f, 1000 }, { 0x41, 6 }, { 0x117, 6 }, { 0x39, 0x38 },
	{ 0x3b, 0x3fc }, { 0x44, 1000 }, { 0x1b7, 1000 }, { 0x118, 1000 }, { 0x119, 1000 },
	{ 0x11a, 1000 }, { 0x3d, 0x3f1 }, { 0x3c, 0x3fc }, { 0x43, 1000 }, { 0x1cf, 1000 },
	{ 0x1d0, 1000 }, { 0x1d3, 1000 }, { 0x42, 1000 }, { 0x1b4, 1000 }, { 0xae, 0x3f2 },
	{ 0xad, 0x3f2 }
};

Scene1Sub0::Scene1Sub0(Game *game, Scene1 *top) : GraveyardRoom(game, 0, top), _revisit(0) {
	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
	_hookTag = 0;
	_nedPos = Common::Point(0, 0);
	_scrollX = _scrollY = 0;
	memset(_palette, 0, sizeof(_palette));
	memset(_fadePal, 0, sizeof(_fadePal));
	_fadeLevel = 0;
	_fadeTime = 0;
	_savedX = _savedFacing = _from = 0;
	_listLoaded = _exit = _stage = _zapped = 0;
	_flashSlot = _leftSlot = _rightSlot = _gateSlot = -1;
	_gateNextA = _gateNextB = -1;
	_lidSlot = _riseSlot = _grabSlot = -1;
	_flashTime = 0;
	_unused5213d4 = 0;
	_testHookRead = false;
}

Scene1Sub0::~Scene1Sub0() {
}

void Scene1Sub0::listCallback(uint32 cb) {
	// 0x47ffc0
	if (cb == kListDone)
		_listLoaded = 1;
}

// m2 0x480240
int Scene1Sub0::load(int fromScene, int fromSub) {
	return submit(kVideoList, 1, 99);
}

// m3 0x480260
int Scene1Sub0::enter(int fromScene, int fromSub) {
	// Test hook: start as a revisit
	if (!_testHookRead) {
		_testHookRead = true;
		if (ConfMan.hasKey("nightmarened_s1_crypt_done"))
			_revisit = ConfMan.getBool("nightmarened_s1_crypt_done") ? 1 : 0;
	}

	_savedX = top()->_nedPos.x;
	_savedFacing = ned()->getFacing();
	_from = fromScene;
	ned()->setWrap1(false, 0, nullptr, nullptr);
	_gateNextA = -1;
	_gateNextB = -1;
	_listLoaded = 0;
	playVideo();
	ned()->setCostume(1);
	if (fromScene == 1)
		top()->layersVisible(false);
	_hookTag = 10;
	ned()->show(false);
	RdxSurface *primary = _game->display().getPrimary();
	primary->setClearColor(0);
	draw();
	primary->setClearColor(-1);

	// 0x487250 / 0x487400
	_world = CollisionWorld();
	_world.setUser(nullptr);
	// The list's callback has run: submit() is synchronous
	ned()->listShow(kNedList);
	addMap(kMapFloor);
	slotAt(kNedArea, 4).offsets[0] = 6;
	loadSst(0, 999, 8, _sst);
	setupSprites();
	for (uint i = 0; i < _sst.size(); i++)
		placeSst(_sst[i]);
	_exit = 0;

	if (_revisit == 0) {
		_nedPos.x = 0x17c;
		_nedPos.y = 0x1df;
		for (int y = 0x1df; y >= 0; y--) {
			if (solidAt(kMapFloor, _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
		_stage = 0;
		_zapped = 0;
		_riseSlot = -1;
		_grabSlot = -1;
		_flashSlot = switchSlot(0x45, -1);
		_leftSlot = switchSlot(0x36, -1);
		_gateSlot = switchSlot(0x3f, -1);
		_rightSlot = switchSlot(0x38, -1);
		_lidSlot = switchSlot(0xad, -1);
		_flashTime = 0;
		_targets.clear();
		place(_flashSlot, 0x45, true, nullptr, nullptr);
		place(_leftSlot, 0x36, true, nullptr, nullptr);
		place(_gateSlot, 0x3f, true, nullptr, nullptr);
		place(_rightSlot, 0x38, true, nullptr, nullptr);
		place(_lidSlot, 0xad, true, nullptr, nullptr);
		targetAdd(kArea, _leftSlot, true);
		targetAdd(kArea, _gateSlot, true);
		targetAdd(kArea, _rightSlot, true);
	} else {
		addMap(kMapOpen);
		_nedPos.x = 0x122;
		_nedPos.y = 0;
		for (int y = 0; y < 0x1e0; y++) {
			if (solidAt(kMapOpen, _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
		// The target list is not created on a revisit
		_targets.clear();
		_stage = 1;
		_zapped = 0;
		_riseSlot = -1;
		_flashSlot = _leftSlot = _gateSlot = -1;
		_flashTime = 0x7fffffff;
		_grabSlot = switchSlot(0x119, -1);
		_rightSlot = switchSlot(0x3a, -1);
		slot(_rightSlot).step = slot(_rightSlot).sequenceLength - 1;
		_lidSlot = switchSlot(0xad, -1);
		place(_grabSlot, 0x118, true, nullptr, nullptr);
		place(_rightSlot, 0x38, true, nullptr, nullptr);
		place(_lidSlot, 0xad, true, nullptr, nullptr);
	}
	int f = 0;
	ned()->tick(_world, 0, f, _nedPos, _game->now(), _scrollX, _scrollY, &_hooks);
	return 0;
}

// m4 0x480770
int Scene1Sub0::leave(int toScene, int toSub) {
	if (_from == 1)
		top()->nedWrap();
	_sst.clear();  // 0x45a5f0
	hideSprites();
	release(kMainList, kMainCount);
	release(kVideoList, 1);
	RdxSurface *primary = _game->display().getPrimary();
	primary->setClearColor(0);
	draw();
	primary->setClearColor(-1);

	// Ned comes back out 100 px beside the crypt, on the graveyard's floor
	Common::Point &pos = top()->_nedPos;
	pos.x = _savedFacing == 0 ? _savedX - 100 : _savedX + 100;
	pos.y = 0;
	while (!solidAt(Scene1::kMapFloor, pos.x, pos.y)) {
		pos.y++;
		if (pos.y >= 0x1e0)
			break;
	}
	ned()->resetIdle();
	_targets.clear();  // 0x486d00
	ned()->listFree(kNedList);
	_world = CollisionWorld();  // 0x487280
	_unused5213d4 = 0;
	if (toScene != 1) {
		ned()->show(false);
		ned()->resetIdle();
		top()->leave(toScene, toSub);
		top()->unload(toScene, toSub);
		return 0;
	}
	_revisit = 1;
	return 0;
}

// m5 0x480a70
int Scene1Sub0::unload(int toScene, int toSub) {
	if (_from == 1)
		top()->exitsEnable();
	return 0;
}

// m7 0x480a90
int Scene1Sub0::tick(uint32 input, uint32 now) {
	fadeTick(now);
	flashTick(now);
	leftTick(now);
	riseTick(now);
	grabTick(now);
	rightTick(now);
	gateTick(now);
	if (_stage != 0)
		input = 0;
	int f = 0;
	ned()->tick(_world, input, f, _nedPos, now, _scrollX, _scrollY, &_hooks);
	draw();
	if (_exit != 0) {
		ned()->show(false);
		switchTo(1, -1);
	}
	return 0;
}

// 0x481b40: the way in, file 0x56 played into the video bitmap at trans_fps
void Scene1Sub0::playVideo() {
	ned()->show(false);
	if (_from == 1) {
		top()->backdropVisible(false);
		top()->hillsVisible(false);
	}
	if (wait(kVideoList) != 0)
		return;
	BitmapRec *rec = bmp(kBmpVideo);
	if (rec && rec->object) {
		rec->object->setDrawOrder(0x63);
		rec->object->setDestination(_game->display().getPrimary());
	}
	ned()->listLoad(kNedList);
	if (submit(kMainList, kMainCount, kMainScene) != 0)
		return;
	MenuVideo video;
	video.open(_game, kVideoFile);
	int count = video.getFrameCount();
	int fps = MAX<int>(1, _game->media()._transFps);
	uint32 next = 0;
	int frame = 0;
	while (frame < count && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		if (next < now) {
			if (_from == 1)
				top()->cloudsDrift(now);
			if (rec)
				video.decodeFrame(frame, rec->bitmap.surface);
			next = 1000 / fps + now;
			frame++;
			draw();
		}
		frameWait(next - now, now);
	}
	video.close();
	release(kVideoList, 1);
	ned()->show(true);
}

// 0x47ffe0
void Scene1Sub0::setupSprites() {
	_fadeTime = 0;
	_fadeLevel = 1;
	attachList(kMainList, kMainCount);
	BitmapRec *room = bmp(kBmpRoom);
	if (room && room->object) {
		room->object->setDrawOrder(5000);
		room->object->setVisibility(true);
	}
	const byte *pal = palette(kPaletteRoom);
	if (pal)
		memcpy(_palette, pal, sizeof(_palette));
	ned()->copyPalette(_palette);
	for (uint i = 0; i < ARRAYSIZE(kSub0Sprites); i++) {
		Slot &s = slot(kSub0Sprites[i][0]);
		if (s.sprite)
			s.sprite->setDrawOrder(kSub0Sprites[i][1]);
	}
}

// 0x481a20 (olist_foreach callback): area-2 entries set the slot's position and place its sprite
void Scene1Sub0::placeSst(const Placement &p) {
	if (p.area != kArea || p.slot < 0 || p.slot >= kAreaSlots)
		return;
	Slot &s = slot(p.slot);
	s.x = p.x;
	s.y = p.y;
	if (!s.sprite)
		return;
	int frame = 0, duration = 0;
	seqStep(kArea, p.slot, s.step, frame, duration);
	int ox = 0, oy = 0;
	frameOffset(kArea, p.slot, frame, ox, oy);
	s.sprite->setPosition(s.x - ox, s.y - oy);
}

void Scene1Sub0::hideSprites() {
	for (uint i = 0; i < ARRAYSIZE(kSub0Sprites); i++)
		visible(kSub0Sprites[i][0], false);
}

// 0x480b50: the room fades in from black in tenths, one every 100 ms
void Scene1Sub0::fadeTick(uint32 now) {
	if (!(_fadeTime < now) || _fadeLevel >= 0xb)
		return;
	for (int i = 0; i < 256 * 3; i++)
		_fadePal[i] = (byte)(_palette[i] * _fadeLevel / 10);
	_game->display().applyPalette(_fadePal, kPalAll);
	_fadeLevel++;
	_fadeTime = now + 100;
}

// 0x480c10: slot 0x45 plays every 10 s; on its step 3 the right thing turns (0x39)
void Scene1Sub0::flashTick(uint32 now) {
	if (!(_flashTime < now))
		return;
	if (_flashSlot == -1) {
		_flashSlot = switchSlotPre(0x45, -1);
		playSound(_flashSlot);
	}
	Slot &s = slot(0x45);
	if (!(s.deadline < now))
		return;
	s.step++;
	if (_rightSlot != 0x3a && s.step == 3) {
		targetRemove(kArea, _rightSlot);
		_rightSlot = switchSlotPre(0x39, _rightSlot);
		targetAdd(kArea, _rightSlot, true);
		playSound(_rightSlot);
	}
	if (s.sequenceLength <= s.step) {
		_flashTime = now + 10000;
		_flashSlot = switchSlot(-1, _flashSlot);
	}
	if (_flashSlot != -1) {
		int duration = place(0x45, 0x45, true, nullptr, nullptr);
		s.deadline = now + (int16)duration * kMsPerStep;
	}
}

// 0x480d80: once both things are zapped and Ned is in, 0x3c rises, then 0x3d calls the hands
void Scene1Sub0::riseTick(uint32 now) {
	if (_stage == 1 && (_zapped & 3) == 3) {
		_stage = 2;
		_riseSlot = switchSlotPre(0x3c, _riseSlot);
		playSound(0x3c);
	}
	if (_riseSlot == -1)
		return;
	Slot &s = slot(_riseSlot);
	if (!(s.deadline < now))
		return;
	s.step++;
	if (s.sequenceLength <= s.step) {
		if (_riseSlot == 0x3b) {
			slot(0x3b).step = 0;
		} else if (_riseSlot == 0x3c) {
			_riseSlot = switchSlot(0x3d, _riseSlot);
			loopSound(0x3d);
			_lidSlot = switchSlot(0xae, _lidSlot);
			place(_lidSlot, 0xad, true, nullptr, nullptr);
		} else if (_riseSlot == 0x3d) {
			if (_grabSlot == -1) {
				_grabSlot = switchSlot(0x44, -1);
				playSound(_grabSlot);
			}
			slot(0x3d).step = 0;
		}
	}
	int duration = place(_riseSlot, 0x3b, true, nullptr, nullptr);
	slot(_riseSlot).deadline = now + (int16)duration * kMsPerStep;
}

// 0x480f50: the left thing (0x36), zapped 0x37
void Scene1Sub0::leftTick(uint32 now) {
	if (_leftSlot == -1)
		return;
	Slot &s = slot(_leftSlot);
	if (!(s.deadline < now))
		return;
	s.step++;
	if (_leftSlot == 0x37) {
		if (slot(0x37).step == 7)
			_riseSlot = switchSlot(0x3b, _riseSlot);
		else if (slot(0x37).step == 0)
			playSound(0x37);
	}
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		if (_leftSlot == 0x37) {
			visible(0x37, false);
			_leftSlot = -1;
			if (_gateNextA != -1) {
				_gateSlot = switchSlotPre(_gateNextA, _gateSlot);
				_gateNextA = -1;
			}
		}
	}
	if (_leftSlot != -1) {
		int duration = place(_leftSlot, 0x36, true, nullptr, nullptr);
		slot(_leftSlot).deadline = now + (int16)duration * kMsPerStep;
	}
}

// 0x4810b0: the right thing (0x38 loops, 0x39 turns, 0x3a zapped and holds)
void Scene1Sub0::rightTick(uint32 now) {
	if (_rightSlot == -1 || !(slot(_rightSlot).deadline < now))
		return;
	Slot &s = slot(_rightSlot);
	s.step++;
	if (s.sequenceLength <= s.step) {
		if (_rightSlot == 0x39) {
			targetRemove(kArea, _rightSlot);
			_rightSlot = switchSlot(0x38, _rightSlot);
			targetAdd(kArea, _rightSlot, true);
		} else if (_rightSlot == 0x3a) {
			s.step = s.sequenceLength - 1;
			if (_gateNextB != -1) {
				_gateSlot = switchSlotPre(_gateNextB, _gateSlot);
				_gateNextB = -1;
			}
		} else {
			s.step = 0;
		}
	}
	if (_rightSlot != -1) {
		int duration = place(_rightSlot, 0x38, true, nullptr, nullptr);
		slot(_rightSlot).deadline = now + (int16)duration * kMsPerStep;
	}
}

// 0x4811f0: the hands; at the end Ned is back in the graveyard
void Scene1Sub0::grabTick(uint32 now) {
	if (_grabSlot == -1)
		return;
	Slot &s = slot(_grabSlot);
	if (!(s.deadline < now))
		return;
	if (_grabSlot == 0x44 && slot(0x44).step == 1) {
		ned()->playAction(0x1b, _nedPos, _scrollX, _scrollY);
	} else if (_grabSlot == 0x119) {
		if (slot(0x119).step == 3)
			ned()->playAction(0x1b, _nedPos, _scrollX, _scrollY);
		else if (slot(0x119).step == 1)
			playSound(0x119);
	}
	s.step++;
	if (s.sequenceLength <= s.step) {
		switch (_grabSlot) {
		case 0x118:
			slot(0x118).step = 0;
			break;
		case 0x44:
			_grabSlot = switchSlot(0x11a, _grabSlot);
			stopSound(0x3d);
			playSound(0x11a);
			_riseSlot = switchSlot(-1, _riseSlot);
			break;
		case 0x119:
			slot(0x119).step = slot(0x119).sequenceLength - 1;
			_exit = 1;
			break;
		case 0x11a:
			_grabSlot = switchSlot(0x1b7, _grabSlot);
			playSound(_grabSlot);
			break;
		case 0x1b7:
			slot(0x1b7).step = 0;
			_exit = 1;
			break;
		default:
			break;
		}
	}
	if (_grabSlot != -1) {
		int duration = place(_grabSlot, 0x118, true, nullptr, nullptr);
		slot(_grabSlot).deadline = now + (int16)duration * kMsPerStep;
	}
}

// 0x481420: the middle thing (0x3f) and what it plays when zapped
void Scene1Sub0::gateTick(uint32 now) {
	if (_gateSlot == -1)
		return;
	Slot &s = slot(_gateSlot);
	if (!(s.deadline < now))
		return;
	s.step++;
	if (_gateSlot == 0x117 && slot(0x117).step == 8) {
		playSound(0x117);
		// The original passes the graveyard's Ned position (DAT_005245e8) here
		ned()->playAction(1, top()->_nedPos, _scrollX, _scrollY);
	} else if (_gateSlot == 0x41 && slot(0x41).step == 10) {
		playSound(0x41);
	}
	if (slot(_gateSlot).step >= slot(_gateSlot).sequenceLength) {
		targetRemove(kArea, _gateSlot);
		bool readd = false;
		switch (_gateSlot) {
		case 0x117:
		case 0x41:
		case 0x1cf:
		case 0x1d0:
			_gateSlot = switchSlot(0x3f, _gateSlot);
			readd = true;
			break;
		case 0x3f:
			slot(0x3f).step = 0;
			readd = true;
			break;
		case 0x42:
		case 0x1d3:
			_gateSlot = switchSlot(0x43, _gateSlot);
			playSound(_gateSlot);
			break;
		case 0x43:
			_gateSlot = switchSlot(0x1b4, _gateSlot);
			slot(0x3f).y -= 0x4b;
			addMap(kMapOpen);
			break;
		case 0x1b4:
			slot(0x1b4).step = 0;
			slot(0x3f).y -= 0x4b;
			break;
		default:
			break;
		}
		if (readd)
			targetAdd(kArea, _gateSlot, true);
	}
	if (_gateSlot != -1) {
		int duration = place(_gateSlot, 0x3f, true, nullptr, nullptr);
		slot(_gateSlot).deadline = now + (int16)duration * kMsPerStep;
	}
}

void Scene1Sub0::loopSound(int id) {
	Slot &s = slot(id);
	_game->stopSlotSound(s);
	if (s.sound.size() < 18)
		return;
	uint32 pcmOffset = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcmOffset >= s.sound.size())
		return;
	_game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcmOffset, s.sound.size() - pcmOffset, 0, 0, true);
}

// 0x481930: the target in front of Ned, as a screen rect (box 0, or 3 when flipped)
int Scene1Sub0::findTarget(int16 &dir, int16 &found, int32 rect[4]) {
	found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, dir, unused, found))
		return 1;
	Slot &s = slot(found);
	Common::Point p = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
	int frame = 0, duration = 0;
	seqStep(kArea, found, s.step, frame, duration);
	boxRect(kArea, found, frame, s.flip ? 3 : 0, rect);
	rect[0] += p.x;
	rect[1] += p.y;
	rect[3] += p.y;
	rect[2] += p.x;
	return 1;
}

// ---- Hooks ----

// 0x481710 (+0x00)
int Scene1Sub0::hookUse(void *user, int x, int y, int16 *state) {
	Scene1Sub0 *s = (Scene1Sub0 *)user;
	int16 dir = -1, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(dir, found, rect);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x4816f0 (+0x04): region 3 counts the stage up; every other region takes state 0xe
int Scene1Sub0::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene1Sub0 *s = (Scene1Sub0 *)user;
	if (id == 3) {
		s->_stage++;
		return 0;
	}
	*state = 0xe;
	return 1;
}

// 0x4818f0 (+0x08)
void Scene1Sub0::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	*a = 2;
	int16 dir = 0;
	((Scene1Sub0 *)user)->findTarget(dir, *b, rect);
}

// 0x481760 (+0x0c)
int Scene1Sub0::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene1Sub0 *s = (Scene1Sub0 *)user;
	switch (*b) {
	case 0x36:
		s->targetRemove(kArea, s->_leftSlot);
		s->_leftSlot = s->switchSlotPre(0x37, s->_leftSlot);
		s->_zapped |= 2;
		s->leftTick(0xffffffff);
		s->_gateNextA = (s->_zapped & 3) != 3 ? 0x1cf : 0x42;
		return 1;
	case 0x38:
	case 0x39:
		s->targetRemove(kArea, s->_rightSlot);
		s->visible(0x3e, true);
		s->_rightSlot = s->switchSlotPre(0x3a, s->_rightSlot);
		s->playSound(s->_rightSlot);
		s->_zapped |= 1;
		s->rightTick(0xffffffff);
		s->_gateNextB = (s->_zapped & 3) != 3 ? 0x1d0 : 0x1d3;
		break;
	case 0x3f:
		// From Ned's side: 0x117 when he stands right of it, else 0x41
		s->_gateSlot = s->switchSlotPre(s->slot(0x3f).x <= s->_nedPos.x ? 0x117 : 0x41, s->_gateSlot);
		s->gateTick(0xffffffff);
		return 1;
	default:
		break;
	}
	return 1;
}

} // End of namespace NightmareNed
