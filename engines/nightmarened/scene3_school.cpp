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

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/util.h"
#include "common/ptr.h"
#include "common/system.h"
#include "common/textconsole.h"

#include <math.h>

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene3_school.h"

namespace NightmareNed {

Scene3School::Scene3School(Game *game) : Scene(game, 0) {
	const ExeImage &exe = _game->exe();
	_trapA = (int16)exe.readUint16(0x49d178);
	_doorStart = (int16)exe.readUint16(0x49d17c);
	_doorExit = (int16)exe.readUint16(0x49d180);
	_trapB = (int16)exe.readUint16(0x49d184);
	_trapC = (int16)exe.readUint16(0x49d188);
	_spawnerA = (int16)exe.readUint16(0x49d18c);
	_spawnerB = (int16)exe.readUint16(0x49d190);
	_unused194 = (int16)exe.readUint16(0x49d194);
	_ghost = (int16)exe.readUint16(0x49d198);

	_scrollX = _scrollY = 0;
	_stripsUnused = 0;
	for (int i = 0; i < kStripCount; i++) {
		_strips[i].worldX = _strips[i].screenX = _strips[i].screenY = _strips[i].index = 0;
		_strips[i].next = _strips[i].prev = 0;
	}
	for (int i = 0; i < kTableSize; i++)
		_sinTable[i] = _cosTable[i] = 0;
	_unportedExit = -1;
	_lastTick = 0;
	_fromScene = _fromSub = 0;
	_startScrollX = _startScrollY = 0;
	_doorStartOn = _doorExitOn = _switchOn = 0;
	_lastScreenX = _lastScreenY = 0;
	_cameraTime = 0;
	for (int i = 0; i < kLifts; i++) {
		_liftDir[i] = _liftMin[i] = _liftMax[i] = 0;
		_liftLast[i] = _liftPause[i] = 0;
	}
	_ghostPhaseX = _ghostPhaseY = _ghostBaseX = _ghostBaseY = 0;
	_ghostPeriodX = _ghostPeriodY = _ghostAmpX = _ghostAmpY = 0;
	_ghostX = _ghostY = _ghostNedX = _ghostNedY = 0;
	memset(_palette, 0, sizeof(_palette));

	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

Scene3School::~Scene3School() {
	freeAll();
}

// ---- Resources ----

Slot &Scene3School::slot(int id) {
	return _game->res().getSlot(kArea, CLIP(id, 0, (int)kAreaSlots - 1));
}

BitmapRec *Scene3School::bmp(uint32 va) {
	return _bitmaps.contains(va) ? _bitmaps[va] : nullptr;
}

CollisionMap *Scene3School::map(uint32 va) {
	return _maps.contains(va) ? _maps[va] : nullptr;
}

// 0x4586e0 (synchronous); the list has no completion callbacks
int Scene3School::submit(uint32 va, int count, int scene) {
	NafArchive *naf = _game->getNaf(kNafSchool);
	if (!naf)
		return 5;
	for (int i = 0; i < count; i++) {
		uint32 e = va + i * 0x24;
		if (_entries.contains(e))
			continue;
		Common::Array<Request> one = _game->readList(e, 1);
		Request &req = one[0];
		req.scene = scene;
		if (_game->res().load(naf, req)) {
			if (req.bitmap)
				_bitmaps[req.destVA] = req.bitmap;
			if (req.collision)
				_maps[req.destVA] = req.collision;
			if (req.hasPalette && req.paletteVA == kPaletteVA)
				memcpy(_palette, req.palette, sizeof(_palette));
			_entries[e] = req;
		}
	}
	return 0;
}

// 0x458dc0: frees backwards
void Scene3School::release(uint32 va, int count) {
	for (int i = count - 1; i >= 0; i--) {
		uint32 e = va + i * 0x24;
		if (!_entries.contains(e))
			continue;
		Common::Array<Request> one;
		one.push_back(_entries[e]);
		if (one[0].bitmap)
			_bitmaps.erase(one[0].destVA);
		if (one[0].collision) {
			_world.removeMap(one[0].collision);
			_maps.erase(one[0].destVA);
		}
		_game->freeList(one);
		_entries.erase(e);
	}
}

void Scene3School::freeAll() {
	release(kList, kListCount);
}

// The list's entry of the given type for id, or 0
uint32 Scene3School::entryFor(int id, int type) const {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < kListCount; i++) {
		uint32 e = kList + i * 0x24;
		if (exe.readSint32(e + 0x0c) == id && exe.readSint32(e + 0x10) == type)
			return e;
	}
	return 0;
}

// 0x459090 res_show_sprite on the list's sprite entry for id
int Scene3School::showSprite(int id, int z, bool visible) {
	uint32 e = entryFor(id, Request::kSprite);
	if (!e || !isLoaded(e))
		return 5;
	Slot &s = slot(id);
	if (s.sprite) {
		s.sprite->setDrawOrder((uint32)z);
		s.sprite->setDestination(_game->display().getPrimary());
		s.sprite->setVisibility(visible);
	}
	return 0;
}

// 0x459100 res_show_bitmap
int Scene3School::showBitmap(uint32 va, int z, bool visible) {
	if (!isLoaded(va))
		return 5;
	const Request &req = _entries[va];
	if (req.bitmap && req.bitmap->object) {
		req.bitmap->object->setDrawOrder((uint32)z);
		req.bitmap->object->setDestination(_game->display().getPrimary());
		req.bitmap->object->setVisibility(visible);
	}
	return 0;
}

// ---- Display helpers (area 4) ----

// 0x430890
int16 Scene3School::switchSlot(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot);
}

// 0x430930
int16 Scene3School::switchSlotPre(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot, true);
}

// 0x430850 with noMirror 1 and the strip xform 0x423ec0: returns the step's duration
int Scene3School::place(int id, int ref) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	if (!_game->display().slotPlace(kArea, id, ref, &frame, &duration, true, Common::Point(0, 0)))
		return 0;
	Slot &s = slot(id);
	Common::Point p = s.sprite->getPosition();
	int x = p.x, y = p.y;
	xform(this, x, y);
	s.sprite->setPosition(x, y);
	return duration;
}

// 0x430440 slot_attach_pos: where slot would be placed against ref's anchor
void Scene3School::attachPos(int id, int ref, int out[2], bool noMirror) {
	Slot &s = slot(id), &r = slot(ref);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	int sax = 0, say = 0, rax = 0, ray = 0, fox = 0, foy = 0;
	if (s.boxes) {
		sax = s.boxes->anchorX;
		say = s.boxes->anchorY;
		if (frame >= 0 && frame < (int)s.boxes->frames.size()) {
			fox = s.boxes->frames[frame].offX;
			foy = s.boxes->frames[frame].offY;
		}
	}
	if (r.boxes) {
		rax = r.boxes->anchorX;
		ray = r.boxes->anchorY;
	}
	int16 d = (int16)(rax + fox - sax);
	out[0] = r.x + (noMirror ? -d : d - s.w);
	out[1] = r.y + (say - ray - foy);
}

// 0x4860d0 on a slot's hit boxes
void Scene3School::boxRect(int id, int frame, int k, int32 out[4]) {
	Slot &s = slot(id);
	if (s.boxes)
		s.boxes->getRawRect(frame, k, out);
	else
		out[0] = out[1] = out[2] = out[3] = -0x7fff;
}

// 0x430a30 slot_nudge
void Scene3School::nudge(int id, int dx, int dy) {
	if (id != -1 && slot(id).sprite)
		slot(id).sprite->adjPosition(dx, dy);
}

// ---- Sound ----

// 0x455880 snd_play
void Scene3School::playSound(int id) {
	_game->playSlotSound(slot(id), nullptr);
}

// 0x4558e0: the slot's sound, looping
void Scene3School::playLoop(int id) {
	Slot &s = slot(id);
	_game->stopSlotSound(s);
	if (s.sound.size() < 18)
		return;
	uint32 pcmOffset = 18 + READ_LE_UINT16(s.sound.data() + 16);
	if (pcmOffset >= s.sound.size())
		return;
	_game->playVoice(s.voice, s.sound.data(), s.sound.data() + pcmOffset, s.sound.size() - pcmOffset, 0, 0, true);
}

// 0x455930
void Scene3School::stopSound(int id) {
	_game->stopSlotSound(slot(id));
}

// 0x455950 -> 0x44d020: every slot voice stops
void Scene3School::stopAllSounds() {
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
}

// ---- Collision ----

// 0x487f50 + 0x4872a0
void Scene3School::addMap(uint32 va) {
	CollisionMap *m = map(va);
	if (m)
		_world.addMap(m, m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom);
}

// 0x488060: bit 31 of the first region with the id is the disabled flag
void Scene3School::regionEnable(CollisionMap *m, int32 id, bool on) {
	if (!m)
		return;
	for (CollisionMap::Region &r : m->regions) {
		if (r.id != id)
			continue;
		if (on)
			r.type &= 0x7fffffff;
		else
			r.type |= 0x80000000;
		return;
	}
}

Common::Array<CollisionMap *> Scene3School::overlayMaps() const {
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	return maps;
}

// ---- Targets ----

// 0x4309c0 olist_add_sprite (always with noFlip = 1 here)
void Scene3School::targetAdd(int id) {
	Placement p;
	memset(&p, 0, sizeof(p));
	p.area = kArea;
	p.slot = (int16)id;
	p.sub = 1;
	p.kind = 2;
	p.flip = 0;
	_targets.push_back(p);
}

// 0x486f50 olist_remove (its index loop skips the entry after each removal)
void Scene3School::targetRemove(int id) {
	for (uint i = 0; i < _targets.size(); i++) {
		if (_targets[i].area == kArea && _targets[i].slot == (int16)id && _targets[i].sub == 1)
			_targets.remove_at(i);
	}
}

// ---- Ned ----

Ned *Scene3School::ned() {
	return _game->ned();
}

// 0x454920 with the scene's world, scroll and hooks
void Scene3School::nedTick(uint32 input, uint32 now) {
	int f = 0;
	ned()->tick(_world, input, f, _nedPos, now, _scrollX, _scrollY, &_hooks);
}

// The screen x of world x through the strip it falls in (the index is clamped here; the
// original reads past the strip table outside 0..1919)
int Scene3School::stripScreenX(int x) const {
	int i = x / kStripW;
	int k = CLIP(i, 0, (int)kStripCount - 1);
	return _strips[k].screenX - i * kStripW + x;
}

// 0x423ec0: world -> screen through the strip under x
void Scene3School::xform(void *user, int &x, int &y) {
	Scene3School *s = (Scene3School *)user;
	x = s->stripScreenX(x);
	y += s->_scrollY;
}

// 0x45a0f0, staying here when the target isn't registered yet
int Scene3School::switchTo(int scene) {
	if (!_game->hasScene(scene, -1)) {
		if (_unportedExit != scene)
			debugC(1, kDebugLoading, "Scene 3:0: exit to scene %d (not ported), staying", scene);
		_unportedExit = scene;
		return 1;
	}
	_unportedExit = -1;
	_game->switchScene(scene, -1);
	return 0;
}

// ---- Scene methods ----

// m2 0x4224f0
int Scene3School::load(int fromScene, int fromSub) {
	_unused194 = -1;
	_ghost = -1;
	_doorStartOn = 0;
	_doorExitOn = 0;
	_switchOn = 0;
	if (!_game->getNaf(kNafSchool))
		return 5;
	ned()->listLoad(kNedList);
	ned()->setWrap1(false, 0, nullptr, nullptr);
	_nedPos.x = 0xbc;
	int r = submit(kList, kListCount, kListScene);
	if (r)
		return r;
	// x87: (float)0.0174 * i, fsin / fcos, * 512.0, truncated by __ftol
	const double step = (double)0.0174f;
	for (int i = 0; i < kTableSize; i++) {
		_sinTable[i] = (int32)(sin(i * step) * 512.0);
		_cosTable[i] = (int32)(cos(i * step) * 512.0);
	}
	return 0;
}

// m3 0x421b50
int Scene3School::enter(int fromScene, int fromSub) {
	ned()->listShow(kNedList);
	_fromScene = fromScene;
	_fromSub = fromSub;
	_scrollX = 0;
	_scrollY = -0x1e0;
	if (!isLoaded(kList))
		return 5;

	// The level, the sliding platform and the four lifts, with their travel limits
	_world = CollisionWorld();
	addMap(kMapLevel);
	CollisionMap *m = map(kMapSlide);
	if (!m)
		return 5;
	_liftMin[0] = m->bounds.left;
	_liftMax[0] = m->bounds.left + 0x5a;
	addMap(kMapSlide);
	if (!(m = map(kMapLift1)))
		return 5;
	_liftMin[1] = m->bounds.top + 0x18;
	_liftMax[1] = m->bounds.top + 0xb2;
	addMap(kMapLift1);
	if (!(m = map(kMapLift2)))
		return 5;
	_liftMin[2] = m->bounds.top;
	_liftMax[2] = m->bounds.top + 0x122;
	addMap(kMapLift2);
	if (!(m = map(kMapLift3)))
		return 5;
	_liftMax[3] = m->bounds.top;
	_liftMin[3] = m->bounds.top - 0xd2;
	addMap(kMapLift3);
	if (!(m = map(kMapLift4)))
		return 5;
	_liftMax[4] = m->bounds.top;
	_liftMin[4] = m->bounds.top - 0xb8;
	addMap(kMapLift4);
	_targets.clear();
	_userMap = CollisionMap();

	for (int i = 0; i < kStripCount; i++) {
		int r = showBitmap(kStripList + i * 0x24, 200, true);
		if (r)
			return r;
	}
	ned()->copyPalette(_palette);
	_game->display().applyPalette(_palette, kPalAll);
	static const int16 kFront[] = { 0xde, 0xe3, 0xe8, 0xef, 0x1a5, 0x187 };
	for (int16 id : kFront) {
		if (showSprite(id, 10, true))
			return 5;
	}
	static const int16 kBack[] = { 0x189, 0x118, 0x119, 0x11a, 0x11b };
	for (int16 id : kBack) {
		if (showSprite(id, 199, true))
			return 5;
	}
	// The fourth lift's direction (DAT_004bc1a8) is not reset
	for (int i = 0; i < 4; i++)
		_liftDir[i] = 0;
	if (showSprite(kDoorStart, 10, true) || showSprite(kDoorExit, 10, true))
		return 5;

	// 0x45a560(4, 999, 1): 95999001.SST, applied by 0x421a30
	_sst.clear();
	Common::String sstName = Common::String::format("%02d%03d0%02d.sst",
		(int16)_game->exe().readUint16(kSstAreas + 2 * kArea), 999, 1);
	Common::ScopedPtr<Common::SeekableReadStream> sst(_game->getEngine()->openFile(sstName));
	if (!sst || !loadPlacements(*sst, _sst)) {
		warning("Can't read %s", sstName.c_str());
		return 5;
	}
	for (const Placement &e : _sst) {
		// 0x421a30
		if (e.area != kArea || e.slot < 0 || e.slot >= kAreaSlots)
			continue;
		Slot &s = slot(e.slot);
		s.x = e.x;
		s.y = e.y;
		if (e.flip)
			s.x += s.w;
		if (!s.sprite)
			continue;
		int frame = 0, duration = 0;
		if (s.sequence)
			s.sequence->getStep(s.step, frame, duration);
		int offX = 0, offY = 0;
		if (s.boxes && frame >= 0 && frame < (int)s.boxes->frames.size()) {
			offX = s.boxes->frames[frame].offX;
			offY = s.boxes->frames[frame].offY;
		}
		s.sprite->setPosition(s.x - offX, s.y - offY);
	}
	targetAdd(kTrapA);
	targetAdd(kTrapB);
	targetAdd(kTrapC);
	targetAdd(kSwitch);

	CollisionMap *level = map(kMapLevel);
	for (int y = 0x3bf; y >= 0; y--) {
		if (level && level->isSolid((int16)_nedPos.x, (int16)y)) {
			_nedPos.y = y;
			break;
		}
	}
	stripsInit();
	ned()->setFacing(1);
	ned()->resetIdle();
	ned()->setWrap1(true, kWorldW, xform, this);
	_startPos = _nedPos;
	_startScrollX = _scrollX;
	_startScrollY = _scrollY;
	regionEnable(level, 0xdc, false);
	static const int16 kStill[] = { 0x189, 0x118, 0x119, 0x11a, 0x11b };
	for (int16 id : kStill) {
		slot(id).step = 0;
		slot(id).deadline = 0;
	}

	_trapA = switchSlot(kTrapA, -1);
	place(_trapA, kTrapA);
	_trapB = switchSlot(kTrapB, -1);
	place(_trapB, kTrapB);
	_trapC = switchSlot(kTrapC, -1);
	place(_trapC, kTrapC);
	_spawnerA = switchSlot(kSpawnerA, -1);
	place(_spawnerA, kSpawnerA);
	_spawnerB = switchSlot(kSpawnerB, -1);
	place(_spawnerB, kSpawnerB);
	switchSlot(kSwitch, -1);
	place(kSwitch, kSwitch);
	_doorStart = switchSlot(kDoorStart, -1);
	place(_doorStart, kDoorStart);
	_doorExit = switchSlot(kDoorExit, -1);
	place(_doorExit, kDoorExit);
	ned()->show(true);
	ned()->enable(true);
	nedTick(0, _game->now());
	_game->media().playMusic(kMusic);
	_game->_optionsAllowed = false;
	_lastTick = 0;
	return 0;
}

// m4 0x4225e0
int Scene3School::leave(int toScene, int toSub) {
	// The debug overlay must not keep the maps freed below
	_game->display().setCollisionOverlay(Common::Array<CollisionMap *>(), Common::Point());
	_game->_optionsAllowed = false;
	_game->media().stopMusic();
	stopAllSounds();
	_sst.clear();
	release(kList, kListCount);
	ned()->setWrap1(false, 0, nullptr, nullptr);
	ned()->show(false);
	ned()->restoreIdle();
	ned()->listFree(kNedList);
	_world = CollisionWorld();
	_targets.clear();
	_userMap = CollisionMap();
	_game->closeNaf(kNafSchool);
	return 0;
}

// m5 0x422690
int Scene3School::unload(int toScene, int toSub) {
	return 0;
}

// m7 0x422bf0. The original has no throttle here (a sub-scene's m7 is called straight from
// 0x45a310); like the other scenes it is held to one tick per 41 ms
int Scene3School::tick(uint32 input, uint32 now) {
	if (now < _lastTick + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastTick + kMsPerStep - now, 10));
		return 0;
	}
	_lastTick = now;

	trapTick(_trapA, kTrapA, 8, 0x21, 4, 0x2e, now);
	trapTick(_trapB, kTrapB, 7, 0x1c, 2, 0x2f, now);
	trapTick(_trapC, kTrapC, 4, 0x0c, 2, 0x30, now);
	spawnerTick(_spawnerA, kSpawnerA, now);
	spawnerTick(_spawnerB, kSpawnerB, now);
	ghostTick(now);
	doorStartTick(now);
	if (doorExitTick(now))
		return 0;

	// The switch steps once per tick once zapped; the deadline goes to the exit door's slot
	if (!_switchOn || now <= slot(kSwitch).deadline) {
		place(kSwitch, kSwitch);
	} else {
		Slot &s = slot(kSwitch);
		if (s.step < s.sequenceLength - 1) {
			s.step++;
		} else {
			_switchOn = 0;
			regionEnable(map(kMapLevel), 0xdc, true);
		}
		int duration = place(kSwitch, kSwitch);
		slot(_doorExit).deadline = now + (int16)duration * kMsPerStep;
	}
	liftsTick(now);
	if (_doorStartOn || _doorExitOn)
		input = 0;
	nedTick(input, now);
	camera(now);
	_game->display().setCollisionOverlay(overlayMaps(), Common::Point(_scrollX, _scrollY));
	_game->display().draw();
	debugC(3, kDebugGraphics, "Scene 3:0: input %x Ned %d,%d state %d slot %d scroll %d,%d", input,
		_nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _scrollX, _scrollY);
	return 0;
}

} // End of namespace NightmareNed
