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
#include "common/events.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "video/smk_decoder.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/placements.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene6.h"

namespace NightmareNed {

// ---- Resources ----

Slot &Scene6::slot(int id) {
	return _game->res().getSlot(kArea, CLIP(id, 0, (int)kAreaSlots - 1));
}

void Scene6::visible(int id, bool on) {
	if (id >= 0 && id < kAreaSlots && slot(id).sprite)
		slot(id).sprite->setVisibility(on);
}

void Scene6::attach(int id, int z) {
	Slot &s = slot(id);
	if (!s.sprite)
		return;
	s.sprite->setDestination(_game->display().getPrimary());
	s.sprite->setDrawOrder(z);
}

BitmapRec *Scene6::bmp(uint32 va) {
	return _bitmaps.contains(va) ? _bitmaps[va] : nullptr;
}

CollisionMap *Scene6::map(uint32 va) {
	return _maps.contains(va) ? _maps[va] : nullptr;
}

const byte *Scene6::palette(uint32 va) {
	return _palettes.contains(va) ? _palettes[va].data() : nullptr;
}

BitmapRec *Scene6::lookupBitmap(void *user, uint32 va) {
	return ((Scene6 *)user)->bmp(va);
}

void Scene6::applyPlacements(const Common::Array<Placement> &list) {
	for (const Placement &e : list)
		applyPlacement(_game, e, lookupBitmap, this);
}

// 0x4586e0 (synchronous): a list's completion callback runs as soon as it is loaded
int Scene6::submit(uint32 va, int count, int scene) {
	NafArchive *naf = _game->getNaf(kNafPlayroom);
	if (!naf)
		return 5;
	const ExeImage &exe = _game->exe();
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
			if (req.hasPalette && req.paletteVA) {
				Common::Array<byte> &p = _palettes[req.paletteVA];
				p.resize(256 * 3);
				memcpy(p.data(), req.palette, 256 * 3);
			}
			_entries[e] = req;
		}
		if (exe.readUint32(e + 0x1c) == kSharedCallback)
			_sharedLoaded = 1; // 0x430d50
	}
	return 0;
}

void Scene6::release(uint32 va, int count) {
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

void Scene6::freeAll() {
	releaseBuilt();
	Common::Array<uint32> keys;
	for (EntryMap::iterator it = _entries.begin(); it != _entries.end(); ++it)
		keys.push_back(it->_key);
	Common::sort(keys.begin(), keys.end());
	for (int i = (int)keys.size() - 1; i >= 0; i--)
		release(keys[i], 1);
}

// 0x434340
uint32 Scene6::entryFor(int id, uint32 list, int count) const {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < count; i++) {
		uint32 e = list + i * 0x24;
		if (exe.readSint32(e + 0x0c) == id)
			return e;
	}
	return 0;
}

// 0x434560
void Scene6::setEntry(int index, int area, int id, int type, uint32 dest, uint32 pal) {
	if (index >= 0x32)
		return;
	Request &r = _built[index];
	r = Request();
	r.scene = 0;
	r.area = area;
	r.id = id;
	r.type = type;
	r.destVA = dest;
	r.paletteVA = pal;
}

int Scene6::submitBuilt() {
	NafArchive *naf = _game->getNaf(kNafPlayroom);
	if (!naf)
		return 5;
	for (int i = 0; i < _builtCount; i++) {
		Request &req = _built[i];
		if (_builtOk[i])
			continue;
		req.scene = 0;
		_builtOk[i] = _game->res().load(naf, req);
		if (!_builtOk[i])
			continue;
		if (req.bitmap)
			_bitmaps[req.destVA] = req.bitmap;
		if (req.collision)
			_maps[req.destVA] = req.collision;
		if (req.hasPalette && req.paletteVA) {
			Common::Array<byte> &p = _palettes[req.paletteVA];
			p.resize(256 * 3);
			memcpy(p.data(), req.palette, 256 * 3);
		}
	}
	return 0;
}

void Scene6::releaseBuilt() {
	for (int i = (int)_built.size() - 1; i >= 0; i--) {
		if (!_builtOk[i])
			continue;
		Common::Array<Request> one;
		one.push_back(_built[i]);
		if (one[0].bitmap)
			_bitmaps.erase(one[0].destVA);
		if (one[0].collision)
			_maps.erase(one[0].destVA);
		_game->freeList(one);
		_built[i].bitmap = nullptr;
		_built[i].collision = nullptr;
		_builtOk[i] = false;
	}
}

bool Scene6::builtLoaded(int index) const {
	return index >= 0 && index < (int)_builtOk.size() && _builtOk[index];
}

void Scene6::showBuilt(int index, uint32 dest, int z, bool position) {
	BitmapRec *rec = builtLoaded(index) ? bmp(dest) : nullptr;
	if (!rec || !rec->object)
		return;
	rec->object->setDrawOrder(z);
	rec->object->setDestination(_game->display().getPrimary());
	if (position)
		rec->object->setPosition(rec->x, rec->y);
	rec->object->setVisibility(true);
}

// ---- Display helpers ----

// 0x430890
int16 Scene6::switchSlot(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot);
}

// 0x430930
int16 Scene6::switchSlotPre(int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(kArea, (int16)newSlot, (int16)oldSlot, true);
}

int Scene6::place(int id, int ref) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	_game->display().slotPlace(kArea, id, ref, &frame, &duration, true, Common::Point(0, 0));
	return duration;
}

int Scene6::setImage(int id) {
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(s.step, frame, duration);
	if (s.sprite)
		s.sprite->setCurrentImage(frame);
	return duration;
}

void Scene6::stepRect(int id, int step, int k, int32 out[4]) {
	Slot &s = slot(id);
	int frame = 0, duration = 0;
	if (s.sequence)
		s.sequence->getStep(step, frame, duration);
	if (s.boxes)
		s.boxes->getRawRect(frame, k, out);
	else
		out[0] = out[1] = out[2] = out[3] = -0x7fff;
}

void Scene6::playSound(int id) {
	_game->playSlotSound(slot(id), nullptr);
}

void Scene6::stopAllSounds() {
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
}

void Scene6::draw(const Common::Array<CollisionMap *> &maps) {
	_game->display().setCollisionOverlay(maps, Common::Point(0, 0));
	_game->display().draw();
}

// ---- What the rooms share ----

// 0x4342b0
int16 Scene6::swapShown(int16 newSlot, int16 oldSlot, uint32 newEntry, uint32 oldEntry) {
	if (oldSlot >= 0 && oldSlot != newSlot && isLoaded(oldEntry))
		visible(oldSlot, false);
	if (newSlot > 0 && oldSlot != newSlot && isLoaded(newEntry))
		visible(newSlot, true);
	return newSlot;
}

// 0x433b20
void Scene6::damageStep() {
	Slot &s = slot(0x12e);
	s.step++;
	if (s.sequenceLength - 1 <= s.step) {
		s.step = s.sequenceLength - 1;
		_beaten = 1;
	}
	if (s.step != -1)
		setImage(0x12e);
}

// 0x433b90
void Scene6::timerTick(uint32 now, bool reset) {
	if (reset) {
		_timer1 = _timer2 = _timer3 = 0;
		if (slot(0x132).sprite)
			slot(0x132).sprite->setCurrentImage(0x15);
		if (slot(0x136).sprite)
			slot(0x136).sprite->setCurrentImage(0);
		if (slot(0x137).sprite)
			slot(0x137).sprite->setCurrentImage(0);
		slot(0x132).step = slot(0x136).step = slot(0x137).step = -1;
		slot(0x132).deadline = slot(0x136).deadline = slot(0x137).deadline = 0;
		return;
	}
	int cur = 0x132;
	visible(0x132, true);
	visible(0x136, false);
	visible(0x137, false);
	if (_timer1) {
		cur = 0x136;
		visible(0x132, false);
		visible(0x137, false);
		visible(0x136, true);
	}
	if (_timer2) {
		cur = 0x137;
		visible(0x132, false);
		visible(0x137, true);
		visible(0x136, false);
	}
	if (_timer3) {
		cur = 0x137;
		visible(0x132, false);
		visible(0x137, false);
		visible(0x136, false);
	}
	Slot &s = slot(cur);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		if (_timer2) {
			_timeUp = 1;
			visible(0x132, false);
			visible(0x137, false);
			visible(0x136, false);
			_timer3 = 1;
		}
		if (_timer1)
			_timer2 = 1;
		_timer1 = 1;
	}
	int duration = setImage(cur);
	s.deadline = now + (int16)duration * kMsPerStep;
}

// 0x4343b0
void Scene6::effect133Tick(uint32 now) {
	Slot &s = slot(0x133);
	if (!_effect133) {
		visible(0x133, false);
		s.step = -1;
		_sound133 = 1;
		return;
	}
	visible(0x133, true);
	if (_sound133) {
		playSound(0x133);
		_sound133 = 0;
	}
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		_effect133 = 0;
		_sound133 = 1;
	}
	s.deadline = now + (int16)place(0x133, 0x133) * kMsPerStep;
}

// 0x434480
void Scene6::effect134Tick(uint32 now) {
	Slot &s = slot(0x134);
	if (!_effect134) {
		visible(0x134, false);
		s.step = -1;
		_sound134 = 1;
		return;
	}
	if (_sound134) {
		playSound(0x134);
		_sound134 = 0;
	}
	visible(0x134, true);
	if (s.deadline >= now)
		return;
	s.step++;
	if (s.step >= s.sequenceLength) {
		s.step = 0;
		_effect134 = 0;
		visible(0x134, false);
		_sound134 = 1;
	}
	s.deadline = now + (int16)place(0x134, 0x134) * kMsPerStep;
}

// 0x433d70
int Scene6::loadWipe() {
	return submit(kWipeList, 1, 99);
}

// 0x433da0
void Scene6::freeWipe() {
	release(kWipeList, 1);
}

// The loop the three wipes share: every frame of the file into the wipe bitmap at
// Ned's order - 2, one per 83 ms, after the start sound (slot 0x30)
int Scene6::playWipe(int fileIndex, int music, int soundFrame, bool hideVehicle, bool hideAfter) {
	if (music >= 0)
		_game->media().playMusic(music);
	Common::String name = _game->media().getFileName(fileIndex);
	Video::SmackerDecoder decoder;
	Common::SeekableReadStream *stream = _game->getEngine()->openFile(name);
	if (!stream || !decoder.loadStream(stream)) {
		warning("Can't play %s", name.c_str());
		return 1;
	}
	BitmapRec *rec = bmp(kWipeBitmap);
	if (!rec || !rec->object)
		return 1;
	debugC(1, kDebugVideo, "Scene 6: wipe %s, %d frames", name.c_str(), decoder.getFrameCount());
	rec->object->setDestination(_game->display().getPrimary());
	rec->object->setDrawOrder(kNedZ - 2);
	rec->object->setVisibility(true);
	if (hideVehicle)
		visible(0x31, false);
	playSound(0x30);
	decoder.start();
	Graphics::Surface &dst = rec->bitmap.surface;
	int count = decoder.getFrameCount();
	int16 played = 0;
	for (int frame = 0; frame < count && !_game->getEngine()->shouldQuit(); frame++) {
		uint32 start = _game->now();
		const Graphics::Surface *src = decoder.decodeNextFrame();
		if (src) {
			int w = MIN<int>(src->w, dst.w), h = MIN<int>(src->h, dst.h);
			for (int y = 0; y < h; y++)
				memcpy(dst.getBasePtr(0, y), src->getBasePtr(0, y), w);
		}
		played++;
		if (played == soundFrame)
			playSound(0x18f);
		_game->display().draw();
		while (_game->now() - start < 0x53 && !_game->getEngine()->shouldQuit()) {
			Common::Event event;
			while (g_system->getEventManager()->pollEvent(event)) {
			}
			g_system->delayMillis(MIN<uint32>(0x53 - (_game->now() - start), 10));
		}
	}
	if (hideAfter)
		rec->object->setVisibility(false);
	return 0;
}

// 0x433db0: slot 0x18f sounds after 24 frames
int Scene6::closePlayroom() {
	return playWipe(kWipeClosePlayroom, kMusicWipe, 0x18, false, true);
}

// 0x433f70: the vehicle 0x31 is hidden
int Scene6::closeRoom() {
	return playWipe(kWipeCloseRoom, kMusicWipe, -1, true, true);
}

// 0x434130: the playroom music starts after; the frame stays shown until freed
int Scene6::openWipe() {
	int r = playWipe(kWipeOpen, -1, -1, false, false);
	if (!r)
		_game->media().playMusic(kMusicPlayroom);
	return r;
}

// 0x45a0f0, staying here when the target isn't registered yet
int Scene6::switchTo(int scene, int sub) {
	if (!_game->hasScene(scene, sub)) {
		int key = scene * 16 + sub + 1;
		if (_unportedExit != key)
			debugC(1, kDebugLoading, "Scene 6: exit to scene %d:%d (not ported), staying", scene, sub);
		_unportedExit = key;
		return 1;
	}
	_unportedExit = -1;
	_game->switchScene(scene, sub);
	return 0;
}

bool Scene6::findTarget(const Common::Array<Placement> &list, int16 &dir, int16 &found, int32 rect[4], bool clampStep) {
	found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), list, dir, unused, found))
		return true;
	Slot &s = slot(found);
	int step = s.step;
	if (clampStep && step == -1)
		step = 0;
	Common::Point pos = s.sprite ? s.sprite->getPosition() : Common::Point(0, 0);
	stepRect(found, step, s.flip ? 3 : 0, rect);
	rect[0] += pos.x;
	rect[1] += pos.y;
	rect[3] += pos.y;
	rect[2] += pos.x;
	return true;
}

// 0x4309c0 (always noFlip = 1 here)
void Scene6::targetAdd(Common::Array<Placement> &list, int id) {
	Placement p;
	memset(&p, 0, sizeof(p));
	p.area = kArea;
	p.slot = (int16)id;
	p.sub = 1;
	p.kind = 2;
	p.flip = 0;
	list.push_back(p);
}

// 0x486f50 olist_remove (its index loop skips the entry after each removal)
void Scene6::targetRemove(Common::Array<Placement> &list, int id) {
	for (uint i = 0; i < list.size(); i++) {
		if (list[i].area == kArea && list[i].slot == (int16)id && list[i].sub == 1)
			list.remove_at(i);
	}
}

// ---- The rooms' shared parts ----

bool Scene6Room::throttle(uint32 now) {
	if (now < _lastTick + kMsPerStep) {
		g_system->delayMillis(MIN<uint32>(_lastTick + kMsPerStep - now, 10));
		return true;
	}
	_lastTick = now;
	return false;
}

void Scene6Room::draw() {
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	maps.push_back(&_userMap);
	hub()->draw(maps);
}

int Scene6Room::setupWorld(int sst, uint32 list, uint32 mapVA) {
	// 0x45a560(3, 999, sst) + 0x43e460
	if (!loadSceneSst(_game, 3, 999, sst, _sst))
		return 5;
	hub()->applyPlacements(_sst);
	if (!hub()->isLoaded(list))
		return 5;
	_world = CollisionWorld();
	_userMap = CollisionMap();
	_world.setUser(&_userMap);
	CollisionMap *m = hub()->map(mapVA);
	if (m)
		_world.addMap(m, 0, 0, 0x27f, 0x1df);
	_game->res().getSlot(1, 4).offsets[0] = 6;   // DAT_004c7324
	for (int y = 0x1df; y >= 0; y--) {
		if (m && m->isSolid((int16)nedPos().x, (int16)y)) {
			nedPos().y = y;
			break;
		}
	}
	return 0;
}

void Scene6Room::showEntry(uint32 list, int count, int id, int z, bool on) {
	if (!hub()->isLoaded(hub()->entryFor(id, list, count)))
		return;
	Slot &s = slot(id);
	if (!s.sprite)
		return;
	s.sprite->setDestination(_game->display().getPrimary());
	s.sprite->setDrawOrder(z);
	s.sprite->setVisibility(on);
}

// 0x426260 and its copies
int Scene6Room::leaveRoom(int toScene, int savedVisited) {
	hub()->stopAllSounds();
	// 0x455ca0 flushes the loader queues; 0x45a5f0 frees the SST list
	_sst.clear();
	if (toScene != 6) {
		ned()->listFree(Scene6::kNedList);
		return 0;
	}
	hub()->_saved[savedVisited] = 1;
	hub()->loadWipe();
	return 0;
}

// 0x4262b0 and its copies
int Scene6Room::unloadRoom(int toScene, uint32 list, int count) {
	_world = CollisionWorld();
	_game->res().getSlot(1, 4).offsets[0] = 0;
	hub()->release(list, count);
	ned()->show(false);
	ned()->enable(false);
	if (toScene != 6) {
		hub()->closeAll(true);
		return 0;
	}
	hub()->closeRoom();
	hub()->visible(0x133, false);
	hub()->visible(0x134, false);
	hub()->visible(0x132, false);
	hub()->visible(0x136, false);
	hub()->visible(0x137, false);
	hub()->visible(0x12e, false);
	return 0;
}

bool Scene6Room::findTarget(int16 &dir, int16 &found, int32 rect[4]) {
	return hub()->findTarget(_targets, dir, found, rect, true);
}

} // End of namespace NightmareNed
