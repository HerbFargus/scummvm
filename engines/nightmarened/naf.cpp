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
#include "common/endian.h"
#include "common/memstream.h"
#include "common/textconsole.h"

#include "nightmarened/detection.h"
#include "nightmarened/naf.h"

namespace NightmareNed {

static const uint32 kIndexTags[kResTypeCount] = {
	MKTAG('x', 's', 'i', 'x'), MKTAG('d', 's', 'i', 'x'), MKTAG('s', 'p', 'i', 'x'),
	MKTAG('b', 'm', 'i', 'x'), MKTAG('c', 'b', 'i', 'x'), MKTAG('a', 'u', 'i', 'x')
};

static uint32 resKey(int scene, int id) {
	return ((uint32)scene << 16) | (uint16)id;
}

NafArchive::NafArchive() : _stream(nullptr), _firstScene(0), _lastScene(-1) {
}

NafArchive::~NafArchive() {
	delete _stream;
}

bool NafArchive::open(Common::SeekableReadStream *stream, const Common::String &name) {
	delete _stream;
	_stream = stream;
	_name = name;
	if (!_stream)
		return false;

	if (_stream->readUint32BE() != MKTAG('R', 'I', 'F', 'F'))
		return false;
	uint32 end = MIN<uint32>(_stream->readUint32LE() + 8, _stream->size());
	if (_stream->readUint32BE() != MKTAG('N', 'N', 'A', 'F'))
		return false;

	bool haveHead = false;
	while (_stream->pos() + 8 <= (int64)end) {
		uint32 tag = _stream->readUint32BE();
		uint32 size = _stream->readUint32LE();
		int64 next = _stream->pos() + size + (size & 1);

		if (tag == MKTAG('h', 'e', 'a', 'd')) {
			int64 start = _stream->pos();
			_stream->skip(8);
			_firstScene = _stream->readSint32LE();
			_lastScene = _stream->readSint32LE();
			_stream->seek(start + 0x98);
			uint32 count = _stream->readUint32LE();
			for (uint32 i = 0; i < count; i++)
				_scenes.push_back(_stream->readUint32LE());
			haveHead = true;
		} else {
			for (int t = 0; t < kResTypeCount; t++) {
				if (tag == kIndexTags[t] && haveHead && !readIndex((ResType)t, size))
					return false;
			}
		}
		_stream->seek(next);
	}

	debugC(1, kDebugLoading, "NAF %s: scenes %d..%d, %d listed, %d sprites, %d backgrounds, %d sounds",
		_name.c_str(), _firstScene, _lastScene, _scenes.size(), _index[kResSprite].slots.size(),
		_index[kResBackground].slots.size(), _index[kResSound].slots.size());
	return haveHead;
}

// u32 slots; i32 first[n]; i32 last[n]; {u32 offset, u32 size}[slots];
// then per listed scene with first >= 0: u16 slot[last - first + 1] (0xFFFF = none)
bool NafArchive::readIndex(ResType type, uint32 size) {
	Index &index = _index[type];
	int sceneCount = _lastScene - _firstScene + 1;
	if (sceneCount <= 0)
		return false;

	uint32 slots = _stream->readUint32LE();
	Common::Array<int32> first(sceneCount), last(sceneCount);
	for (int i = 0; i < sceneCount; i++)
		first[i] = _stream->readSint32LE();
	for (int i = 0; i < sceneCount; i++)
		last[i] = _stream->readSint32LE();
	index.offsets.resize(slots);
	index.sizes.resize(slots);
	for (uint32 i = 0; i < slots; i++) {
		index.offsets[i] = _stream->readUint32LE();
		index.sizes[i] = _stream->readUint32LE();
	}
	for (uint i = 0; i < _scenes.size(); i++) {
		int s = _scenes[i] - _firstScene;
		if (s < 0 || s >= sceneCount || first[s] < 0)
			continue;
		for (int id = first[s]; id <= last[s]; id++) {
			uint16 slot = _stream->readUint16LE();
			if (slot != 0xFFFF && slot < slots)
				index.slots[resKey(_scenes[i], id)] = slot;
		}
	}
	return !_stream->err();
}

bool NafArchive::hasResource(ResType type, int scene, int id) const {
	return _index[type].slots.contains(resKey(scene, id));
}

Common::SeekableReadStream *NafArchive::getResource(ResType type, int scene, int id) const {
	const Index &index = _index[type];
	if (!index.slots.contains(resKey(scene, id)))
		return nullptr;
	uint16 slot = index.slots[resKey(scene, id)];
	if (!index.sizes[slot])
		return nullptr;
	_stream->seek(index.offsets[slot]);
	return _stream->readStream(index.sizes[slot]);
}

Common::Array<ResKey> NafArchive::listResources(ResType type) const {
	Common::Array<uint32> keys;
	for (auto &i : _index[type].slots)
		keys.push_back(i._key);
	Common::sort(keys.begin(), keys.end());
	Common::Array<ResKey> list;
	for (uint32 k : keys) {
		ResKey r = { (int)(k >> 16), (int)(k & 0xFFFF) };
		list.push_back(r);
	}
	return list;
}

// MSB-first codes from 9 bits; 0x100 ends, 0x101 widens the code, 0x102 resets the table
bool decodeLZW(const byte *src, uint32 srcSize, Common::Array<byte> &dst, uint32 maxSize) {
	enum { kEnd = 0x100, kWiden = 0x101, kReset = 0x102, kFirst = 0x103, kMaxCodes = 0x10000 };
	Common::Array<uint16> prefix(kMaxCodes);
	Common::Array<byte> suffix(kMaxCodes), stack(kMaxCodes);
	uint32 pos = 0, bitBuf = 0;
	int bitCount = 0;
	dst.clear();

	auto readCode = [&](int width) -> uint32 {
		while (bitCount < width) {
			bitBuf = (bitBuf << 8) | (pos < srcSize ? src[pos] : 0);
			pos++;
			bitCount += 8;
		}
		bitCount -= width;
		uint32 code = bitBuf >> bitCount;
		bitBuf &= (1 << bitCount) - 1;
		return code;
	};

	for (;;) {
		uint32 next = kFirst;
		int width = 9;
		uint32 code = readCode(width);
		if (code == kEnd)
			return true;
		if (code > 0xFF)
			return false;
		dst.push_back(code);
		uint32 prev = code;
		byte firstChar = code;

		for (;;) {
			if (pos > srcSize + 4)
				return false;
			code = readCode(width);
			if (code == kEnd || dst.size() >= maxSize)
				return true;
			if (code == kReset)
				break;
			if (code == kWiden) {
				if (++width > 16)
					return false;
				continue;
			}

			// Unwind the string for code (or prev + its first char when code is new)
			uint32 sp = 0, c = code;
			if (code >= next) {
				if (code != next)
					return false;
				stack[sp++] = firstChar;
				c = prev;
			}
			while (c > 0xFF) {
				stack[sp++] = suffix[c];
				c = prefix[c];
			}
			stack[sp++] = c;
			firstChar = c;
			while (sp)
				dst.push_back(stack[--sp]);

			if (next < kMaxCodes) {
				prefix[next] = prev;
				suffix[next] = firstChar;
				next++;
			}
			prev = code;
		}
	}
}

// 'cC' u16 1, kind FourCC ('TRPS' sprite bank, 'KCAB' background), u16 na, u16 nb,
// codec FourCC, u32 header size, u32 header offset, u32 data offset, {u32 off, u32 size}
static bool readContainer(const byte *blob, uint32 size, Common::Array<byte> &header, Common::Array<byte> &payload) {
	if (size < 0x24 || READ_LE_UINT16(blob) != MKTAG16('C', 'c'))
		return false;
	uint32 codec = READ_LE_UINT32(blob + 12);
	uint32 hdrSize = READ_LE_UINT32(blob + 0x10), hdrOff = READ_LE_UINT32(blob + 0x14);
	uint32 dataOff = READ_LE_UINT32(blob + 0x1c), dataSize = READ_LE_UINT32(blob + 0x20);
	if (hdrOff + hdrSize > size || dataOff > size)
		return false;
	// Sprite banks count their 4-byte size prefix in dataSize
	dataSize = MIN(dataSize, size - dataOff);
	header.resize(hdrSize);
	memcpy(header.data(), blob + hdrOff, hdrSize);
	if (codec == MKTAG('L', 'Z', '7', '8'))
		return decodeLZW(blob + dataOff, dataSize, payload, 64 * 1024 * 1024);
	if (codec == MKTAG('T', 'I', 'B', '9')) {
		warning("NAF: unsupported 9BIT codec");
		return false;
	}
	payload.resize(dataSize);
	memcpy(payload.data(), blob + dataOff, dataSize);
	return true;
}

static Common::Array<byte> readAll(Common::SeekableReadStream &stream) {
	Common::Array<byte> data(stream.size());
	if (!data.empty())
		stream.read(data.data(), data.size());
	return data;
}

// Header: u16 w, h, bpp, ?, ?; then 256 x (r, g, b, flags) when the background carries a palette
bool Background::load(Common::SeekableReadStream &stream) {
	Common::Array<byte> blob = readAll(stream), header, pixels;
	if (!readContainer(blob.data(), blob.size(), header, pixels) || header.size() < 6)
		return false;
	uint16 w = READ_LE_UINT16(header.data()), h = READ_LE_UINT16(header.data() + 2);
	if (pixels.size() < (uint32)w * h)
		return false;
	surface.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
	for (int y = 0; y < h; y++)
		memcpy(surface.getBasePtr(0, y), pixels.data() + y * w, w);
	hasPalette = header.size() >= 10 + 1024;
	memset(palette, 0, sizeof(palette));
	if (hasPalette) {
		for (int i = 0; i < 256; i++)
			memcpy(palette + i * 3, header.data() + 10 + i * 4, 3);
	}
	return true;
}

static const byte kRdxGuid[16] = {
	0x80, 0x5a, 0x8a, 0x9c, 0x18, 0x37, 0xd0, 0x11, 0xbf, 0x20, 0x00, 0xa0, 0xc9, 0x1a, 0x26, 0x29
};

// u32 unpacked size, then a 'cC' bank whose payload is u32 size[n] + n RDX compressed images
bool loadSpriteBank(Common::SeekableReadStream &stream, Common::Array<SpriteFrame *> &frames) {
	Common::Array<byte> blob = readAll(stream), header, payload;
	if (blob.size() < 0x28 || !readContainer(blob.data() + 4, blob.size() - 4, header, payload))
		return false;

	uint32 first = 0;
	while (first + 16 <= payload.size() && memcmp(payload.data() + first, kRdxGuid, 16))
		first += 4;
	if (first + 16 > payload.size())
		return false;
	uint32 count = first / 4, pos = first;
	for (uint32 i = 0; i < count; i++) {
		uint32 size = READ_LE_UINT32(payload.data() + i * 4);
		if (pos + size > payload.size())
			return false;
		SpriteFrame *frame = new SpriteFrame();
		if (!decodeRdxImage(payload.data() + pos, size, *frame)) {
			delete frame;
			return false;
		}
		frames.push_back(frame);
		pos += size;
	}
	return true;
}

// GUID, u32 w, h, bpp, ?, size, size, rows; opcodes from +44:
// 00 n skip | 01 n c c c c run | 02 n px[n] literal | 03 n k col[k] mask.. 00 masked run
// (LSB-first bits pick opaque pixels) | 04 end of row | 05 end of image
bool decodeRdxImage(const byte *data, uint32 size, SpriteFrame &frame) {
	if (size < 45 || memcmp(data, kRdxGuid, 16))
		return false;
	uint32 w = READ_LE_UINT32(data + 16), h = READ_LE_UINT32(data + 20);
	if (!w || !h || w > 8192 || h > 8192)
		return false;
	frame.surface.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
	memset(frame.surface.getPixels(), 0, frame.surface.pitch * h);
	frame.mask.resize(w * h);
	memset(frame.mask.data(), 0, w * h);

	uint32 p = 44, x = 0, y = 0;
	auto put = [&](byte c) {
		if (x < w && y < h) {
			*(byte *)frame.surface.getBasePtr(x, y) = c;
			frame.mask[y * w + x] = 1;
		}
		x++;
	};

	while (p < size) {
		byte op = data[p];
		if (op == 5)
			return y == h;
		if (op == 4) {
			if (x != w)
				return false;
			x = 0;
			y++;
			p++;
			continue;
		}
		if (p + 2 > size)
			return false;
		uint32 n = data[p + 1];
		switch (op) {
		case 0:
			x += n;
			p += 2;
			break;
		case 1:
			if (p + 6 > size)
				return false;
			for (uint32 i = 0; i < n; i++)
				put(data[p + 2]);
			p += 6;
			break;
		case 2:
			if (p + 2 + n > size)
				return false;
			for (uint32 i = 0; i < n; i++)
				put(data[p + 2 + i]);
			p += 2 + n;
			break;
		case 3: {
			if (p + 3 > size)
				return false;
			uint32 k = data[p + 2], colors = p + 3, mask = colors + k, end = mask, used = 0;
			while (end < size && data[end])
				end++;
			if (end >= size)
				return false;
			for (uint32 i = 0; i < n; i++) {
				uint32 b = mask + i / 8;
				if (b < end && (data[b] >> (i % 8)) & 1 && used < k)
					put(data[colors + used++]);
				else
					x++;
			}
			p = end + 1;
			break;
		}
		default:
			return false;
		}
	}
	return false;
}

} // End of namespace NightmareNed
