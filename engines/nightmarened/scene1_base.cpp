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
#include "common/file.h"
#include "graphics/paletteman.h"
#include "image/png.h"
#include "common/ptr.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_base.h"

namespace NightmareNed {

GraveyardRoom::GraveyardRoom(Game *game, int id, Scene1 *top) : Scene(game, id), _top(top), _unportedExit(-1) {
}

GraveyardRoom::~GraveyardRoom() {
	freeAll();
}

// ---- Resources ----

Slot &GraveyardRoom::slotAt(int area, int id) {
	return _game->res().getSlot(CLIP(area, 0, (int)kAreaCount - 1), CLIP(id, 0, (int)kAreaSlots - 1));
}

void GraveyardRoom::visibleAt(int area, int id, bool on) {
	if (id >= 0 && id < kAreaSlots && slotAt(area, id).sprite)
		slotAt(area, id).sprite->setVisibility(on);
}

BitmapRec *GraveyardRoom::bmp(uint32 va) {
	if (_bitmaps.contains(va))
		return _bitmaps[va];
	if (_top && (GraveyardRoom *)_top != this)
		return ((GraveyardRoom *)_top)->bmp(va);
	return nullptr;
}

CollisionMap *GraveyardRoom::map(uint32 va) {
	if (_maps.contains(va))
		return _maps[va];
	if (_top && (GraveyardRoom *)_top != this)
		return ((GraveyardRoom *)_top)->map(va);
	return nullptr;
}

const byte *GraveyardRoom::palette(uint32 va) {
	if (_palettes.contains(va))
		return _palettes[va].data();
	if (_top && (GraveyardRoom *)_top != this)
		return ((GraveyardRoom *)_top)->palette(va);
	return nullptr;
}

int GraveyardRoom::submit(uint32 va, int count, int scene) {
	NafArchive *naf = _game->getNaf(kNafGrave);
	if (!naf)
		return 5;
	const ExeImage &exe = _game->exe();
	Common::Array<uint32> callbacks;
	for (int i = 0; i < count; i++) {
		uint32 e = va + i * 0x24;
		uint32 cb = exe.readUint32(e + 0x1c);
		if (cb)
			callbacks.push_back(cb);
		if (_entries.contains(e))
			continue;
		Common::Array<Request> one = _game->readList(e, 1);
		Request &req = one[0];
		req.scene = scene;
		if (!_game->res().load(naf, req)) {
			warning("Graveyard: entry %x (type %d, id %d) did not load", e, req.type, req.id);
			continue;
		}
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
	for (uint32 cb : callbacks)
		listCallback(cb);
	return 0;
}

void GraveyardRoom::release(uint32 va, int count) {
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
		if (one[0].type == Request::kSprite && one[0].area >= 0 && one[0].area < kAreaCount) {
			Slot &s = _game->res().getSlot(one[0].area, one[0].id);
			for (uint t = 0; t < _targets.size(); t++) {
				if (_targets[t].area == one[0].area && _targets[t].slot == one[0].id)
					_targets.remove_at(t--);
			}
			s.flip = nullptr;
		}
		_game->freeList(one);
		_entries.erase(e);
	}
}

bool GraveyardRoom::isLoaded(uint32 va) const {
	return _entries.contains(va);
}

void GraveyardRoom::freeAll() {
	Common::Array<uint32> keys;
	for (EntryMap::iterator it = _entries.begin(); it != _entries.end(); ++it)
		keys.push_back(it->_key);
	Common::sort(keys.begin(), keys.end());
	for (int i = (int)keys.size() - 1; i >= 0; i--)
		release(keys[i], 1);
}

uint32 GraveyardRoom::entryFor(uint32 list, int count, int id) const {
	const ExeImage &exe = _game->exe();
	for (int i = 0; i < count; i++) {
		uint32 e = list + i * 0x24;
		if (exe.readSint32(e + 0x0c) == id && exe.readSint32(e + 0x10) == Request::kSprite)
			return e;
	}
	return 0;
}

void GraveyardRoom::showSprite(uint32 entryVA, int z, bool on) {
	if (!isLoaded(entryVA))
		return;
	const Request &req = _entries[entryVA];
	if (req.type != Request::kSprite || req.area < 0 || req.area >= kAreaCount)
		return;
	Slot &s = _game->res().getSlot(req.area, req.id);
	if (s.sprite) {
		s.sprite->setDrawOrder((int16)z);
		s.sprite->setDestination(_game->display().getPrimary());
		s.sprite->setVisibility(on);
	}
}

void GraveyardRoom::showBitmap(uint32 entryVA, int z, bool on) {
	if (!isLoaded(entryVA))
		return;
	const Request &req = _entries[entryVA];
	if (req.type != Request::kBitmap || !req.bitmap || !req.bitmap->object)
		return;
	req.bitmap->object->setDrawOrder((int16)z);
	req.bitmap->object->setDestination(_game->display().getPrimary());
	req.bitmap->object->setVisibility(on);
}

void GraveyardRoom::attachList(uint32 list, int count) {
	for (int i = 0; i < count; i++) {
		uint32 e = list + i * 0x24;
		if (!isLoaded(e))
			continue;
		const Request &req = _entries[e];
		RdxObject *obj = nullptr;
		if (req.type == Request::kSprite && req.area >= 0 && req.area < kAreaCount)
			obj = _game->res().getSlot(req.area, req.id).sprite;
		else if (req.type == Request::kBitmap && req.bitmap)
			obj = req.bitmap->object;
		if (obj) {
			obj->setDestination(_game->display().getPrimary());
			obj->setVisibility(false);
		}
	}
}

void GraveyardRoom::saveVisibility(uint32 list, int count, bool restore, Common::Array<int> &state) {
	state.resize(count);
	for (int i = count - 1, k = 0; i >= 0; i--, k++) {
		uint32 e = list + i * 0x24;
		if (!isLoaded(e))
			continue;
		const Request &req = _entries[e];
		RdxObject *obj = nullptr;
		if (req.type == Request::kSprite && req.area >= 0 && req.area < kAreaCount)
			obj = _game->res().getSlot(req.area, req.id).sprite;
		else if (req.type == Request::kBitmap && req.bitmap)
			obj = req.bitmap->object;
		if (!obj)
			continue;
		if (!restore) {
			state[k] = obj->getVisibility();
			if (state[k])
				obj->setVisibility(false);
		} else if (state[k]) {
			obj->setVisibility(true);
		}
	}
}

// ---- Display ----

int16 GraveyardRoom::switchSlotAt(int area, int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(area, (int16)newSlot, (int16)oldSlot);
}

int16 GraveyardRoom::switchSlotPreAt(int area, int newSlot, int oldSlot) {
	return (int16)_game->display().slotSwitch(area, (int16)newSlot, (int16)oldSlot, true);
}

int GraveyardRoom::placeAt(int area, int id, int ref, bool noMirror, NedXform xf, void *user, int *outFrame,
		bool relative) {
	int frame = 0, duration = 0;
	if (id < 0 || id >= kAreaSlots || ref < 0 || ref >= kAreaSlots)
		return 0;
	_game->display().slotPlace(area, id, ref, &frame, &duration, noMirror, Common::Point(0, 0), relative);
	Slot &s = slotAt(area, id);
	if (xf && s.sprite) {
		Common::Point p = s.sprite->getPosition();
		int x = p.x, y = p.y;
		xf(user, x, y);
		s.sprite->setPosition(x, y);
	}
	if (outFrame)
		*outFrame = frame;
	return duration;
}

void GraveyardRoom::adjAt(int area, int id, int dx, int dy) {
	if (id >= 0 && id < kAreaSlots && slotAt(area, id).sprite)
		slotAt(area, id).sprite->adjPosition(dx, dy);
}

void GraveyardRoom::attachPosAt(int area, int id, int ref, int out[2], bool noMirror) {
	Slot &s = slotAt(area, id), &r = slotAt(area, ref);
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

bool GraveyardRoom::seqStep(int area, int id, int step, int &frame, int &duration) {
	Slot &s = slotAt(area, id);
	return s.sequence && s.sequence->getStep(step, frame, duration);
}

int GraveyardRoom::currentFrame(int area, int id) {
	int frame = 0, duration = 0;
	seqStep(area, id, slotAt(area, id).step, frame, duration);
	return frame;
}

void GraveyardRoom::boxRect(int area, int id, int frame, int k, int32 out[4]) {
	Slot &s = slotAt(area, id);
	if (s.boxes)
		s.boxes->getRawRect(frame, k, out);
	else
		out[0] = out[1] = out[2] = out[3] = -0x7fff;
}

void GraveyardRoom::frameOffset(int area, int id, int frame, int &x, int &y, bool alt, int addX) {
	Slot &s = slotAt(area, id);
	if (!s.boxes || frame < 0 || frame >= (int)s.boxes->frameCount || frame >= (int)s.boxes->frames.size())
		return;
	const HitBoxes::Frame &f = s.boxes->frames[frame];
	if (alt) {
		x = (int16)(f.altOffX + addX);
		y = f.altOffY;
	} else {
		x = f.offX;
		y = f.offY;
	}
}

void GraveyardRoom::anchor(int area, int id, int &x, int &y) {
	Slot &s = slotAt(area, id);
	if (!s.boxes)
		return;
	x = s.boxes->anchorX;
	y = s.boxes->anchorY;
}

RdxEffect *GraveyardRoom::ensureFlip(int area, int id) {
	Slot &s = slotAt(area, id);
	if (s.sprite && !s.flip) {
		s.flip = s.sprite->applyEffect(kFxHorizFlip);
	}
	return s.flip;
}

void GraveyardRoom::setFlip(int area, int id, bool active) {
	RdxEffect *fx = ensureFlip(area, id);
	if (fx)
		fx->active = active;
}

void GraveyardRoom::flipActive(int area, int id, bool active) {
	Slot &s = slotAt(area, id);
	if (s.flip)
		s.flip->active = active;
}

bool GraveyardRoom::isFlipped(int area, int id) {
	Slot &s = slotAt(area, id);
	return s.flip && s.flip->active;
}

bool GraveyardRoom::intersect(const int32 a[4], const int32 b[4]) {
	return MAX(a[0], b[0]) < MIN(a[2], b[2]) && MAX(a[1], b[1]) < MIN(a[3], b[3]);
}

bool GraveyardRoom::inRect(const int32 r[4], int x, int y) {
	return r[0] <= x && x < r[2] && r[1] <= y && y < r[3];
}

// ---- Sound ----

void GraveyardRoom::playSoundAt(int area, int id, int stop) {
	_game->playSlotSound(slotAt(area, id), stop >= 0 ? &slotAt(area, stop) : nullptr);
}

void GraveyardRoom::stopSoundAt(int area, int id) {
	_game->stopSlotSound(slotAt(area, id));
}

void GraveyardRoom::stopAllSounds() {
	for (int a = 0; a < kAreaCount; a++) {
		for (int i = 0; i < kAreaSlots; i++)
			_game->stopSlotSound(_game->res().getSlot(a, i));
	}
}

// ---- Collision ----

void GraveyardRoom::addMap(uint32 va) {
	CollisionMap *m = map(va);
	if (m)
		_world.addMap(m, m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom);
}

void GraveyardRoom::addMapBounds(uint32 va, int x0, int y0, int x1, int y1) {
	CollisionMap *m = map(va);
	if (m)
		_world.addMap(m, x0, y0, x1, y1);
}

void GraveyardRoom::removeMap(uint32 va) {
	CollisionMap *m = map(va);
	if (m)
		_world.removeMap(m);
}

bool GraveyardRoom::solidAt(uint32 va, int x, int y) {
	CollisionMap *m = map(va);
	return m && m->isSolid((int16)x, (int16)y);
}

void GraveyardRoom::regionEnable(CollisionMap *m, int32 id, bool on) {
	CollisionMap::Region *r = regionFind(m, id);
	if (!r)
		return;
	if (on)
		r->type &= 0x7fffffff;
	else
		r->type |= 0x80000000;
}

void GraveyardRoom::regionSetRect(CollisionMap *m, int32 id, const int32 rect[4]) {
	CollisionMap::Region *r = regionFind(m, id);
	if (!r)
		return;
	r->rect.left = rect[0];
	r->rect.top = rect[1];
	r->rect.right = rect[2];
	r->rect.bottom = rect[3];
}

void GraveyardRoom::regionShift(CollisionMap *m, int dx, int dy) {
	if (!m)
		return;
	for (CollisionMap::Region &r : m->regions) {
		r.rect.left += dx;
		r.rect.top += dy;
		r.rect.right += dx;
		r.rect.bottom += dy;
	}
}

CollisionMap::Region *GraveyardRoom::regionFind(CollisionMap *m, int32 id) {
	if (!m)
		return nullptr;
	for (CollisionMap::Region &r : m->regions) {
		if (r.id == id)
			return &r;
	}
	return nullptr;
}

void GraveyardRoom::addRegion(int16 type, int16 id, const int32 rect[4], int32 *outId) {
	CollisionMap::Region r;
	r.type = (uint32)(int32)type;
	r.id = id;
	r.rect.left = rect[0];
	r.rect.top = rect[1];
	r.rect.right = rect[2];
	r.rect.bottom = rect[3];
	_userMap.regions.push_back(r);
	if (outId)
		*outId = id;
	_world.setUser(&_userMap);
}

void GraveyardRoom::removeRegion(int32 id) {
	for (uint i = 0; i < _userMap.regions.size(); i++) {
		if (_userMap.regions[i].id == id) {
			_userMap.regions.remove_at(i);
			break;
		}
	}
	_world.setUser(&_userMap);
}

Common::Array<CollisionMap *> GraveyardRoom::overlayMaps() const {
	Common::Array<CollisionMap *> maps;
	for (int i = 0; i < _world.getMapCount(); i++)
		maps.push_back(_world.getMap(i));
	if (_world.getColumnMap())
		maps.push_back(_world.getColumnMap());
	maps.push_back(const_cast<CollisionMap *>(&_userMap));
	return maps;
}

// ---- Targets ----

void GraveyardRoom::targetAdd(int area, int id, bool noFlip) {
	Placement p;
	memset(&p, 0, sizeof(p));
	p.area = (int16)area;
	p.slot = (int16)id;
	p.sub = 1;
	p.kind = 2;
	p.flip = noFlip ? 0 : 1;
	_targets.push_back(p);
}

void GraveyardRoom::targetRemove(int area, int id) {
	for (uint i = 0; i < _targets.size(); i++) {
		if (_targets[i].area == area && _targets[i].slot == (int16)id && _targets[i].sub == 1)
			_targets.remove_at(i);
	}
}

// ---- Misc ----

void GraveyardRoom::frameWait(uint32 ms, uint32 start) {
	uint32 elapsed = _game->now() - start;
	if (elapsed < ms)
		g_system->delayMillis(ms - elapsed);
	// The original sleeps inside its blocking loops; ScummVM still has to see its events
	_game->getEngine()->pumpEvents();
}

// Test hook: nightmarened_s1_drawshots=<n>:<png>,... saves the screen after the n-th
// srfDraw of the session, including those inside blocking cut-scene loops
void GraveyardRoom::draw() {
	Display &display = _game->display();
	display.draw();
	if (!ConfMan.hasKey("nightmarened_s1_drawshots"))
		return;
	Common::String spec = ConfMan.get("nightmarened_s1_drawshots");
	const char *p = spec.c_str();
	while (*p) {
		char *end;
		long n = strtol(p, &end, 0);
		if (end == p || *end != ':')
			return;
		p = end + 1;
		Common::String path;
		while (*p && *p != ',')
			path += *p++;
		if (*p == ',')
			p++;
		if ((uint32)n == display._drawCount) {
			Common::DumpFile out;
			byte pal[256 * 3];
			g_system->getPaletteManager()->grabPalette(pal, 0, 256);
			if (out.open(Common::Path(path, '/'), true))
				Image::writePNG(out, display.getPrimary()->getBuffer(), pal);
		}
	}
}

bool GraveyardRoom::loadSst(int areaIdx, int scene, int n, Common::Array<Placement> &out) {
	out.clear();
	Common::String name = Common::String::format("%02d%03d0%02d.sst",
		(int16)_game->exe().readUint16(0x4afcd8 + 2 * areaIdx), scene, n);
	Common::ScopedPtr<Common::SeekableReadStream> sst(_game->getEngine()->openFile(name));
	if (!sst || !loadPlacements(*sst, out)) {
		warning("Can't read %s", name.c_str());
		return false;
	}
	return true;
}

void GraveyardRoom::applyPlacement(const Placement &p) {
	if (p.kind == 2) {
		if (p.area <= 0 || p.area >= 5 || p.slot < 0 || p.slot >= kAreaSlots)
			return;
		Slot &s = slotAt(p.area, p.slot);
		s.x = p.x;
		s.y = p.y;
		s.z = p.z;
		s.offsets[0] = p.offX;
		s.offsets[1] = p.offY;
		s.key = p.transparentColor;
		if (p.flip)
			s.flags |= 4;
	}
	if ((p.kind != 0 && p.kind != 1) || p.area <= 0)
		return;
	// 0x45a440: the area's index (u16 table 0x4afc60), then the bitmap record pointer at
	// DAT_004ddb88[(index * 15 + slot) * 100 + sub]
	int16 index = (int16)_game->exe().readUint16(0x4afc60 + p.area * 2);
	BitmapRec *rec = bmp(0x4ddb88 + 4 * (uint32)(p.sub + (index * 15 + p.slot) * 100));
	if (!rec)
		return;
	rec->x = p.x;
	rec->y = p.y;
	rec->z = p.z;
}

int GraveyardRoom::switchTo(int scene, int sub) {
	if (!_game->hasScene(scene, sub)) {
		if (_unportedExit != scene * 100 + sub)
			debugC(1, kDebugLoading, "Graveyard: exit to scene %d:%d (not ported), staying", scene, sub);
		_unportedExit = scene * 100 + sub;
		return 1;
	}
	_unportedExit = -1;
	_game->switchScene(scene, sub);
	return 0;
}

} // End of namespace NightmareNed
