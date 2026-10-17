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

#ifndef NIGHTMARENED_RESOURCES_H
#define NIGHTMARENED_RESOURCES_H

#include "common/array.h"
#include "common/stream.h"

#include "nightmarened/data.h"
#include "nightmarened/naf.h"

namespace NightmareNed {

class RdxBackground;
class RdxSprite;
class RdxSurface;
struct RdxEffect;

/**
 * NITENED.EXE itself (from DATA32.Z): the game's static tables (request lists, action
 * tables, room rects) are read from it by virtual address rather than copied.
 */
class ExeImage {
public:
	bool load(Common::SeekableReadStream *stream);
	bool isLoaded() const { return !_data.empty(); }
	uint32 readUint32(uint32 va) const;
	int32 readSint32(uint32 va) const { return (int32)readUint32(va); }
	uint16 readUint16(uint32 va) const;

private:
	const byte *ptr(uint32 va, uint32 size) const;

	struct Section {
		uint32 va, size, offset, rawSize;
	};
	Common::Array<Section> _sections;
	Common::Array<byte> _data;
};

enum {
	kAreaCount = 10,
	kAreaSlots = 500,
	kMsPerStep = 41
};

/** The game's 100-byte sprite slot record (DAT_004afcb0[area] + id * 100) */
struct Slot {
	int x, y;
	uint32 z;
	int16 sequenceLength;
	uint16 flags;          // 1 backwards, 2 no flip effect, 4 placed mirrored
	int16 offsets[6];      // +0x14..+0x1e
	int16 w, h;
	int16 key;
	RdxSprite *sprite;
	RdxEffect *flip;
	Sequence *sequence;
	HitBoxes *boxes;       // shared with the alias source
	Common::Array<SpriteFrame *> frames;
	bool alias, loaded;
	int16 step;
	uint32 deadline;
	Common::Array<byte> sound; // auix blob: WAVEFORMATEX + PCM

	Slot();
	void clear();
};

/** The 0x2c-byte bitmap record */
struct BitmapRec {
	int x, y;
	uint32 z;
	Background bitmap;
	RdxBackground *object;

	BitmapRec() : x(0), y(0), z(0), object(nullptr) {}
	~BitmapRec();
};

/** A 0x24-byte request entry */
struct Request {
	enum Type {
		kSprite = 1,
		kBitmap = 2,
		kCollision = 3,
		kSound = 4
	};

	uint16 flags;
	int32 scene;
	int32 area;  // the transparent colour for bitmaps (-1 = none)
	int32 id;
	int32 type;
	uint32 destVA, paletteVA; // where the original stored its result

	// Results
	BitmapRec *bitmap;
	CollisionMap *collision;
	bool hasPalette;
	byte palette[256 * 3];

	Request() : flags(0), scene(0), area(-1), id(0), type(0), destVA(0), paletteVA(0),
		bitmap(nullptr), collision(nullptr), hasPalette(false) {}
};

class Resources {
public:
	Resources();
	~Resources();

	Slot &getSlot(int area, int id) { return _areas[area][id]; }

	/** n entries at va in NITENED.EXE */
	static Common::Array<Request> readRequests(const ExeImage &exe, uint32 va, int count);
	/** FUN_004586e0 + the loader threads, run synchronously; the scene overrides every entry's */
	bool loadList(NafArchive *naf, Common::Array<Request> &list, int scene);
	bool load(NafArchive *naf, Request &req);
	/** FUN_00458dc0: frees in reverse order */
	void freeList(Common::Array<Request> &list);

private:
	bool loadSprite(NafArchive *naf, Request &req);
	Slot *_areas[kAreaCount];
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_RESOURCES_H
