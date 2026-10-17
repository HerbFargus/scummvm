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

#include "dreamforge/detection.h"
#include "dreamforge/dfa.h"

namespace DreamForge {

namespace {

// Bounds-checked reader over one chunk's bytes
struct ByteReader {
	const byte *p, *end;
	ByteReader(const byte *data, uint32 size) : p(data), end(data + size) {}
	uint32 left() const { return end - p; }
	byte u8() { return p < end ? *p++ : 0; }
	uint16 u16() { uint16 v = u8(); return v | (u8() << 8); }
	uint32 u32() { uint32 v = u16(); return v | ((uint32)u16() << 16); }
	byte peek() const { return p < end ? *p : 0; }
	uint32 read(byte *dst, uint32 n) {
		n = MIN(n, left());
		memcpy(dst, p, n);
		p += n;
		return n;
	}
};

// av_memcpy_backptr: copy count bytes from back bytes behind dst, overlapping
void copyBack(byte *dst, uint back, uint count) {
	const byte *src = dst - back;
	for (uint i = 0; i < count; i++)
		dst[i] = src[i];
}

bool decodeCopy(ByteReader &gb, byte *frame, int width, int height) {
	uint32 size = width * height;
	return gb.read(frame, size) == size;
}

bool decodeTSW1(ByteReader &gb, byte *frame, int width, int height) {
	const byte *frameStart = frame;
	const byte *frameEnd = frame + width * height;
	uint mask = 0x10000, bitbuf = 0;

	uint32 segments = gb.u32();
	uint32 offset = gb.u32();
	if (segments == 0 && offset == (uint32)(frameEnd - frame))
		return true;  // skip frame
	if ((uint32)(frameEnd - frame) <= offset)
		return false;
	frame += offset;
	while (segments--) {
		if (gb.left() < 2)
			return false;
		if (mask == 0x10000) {
			bitbuf = gb.u16();
			mask = 1;
		}
		if (frameEnd - frame < 2)
			return false;
		if (bitbuf & mask) {
			uint v = gb.u16();
			uint back = (v & 0x1FFF) << 1;
			uint count = ((v >> 13) + 2) << 1;
			if ((uint)(frame - frameStart) < back || (uint)(frameEnd - frame) < count)
				return false;
			copyBack(frame, back, count);
			frame += count;
		} else {
			*frame++ = gb.u8();
			*frame++ = gb.u8();
		}
		mask <<= 1;
	}
	return true;
}

bool decodeDSW1(ByteReader &gb, byte *frame, int width, int height) {
	const byte *frameStart = frame;
	const byte *frameEnd = frame + width * height;
	uint mask = 0x10000, bitbuf = 0;

	int segments = gb.u16();
	while (segments--) {
		if (gb.left() < 2)
			return false;
		if (mask == 0x10000) {
			bitbuf = gb.u16();
			mask = 1;
		}
		if (frameEnd - frame < 2)
			return false;
		if (bitbuf & mask) {
			uint v = gb.u16();
			uint back = (v & 0x1FFF) << 1;
			uint count = ((v >> 13) + 2) << 1;
			if ((uint)(frame - frameStart) < back || (uint)(frameEnd - frame) < count)
				return false;
			copyBack(frame, back, count);
			frame += count;
		} else if (bitbuf & (mask << 1)) {
			uint skip = gb.u16();
			if ((uint)(frameEnd - frame) < skip)
				return false;
			frame += skip;
		} else {
			*frame++ = gb.u8();
			*frame++ = gb.u8();
		}
		mask <<= 2;
	}
	return true;
}

bool decodeDDS1(ByteReader &gb, byte *frame, int width, int height) {
	const byte *frameStart = frame;
	const byte *frameEnd = frame + width * height;
	uint mask = 0x10000, bitbuf = 0;

	if ((width | height) & 1)
		return false;
	int segments = gb.u16();
	while (segments--) {
		if (gb.left() < 2)
			return false;
		if (mask == 0x10000) {
			bitbuf = gb.u16();
			mask = 1;
		}
		if (bitbuf & mask) {
			uint v = gb.u16();
			uint back = (v & 0x1FFF) << 2;
			uint count = ((v >> 13) + 2) << 1;
			if ((uint)(frame - frameStart) < back || frameEnd - frame < (int)(count * 2 + width))
				return false;
			for (uint i = 0; i < count; i++) {
				frame[0] = frame[1] = frame[width] = frame[width + 1] = frame[-(int)back];
				frame += 2;
			}
		} else if (bitbuf & (mask << 1)) {
			uint skip = gb.u16() * 2;
			if ((uint)(frameEnd - frame) < skip)
				return false;
			frame += skip;
		} else {
			if (width < 4 || frameEnd - frame < width + 4)
				return false;
			frame[0] = frame[1] = frame[width] = frame[width + 1] = gb.u8();
			frame += 2;
			frame[0] = frame[1] = frame[width] = frame[width + 1] = gb.u8();
			frame += 2;
		}
		mask <<= 2;
	}
	return true;
}

bool decodeBDLT(ByteReader &gb, byte *frame, int width, int height) {
	int count = gb.u16();
	if (count >= height)
		return false;
	frame += width * count;
	int lines = gb.u16();
	if (count + lines > height)
		return false;

	while (lines--) {
		if (gb.left() < 1)
			return false;
		byte *linePtr = frame;
		frame += width;
		int segments = gb.u8();
		while (segments--) {
			if (frame - linePtr <= gb.peek())
				return false;
			linePtr += gb.u8();
			count = (int8)gb.u8();
			if (count >= 0) {
				if (frame - linePtr < count || (int)gb.read(linePtr, count) != count)
					return false;
			} else {
				count = -count;
				if (frame - linePtr < count)
					return false;
				memset(linePtr, gb.u8(), count);
			}
			linePtr += count;
		}
	}
	return true;
}

bool decodeWDLT(ByteReader &gb, byte *frame, int width, int height) {
	const byte *frameEnd = frame + width * height;
	int y = 0;

	int lines = gb.u16();
	if (lines > height)
		return false;

	while (lines--) {
		if (gb.left() < 2)
			return false;
		uint segments = gb.u16();
		while ((segments & 0xC000) == 0xC000) {
			uint skipLines = -(int16)segments;
			int64 delta = -((int16)segments * (int64)width);
			if (frameEnd - frame <= delta || y + lines + (int)skipLines > height)
				return false;
			frame += delta;
			y += skipLines;
			segments = gb.u16();
		}
		if (frameEnd <= frame)
			return false;
		if (segments & 0x8000) {
			frame[width - 1] = segments & 0xFF;
			segments = gb.u16();
		}
		byte *linePtr = frame;
		if (frameEnd - frame < width)
			return false;
		frame += width;
		y++;
		while (segments--) {
			if (frame - linePtr <= gb.peek())
				return false;
			linePtr += gb.u8();
			int count = (int8)gb.u8();
			if (count >= 0) {
				if (frame - linePtr < count * 2 || (int)gb.read(linePtr, count * 2) != count * 2)
					return false;
				linePtr += count * 2;
			} else {
				count = -count;
				if (frame - linePtr < count * 2)
					return false;
				uint16 v = gb.u16();
				for (int i = 0; i < count; i++) {
					WRITE_LE_UINT16(linePtr, v);
					linePtr += 2;
				}
			}
		}
	}
	return true;
}

bool decodeTDLT(ByteReader &gb, byte *frame, int width, int height) {
	const byte *frameEnd = frame + width * height;
	uint32 segments = gb.u32();
	while (segments--) {
		if (gb.left() < 2)
			return false;
		int copy = gb.u8() * 2;
		int skip = gb.u8() * 2;
		if (frameEnd - frame < copy + skip || (int)gb.left() < copy)
			return false;
		frame += skip;
		gb.read(frame, copy);
		frame += copy;
	}
	return true;
}

bool decodeBLCK(ByteReader &gb, byte *frame, int width, int height) {
	memset(frame, 0, width * height);
	return true;
}

typedef bool (*ChunkDecoder)(ByteReader &gb, byte *frame, int width, int height);

// Chunk types 2..9
const ChunkDecoder chunkDecoders[8] = {
	decodeCopy, decodeTSW1, decodeBDLT, decodeWDLT, decodeTDLT, decodeDSW1, decodeBLCK, decodeDDS1
};

} // End of anonymous namespace

DFADecoder::DFADecoder() : _stream(nullptr), _version(0), _frameCount(0), _width(0), _height(0),
	_frameTime(100), _curFrame(-1), _paletteDirty(false) {
	memset(_palette, 0, sizeof(_palette));
}

DFADecoder::~DFADecoder() {
	close();
}

void DFADecoder::close() {
	delete _stream;
	_stream = nullptr;
	_surface.free();
	_curFrame = -1;
	_frameCount = 0;
}

bool DFADecoder::load(Common::SeekableReadStream *stream) {
	close();
	if (!stream)
		return false;
	if (stream->readUint32BE() != MKTAG('D', 'F', 'I', 'A')) {
		delete stream;
		return false;
	}
	_stream = stream;
	_version = _stream->readUint16LE();
	_frameCount = _stream->readUint16LE();
	_width = _stream->readUint16LE();
	_height = _stream->readUint16LE();
	_frameTime = _stream->readUint32LE();
	if (!_frameTime)
		_frameTime = 100;
	if (_version == 0x100)
		warning("DFA: interlaced version 0x100 is not supported");
	_surface.create(_width, _height, Graphics::PixelFormat::createFormatCLUT8());
	_surface.fillRect(Common::Rect(_width, _height), 0);
	rewind();
	debugC(2, kDebugVideo, "DFA %dx%d, %d frames, %u ms/frame", _width, _height, _frameCount, _frameTime);
	return true;
}

void DFADecoder::rewind() {
	if (_stream)
		_stream->seek(0x80);
	_curFrame = -1;
}

bool DFADecoder::decodeChunk(uint32 type, Common::SeekableReadStream &chunk) {
	uint32 size = chunk.size();
	byte *data = (byte *)malloc(size ? size : 1);
	chunk.read(data, size);
	bool ok = true;
	if (type == 1) {
		// PAL1: 6-bit VGA values, widened as FFmpeg does
		uint n = MIN<uint>(size / 3, 256);
		for (uint i = 0; i < n * 3; i++) {
			byte c = data[i] << 2;
			_palette[i] = c | (c >> 6);
		}
		_paletteDirty = true;
	} else if (type >= 2 && type <= 9) {
		ByteReader gb(data, size);
		ok = chunkDecoders[type - 2](gb, (byte *)_surface.getPixels(), _width, _height);
	} else {
		warning("DFA: unknown chunk type %u", type);
	}
	free(data);
	return ok;
}

const Graphics::Surface *DFADecoder::decodeNextFrame() {
	if (!_stream || _curFrame + 1 >= _frameCount)
		return nullptr;
	while (!_stream->eos() && _stream->pos() + 12 <= _stream->size()) {
		_stream->skip(4);  // tag
		uint32 size = _stream->readUint32LE();
		uint32 type = _stream->readUint32LE();
		if (type == 0) {  // EOFR
			_stream->skip(size);
			break;
		}
		Common::SeekableReadStream *chunk = _stream->readStream(size);
		bool ok = decodeChunk(type, *chunk);
		delete chunk;
		if (!ok) {
			warning("DFA: bad chunk type %u in frame %d", type, _curFrame + 1);
			return nullptr;
		}
	}
	_curFrame++;
	return &_surface;
}

} // End of namespace DreamForge
