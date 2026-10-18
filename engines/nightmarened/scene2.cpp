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
#include "common/ptr.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene2.h"
#include "nightmarened/slothelpers.h"

namespace NightmareNed {

// ---- Scene2Part: what the sub-scenes share ----

Scene2Part::~Scene2Part() {
	releaseAll();
}

Slot &Scene2Part::slot(int area, int id) {
	return _game->res().getSlot(area, CLIP(id, 0, (int)kAreaSlots - 1));
}

BitmapRec *Scene2Part::bmp(uint32 va) {
	return _bitmaps.contains(va) ? _bitmaps[va] : nullptr;
}

CollisionMap *Scene2Part::map(uint32 va) {
	return _maps.contains(va) ? _maps[va] : nullptr;
}

const byte *Scene2Part::palette(uint32 va) {
	return _palettes.contains(va) ? _palettes[va].data() : nullptr;
}

// 0x4586e0 (synchronous); the list's scene argument replaces every entry's
int Scene2Part::submit(uint32 va, int count, int scene) {
	Common::Array<Request> list = _game->readList(va, count);
	return submitBuilt(va, list, scene);
}

int Scene2Part::submitBuilt(uint32 va, Common::Array<Request> &list, int scene) {
	NafArchive *naf = _game->getNaf(kNaf);
	if (!naf)
		return 5;
	for (uint i = 0; i < list.size(); i++) {
		uint32 e = va + i * 0x24;
		if (_entries.contains(e))
			continue;
		Request &req = list[i];
		req.scene = scene;
		if (!_game->res().load(naf, req))
			continue;
		if (req.bitmap)
			_bitmaps[req.destVA] = req.bitmap;
		if (req.collision)
			_maps[req.destVA] = req.collision;
		if (req.hasPalette && req.paletteVA) {
			Common::Array<byte> &pal = _palettes[req.paletteVA];
			pal.resize(256 * 3);
			memcpy(pal.data(), req.palette, 256 * 3);
		}
		_entries[e] = req;
	}
	return 0;
}

// 0x458dc0: frees backwards
void Scene2Part::release(uint32 va, int count) {
	for (int i = count - 1; i >= 0; i--) {
		uint32 e = va + i * 0x24;
		if (!_entries.contains(e))
			continue;
		Common::Array<Request> one;
		one.push_back(_entries[e]);
		if (one[0].bitmap)
			_bitmaps.erase(one[0].destVA);
		if (one[0].collision)
			_maps.erase(one[0].destVA);
		_game->freeList(one);
		_entries.erase(e);
	}
}

void Scene2Part::releaseAll() {
	Common::Array<uint32> keys;
	for (EntryMap::iterator it = _entries.begin(); it != _entries.end(); ++it)
		keys.push_back(it->_key);
	Common::sort(keys.begin(), keys.end());
	for (int i = (int)keys.size() - 1; i >= 0; i--)
		release(keys[i], 1);
	_notify.clear();
}

// 0x434370
uint32 Scene2Part::findEntry(int type, int id, uint32 list, int count) const {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < count; i++) {
		uint32 e = list + i * 0x24;
		if (exe.readSint32(e + 0x0c) == id && exe.readSint32(e + 0x10) == type)
			return e;
	}
	return 0;
}

// 0x459090
void Scene2Part::showSprite(uint32 entry, int z, bool on) {
	if (!entry || !_entries.contains(entry))
		return;
	const Request &req = _entries[entry];
	if (req.type != Request::kSprite)
		return;
	RdxSprite *spr = slot(req.area, req.id).sprite;
	if (!spr)
		return;
	spr->setDrawOrder((uint32)z);
	spr->setDestination(_game->display().getPrimary());
	spr->setVisibility(on);
}

// 0x459100
void Scene2Part::showBitmap(uint32 entry, int z, bool on) {
	if (!entry || !_entries.contains(entry))
		return;
	const Request &req = _entries[entry];
	if (req.type != Request::kBitmap || !req.bitmap || !req.bitmap->object)
		return;
	RdxBackground *obj = req.bitmap->object;
	obj->setDrawOrder((uint32)z);
	obj->setDestination(_game->display().getPrimary());
	obj->setVisibility(on);
}

// 0x459000
void Scene2Part::attachList(uint32 list, int count) {
	RdxSurface *primary = _game->display().getPrimary();
	for (int i = 0; i < count; i++) {
		uint32 e = list + i * 0x24;
		if (!_entries.contains(e))
			continue;
		const Request &req = _entries[e];
		RdxObject *obj = nullptr;
		if (req.type == Request::kSprite)
			obj = slot(req.area, req.id).sprite;
		else if (req.type == Request::kBitmap && req.bitmap)
			obj = req.bitmap->object;
		if (obj) {
			obj->setDestination(primary);
			obj->setVisibility(false);
		}
	}
}

// hbmpSetTransparencyColor
void Scene2Part::setBitmapKey(BitmapRec *rec, int key) {
	if (rec && rec->object)
		rec->object->setBitmap(&rec->bitmap.surface, key);
}

// 0x430530: slot_place, noMirror 1, no xform; returns the step's duration
int Scene2Part::place(int area, int id, int ref) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	_game->display().slotPlace(area, id, ref, &frame, &duration, true, Common::Point(0, 0));
	return duration;
}

// 0x4411b0
int Scene2Part::stepSlot(int area, int id, int ref, uint32 now) {
	if (id < 0 || id >= kAreaSlots)
		return 0;
	Slot &s = slot(area, id);
	if (now <= s.deadline) {
		place(area, id, ref);
		return 1;
	}
	s.step++;
	if (s.sequenceLength <= s.step)
		return 0;
	int duration = place(area, id, ref);
	s.deadline = now + (int16)duration * kMsPerStep;
	return 1;
}

// 0x441280
int16 Scene2Part::switchPlace(int area, int newSlot, int oldSlot, int ref, uint32 now, bool random) {
	int16 id = (int16)newSlot;
	_game->display().slotSwitch(area, id, (int16)oldSlot);
	if (id > 0) {
		Slot &s = slot(area, id);
		if (random && s.sequenceLength > 0)
			s.step = (int16)((int)_game->crtRandom() % s.sequenceLength);
		int duration = place(area, id, ref);
		s.deadline = now + (int16)duration * kMsPerStep;
	}
	return id;
}

void Scene2Part::visible(int area, int id, bool on) {
	if (id >= 0 && id < kAreaSlots && slot(area, id).sprite)
		slot(area, id).sprite->setVisibility(on);
}

// slot +0x0e: the sprite's frame count
int Scene2Part::frameCount(int area, int id) {
	Slot &s = slot(area, id);
	return s.boxes ? s.boxes->frameCount : (int)s.frames.size();
}

// 0x455880
void Scene2Part::playSound(int area, int id) {
	_game->playSlotSound(slot(area, id), nullptr);
}

// 0x455800
void Scene2Part::playSoundNotify(int area, int id, uint32 callback) {
	for (uint i = 0; i < _notify.size(); i++) {
		if (_notify[i].area == area && _notify[i].id == id) {
			_notify.remove_at(i);
			break;
		}
	}
	_game->playSlotSound(slot(area, id), nullptr);
	Notify n;
	n.area = area;
	n.id = id;
	n.callback = callback;
	_notify.push_back(n);
}

void Scene2Part::pollSounds() {
	for (uint i = 0; i < _notify.size();) {
		if (_game->isVoicePlaying(slot(_notify[i].area, _notify[i].id).voice)) {
			i++;
			continue;
		}
		uint32 cb = _notify[i].callback;
		_notify.remove_at(i);
		soundEnded(cb);
	}
}

// 0x455950 -> 0x44d020: every slot voice stops; the streamed music plays on
void Scene2Part::stopAllSounds() {
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
	_notify.clear();
}

// 0x45a560(5, 999, index): 94999<index>.SST
bool Scene2Part::loadSst(int index, Common::Array<Placement> &out) {
	out.clear();
	Common::String name = Common::String::format("%02d%03d0%02d.sst",
		(int16)_game->exe().readUint16(kSstAreas + 2 * kSstArea), 999, index);
	Common::ScopedPtr<Common::SeekableReadStream> s(_game->getEngine()->openFile(name));
	if (!s || !loadPlacements(*s, out)) {
		warning("Can't read %s", name.c_str());
		return false;
	}
	return true;
}

// 0x4870a0 with 0x441330: each area-5 entry moves its slot (plus the offset), the sprite at
// the frame's offset from there
void Scene2Part::applySst(const Common::Array<Placement> &sst, const int *offset) {
	for (const Placement &e : sst) {
		if (e.area != kArea || e.slot < 0 || e.slot >= kAreaSlots)
			continue;
		Slot &s = slot(kArea, e.slot);
		s.x = e.x;
		s.y = e.y;
		if (offset) {
			s.x += offset[0];
			s.y += offset[1];
		}
		if (e.flip)
			s.x += s.w;
		if (!s.sprite)
			continue;
		int frame = 0, duration = 0;
		if (s.sequence)
			s.sequence->getStep(s.step, frame, duration);
		int ox = 0, oy = 0;
		if (s.boxes && frame >= 0 && frame < (int)s.boxes->frames.size()) {
			ox = s.boxes->frames[frame].offX;
			oy = s.boxes->frames[frame].offY;
		}
		s.sprite->setPosition(s.x - ox, s.y - oy);
	}
}

// The sub-scene ticks have no wait of their own (they ran at the page-flip rate); the port
// paces them at the 41 ms of the other scenes
bool Scene2Part::wait(uint32 now) {
	if (now < _lastTick + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastTick + kMsPerStep - now, 10));
		return true;
	}
	_lastTick = now;
	return false;
}

// ---- Scene2: the top-level scene ----

Scene2::Scene2(Game *game) : Scene(game, 2) {
	_next = 0;          // set by 0x449f70
	_from = 0;
	_exitOpen = 0;
	_caught = (int16)game->exe().readUint16(0x4a3df4);
	// Test hook: nightmarened_s2_caught=<n> sets the belt's catches (the board's pieces)
	if (ConfMan.hasKey("nightmarened_s2_caught"))
		_caught = (int16)CLIP(ConfMan.getInt("nightmarened_s2_caught"), 0, 8);
	_unportedExit = -1;
	addSubScene(new Scene2Hall(game, this));
	addSubScene(new Scene2Belt(game, this));
	addSubScene(new Scene2Board(game, this));
	addSubScene(new Scene2Empty(game));
}

// m2 0x44a140
int Scene2::load(int fromScene, int fromSub) {
	_from = fromScene;
	if (fromScene != 2 && !_game->getNaf(kNaf))
		return 5;
	return 0;
}

// m5 0x44a1a0
int Scene2::unload(int toScene, int toSub) {
	if (toScene != 2)
		_game->closeNaf(kNaf);
	return 0;
}

// m7 0x44a1c0: the sub-scene, or the world, that DAT_004c4434 names
int Scene2::tick(uint32 input, uint32 now) {
	int target = _next;
	if (target == 4 || target == 5 || target == 7) {
		_next = 0;
		if (!_game->hasScene(target, -1)) {
			if (_unportedExit != target)
				debugC(1, kDebugLoading, "Scene 2: exit to scene %d (not ported), back to the hallway", target);
			_unportedExit = target;
			_game->switchScene(2, 0);
			return 0;
		}
		_unportedExit = -1;
		_game->switchScene(target, -1);
		return 0;
	}
	_game->switchScene(2, target);
	_next = 0;
	return 0;
}

// ---- Sub 0: the hallway ----

Scene2Hall::Scene2Hall(Game *game, Scene2 *world) : Scene2Part(game, 0, world) {
	const ExeImage &exe = game->exe();
	_hintPending = exe.readSint32(0x4b63a8);
	_sstPending = exe.readSint32(0x4b63ac);
	_door = _doorSlot = _doorOpening = 0;
	_useWanted = 0;
	_scrollX = _scrollY = 0;
	_lampSlot = _nedSlot = 0;
	_nedActive = 0;
	_doorTime = 0;
	_atticLoaded = _beltShown = _mouthShown = _atticShown = 0;
	_buzzEnded = _buzzStarted = _leaving = 0;
	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = nullptr; // 0x482e50 returns 0 and leaves the target unset
	_hooks.hitTarget = hookHit;
}

// m0 0x481d40: the 4-byte state (DAT_00521840) is allocated and cleared
int Scene2Hall::init() {
	_s2->_exitOpen = 0;
	return 0;
}

// m1 0x481d00
void Scene2Hall::shutdown() {
}

// m6 0x481d20: the state is freed
void Scene2Hall::suspend() {
	_s2->_exitOpen = 0;
}

// m9 0x481d70
void Scene2Hall::saveState(int32 *out) {
	out[0] = _s2->_exitOpen;
}

// m10 0x481d80
void Scene2Hall::restoreState(const int32 *in, int size) {
	if (size == 4 && in) {
		_s2->_exitOpen = in[0];
		return;
	}
	_s2->_exitOpen = 0;
}

void Scene2Hall::setupWorld() {
	// 0x487250 + 0x487f50 + 0x4872a0, then Ned at x 0x20 on the floor below the backdrop's height
	_world = CollisionWorld();
	CollisionMap *m = map(kMap);
	if (m)
		_world.addMap(m, m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom);
	_s2->_nedPos.x = 0x20;
	BitmapRec *rec = bmp(kBackdrop);
	int h = rec ? rec->bitmap.surface.h : 0;
	for (int y = h; y >= 0; y--) {
		if (m && m->isSolid((int16)_s2->_nedPos.x, (int16)y)) {
			_s2->_nedPos.y = y;
			break;
		}
	}
}

// m2 0x481db0
int Scene2Hall::load(int fromScene, int fromSub) {
	_hintPending = 1;
	if (!_game->getNaf(kNaf))
		return 5;
	_sstPending = 1;
	_atticLoaded = 0;
	_beltShown = _mouthShown = _atticShown = 0;
	_leaving = 0;
	if (_s2->_from == 0)
		_game->media().loadClip(4);
	if (submit(kMainList, 12, 0))
		return 5;
	if (_s2->_exitOpen)
		setupWorld();
	ned()->listLoad(10);
	if (submit(kDoorList, 2, 0))
		return 5;
	ned()->listLoad(0x12);
	if (_s2->_exitOpen) {
		if (submit(kAtticList, 2, 0))
			return 5;
		_atticLoaded = 1;
		ned()->listLoad(0x13);
	}
	return 0;
}

// m3 0x481fb0
int Scene2Hall::enter(int fromScene, int fromSub) {
	if (_s2->_from == 0)
		_game->media().playClipWithMusic(1, 4);   // 0x47a6b0 "87040001.wav"
	_game->_optionsAllowed = true;
	showBitmap(findEntry(Request::kBitmap, 1, kMainList, 12), 0x66, false);
	byte pal[256 * 3];
	memset(pal, 0, sizeof(pal));
	if (palette(kPalette))
		memcpy(pal, palette(kPalette), sizeof(pal));
	ned()->copyPalette(pal);
	_game->display().applyPalette(pal, kPalAll);
	BitmapRec *rec = bmp(kBackdrop);
	if (rec && rec->object) {
		rec->object->setDrawOrder(0x66);
		rec->object->setVisibility(true);
	}
	_door = kDoorAttic;
	_doorOpening = 0x5e;
	_doorSlot = -2;
	_doorTime = 0;
	_lampSlot = -1;
	if (!_s2->_exitOpen)
		setupWorld();
	_game->res().getSlot(kNedArea, 4).offsets[0] = 6;
	ned()->setFacing(1);
	ned()->resetIdle();
	ned()->setWrap1(false, rec ? rec->bitmap.surface.w : 0, xform, this);
	ned()->show(true);
	ned()->enable(true);
	_scrollX = _scrollY = 0;
	_nedActive = 1;
	_nedSlot = -1;
	_useWanted = 0;
	_game->media().playMusic(kHallMusic);
	return 0;
}

// m4 0x4821f0
int Scene2Hall::leave(int toScene, int toSub) {
	stopAllSounds();
	visible(kArea, 0xb8, false);
	visible(kArea, 0x60, false);
	ned()->setWrap1(false, 0, nullptr, nullptr);
	_sst.clear();                         // 0x45a5f0
	release(kMainList, 12);
	release(kDoorList, 2);
	ned()->listFree(10);
	ned()->listFree(0x12);
	if (_atticLoaded) {
		release(kAtticList, 2);
		ned()->listFree(0x13);
	}
	ned()->show(false);
	ned()->enable(false);
	if (toScene != 2)
		_game->closeNaf(kNaf);
	_world = CollisionWorld();            // 0x487280
	return 0;
}

// m5 0x4821e0
int Scene2Hall::unload(int toScene, int toSub) {
	return 0;
}

// 0x482e80
void Scene2Hall::placeAt(int area, int id, int x, int y) {
	Slot &s = slot(area, id);
	s.x = x;
	s.y = y;
	if (s.sprite)
		s.sprite->setPosition(x, y);
}

// 0x454920 with the hall's world, scroll (0, 0) and hooks
void Scene2Hall::nedTick(uint32 input, uint32 now) {
	int f = 0;
	ned()->tick(_world, input, f, _s2->_nedPos, now, _scrollX, _scrollY, &_hooks);
}

// 0x482de0
void Scene2Hall::xform(void *user, int &x, int &y) {
	Scene2Hall *s = (Scene2Hall *)user;
	x += s->_scrollX;
	y += s->_scrollY;
}

// 0x482e30 (+0x00)
int Scene2Hall::hookUse(void *user, int x, int y, int16 *state) {
	return 1;
}

// 0x482e00 (+0x04): region 2 stops Ned before the first door turns; input is off for 24 ticks
int Scene2Hall::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene2Hall *s = (Scene2Hall *)user;
	if (id == 2 && s->_doorSlot == -2) {
		s->_nedActive = -24;
		*state = 4;
		return 1;
	}
	return 0;
}

// 0x482e40 (+0x0c)
int Scene2Hall::hookHit(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	return 1;
}

// 0x482e70
void Scene2Hall::soundEnded(uint32 callback) {
	if (callback == kListCb)
		_buzzEnded = 1;
}

// 0x482850: the doors turn in order (the belt, the mouth, then the attic once it is open)
void Scene2Hall::doorTick(uint32 now) {
	if (_doorSlot < 0) {
		if (_doorSlot != -1 || now < _doorTime)
			return;
		switch (_door) {
		case kDoorAttic:
			if (_sstPending) {
				loadSst(0, _sst);
				_sstPending = 0;
				showSprite(findEntry(Request::kSprite, 0xb3, kMainList, 12), 0x65, false);
				showSprite(findEntry(Request::kSprite, 0xb5, kMainList, 12), 0x65, false);
				showSprite(findEntry(Request::kSprite, 0x5c, kMainList, 12), 0x65, false);
				showSprite(findEntry(Request::kSprite, 0xb8, kMainList, 12), 100, false);
				showSprite(findEntry(Request::kSprite, 0x60, kMainList, 12), 100, false);
				applySst(_sst, nullptr);
			}
			_door = kDoorBelt;
			_doorOpening = 0x5c;
			break;
		case kDoorMouth:
			if (!_s2->_exitOpen) {
				_door = kDoorBelt;
				_doorOpening = 0x5c;
				break;
			}
			_door = kDoorAttic;
			_doorOpening = 0x5e;
			showSprite(findEntry(Request::kSprite, 0xad, kMainList, 12), 0x65, false);
			showSprite(findEntry(Request::kSprite, 0xae, kAtticList, 2), 0x65, false);
			showSprite(findEntry(Request::kSprite, 0x5e, kAtticList, 2), 0x65, false);
			break;
		case kDoorBelt:
			_door = kDoorMouth;
			_doorOpening = 0x5d;
			showSprite(findEntry(Request::kSprite, 0xaf, kMainList, 12), 0x65, false);
			showSprite(findEntry(Request::kSprite, 0xb1, kDoorList, 2), 0x65, false);
			showSprite(findEntry(Request::kSprite, 0x5d, kDoorList, 2), 0x65, false);
			break;
		default:
			break;
		}
		_doorSlot = switchPlace(kArea, _doorOpening, _doorSlot, _door, now, false);
		_lampSlot = switchPlace(kArea, 0x60, _lampSlot, 0x60, now, false);
		playSound(kArea, 0x60);
		return;
	}
	if (stepSlot(kArea, _doorSlot, _door, now))
		return;
	switch (_doorSlot) {
	case 0x5c:
		_doorSlot = switchPlace(kArea, 0xb3, _doorSlot, _door, now, false);
		break;
	case 0x5d:
		_doorSlot = switchPlace(kArea, 0xaf, _doorSlot, _door, now, false);
		break;
	case 0x5e:
		_doorSlot = switchPlace(kArea, 0xad, _doorSlot, _door, now, false);
		break;
	case 0xad:
	case 0xaf:
	case 0xb3:
		// open: the next step closes it, and the lamp swings
		_doorSlot = switchPlace(kArea, _doorSlot == 0xad ? 0xae : (_doorSlot == 0xaf ? 0xb1 : 0xb5), _doorSlot, _door, now, false);
		_buzzStarted = 0;
		_lampSlot = switchPlace(kArea, 0xb8, _lampSlot, 0x60, now, false);
		if (!_leaving)
			playSound(kArea, 0xb8);
		break;
	case 0xae:
	case 0xb1:
	case 0xb5:
		_doorSlot = switchPlace(kArea, -1, _doorSlot, _door, now, false);
		_doorTime = now + 1000;
		break;
	default:
		break;
	}
}

// m7 0x4822e0
int Scene2Hall::tick(uint32 input, uint32 now) {
	if (wait(now))
		return 0;
	pollSounds();
	if (_hintPending) {
		_hintPending = 0;
		if (_s2->_exitOpen)
			_game->hints().trigger(4, true, _s2->_nedPos.x, _s2->_nedPos.y, xform, this);
	}
	if (_nedActive != 0) {
		if (_nedActive < 0) {
			input = 0;
			_nedActive++;
			if (_nedActive == 0)
				_doorSlot = -1;
		}
		nedTick(input, now);
	} else if (_nedSlot != -1) {
		// Ned walks through the door; the mouth's walk plays the belt walk's sound (original)
		if (_nedSlot == 0x146 && slot(kNedArea, 0x146).step == 0xb)
			playSound(kNedArea, 0x146);
		if (_nedSlot == 400 && slot(kNedArea, 400).step == 0xb)
			playSound(kNedArea, 0x146);
		if (!stepSlot(kNedArea, _nedSlot, _nedSlot, now)) {
			if (_door == kDoorAttic)
				_s2->_next = 4;
			else if (_door == kDoorMouth)
				_s2->_next = 7;
			else if (_door == kDoorBelt)
				_s2->_next = 1;
			_nedSlot = switchPlace(kNedArea, -1, _nedSlot, _nedSlot, now, false);
			_game->display().draw();
			_game->switchScene(2, -1);
			return 0;
		}
		if (_lampSlot == 0x60) {
			Slot &n = slot(kNedArea, _nedSlot);
			if (n.sequenceLength - n.step < 0x10) {
				_buzzStarted = 0;
				_lampSlot = switchPlace(kArea, 0xb8, 0x60, 0x60, now, false);
				if (!_leaving)
					playSound(kArea, 0xb8);
			}
		}
	} else {
		if ((_doorSlot == _door || _doorSlot == _doorOpening) && (input & (kInputButton1 | kInputButton2)))
			_useWanted = 1;
		if (_doorSlot == _door && _useWanted) {
			_leaving = 1;
			_game->_optionsAllowed = false;
			int walk = -1;
			switch (_door) {
			case kDoorAttic:
				if (!_atticShown) {
					stopAllSounds();
					ned()->listShow(0x13);
					slotShow(_game, kNedArea, 0x19f, 100, false);
					placeAt(kNedArea, 0x19f, 0x80, 0xd6);
					_atticShown = 1;
				}
				walk = 0x19f;
				break;
			case kDoorMouth:
				if (!_mouthShown) {
					stopAllSounds();
					ned()->listShow(0x12);
					_mouthShown = 1;
					slotShow(_game, kNedArea, 400, 100, false);
					placeAt(kNedArea, 400, 0x80, 0xaf);
				}
				walk = 400;
				break;
			case kDoorBelt:
				if (!_beltShown) {
					stopAllSounds();
					ned()->listShow(10);
					slotShow(_game, kNedArea, 0x146, 100, false);
					placeAt(kNedArea, 0x146, 0x80, 0xb2);
					_beltShown = 1;
				}
				walk = 0x146;
				break;
			default:
				break;
			}
			if (walk != -1)
				_nedSlot = switchPlace(kNedArea, walk, -1, walk, now, false);
			visible(kArea, _doorSlot, false);
			visible(kArea, 0xb8, false);
			visible(kArea, 0x60, false);
			_doorSlot = -2;
			ned()->show(false);
		}
	}

	doorTick(now);
	if (_lampSlot != -1) {
		if (_buzzEnded && _buzzStarted && !_leaving)
			playSoundNotify(kArea, 100, kListCb);
		if (!stepSlot(kArea, _lampSlot, _lampSlot, now)) {
			if (_lampSlot == 0x60) {
				if (!_buzzStarted) {
					_buzzEnded = 0;
					if (!_leaving)
						playSoundNotify(kArea, 100, kListCb);
					_buzzStarted = 1;
				}
				// held on the sprite's last frame (the frame count, not the sequence length)
				slot(kArea, 0x60).step = (int16)(frameCount(kArea, 0x60) - 1);
			} else if (_lampSlot == 0xb8) {
				_lampSlot = switchPlace(kArea, -1, 0xb8, 0x60, now, false);
				_game->display().draw();
				return 0;
			}
		}
	}
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	_game->display().setCollisionOverlay(maps, Common::Point(_scrollX, _scrollY));
	_game->display().draw();
	debugC(3, kDebugGraphics, "Scene 2:0: input %x Ned %d,%d state %d door %d/%d lamp %d", input,
		_s2->_nedPos.x, _s2->_nedPos.y, ned()->getState(), _door, _doorSlot, _lampSlot);
	return 0;
}

} // End of namespace NightmareNed
