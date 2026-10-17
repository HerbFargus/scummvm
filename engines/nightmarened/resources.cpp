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
#include "common/endian.h"
#include "common/ptr.h"
#include "common/textconsole.h"

#include "nightmarened/detection.h"
#include "nightmarened/rdx.h"
#include "nightmarened/resources.h"

namespace NightmareNed {

bool ExeImage::load(Common::SeekableReadStream *stream) {
	Common::ScopedPtr<Common::SeekableReadStream> s(stream);
	if (!s)
		return false;
	_data.resize(s->size());
	s->read(_data.data(), _data.size());
	if (_data.size() < 0x40 || READ_LE_UINT16(_data.data()) != MKTAG16('Z', 'M'))
		return false;
	uint32 pe = READ_LE_UINT32(_data.data() + 0x3c);
	if (pe + 0xf8 > _data.size() || READ_LE_UINT32(_data.data() + pe) != MKTAG('\0', '\0', 'E', 'P'))
		return false;
	uint16 sectionCount = READ_LE_UINT16(_data.data() + pe + 6);
	uint16 optSize = READ_LE_UINT16(_data.data() + pe + 20);
	uint32 imageBase = READ_LE_UINT32(_data.data() + pe + 24 + 28);
	uint32 table = pe + 24 + optSize;
	for (uint i = 0; i < sectionCount && table + 40 * (i + 1) <= _data.size(); i++) {
		const byte *h = _data.data() + table + 40 * i;
		Section sec;
		sec.size = READ_LE_UINT32(h + 8);
		sec.va = imageBase + READ_LE_UINT32(h + 12);
		sec.rawSize = READ_LE_UINT32(h + 16);
		sec.offset = READ_LE_UINT32(h + 20);
		_sections.push_back(sec);
	}
	return true;
}

const byte *ExeImage::ptr(uint32 va, uint32 size) const {
	for (const Section &sec : _sections) {
		if (va >= sec.va && va + size <= sec.va + MAX(sec.size, sec.rawSize)) {
			uint32 off = va - sec.va;
			if (off + size <= sec.rawSize && sec.offset + off + size <= _data.size())
				return _data.data() + sec.offset + off;
			return nullptr; // uninitialised data
		}
	}
	return nullptr;
}

uint32 ExeImage::readUint32(uint32 va) const {
	const byte *p = ptr(va, 4);
	return p ? READ_LE_UINT32(p) : 0;
}

uint16 ExeImage::readUint16(uint32 va) const {
	const byte *p = ptr(va, 2);
	return p ? READ_LE_UINT16(p) : 0;
}

Slot::Slot() : x(0), y(0), z(0), sequenceLength(0), flags(0), w(0), h(0), key(0), sprite(nullptr), flip(nullptr),
	sequence(nullptr), boxes(nullptr), alias(false), loaded(false), step(0), deadline(0) {
	memset(offsets, 0, sizeof(offsets));
}

void Slot::clear() {
	delete sprite;
	sprite = nullptr;
	flip = nullptr;
	delete sequence;
	sequence = nullptr;
	if (!alias) {
		delete boxes;
		for (SpriteFrame *f : frames)
			delete f;
	}
	boxes = nullptr;
	frames.clear();
	alias = loaded = false;
	sound.clear();
	step = 0;
	deadline = 0;
}

BitmapRec::~BitmapRec() {
	delete object;
}

Resources::Resources() {
	for (int i = 0; i < kAreaCount; i++)
		_areas[i] = new Slot[kAreaSlots];
}

Resources::~Resources() {
	for (int i = 0; i < kAreaCount; i++) {
		for (int j = 0; j < kAreaSlots; j++)
			_areas[i][j].clear();
		delete[] _areas[i];
	}
}

// 0x24 bytes: u16 flags, pad, i32 scene, area (or colour key), id, type, dest, naf, callback, palette
Common::Array<Request> Resources::readRequests(const ExeImage &exe, uint32 va, int count) {
	Common::Array<Request> list(count);
	for (int i = 0; i < count; i++) {
		uint32 e = va + 0x24 * i;
		Request &r = list[i];
		r.flags = exe.readUint16(e);
		r.scene = exe.readSint32(e + 4);
		r.area = exe.readSint32(e + 8);
		r.id = exe.readSint32(e + 0x0c);
		r.type = exe.readSint32(e + 0x10);
		r.destVA = exe.readUint32(e + 0x14);
		r.paletteVA = exe.readUint32(e + 0x20);
	}
	return list;
}

bool Resources::loadList(NafArchive *naf, Common::Array<Request> &list, int scene) {
	bool ok = true;
	for (Request &r : list) {
		r.scene = scene;
		ok &= load(naf, r);
	}
	return ok;
}

// FUN_00458380 (final pass) + FUN_00458010: a sequence either owns its frames and hit
// boxes, or borrows those of the slot it names (an alias)
bool Resources::loadSprite(NafArchive *naf, Request &req) {
	if (req.area < 0 || req.area >= kAreaCount || req.id < 0 || req.id >= kAreaSlots)
		return false;
	Slot &slot = _areas[req.area][req.id];
	slot.clear();

	Common::ScopedPtr<Common::SeekableReadStream> seqStream(naf->getResource(kResSequence, req.scene, req.id));
	Sequence *seq = new Sequence();
	if (!seqStream || !seq->load(*seqStream)) {
		delete seq;
		warning("No sequence %d:%d in %s", req.scene, req.id, naf->getName().c_str());
		return false;
	}
	slot.sequence = seq;
	slot.sequenceLength = seq->steps.size();

	if (seq->sprite == req.id) {
		Common::ScopedPtr<Common::SeekableReadStream> boxStream(naf->getResource(kResHitBoxes, req.scene, req.id));
		slot.boxes = new HitBoxes();
		if (!boxStream || !slot.boxes->load(*boxStream)) {
			delete slot.boxes;
			slot.boxes = nullptr;
		}
		Common::ScopedPtr<Common::SeekableReadStream> sprStream(naf->getResource(kResSprite, req.scene, req.id));
		if (sprStream)
			loadSpriteBank(*sprStream, slot.frames);
		if (slot.boxes) {
			slot.key = slot.boxes->transparentColor;
			slot.w = slot.boxes->width;
			slot.h = slot.boxes->height;
		} else if (!slot.frames.empty()) {
			slot.w = slot.frames[0]->surface.w;
			slot.h = slot.frames[0]->surface.h;
		}
	} else if (seq->sprite < kAreaSlots) {
		const Slot &src = _areas[req.area][seq->sprite];
		slot.alias = true;
		slot.key = src.key;
		slot.w = src.w;
		slot.h = src.h;
		slot.boxes = src.boxes;
		slot.frames = src.frames;
	}

	slot.sprite = new RdxSprite();
	slot.sprite->setFrames(slot.frames, slot.w, slot.h);
	slot.step = 0;
	slot.deadline = 0;
	slot.loaded = true;
	debugC(3, kDebugLoading, "Slot %d:%d from %d:%d, %d steps, %d frames%s", req.area, req.id, req.scene, req.id,
		slot.sequenceLength, slot.frames.size(), slot.alias ? " (alias)" : "");
	return true;
}

bool Resources::load(NafArchive *naf, Request &req) {
	switch (req.type) {
	case Request::kSprite:
		return loadSprite(naf, req);

	case Request::kBitmap: {
		Common::ScopedPtr<Common::SeekableReadStream> s(naf->getResource(kResBackground, req.scene, req.id));
		BitmapRec *rec = new BitmapRec();
		if (!s || !rec->bitmap.load(*s)) {
			delete rec;
			warning("No bitmap %d:%d in %s", req.scene, req.id, naf->getName().c_str());
			return false;
		}
		req.hasPalette = rec->bitmap.hasPalette;
		if (req.hasPalette)
			memcpy(req.palette, rec->bitmap.palette, sizeof(req.palette));
		// Height 1 bitmaps (NED92000's 8x1) only carry a palette
		if (rec->bitmap.surface.h >= 2) {
			rec->object = new RdxBackground();
			rec->object->setBitmap(&rec->bitmap.surface, req.area);
		}
		req.bitmap = rec;
		return true;
	}

	case Request::kCollision: {
		Common::ScopedPtr<Common::SeekableReadStream> s(naf->getResource(kResCollision, req.scene, req.id));
		CollisionMap *map = new CollisionMap();
		if (!s || !map->load(*s)) {
			delete map;
			warning("No collision map %d:%d in %s", req.scene, req.id, naf->getName().c_str());
			return false;
		}
		req.collision = map;
		return true;
	}

	case Request::kSound: {
		if (req.area < 0 || req.area >= kAreaCount || req.id < 0 || req.id >= kAreaSlots)
			return false;
		Common::ScopedPtr<Common::SeekableReadStream> s(naf->getResource(kResSound, req.scene, req.id));
		if (!s)
			return false;
		Slot &slot = _areas[req.area][req.id];
		slot.sound.resize(s->size());
		s->read(slot.sound.data(), slot.sound.size());
		return true;
	}

	default:
		return false;
	}
}

void Resources::freeList(Common::Array<Request> &list) {
	for (int i = (int)list.size() - 1; i >= 0; i--) {
		Request &r = list[i];
		if (r.type == Request::kSprite && r.area >= 0 && r.area < kAreaCount && r.id >= 0 && r.id < kAreaSlots)
			_areas[r.area][r.id].clear();
		else if (r.type == Request::kSound && r.area >= 0 && r.area < kAreaCount && r.id >= 0 && r.id < kAreaSlots)
			_areas[r.area][r.id].sound.clear();
		delete r.bitmap;
		r.bitmap = nullptr;
		delete r.collision;
		r.collision = nullptr;
	}
}

} // End of namespace NightmareNed
