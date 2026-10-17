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

#ifndef NIGHTMARENED_NAF_H
#define NIGHTMARENED_NAF_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/stream.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace NightmareNed {

/** Resource kinds, named after their NAF index chunk */
enum ResType {
	kResSequence,   // xsix -> 'yX' animation sequences
	kResHitBoxes,   // dsix -> 'eD' per-frame hit boxes
	kResSprite,     // spix -> 'cC' sprite banks
	kResBackground, // bmix -> 'cC' backgrounds
	kResCollision,  // cbix -> 'BC' level collision rects
	kResSound,      // auix -> WAVEFORMATEX + PCM
	kResTypeCount
};

struct ResKey {
	int scene;
	int id;
};

/**
 * A NAF archive (RIFF form 'NNAF', NITENED.EXE 0x456600). Resources are
 * addressed by (scene, id); index offsets are absolute file offsets.
 */
class NafArchive {
public:
	NafArchive();
	~NafArchive();

	bool open(Common::SeekableReadStream *stream, const Common::String &name);
	const Common::String &getName() const { return _name; }

	bool hasResource(ResType type, int scene, int id) const;
	/** The raw resource bytes, or nullptr */
	Common::SeekableReadStream *getResource(ResType type, int scene, int id) const;
	/** Every (scene, id) of a type, sorted */
	Common::Array<ResKey> listResources(ResType type) const;

	const Common::Array<int> &getScenes() const { return _scenes; }

private:
	struct Index {
		Common::Array<uint32> offsets;
		Common::Array<uint32> sizes;
		Common::HashMap<uint32, uint16> slots; // (scene << 16 | id) -> slot
	};

	bool readIndex(ResType type, uint32 size);

	Common::SeekableReadStream *_stream;
	Common::String _name;
	int _firstScene, _lastScene;
	Common::Array<int> _scenes;
	Index _index[kResTypeCount];
};

/** LZW as used by the 'LZ78' codec (NITENED.EXE 0x48a750). Returns false on bad data. */
bool decodeLZW(const byte *src, uint32 srcSize, Common::Array<byte> &dst, uint32 maxSize);

/** An 8-bit image with its own opacity mask (RDX compressed images have no key colour) */
struct SpriteFrame {
	Graphics::Surface surface;
	Common::Array<byte> mask; // w*h, nonzero = opaque

	~SpriteFrame() { surface.free(); }
	bool isOpaque(int x, int y) const { return mask[y * surface.w + x] != 0; }
};

/** A bmix 'cC' background: 8-bit pixels plus (usually) its palette */
struct Background {
	Graphics::Surface surface;
	byte palette[256 * 3];
	bool hasPalette;

	Background() : hasPalette(false) {}
	~Background() { surface.free(); }
	bool load(Common::SeekableReadStream &stream);
};

/** Decodes every frame of a spix sprite bank. Frames are owned by the caller. */
bool loadSpriteBank(Common::SeekableReadStream &stream, Common::Array<SpriteFrame *> &frames);

/** Decodes one RDX compressed image (Dino2d.dll 0x100018f0 opcodes) */
bool decodeRdxImage(const byte *data, uint32 size, SpriteFrame &frame);

} // End of namespace NightmareNed

#endif // NIGHTMARENED_NAF_H
