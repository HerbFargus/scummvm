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

#include "common/archive.h"
#include "common/debug.h"
#include "common/rect.h"
#include "common/substream.h"
#include "common/textconsole.h"

#include "dreamforge/detection.h"
#include "dreamforge/resource.h"

namespace DreamForge {

Resource *readResource(Common::SeekableReadStream *stream) {
	if (!stream)
		return nullptr;
	if (stream->readUint16BE() != MKTAG16('E', 'H')) {
		delete stream;
		return nullptr;
	}
	Resource *res = new Resource();
	res->type = stream->readUint16LE();
	uint16 headerSize = stream->readUint16LE();
	res->header.resize(headerSize);
	if (headerSize)
		stream->read(res->header.data(), headerSize);
	uint32 start = stream->pos();
	res->body = new Common::SeekableSubReadStream(stream, start, stream->size(), DisposeAfterUse::YES);
	return res;
}

ResourceArchive::ResourceArchive() : _stream(nullptr), _end(0) {
}

ResourceArchive::~ResourceArchive() {
	delete _stream;
}

bool ResourceArchive::open(const Common::Path &path) {
	delete _stream;
	_offsets.clear();
	_path = path;
	_stream = SearchMan.createReadStreamForMember(path);
	if (!_stream)
		return false;
	uint32 count = _stream->readUint32LE();
	_offsets.resize(count);
	for (uint32 i = 0; i < count; i++)
		_offsets[i] = _stream->readUint32LE();
	_end = _stream->size();
	debugC(1, kDebugLoading, "%s: %u entries", path.toString().c_str(), count);
	return true;
}

Common::SeekableReadStream *ResourceArchive::createEntryStream(uint index) const {
	if (!_stream || index >= _offsets.size())
		return nullptr;
	uint32 start = _offsets[index];
	uint32 end = index + 1 < _offsets.size() ? _offsets[index + 1] : _end;
	_stream->seek(start);
	return _stream->readStream(end - start);
}

Resource *ResourceArchive::getResource(uint index) const {
	return readResource(createEntryStream(index));
}

uint32 decodeLZSS(Common::ReadStream &in, uint32 inSize, byte *out, uint32 outSize) {
	byte ring[4096];
	memset(ring, 0xFE, sizeof(ring));
	uint r = 0xFEE;
	uint32 written = 0, read = 0;
	uint flags = 0;
	while (written < outSize && read < inSize) {
		flags >>= 1;
		if (!(flags & 0x100)) {
			flags = in.readByte() | 0xFF00;
			read++;
		}
		if (flags & 1) {
			if (read >= inSize)
				break;
			byte b = in.readByte();
			read++;
			out[written++] = b;
			ring[r] = b;
			r = (r + 1) & 0xFFF;
		} else {
			if (read + 2 > inSize)
				break;
			byte lo = in.readByte(), hi = in.readByte();
			read += 2;
			uint pos = ((hi & 0xF0) << 4) | lo;
			uint len = (hi & 0x0F) + 3;
			for (uint k = 0; k < len && written < outSize; k++) {
				byte b = ring[(pos + k) & 0xFFF];
				out[written++] = b;
				ring[r] = b;
				r = (r + 1) & 0xFFF;
			}
		}
	}
	return written;
}

uint32 decodePackBits(Common::ReadStream &in, uint32 inSize, byte *out, uint32 outSize) {
	uint32 written = 0, read = 0;
	while (written < outSize && read < inSize) {
		int8 c = (int8)in.readByte();
		read++;
		if (c < 0) {
			byte b = in.readByte();
			read++;
			for (int k = 0; k < 1 - c && written < outSize; k++)
				out[written++] = b;
		} else {
			for (int k = 0; k <= c && written < outSize && read < inSize; k++, read++)
				out[written++] = in.readByte();
		}
	}
	return written;
}

bool readImageBlock(Common::SeekableReadStream &in, Graphics::Surface &surf, uint16 width, uint16 height) {
	uint32 size = in.readUint32LE();
	uint32 start = in.pos();
	surf.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
	uint32 want = (uint32)width * height;
	uint32 got = decodeLZSS(in, size, (byte *)surf.getPixels(), want);
	in.seek(start + size);
	if (got != want)
		warning("readImageBlock: %u of %u pixels", got, want);
	return got == want;
}

bool Sprite::load(Resource &res) {
	if (res.type != kResSprite || res.header.size() < 10)
		return false;
	const byte *h = res.header.data();
	height = READ_LE_UINT16(h);
	frames = READ_LE_UINT16(h + 2);
	width = READ_LE_UINT16(h + 4);
	uint16 compression = READ_LE_UINT16(h + 6);
	flags = READ_LE_UINT16(h + 8);
	if (!width || !height || !frames)
		return false;

	surface.create(width, height * frames, Graphics::PixelFormat::createFormatCLUT8());
	uint32 want = (uint32)width * height * frames;
	byte *dst = (byte *)surface.getPixels();
	Common::SeekableReadStream &in = *res.body;
	uint32 got;
	switch (compression) {
	case 0:
		got = in.read(dst, want);
		break;
	case 1:
		got = decodePackBits(in, in.size(), dst, want);
		break;
	case 3:
		got = decodeLZSS(in, in.size(), dst, want);
		break;
	default:
		warning("Sprite: unknown compression %d", compression);
		return false;
	}
	if (got != want)
		warning("Sprite: %u of %u pixels", got, want);
	return true;
}

// A strip of frames (the cursors) reads the header the other way round: the first
// field counts the frames and the second is each one's height (0x4e6c0 steps by
// second field x width), so the cursor sheet is 45 frames of 43 x 43
Common::Rect Sprite::frameRect(uint frame) const {
	return Common::Rect(0, frame * frames, width, (frame + 1) * frames);
}

} // End of namespace DreamForge
