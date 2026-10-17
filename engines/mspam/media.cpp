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
#include "common/memstream.h"
#include "common/textconsole.h"

#include "mspam/detection.h"
#include "mspam/media.h"

namespace MSPAM {

PibList::~PibList() {
	for (Pib *p : pibs)
		delete p;
}

const Pib *PibList::getFrame(uint frame) const {
	if (frames.empty())
		return frame < pibs.size() ? pibs[frame] : nullptr;
	if (frame >= frames.size() || frames[frame].pib >= pibs.size())
		return nullptr;
	return pibs[frames[frame].pib];
}

MediaManager::MediaManager() {
}

MediaManager::~MediaManager() {
	clearCache();
	for (Common::File *f : _containers)
		delete f;
}

void MediaManager::clearCache() {
	for (auto &it : _cache)
		delete it._value;
	_cache.clear();
}

// MDF: u16 0x2a, u16 3, u32 slots, u32 entries, u32 CMF size, u16 2, then
// 14-byte hash slots { u32 id, u16 type, u32 offset, u32 size } (size bit 31 = in the CMF)
bool MediaManager::addIndex(const Common::Path &mdf, const Common::Path &cmf) {
	Common::File index;
	if (!index.open(mdf))
		return false;
	Common::File *container = new Common::File();
	if (!container->open(cmf)) {
		delete container;
		return false;
	}
	index.skip(4);
	uint32 slots = index.readUint32LE();
	uint32 count = index.readUint32LE();
	index.skip(6);
	uint added = 0;
	for (uint32 i = 0; i < slots && !index.eos(); i++) {
		MediaEntry e;
		e.id = index.readUint32LE();
		e.type = index.readUint16LE();
		e.offset = index.readUint32LE();
		e.size = index.readUint32LE() & 0x7FFFFFFF;
		e.container = _containers.size();
		if ((e.id == 0 && e.type == 0) || index.eos())
			continue;
		if (!_entries.contains(e.id)) {
			_entries[e.id] = e;
			added++;
		}
	}
	_containers.push_back(container);
	debugC(1, kDebugLoading, "%s: %d of %d entries added", mdf.toString().c_str(), added, count);
	return true;
}

const MediaEntry *MediaManager::find(uint32 id) const {
	auto it = _entries.find(id);
	return it == _entries.end() ? nullptr : &it->_value;
}

Common::Array<uint32> MediaManager::listIds(uint16 type) const {
	Common::Array<uint32> ids;
	for (const auto &it : _entries) {
		if (it._value.type == type)
			ids.push_back(it._key);
	}
	Common::sort(ids.begin(), ids.end());
	return ids;
}

Common::SeekableReadStream *MediaManager::open(uint32 id) {
	const MediaEntry *e = find(id);
	if (!e || e->type == kMediaVideo)
		return nullptr;
	Common::File *f = _containers[e->container];
	if (!f->seek(e->offset))
		return nullptr;
	return f->readStream(e->size);
}

Common::Path MediaManager::getVideoPath(uint32 id) const {
	const MediaEntry *e = find(id);
	if (!e || e->type != kMediaVideo)
		return Common::Path();
	uint n = e->offset % 36;
	char name[2] = { (char)(n < 10 ? '0' + n : 'A' + n - 10), 0 };
	return Common::Path(Common::String::format("%d", e->offset / 36)).appendComponent(name);
}

void MediaManager::setStaticColors(byte *palette) {
	static const byte kStatic[20][3] = {
		{ 0, 0, 0 }, { 128, 0, 0 }, { 0, 128, 0 }, { 128, 128, 0 }, { 0, 0, 128 },
		{ 128, 0, 128 }, { 0, 128, 128 }, { 192, 192, 192 }, { 192, 220, 192 }, { 166, 202, 240 },
		{ 255, 251, 240 }, { 160, 160, 164 }, { 128, 128, 128 }, { 255, 0, 0 }, { 0, 255, 0 },
		{ 255, 255, 0 }, { 0, 0, 255 }, { 255, 0, 255 }, { 0, 255, 255 }, { 255, 255, 255 }
	};
	for (int i = 0; i < 10; i++) {
		memcpy(palette + i * 3, kStatic[i], 3);
		memcpy(palette + (246 + i) * 3, kStatic[10 + i], 3);
	}
}

// Palette (CMF type 6): u16 1, then 236 x { r, g, b, flags } for indices 10..245
bool MediaManager::loadPalette(uint32 id, byte *palette) {
	Common::ScopedPtr<Common::SeekableReadStream> s(open(id));
	if (!s)
		return false;
	setStaticColors(palette);
	s->skip(2);
	uint count = MIN<uint>((s->size() - 2) / 4, 236);
	for (uint i = 0; i < count; i++) {
		s->read(palette + (10 + i) * 3, 3);
		s->skip(1);
	}
	debugC(2, kDebugGraphics, "Palette %x: %d colours", id, count);
	return true;
}

// PIB flags bit 1: a table of h u32 row offsets (each relative to its own entry),
// then RLE rows. Ops: 00-7f run of n+1, 80-ef literal of (n & 7f)+1 (padded to an
// even length from the row start), f0-f3 long run, f4-f7 long literal, f8-fb skip
// (transparent), fc end of row, ff end of image. Otherwise raw 8bpp DWORD-padded rows.
bool MediaManager::decodePib(const byte *data, uint32 size, int32 w, int32 nh, uint32 flags, Pib *pib) {
	int h = ABS(nh);
	pib->surface.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
	pib->flags = flags;
	bool ok = true;
	if (!(flags & 2)) {
		uint32 pitch = (w + 3) & ~3;
		for (int y = 0; y < h; y++) {
			if (y * pitch + w > size) {
				ok = false;
				break;
			}
			memcpy(pib->surface.getBasePtr(0, y), data + y * pitch, w);
		}
	} else {
		pib->mask.resize(w * h);
		memset(pib->mask.data(), 0, w * h);
		memset(pib->surface.getPixels(), 0, w * h);
		for (int y = 0; y < h && ok; y++) {
			if ((uint32)(y + 1) * 4 > size) {
				ok = false;
				break;
			}
			uint32 start = y * 4 + READ_LE_UINT32(data + y * 4);
			uint32 p = start;
			byte *row = (byte *)pib->surface.getBasePtr(0, y);
			byte *mrow = &pib->mask[y * w];
			int x = 0;
			while (true) {
				if (p >= size) {
					ok = false;
					break;
				}
				byte op = data[p];
				if (op == 0xFC || op == 0xFF)
					break;
				int n;
				if (op < 0x80) {
					n = op + 1;
					if (x + n > w || p + 1 >= size) {
						ok = false;
						break;
					}
					memset(row + x, data[p + 1], n);
					memset(mrow + x, 1, n);
					p += 2;
				} else if (op < 0xF0 || (op >= 0xF4 && op < 0xF8)) {
					uint32 src;
					if (op < 0xF0) {
						n = (op & 0x7F) + 1;
						src = p + 1;
					} else {
						n = (((op & 3) << 8) | data[p + 1]) + 1;
						src = p + 2;
					}
					if (x + n > w || src + n > size) {
						ok = false;
						break;
					}
					memcpy(row + x, data + src, n);
					memset(mrow + x, 1, n);
					p = src + n;
					if ((p - start) & 1)
						p++;
				} else if (op < 0xF4) {
					n = (((op & 3) << 8) | data[p + 1]) + 1;
					if (x + n > w || p + 2 >= size) {
						ok = false;
						break;
					}
					memset(row + x, data[p + 2], n);
					memset(mrow + x, 1, n);
					p += 4;
				} else if (op < 0xFC) {
					n = (((op & 3) << 8) | data[p + 1]) + 1;
					p += 2;
				} else {
					ok = false;
					break;
				}
				x += n;
			}
		}
	}
	// A positive height means the rows are stored bottom-up
	if (nh > 0) {
		Common::Array<byte> tmp(w);
		for (int y = 0; y < h / 2; y++) {
			byte *a = (byte *)pib->surface.getBasePtr(0, y), *b = (byte *)pib->surface.getBasePtr(0, h - 1 - y);
			memcpy(tmp.data(), a, w);
			memcpy(a, b, w);
			memcpy(b, tmp.data(), w);
			if (!pib->mask.empty()) {
				memcpy(tmp.data(), &pib->mask[y * w], w);
				memcpy(&pib->mask[y * w], &pib->mask[(h - 1 - y) * w], w);
				memcpy(&pib->mask[(h - 1 - y) * w], tmp.data(), w);
			}
		}
	}
	return ok;
}

// PibList (CMF type 4): u16 1, u32 id, u16 nmap, u16 0, u16 npib; npib x 24-byte
// headers { u32 w, i32 -h, i32 regX, i32 regY, u32 flags, u32 size }; nmap x
// { u16 hold, u16 pib } frame map; then each PIB's data
PibList *MediaManager::loadPibList(uint32 id) {
	Common::ScopedPtr<Common::SeekableReadStream> s(open(id));
	if (!s)
		return nullptr;
	Common::Array<byte> data(s->size());
	s->read(data.data(), data.size());
	if (data.size() < 12)
		return nullptr;
	const byte *d = data.data();
	uint16 nmap = READ_LE_UINT16(d + 6);
	uint16 npib = READ_LE_UINT16(d + 10);
	uint32 p = 12 + 24 * npib + 4 * nmap;
	if (p > data.size())
		return nullptr;

	PibList *list = new PibList();
	list->id = READ_LE_UINT32(d + 2);
	for (uint i = 0; i < nmap; i++) {
		PibList::Frame f;
		f.hold = READ_LE_UINT16(d + 12 + 24 * npib + 4 * i);
		f.pib = READ_LE_UINT16(d + 14 + 24 * npib + 4 * i);
		list->frames.push_back(f);
	}
	bool ok = true;
	for (uint i = 0; i < npib; i++) {
		const byte *hdr = d + 12 + 24 * i;
		int32 w = READ_LE_INT32(hdr), nh = READ_LE_INT32(hdr + 4);
		uint32 flags = READ_LE_UINT32(hdr + 16), size = READ_LE_UINT32(hdr + 20);
		Pib *pib = new Pib();
		pib->regX = READ_LE_INT32(hdr + 8);
		pib->regY = READ_LE_INT32(hdr + 12);
		if (w <= 0 || w > 2048 || ABS(nh) > 2048 || p + size > data.size() ||
				!decodePib(d + p, size, w, nh, flags, pib))
			ok = false;
		list->pibs.push_back(pib);
		p += size;
	}
	if (!ok)
		warning("PibList %x doesn't decode cleanly", id);
	debugC(2, kDebugGraphics, "PibList %x: %d pibs, %d frames", id, npib, nmap);
	return list;
}

const PibList *MediaManager::getPibList(uint32 id) {
	auto it = _cache.find(id);
	if (it != _cache.end())
		return it->_value;
	const MediaEntry *e = find(id);
	PibList *list = (e && (e->type == kMediaPibList || e->type == kMediaPibListLow)) ? loadPibList(id) : nullptr;
	_cache[id] = list;
	return list;
}

} // End of namespace MSPAM
