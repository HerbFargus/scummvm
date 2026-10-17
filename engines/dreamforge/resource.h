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

#ifndef DREAMFORGE_RESOURCE_H
#define DREAMFORGE_RESOURCE_H

#include "common/array.h"
#include "common/path.h"
#include "common/stream.h"
#include "graphics/surface.h"

namespace DreamForge {

// EH resource types (vge.c)
enum ResourceType {
	kResSprite = 1,
	kResTable = 2,
	kResSound = 4,
	kResScene = 5,
	kResAnimation = 6,
	kResLipSync = 8
};

/**
 * An EH resource: 'EH', uint16 type, uint16 header length, the type's header,
 * then the body. The stream is positioned at the start of the body.
 */
struct Resource {
	uint16 type;
	Common::Array<byte> header;
	Common::SeekableReadStream *body;

	Resource() : type(0), body(nullptr) {}
	~Resource() { delete body; }
};

/** Reads an EH header from the stream; takes ownership of it. Returns nullptr if it isn't one. */
Resource *readResource(Common::SeekableReadStream *stream);

/** A .USR archive: uint32 count, count offsets; entry i runs to entry i + 1 (or the end). */
class ResourceArchive {
public:
	ResourceArchive();
	~ResourceArchive();

	bool open(const Common::Path &path);
	uint size() const { return _offsets.size(); }
	const Common::Path &getPath() const { return _path; }

	/** The raw bytes of entry i */
	Common::SeekableReadStream *createEntryStream(uint index) const;
	/** Entry i as an EH resource */
	Resource *getResource(uint index) const;

private:
	Common::Path _path;
	Common::SeekableReadStream *_stream;
	Common::Array<uint32> _offsets;
	uint32 _end;
};

/**
 * Okumura LZSS (C.PRG 0x58aa0): 4 KB ring pre-filled with 0xFE, writing from
 * 0xFEE; a flag bit of 1 is a literal, otherwise a 12-bit position and 4-bit length (+3).
 * Returns the number of bytes written.
 */
uint32 decodeLZSS(Common::ReadStream &in, uint32 inSize, byte *out, uint32 outSize);

/** PackBits (C.PRG 0x50060): n < 0 repeats the next byte 1 - n times, else copies n + 1 bytes. */
uint32 decodePackBits(Common::ReadStream &in, uint32 inSize, byte *out, uint32 outSize);

/** Scene image block (C.PRG 0x1d690): uint32 compressed size, then an LZSS stream. */
bool readImageBlock(Common::SeekableReadStream &in, Graphics::Surface &surf, uint16 width, uint16 height);

/**
 * A type 1 sprite (sprite.c). The header is height, frames, width, compression,
 * flags; the frames are stacked vertically in the surface.
 */
struct Sprite {
	uint16 width, height, frames, flags;
	Graphics::Surface surface;

	Sprite() : width(0), height(0), frames(0), flags(0) {}
	~Sprite() { surface.free(); }

	bool load(Resource &res);
	/** Frame n of a strip; there are 'height' frames of 'frames' rows each */
	Common::Rect frameRect(uint frame) const;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_RESOURCE_H
