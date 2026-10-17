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

#include "common/textconsole.h"
#include "graphics/pixelformat.h"

#include "bigtuna/bitmap.h"

namespace BigTuna {

bool Bitmap::load(Common::SeekableReadStream &stream, bool v1) {
	uint16 width = stream.readUint16LE();
	uint16 height = stream.readUint16LE();
	_format = stream.readByte();
	_opaque = stream.readByte() == 1;
	_regX = stream.readSint16LE();
	_regY = stream.readSint16LE();
	if (!v1)
		stream.readUint16LE();

	if (_format == kBitmapRaw16) {
		_surface.create(width, height, Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0));
		for (uint y = 0; y < height; y++) {
			uint16 *row = (uint16 *)_surface.getBasePtr(0, y);
			for (uint x = 0; x < width; x++)
				row[x] = stream.readUint16LE();
		}
		return !stream.err();
	}

	_surface.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
	_mask.resize(width * height);

	switch (_format) {
	case kBitmapRLE:
		if (!decodeRLE(stream))
			return false;
		break;
	case kBitmapRaw8:
		for (uint y = 0; y < height; y++)
			stream.read(_surface.getBasePtr(0, y), width);
		break;
	case kBitmapLZ:
		if (!decodeLZ(stream))
			return false;
		break;
	default:
		warning("Bitmap::load(): unknown format %d", _format);
		return false;
	}

	if (_format != kBitmapRLE) {
		for (uint y = 0; y < height; y++) {
			const byte *src = (const byte *)_surface.getBasePtr(0, y);
			for (uint x = 0; x < width; x++)
				_mask[y * width + x] = src[x] != 0;
		}
	}
	if (_opaque)
		memset(&_mask[0], 1, _mask.size());
	return !stream.err();
}

// u16 width, u16 height, u16 flags (always 0), then per row a u16 byte count and
// 00 = end of row, 01-7F = literal bytes, 80-FF = run of (code & 7F). Index 255
// and pixels a row never reaches are transparent.
bool Bitmap::loadImp(Common::SeekableReadStream &stream) {
	uint16 width = stream.readUint16LE();
	uint16 height = stream.readUint16LE();
	stream.readUint16LE();
	_format = kBitmapRLE;
	_opaque = false;
	_regX = _regY = 0;
	_surface.create(width, height, Graphics::PixelFormat::createFormatCLUT8());
	_surface.fillRect(Common::Rect(width, height), 0);
	_mask.resize(width * height);
	memset(&_mask[0], 0, _mask.size());

	for (int y = 0; y < height; y++) {
		uint16 len = stream.readUint16LE();
		int64 end = stream.pos() + len;
		byte *dst = (byte *)_surface.getBasePtr(0, y);
		int x = 0;
		while (stream.pos() < end) {
			byte code = stream.readByte();
			if (code == 0)
				break;
			int count = code & 0x7F;
			if (x + count > width)
				return false;
			if (code < 0x80)
				stream.read(dst + x, count);
			else
				memset(dst + x, stream.readByte(), count);
			x += count;
		}
		for (int i = 0; i < width; i++)
			_mask[y * width + i] = i < x && dst[i] != 0xFF;
		stream.seek(end);
	}
	return !stream.err();
}

// Per row: 00 = end of row, 01-7F = literal bytes, 80-BF = run of one byte,
// C0-FF = transparent skip; counts are (code & 3F) + 1 for runs and skips.
bool Bitmap::decodeRLE(Common::SeekableReadStream &stream) {
	_surface.fillRect(Common::Rect(_surface.w, _surface.h), 0);
	for (int y = 0; y < _surface.h; y++) {
		byte *dst = (byte *)_surface.getBasePtr(0, y);
		byte *mask = &_mask[y * _surface.w];
		int x = 0;
		for (;;) {
			byte code = stream.readByte();
			if (stream.eos())
				return false;
			if (code == 0)
				break;
			int count;
			if (code < 0x80) {
				count = code;
				if (x + count > _surface.w)
					return false;
				stream.read(dst + x, count);
				memset(mask + x, 1, count);
			} else if (code < 0xC0) {
				count = (code & 0x3F) + 1;
				if (x + count > _surface.w)
					return false;
				memset(dst + x, stream.readByte(), count);
				memset(mask + x, 1, count);
			} else {
				count = (code & 0x3F) + 1;
			}
			x += count;
		}
	}
	return true;
}

// 02 x = literal x; 03-0A d = copy that many bytes from d + 1 back; else literal
bool Bitmap::decodeLZ(Common::SeekableReadStream &stream) {
	byte *dst = (byte *)_surface.getPixels();
	uint32 total = _surface.w * _surface.h;
	uint32 pos = 0;
	while (pos < total) {
		byte code = stream.readByte();
		if (stream.eos())
			return false;
		if (code == 2) {
			dst[pos++] = stream.readByte();
		} else if (code >= 3 && code <= 10) {
			uint32 dist = stream.readByte() + 1;
			if (dist > pos || pos + code > total)
				return false;
			for (uint i = 0; i < code; i++, pos++)
				dst[pos] = dst[pos - dist];
		} else {
			dst[pos++] = code;
		}
	}
	return true;
}

} // End of namespace BigTuna
