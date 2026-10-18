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
#include "nightmarened/scene1_enemies2.h"
#include "nightmarened/scene1_sub7.h"

namespace NightmareNed {

// The sprites 0x447630 attaches hidden, with the slot whose z they take (or a fixed z)
static const struct {
	int16 id;
	int16 zSlot;
	int16 z;
} kSub7Sprites[] = {
	{ 0x6a, 0x69, 0 }, { 0x6c, 0x69, 0 }, { 0x6b, 0x69, 0 }, { 0x69, 0x69, 0 },
	{ 0x68, 0x67, 0 }, { 0x67, 0x67, 0 }, { 0x71, -1, 9 }, { 0x72, -1, 9 },
	{ 0x77, 0x70, 0 }, { 0x75, 0x70, 0 }, { 0x76, 0x70, 0 }, { 0x73, 0x70, 0 },
	{ 0x7f, 0x70, 0 }, { 0x74, 0x70, 0 }, { 0x70, -1, 9 }, { 0x6d, 0x6d, 0 },
	{ 0x6f, 0x6d, 0 }, { 0x6e, 0x6d, 0 }, { 0x1c7, 0x6d, 0 }, { 0x80, 0x80, 0 },
	{ 0x1ca, 0x70, 0 }, { 0x1cb, 0x70, 0 }, { 0x1cc, 0x70, 0 }, { 0x1e3, 0x6d, 0 },
	{ 0x195, 0x195, 0 }, { 0x1e4, 0x195, 0 }, { 0x13f, -1, 9 }, { 0x141, -1, 9 },
	{ 0x142, -1, 9 }, { 0x143, -1, 9 }
};

// The slots that get a FX_HORIZ_FLIP effect
static const int16 kSub7Flips[] = { 0x1ca, 0x74, 0x1cb, 0x76, 0x7f };

Scene1Sub7::Scene1Sub7(Game *game, Scene1 *top) : GraveyardRoom(game, 7, top), _revisit(0) {
	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
	_nedPos = Common::Point(0, 0);
	_scrollX = _scrollY = 0;
	memset(_paletteRoom, 0, sizeof(_paletteRoom));
	memset(_palette, 0, sizeof(_palette));
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < kPathSteps; i++) {
		uint32 va = 0x4a4968 + i * 12;
		_path[i].flip = exe.readSint32(va);
		_path[i].left = (int16)exe.readUint16(va + 4);
		_path[i].dx = (int16)exe.readUint16(va + 6);
		_path[i].slot = (int16)exe.readUint16(va + 8);
	}
	for (int i = 0; i < kReactions; i++)
		_reactions[i] = (int16)exe.readUint16(0x4a4958 + 2 * i);
	_bounce = (int16)exe.readUint16(0x4a4a34);
	_entryZ = 0;
	_coverSlot = _boxSlot = _shelfSlot = _figureSlot = _dropSlot = _doorSlot = -1;
	_knocked = _reactPending = _reacted = 0;
	_idleTime = 0;
	_landed = _walking = _pathIndex = _inputLock = _reaction = 0;
	_fallSpeed = _fallAccel = _floorY = 0;
	_testHookRead = false;
}

Scene1Sub7::~Scene1Sub7() {
}

// m2 0x447610
int Scene1Sub7::load(int fromScene, int fromSub) {
	return submit(kVideoList, 1, 99);
}

// m3 0x447630
int Scene1Sub7::enter(int fromScene, int fromSub) {
	// Test hook: start as a revisit
	if (!_testHookRead) {
		_testHookRead = true;
		if (ConfMan.hasKey("nightmarened_s1_sub7_done"))
			_revisit = ConfMan.getBool("nightmarened_s1_sub7_done") ? 1 : 0;
	}

	_entryZ = slot(0x70).z;
	stopAllSounds();
	if (wait(kVideoList) != 0)
		return 5;
	if (submit(kMainList, kMainCount, kMainScene) != 0)
		return 5;
	videoIn();
	ned()->enable(true);
	_bounce = 0x177;
	slotAt(kNedArea, 4).offsets[0] = 6;
	if (!loadSst(0, 999, 3, _sst))
		return 5;
	_targets.clear();  // 0x486cc0
	targetAdd(kArea, 0x6a, true);
	if (_revisit == 0) {
		targetAdd(kArea, 0x71, true);
		targetAdd(kArea, 0x67, true);
	}
	_world = CollisionWorld();  // 0x487250
	addMap(kMapFloor);
	_nedPos.x = 0x5c;
	_nedPos.y = 0x1df;
	for (int y = 0x1df; y >= 0; y--) {
		if (solidAt(kMapFloor, _nedPos.x, y)) {
			_nedPos.y = y;
			break;
		}
	}
	for (uint i = 0; i < _sst.size(); i++)
		applyPlacementRoom(_sst[i]);
	for (uint i = 0; i < ARRAYSIZE(kSub7Sprites); i++)
		showSprite(kSub7Sprites[i].id, kSub7Sprites[i].zSlot >= 0 ? slot(kSub7Sprites[i].zSlot).z : (uint32)kSub7Sprites[i].z);
	// objApplyEffect leaves the new effect active; the figure's steps set it
	for (uint i = 0; i < ARRAYSIZE(kSub7Flips); i++)
		ensureFlip(kArea, kSub7Flips[i]);
	static const uint32 bitmaps[] = { kBmpBack, kBmp2, kBmp3 };
	for (int i = 0; i < 3; i++) {
		BitmapRec *rec = bmp(bitmaps[i]);
		if (!rec || !rec->object)
			continue;
		rec->object->setDrawOrder(rec->z);
		rec->object->setDestination(_game->display().getPrimary());
		rec->object->setVisibility(true);
		if (i > 0)
			rec->object->setPosition(rec->x, rec->y);
	}

	const byte *pal = palette(kPaletteRoom);
	if (pal)
		memcpy(_paletteRoom, pal, sizeof(_paletteRoom));
	ned()->copyPalette(_paletteRoom);
	memcpy(_palette, _paletteRoom, sizeof(_palette));
	if (_revisit == 0) {
		_knocked = 0;
		_reactPending = 0;
		_reacted = 0;
		_idleTime = 0;
		_landed = 0;
		_walking = 0;
		_pathIndex = 0;
		_inputLock = 0;
		_reaction = 0;
		_coverSlot = switchSlotPre(0x67, -1);
		_boxSlot = switchSlotPre(0x6d, -1);
		_shelfSlot = switchSlotPre(0x6a, -1);
		_figureSlot = switchSlotPre(0x70, -1);
		_dropSlot = switchSlotPre(-1, -1);
		_doorSlot = switchSlotPre(-1, -1);
		scalePalette(_palette, 0.75);
		// 0x4871e0 makes an empty map at DAT_004c4004 that nothing reads
	} else {
		_knocked = 1;
		_reactPending = 1;
		_reacted = 1;
		_idleTime = 0x7fffffff;
		_landed = 1;
		_walking = 1;
		_pathIndex = 0x10;
		_inputLock = 0;
		_reaction = 4;
		_coverSlot = switchSlotPre(0x68, -1);
		_boxSlot = switchSlotPre(-1, -1);
		_shelfSlot = switchSlotPre(0x6a, -1);
		_figureSlot = switchSlotPre(_path[_pathIndex].slot, -1);
		_dropSlot = switchSlotPre(-1, -1);
		_doorSlot = switchSlotPre(0x195, -1);
		slot(0x195).step = slot(0x195).sequenceLength - 1;
		regionEnable(map(kMapFloor), 1, false);
		addMap(kMapDoor);
		_world.setUser(nullptr);
	}
	_game->display().applyPalette(_palette, kPalAll);
	ned()->setWrap1(false, 0, nullptr, nullptr);
	return 0;
}

// m4 0x448770
int Scene1Sub7::leave(int toScene, int toSub) {
	_world = CollisionWorld();  // 0x487280
	_targets.clear();           // 0x486d00
	_sst.clear();               // 0x45a5f0
	release(kMainList, kMainCount);
	if (toScene != 1) {
		ned()->show(false);
		ned()->resetIdle();
		top()->leave(toScene, toSub);
		top()->unload(toScene, toSub);
		return 0;
	}
	_revisit = 1;
	return submit(kVideoList, 1, 99);
}

// m5 0x448820
int Scene1Sub7::unload(int toScene, int toSub) {
	if (toScene == 1) {
		if (wait(kVideoList) != 0)
			return 5;
		videoOut();
		top()->exitsEnable();
		ned()->resetIdle();
		// The graveyard's two ambient loops start again
		loopSound(0x120);
		loopSound(0x82);
	}
	return 0;
}

// m7 0x448880
int Scene1Sub7::tick(uint32 input, uint32 now) {
	coverTick(now);
	shelfTick(now);
	boxTick(now);
	figureTick(now);
	dropTick(now);
	if (_doorSlot != -1 && slot(_doorSlot).deadline < now) {
		Slot &s = slot(_doorSlot);
		s.step++;
		if (s.sequenceLength <= s.step)
			s.step--;
		int duration = place(_doorSlot, _doorSlot, true, nullptr, nullptr);
		s.deadline = now + (int16)duration * kMsPerStep;
	}
	if (_inputLock != 0)
		input = 0;
	int f = 0;
	ned()->tick(_world, input, f, _nedPos, now, _scrollX, _scrollY, &_hooks);
	draw();
	// Off the left edge: back to the graveyard
	if (_nedPos.x < 0xb) {
		ned()->show(false);
		BitmapRec *back = bmp(kBmpBack);
		if (back && back->object)
			back->object->setVisibility(false);
		_coverSlot = switchSlotPre(-1, _coverSlot);
		_boxSlot = switchSlotPre(-1, _boxSlot);
		_shelfSlot = switchSlotPre(-1, _shelfSlot);
		_figureSlot = switchSlotPre(-1, _figureSlot);
		switchTo(1, -1);
	}
	return 0;
}

// 0x449c40: the way in (file 0x4e); the graveyard's layers go
void Scene1Sub7::videoIn() {
	ned()->show(false);
	top()->backdropVisible(false);
	top()->hillsVisible(false);
	ghoulVisible(false);
	playVideo(kVideoIn);
	top()->layersVisible(false);
	top()->cloudsVisible(false);
	release(kVideoList, 1);
}

// 0x449dd0: the way out (file 0x4f) with the graveyard's palette; its layers come back
void Scene1Sub7::videoOut() {
	top()->layersVisible(true);
	top()->backdropVisible(false);
	top()->hillsVisible(false);
	top()->cloudsVisible(true);
	const byte *pal = palette(Scene1::kPaletteGame);
	if (pal)
		_game->display().applyPalette(pal, kPalGame);
	playVideo(kVideoOut);
	top()->backdropVisible(true);
	top()->hillsVisible(true);
	ghoulVisible(true);
	release(kVideoList, 1);
}

// The frame loop of 0x449c40 / 0x449dd0: one frame per 1000 / trans_fps ms, the clouds drifting
void Scene1Sub7::playVideo(int fileIndex) {
	MenuVideo video;
	video.open(_game, fileIndex);
	int count = video.getFrameCount();
	BitmapRec *rec = bmp(kBmpVideo);
	if (rec && rec->object) {
		rec->object->setDestination(_game->display().getPrimary());
		rec->object->setDrawOrder(0x14);
		rec->object->setVisibility(true);
	}
	int fps = MAX<int>(1, _game->media()._transFps);
	uint32 next = 0;
	int frame = 0;
	while (frame < count && !_game->getEngine()->shouldQuit()) {
		uint32 now = _game->now();
		if (next < now) {
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
}

// 0x484f70 (the Ghoul's, in the second creature module)
void Scene1Sub7::ghoulVisible(bool on) {
	top()->enemies2()->ghoulVisible(on);
}

// 0x446f80 (the result's low byte, 0xff and up kept as 0xff)
void Scene1Sub7::scalePalette(byte *pal, double f) {
	if (f < 0.0)
		f = -f;
	for (int i = 0; i < 256 * 3; i++) {
		uint v = (uint)(int32)(pal[i] * f) & 0xff;
		pal[i] = (byte)(v >= 0xff ? 0xff : v);
	}
}

void Scene1Sub7::applyPlacementRoom(const Placement &p) {
	if (p.kind == 2) {
		applyPlacement(p);
		return;
	}
	if ((p.kind != 0 && p.kind != 1) || p.area <= 0)
		return;
	// 0x45a440: the area's index (0x4afc60), then DAT_004ddb88[(index * 15 + slot) * 100 + sub]
	int16 index = (int16)_game->exe().readUint16(0x4afc60 + p.area * 2);
	BitmapRec *rec = bmp(0x4ddb88 + 4 * (uint32)(p.sub + (index * 15 + p.slot) * 100));
	if (!rec)
		return;
	rec->x = p.x;
	rec->y = p.y;
	rec->z = p.z;
}

// objSetDrawOrder, objSetDestination(primary), objSetVisibility(0)
void Scene1Sub7::showSprite(int id, uint32 z) {
	Slot &s = slot(id);
	if (!s.sprite)
		return;
	s.sprite->setDrawOrder(z);
	s.sprite->setDestination(_game->display().getPrimary());
	s.sprite->setVisibility(false);
}

// 0x448a40: the thing at the door (0x67), zapped 0x68; both hold their last step
void Scene1Sub7::coverTick(uint32 now) {
	if (_coverSlot == -1)
		return;
	Slot &s = slot(_coverSlot);
	if (!(s.deadline < now))
		return;
	s.step++;
	if (s.sequenceLength - 1 <= s.step)
		s.step = s.sequenceLength - 1;
	if (_coverSlot == 0x68 && slot(0x68).step == 0)
		playSound(0x68);
	int duration = place(_coverSlot, 0x67, true, nullptr, nullptr);
	s.deadline = now + (int16)duration * kMsPerStep;
}

// 0x448bc0: 0x1e3 falls from 0x6d to the floor; landing starts the figure's walk
void Scene1Sub7::dropTick(uint32 now) {
	if (_dropSlot == -1)
		return;
	Slot &s = slot(_dropSlot);
	if (!(s.deadline < now))
		return;
	s.step++;
	if (s.sequenceLength - 1 <= s.step)
		s.step = 0;
	if (_dropSlot == 0x1e4) {
		// No new deadline: 0x1e4 steps on every tick
		place(0x1e4, 0x1e4, true, nullptr, nullptr);
		return;
	}
	s.y += _fallSpeed;
	_fallSpeed += _fallAccel;
	if (_floorY < slot(0x6d).y + s.y) {
		_landed = 1;
		_fallSpeed = 0;
		_fallAccel = 0;
		s.y = _floorY - slot(0x6d).y;
	}
	int duration = placeAt(kArea, _dropSlot, 0x6d, true, nullptr, nullptr, nullptr, true);
	s.deadline = now + (int16)duration * kMsPerStep;
}

// 0x448d40: 0x6d; knocked (0x6e) it drops 0x1e3, then rolls out to the left (0x6f, 0x1c7)
void Scene1Sub7::boxTick(uint32 now) {
	if (_boxSlot == -1)
		return;
	if (!(slot(_boxSlot).deadline < now))
		return;
	Slot &box = slot(0x6d);
	if (_boxSlot == 0x1c7) {
		_bounce = _bounce / 3;
		box.y += _bounce < 1 ? 0 : _bounce;
		box.x -= 10;
	}
	Slot &s = slot(_boxSlot);
	s.step++;
	if (s.step >= s.sequenceLength) {
		int16 next = 0x6d;
		bool doSwitch = true;
		switch (_boxSlot) {
		case 0x6e:
			for (_floorY = 0; _floorY < 0x1e0; _floorY++) {
				if (solidAt(kMapFloor, box.x, _floorY)) {
					_floorY -= box.h;
					break;
				}
			}
			_fallSpeed = 0x1c;
			_fallAccel = 1;
			slot(0x1e3).y = 0;
			slot(0x1e3).x = 0;
			_dropSlot = switchSlotPre(0x1e3, _dropSlot);
			break;
		case 0x6f:
			next = 0x1c7;
			break;
		case 0x1c7:
			if (-0x65 < box.x) {
				slot(0x1c7).step = 0;
				doSwitch = false;
			} else {
				next = -1;
			}
			break;
		default:
			break;
		}
		if (doSwitch)
			_boxSlot = switchSlot(next, _boxSlot);
	}
	if (_boxSlot == 0x1c7) {
		if (box.x < 0x32 && _doorSlot == -1) {
			// Region 1 held Ned back from the way out
			regionEnable(map(kMapFloor), 1, false);
			_doorSlot = switchSlotPre(0x195, _doorSlot);
		}
		if (slot(0x1c7).step == 0)
			playSound(0x1c7);
	}
	if (_boxSlot == 0x6f && (slot(0x6f).step == 2 || slot(0x6f).step == 9))
		playSound(0x1c7);
	if (_boxSlot != -1) {
		int duration = place(_boxSlot, 0x6d, true, nullptr, nullptr);
		slot(_boxSlot).deadline = now + (int16)duration * kMsPerStep;
	}
}

// 0x449000: the thing on the right (0x6a), zapped 0x6b (from its right side) or 0x6c
void Scene1Sub7::shelfTick(uint32 now) {
	if (_shelfSlot == -1)
		return;
	if (!(slot(_shelfSlot).deadline < now))
		return;
	if (_shelfSlot == 0x6b && _knocked == 0 && slot(0x6b).step == 0x11) {
		_boxSlot = switchSlot(0x6e, _boxSlot);
		_knocked = 1;
		place(_boxSlot, 0x6d, true, nullptr, nullptr);
		playSound(0x6e);
	}
	Slot &s = slot(_shelfSlot);
	s.step++;
	if (s.sequenceLength <= s.step) {
		targetRemove(kArea, _shelfSlot);
		if (_shelfSlot == 0x6b || _shelfSlot == 0x6c) {
			stopSound(_shelfSlot);
			_shelfSlot = switchSlot(0x6a, _shelfSlot);
		}
		targetAdd(kArea, _shelfSlot, true);
		slot(_shelfSlot).step = 0;
	}
	int16 id = _shelfSlot;
	if ((id == 0x6c && slot(0x6c).step == 1) || (id == 0x6a && slot(0x6a).step == 0) ||
			(id == 0x6b && slot(0x6b).step == 0))
		playSound(id);
	int duration = place(_shelfSlot, 0x69, true, nullptr, nullptr);
	slot(_shelfSlot).deadline = now + (int16)duration * kMsPerStep;
}

// 0x449220: the figure; idle (0x70, now and then 0x71), its reactions, and its walk
void Scene1Sub7::figureTick(uint32 now) {
	if (_figureSlot == -1)
		return;
	if (!(slot(_figureSlot).deadline < now))
		return;
	Slot &fig = slot(0x70);
	int duration = 0;

	if (_walking != 0) {
		PathStep &st = _path[_pathIndex];
		if (st.left == 0 && st.dx != 0) {
			_pathIndex++;
			_figureSlot = switchSlotPre(_path[_pathIndex].slot, _figureSlot);
		} else {
			fig.x += st.dx;
			st.left -= st.dx;
		}
		Slot &s = slot(_figureSlot);
		s.step++;
		if (_figureSlot == 0x75 && slot(0x75).step == 6)
			_dropSlot = switchSlot(-1, _dropSlot);
		if (s.sequenceLength <= s.step) {
			if (_pathIndex == 0x10) {
				s.deadline = 0;
				s.step = 0;
			} else if (_path[_pathIndex].left == 0 || _path[_pathIndex].dx == 0) {
				_pathIndex++;
				_figureSlot = switchSlot(_path[_pathIndex].slot, _figureSlot);
			} else {
				s.step = 0;
			}
		}
		if (_figureSlot == 0x76) {
			int16 step = slot(0x76).step;
			if (step == 2)
				playSound(0x76);
			if (step == 5) {
				if (_boxSlot != -1 && _boxSlot != 0x1c7 && _boxSlot != 0x6f) {
					// The lights come up and Ned is free again
					_inputLock = 0;
					_boxSlot = switchSlotPre(0x6f, _boxSlot);
					boxTick(0xffffffff);
					memcpy(_palette, _paletteRoom, sizeof(_palette));
					_game->display().applyPalette(_palette, kPalAll);
				}
			} else if (step == 6) {
				_dropSlot = switchSlotPre(0x1e4, _dropSlot);
			}
		}
		bool flip = false;
		if (slot(_figureSlot).flip) {
			flip = _path[_pathIndex].flip != 0;
			flipActive(kArea, _figureSlot, flip);
		}
		int16 halfW = slot(_figureSlot).w / 2;
		if (flip)
			fig.x += halfW;
		duration = place(_figureSlot, 0x70, !flip, nullptr, nullptr);
		if (flip)
			fig.x -= halfW;
		slot(_figureSlot).deadline = now + (int16)duration * kMsPerStep;
		return;
	}

	if (_landed != 0) {
		// The walk starts: Ned's input goes
		_inputLock = 1;
		_walking = 1;
		_pathIndex = 0;
		_figureSlot = switchSlot(_path[0].slot, _figureSlot);
		if (slot(_figureSlot).flip)
			flipActive(kArea, _figureSlot, _path[_pathIndex].flip == 0);
		place(_figureSlot, 0x70, _path[_pathIndex].flip == 0, nullptr, nullptr);
		playSound(_path[_pathIndex].slot);
		return;
	}

	Slot &s = slot(_figureSlot);
	s.step++;
	if (s.sequenceLength <= s.step) {
		targetRemove(kArea, _figureSlot);
		if (_reactPending == 0) {
			switch (_figureSlot) {
			case 0x70:
				if (_idleTime < now)
					_figureSlot = switchSlot(0x71, _figureSlot);
				else
					slot(0x70).step = 0;
				targetAdd(kArea, _figureSlot, true);
				break;
			case 0x71:
				_figureSlot = switchSlot(0x70, _figureSlot);
				_idleTime = (int32)_game->crtRandom() % 5000 + now;
				targetAdd(kArea, _figureSlot, true);
				break;
			case 0x72:
			case 0x13f:
			case 0x141:
			case 0x142:
			case 0x143:
				_figureSlot = switchSlot(0x70, _figureSlot);
				_idleTime = (int32)_game->crtRandom() % 5000 + now;
				if (_reactions[_reaction] != 0)
					_reaction++;
				break;
			default:
				break;
			}
		} else {
			_reactPending = 0;
			_reacted = 1;
			_figureSlot = switchSlot(_reactions[_reaction], _figureSlot);
		}
	}
	int16 react = _reactions[_reaction];
	if (_figureSlot != 0x71 && _figureSlot != 0x70 && react != 0 && slot(react).step == 0)
		playSound(react);
	duration = place(_figureSlot, 0x70, true, nullptr, nullptr);
	slot(_figureSlot).deadline = now + (int16)duration * kMsPerStep;
}

void Scene1Sub7::loopSound(int id) {
	Slot &s = slot(id);
	_game->stopSlotSound(s);
	if (s.sound.size() < 18)
		return;
	uint32 pcmOffset = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcmOffset >= s.sound.size())
		return;
	_game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcmOffset, s.sound.size() - pcmOffset, 0, 0, true);
}

// 0x449900: the target in front of Ned, as a screen rect (box 0, or 3 when flipped)
int Scene1Sub7::findTarget(int16 &dir, int16 &found, int32 rect[4]) {
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

// 0x449a00 (+0x00)
int Scene1Sub7::hookUse(void *user, int x, int y, int16 *state) {
	Scene1Sub7 *s = (Scene1Sub7 *)user;
	int16 dir = -1, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(dir, found, rect);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x4499f0 (+0x04): every region takes state 0xe
int Scene1Sub7::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	*state = 0xe;
	return 1;
}

// 0x449c00 (+0x08)
void Scene1Sub7::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	*a = 2;
	int16 dir = 0;
	((Scene1Sub7 *)user)->findTarget(dir, *b, rect);
}

// 0x449a50 (+0x0c)
int Scene1Sub7::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene1Sub7 *s = (Scene1Sub7 *)user;
	switch (*b) {
	case 0x67:
		s->targetRemove(kArea, 0x67);
		s->_coverSlot = s->switchSlotPre(0x68, s->_coverSlot);
		s->addMap(kMapDoor);
		s->coverTick(0xffffffff);
		return 1;
	case 0x6a: {
		s->targetRemove(kArea, s->_shelfSlot);
		s->stopSound(s->_shelfSlot);
		Slot &ref = s->slot(0x69);
		int16 next = (s->_knocked == 0 && ref.w / 2 + ref.x <= s->_nedPos.x) ? 0x6b : 0x6c;
		s->_shelfSlot = s->switchSlotPre(next, s->_shelfSlot);
		s->targetAdd(kArea, s->_shelfSlot, true);
		s->shelfTick(0xffffffff);
		break;
	}
	case 0x70:
	case 0x71: {
		int16 react = s->_reactions[s->_reaction];
		if (react != 0 && s->_figureSlot != react) {
			s->_reactPending = 1;
			return 1;
		}
		break;
	}
	default:
		break;
	}
	return 1;
}

} // End of namespace NightmareNed
