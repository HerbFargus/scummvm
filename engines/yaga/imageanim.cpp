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

// zlib.h comes before ScummVM's headers, which forbid some of what it uses
#include "config.h"
#ifdef USE_ZLIB
#include <zlib.h>
#endif

#include "common/debug.h"
#include "common/endian.h"
#include "common/textconsole.h"
#include "graphics/blit.h"

#include "yaga/imageanim.h"

// The games' animation formats. MNG files are a cut-down MNG (one PNG per
// layer, positioned by DEFI, named by tEXt, masked by flAG); RLE files are
// the engine's own run-length format. Both carry several frames of layers.

namespace Yaga {

static const byte kMngSignature[8] = { 0x8a, 'M', 'N', 'G', '\r', '\n', 0x1a, '\n' };
static const byte kRleSignature[8] = { 0xf2, 'e', 'l', 'r', 0, 0, ' ', 'M' };

enum {
	kRleFormatPaletted = 0x40012F9,
	kRleFormatPaletted2 = 0x40012FB,
	kRleFormatBGRA = 0xC0012F9
};

AnimData::~AnimData() {
	for (uint i = 0; i < frames.size(); i++)
		for (uint j = 0; j < frames[i].layers.size(); j++)
			frames[i].layers[j].surface.free();
}

Common::Rect AnimData::bounds() const {
	Common::Rect r;
	bool any = false;
	for (uint i = 0; i < frames.size(); i++) {
		for (uint j = 0; j < frames[i].layers.size(); j++) {
			const AnimLayer &l = frames[i].layers[j];
			if (!l.surface.getPixels() || (l.mask && !(l.mask & 1)))
				continue;
			Common::Rect lr(l.x, l.y, l.x + l.surface.w, l.y + l.surface.h);
			if (any)
				r.extend(lr);
			else
				r = lr;
			any = true;
		}
	}
	return r;
}

// An RGBA palette, four bytes an entry
struct Palette {
	byte rgba[256 * 4];
	Palette() {
		for (int i = 0; i < 256; i++) {
			rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 0;
			rgba[i * 4 + 3] = 0xff;
		}
	}
};

static void createSurface(AnimLayer &layer, int w, int h) {
	layer.surface.create(w, h, Graphics::BlendBlit::getSupportedPixelFormat());
	memset(layer.surface.getPixels(), 0, layer.surface.pitch * h);
}

static inline void putPixel(Graphics::Surface &s, uint32 offset, byte r, byte g, byte b, byte a) {
	((uint32 *)s.getPixels())[offset] = s.format.ARGBToColor(a, r, g, b);
}

// ---- MNG ----------------------------------------------------------------------------

// Inflates as much as there is: 1,568 small sprites ship with a truncated
// zlib stream, and the rest of such an image stays transparent
static uint32 inflatePartial(const Common::Array<byte> &in, byte *out, uint32 outSize) {
#ifdef USE_ZLIB
	z_stream z;
	memset(&z, 0, sizeof(z));
	if (inflateInit(&z) != Z_OK)
		return 0;
	z.next_in = const_cast<byte *>(in.data());
	z.avail_in = in.size();
	z.next_out = out;
	z.avail_out = outSize;
	inflate(&z, Z_SYNC_FLUSH);
	uint32 got = z.total_out;
	inflateEnd(&z);
	return got;
#else
	return 0;
#endif
}

static inline byte paeth(int a, int b, int c) {
	int p = a + b - c;
	int pa = ABS(p - a), pb = ABS(p - b), pc = ABS(p - c);
	if (pa <= pb && pa <= pc)
		return a;
	return pb <= pc ? b : c;
}

// Undoes the PNG row filters in place, leaving rows of stride bytes at raw + 1 + y * (stride + 1)
static bool unfilter(byte *raw, uint32 stride, uint32 h, uint32 bpp) {
	byte *prev = nullptr;
	for (uint32 y = 0; y < h; y++) {
		byte *line = raw + y * (stride + 1);
		byte type = line[0];
		byte *p = line + 1;
		for (uint32 i = 0; i < stride; i++) {
			int left = i >= bpp ? p[i - bpp] : 0;
			int up = prev ? prev[i] : 0;
			int upLeft = (prev && i >= bpp) ? prev[i - bpp] : 0;
			switch (type) {
			case 0: break;
			case 1: p[i] += left; break;
			case 2: p[i] += up; break;
			case 3: p[i] += (left + up) >> 1; break;
			case 4: p[i] += paeth(left, up, upLeft); break;
			default: return false;
			}
		}
		prev = p;
	}
	return true;
}

static void decodePng(AnimLayer &layer, uint32 w, uint32 h, byte colorType, const Common::Array<byte> &zdata, const Palette &pal, const Common::String &path) {
	uint32 bpp = colorType == 2 ? 3 : colorType == 3 ? 1 : colorType == 6 ? 4 : 0;
	if (!bpp || !w || !h) {
		warning("yaga: %s: unsupported PNG colour type %d", path.c_str(), colorType);
		return;
	}
	uint32 need = h * (w * bpp + 1);
	Common::Array<byte> raw;
	raw.resize(need + 16);
	memset(raw.data(), 0, raw.size());
	uint32 got = inflatePartial(zdata, raw.data(), raw.size());
	if (got != need && colorType == 3 && w == 2 && h == 2 && got == 18) {
		// A mislabelled 2x2 RGBA image
		bpp = 4;
		colorType = 6;
		need = 18;
	} else if (got == 0 && zdata.size() >= need) {
		memcpy(raw.data(), zdata.data(), need); // stored uncompressed
	}
	if (!unfilter(raw.data(), w * bpp, h, bpp)) {
		warning("yaga: %s: bad PNG filter", path.c_str());
		return;
	}
	createSurface(layer, w, h);
	for (uint32 y = 0; y < h; y++) {
		const byte *p = raw.data() + y * (w * bpp + 1) + 1;
		for (uint32 x = 0; x < w; x++, p += bpp) {
			if (bpp == 1)
				putPixel(layer.surface, y * w + x, pal.rgba[*p * 4], pal.rgba[*p * 4 + 1], pal.rgba[*p * 4 + 2], pal.rgba[*p * 4 + 3]);
			else
				putPixel(layer.surface, y * w + x, p[0], p[1], p[2], bpp == 4 ? p[3] : 0xff);
		}
	}
}

static AnimData *decodeMng(const byte *data, uint32 size, const Common::String &path) {
	AnimData *anim = new AnimData();
	Palette globalPal, framePal;
	bool inFrames = false;
	int layersHere = 0;
	AnimFrame *frame = nullptr;
	AnimLayer *layer = nullptr;
	uint32 imgW = 0, imgH = 0;
	byte imgColor = 0;
	Common::Array<byte> zdata;

	uint32 pos = 8;
	while (pos + 8 <= size) {
		uint32 len = READ_BE_UINT32(data + pos);
		uint32 tag = READ_BE_UINT32(data + pos + 4);
		const byte *body = data + pos + 8;
		if (pos + 8 + len > size)
			break;
		pos += 12 + len;

		switch (tag) {
		case MKTAG('t', 'R', 'N', 'S'):
			if (len == 256) {
				Palette &p = inFrames ? framePal : globalPal;
				for (int i = 0; i < 256; i++)
					p.rgba[i * 4 + 3] = body[i];
			}
			break;
		case MKTAG('P', 'L', 'T', 'E'):
			if (len == 768) {
				Palette &p = inFrames ? framePal : globalPal;
				for (int i = 0; i < 256; i++) {
					p.rgba[i * 4] = body[i * 3];
					p.rgba[i * 4 + 1] = body[i * 3 + 1];
					p.rgba[i * 4 + 2] = body[i * 3 + 2];
				}
			}
			break;
		case MKTAG('F', 'R', 'A', 'M'):
			if (len == 10)
				inFrames = true;
			else if (layersHere == 0)
				break; // an empty frame
			anim->frames.push_back(AnimFrame());
			frame = &anim->frames.back();
			layersHere = 0;
			layer = nullptr;
			framePal = globalPal;
			break;
		case MKTAG('D', 'E', 'F', 'I'):
			if (len != 12)
				break;
			if (!frame) {
				anim->frames.push_back(AnimFrame());
				frame = &anim->frames.back();
			}
			frame->layers.push_back(AnimLayer());
			layer = &frame->layers.back();
			layer->x = (int32)READ_BE_UINT32(body + 4);
			layer->y = (int32)READ_BE_UINT32(body + 8);
			layersHere++;
			break;
		case MKTAG('t', 'E', 'X', 't'):
			if (layer && len >= 6 && !memcmp(body, "LAYER\0", 6))
				layer->name = Common::String((const char *)body + 6, Common::strnlen((const char *)body + 6, len - 6));
			break;
		case MKTAG('f', 'l', 'A', 'G'):
			if (layer && len == 4)
				layer->mask = READ_LE_UINT32(body);
			break;
		case MKTAG('I', 'H', 'D', 'R'):
			if (len != 13)
				break;
			imgW = READ_BE_UINT32(body);
			imgH = READ_BE_UINT32(body + 4);
			imgColor = body[9];
			if (body[8] != 8 || body[10] || body[11] || body[12])
				warning("yaga: %s: unsupported PNG variant", path.c_str());
			zdata.clear();
			break;
		case MKTAG('I', 'D', 'A', 'T'):
			if (len) {
				uint32 at = zdata.size();
				zdata.resize(at + len);
				memcpy(zdata.data() + at, body, len);
			}
			break;
		case MKTAG('I', 'E', 'N', 'D'):
			if (layer)
				decodePng(*layer, imgW, imgH, imgColor, zdata, framePal, path);
			zdata.clear();
			break;
		case MKTAG('M', 'E', 'N', 'D'):
			pos = size;
			break;
		default:
			break;
		}
	}
	return anim;
}

// ---- RLE ----------------------------------------------------------------------------

static void readBgraPalette(const byte *p, Palette &pal) {
	for (int i = 0; i < 256; i++) {
		pal.rgba[i * 4] = p[i * 4 + 2];
		pal.rgba[i * 4 + 1] = p[i * 4 + 1];
		pal.rgba[i * 4 + 2] = p[i * 4];
		pal.rgba[i * 4 + 3] = p[i * 4 + 3];
	}
}

// Runs: 11xxxxxx transparent, 10xxxxxx one repeated pixel, 0xxxxxxx literal pixels
static void decodeRleLayer(AnimLayer &layer, const byte *p, uint32 size, uint32 format, const Palette &pal) {
	uint32 n = layer.surface.w * layer.surface.h;
	uint32 pixelSize = format == kRleFormatBGRA ? 4 : 1;
	uint32 off = 0;
	const byte *end = p + size;
	while (p < end) {
		byte code = *p++;
		uint32 count = (code & 0x3f) + 1;
		uint32 room = off < n ? MIN(count, n - off) : 0;
		if ((code & 0xc0) == 0xc0) {
			// already transparent
		} else if (code & 0x80) {
			if (p + pixelSize > end)
				break;
			for (uint32 i = 0; i < room; i++) {
				if (pixelSize == 4)
					putPixel(layer.surface, off + i, p[2], p[1], p[0], p[3]);
				else
					putPixel(layer.surface, off + i, pal.rgba[*p * 4], pal.rgba[*p * 4 + 1], pal.rgba[*p * 4 + 2], pal.rgba[*p * 4 + 3]);
			}
			p += pixelSize;
		} else {
			if (p + count * pixelSize > end)
				break;
			for (uint32 i = 0; i < room; i++) {
				const byte *q = p + i * pixelSize;
				if (pixelSize == 4)
					putPixel(layer.surface, off + i, q[2], q[1], q[0], q[3]);
				else
					putPixel(layer.surface, off + i, pal.rgba[*q * 4], pal.rgba[*q * 4 + 1], pal.rgba[*q * 4 + 2], pal.rgba[*q * 4 + 3]);
			}
			p += count * pixelSize;
		}
		off += count;
	}
}

static AnimData *decodeRle(const byte *data, uint32 size, const Common::String &path) {
	AnimData *anim = new AnimData();
	if (size < 16)
		return anim;
	uint32 frameCount = READ_LE_UINT32(data + 8);
	uint32 flags = READ_LE_UINT32(data + 12);
	uint32 pos = 16;
	Palette globalPal;
	if (flags & 1) {
		if (pos + 1024 > size)
			return anim;
		readBgraPalette(data + pos, globalPal);
		pos += 1024;
	}
	for (uint32 f = 0; f < frameCount && pos + 16 <= size; f++) {
		uint32 layerCount = READ_LE_UINT32(data + pos + 12);
		pos += 16;
		anim->frames.push_back(AnimFrame());
		AnimFrame &frame = anim->frames.back();
		for (uint32 l = 0; l < layerCount; l++) {
			if (pos + 16 + 0x39 + 4 + 7 + 24 > size) {
				warning("yaga: %s is truncated", path.c_str());
				return anim;
			}
			frame.layers.push_back(AnimLayer());
			AnimLayer &layer = frame.layers.back();
			layer.x = (int)READ_LE_FLOAT32(data + pos);
			layer.y = (int)READ_LE_FLOAT32(data + pos + 4);
			layer.mask = READ_LE_UINT32(data + pos + 12);
			pos += 16;
			layer.name = Common::String((const char *)data + pos, Common::strnlen((const char *)data + pos, 0x39));
			pos += 0x39;
			if (memcmp(data + pos, "rle\0", 4)) {
				warning("yaga: %s: missing rle tag at 0x%x", path.c_str(), pos);
				return anim;
			}
			pos += 4;
			uint32 format = READ_LE_UINT32(data + pos);
			pos += 7; // the format and three bytes of padding
			uint32 w = READ_LE_UINT32(data + pos);
			uint32 h = READ_LE_UINT32(data + pos + 4);
			uint32 layerFlags = READ_LE_UINT32(data + pos + 8);
			uint32 dataSize = READ_LE_UINT32(data + pos + 20);
			pos += 24;
			Palette layerPal = globalPal;
			if (layerFlags & 1) {
				if (pos + 1024 > size)
					return anim;
				readBgraPalette(data + pos, layerPal);
				pos += 1024;
			}
			if (pos + dataSize > size) {
				warning("yaga: %s is truncated", path.c_str());
				return anim;
			}
			if (format == kRleFormatPaletted || format == kRleFormatPaletted2 || format == kRleFormatBGRA) {
				if (w && h && w < 4096 && h < 4096) {
					createSurface(layer, w, h);
					decodeRleLayer(layer, data + pos, dataSize, format, layerPal);
				}
			} else {
				warning("yaga: %s: unsupported pixel format 0x%x", path.c_str(), format);
			}
			pos += dataSize;
		}
	}
	return anim;
}

AnimData *decodeAnim(const byte *data, uint32 size, const Common::String &path) {
	if (size >= 8 && !memcmp(data, kMngSignature, 8))
		return decodeMng(data, size, path);
	if (size >= 8 && !memcmp(data, kRleSignature, 8))
		return decodeRle(data, size, path);
	return nullptr;
}

} // End of namespace Yaga
