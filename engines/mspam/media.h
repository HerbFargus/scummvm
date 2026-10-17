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

#ifndef MSPAM_MEDIA_H
#define MSPAM_MEDIA_H

#include "common/array.h"
#include "common/file.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/stream.h"
#include "graphics/surface.h"

namespace MSPAM {

/** Media types of MDF entries (the [MT] section of MSBSOLAR.INI) */
enum MediaType {
	kMediaPaletteLow = 3,
	kMediaPibList = 4,
	kMediaPibListLow = 5,
	kMediaPalette = 6,
	kMediaSound = 7,
	kMediaVideo = 10
};

struct MediaEntry {
	uint32 id;
	uint16 type;
	uint32 offset;   ///< CMF offset, or the file index for external AVIs
	uint32 size;
	uint container;
};

/** One PIB (picture) of a PibList: an 8-bit image with a transparency mask */
struct Pib {
	Graphics::Surface surface;
	Common::Array<byte> mask; ///< 1 = opaque; empty when the PIB has no transparency
	int32 regX, regY;
	uint32 flags;

	~Pib() { surface.free(); }
	bool isOpaque(int x, int y) const {
		if (x < 0 || y < 0 || x >= surface.w || y >= surface.h)
			return false;
		return mask.empty() || mask[y * surface.w + x];
	}
};

/** A PibList (CMF type 4): pictures plus an optional animation frame map */
struct PibList {
	struct Frame {
		uint16 hold;   ///< ticks to show the frame
		uint16 pib;
	};

	uint32 id;
	Common::Array<Pib *> pibs;
	Common::Array<Frame> frames;

	~PibList();
	uint frameCount() const { return frames.empty() ? pibs.size() : frames.size(); }
	const Pib *getFrame(uint frame) const;
};

/**
 * The Media Object Manager's files: MDF hash tables indexing the CMF
 * containers. MSBSOLAR.MDF/CMF holds nearly everything, MSBAMB.MDF/CMF
 * (installed to the hard disk) the ambient sound and its palettes.
 */
class MediaManager {
public:
	MediaManager();
	~MediaManager();

	bool addIndex(const Common::Path &mdf, const Common::Path &cmf);
	const MediaEntry *find(uint32 id) const;
	uint size() const { return _entries.size(); }
	Common::Array<uint32> listIds(uint16 type) const;

	/** Reads a CMF object; nullptr for missing objects and external AVIs */
	Common::SeekableReadStream *open(uint32 id);
	/** The file of an external AVI (type 10): 0\0..0\Z, then 1\0.. */
	Common::Path getVideoPath(uint32 id) const;

	/** Fills a 256-entry RGB palette: CMF indices 10..245 plus the Windows static colours */
	bool loadPalette(uint32 id, byte *palette);
	/** Decodes a PibList; the caller owns it */
	PibList *loadPibList(uint32 id);
	/** Cached PibList, owned by the manager */
	const PibList *getPibList(uint32 id);
	void clearCache();

	static void setStaticColors(byte *palette);

private:
	bool decodePib(const byte *data, uint32 size, int32 w, int32 h, uint32 flags, Pib *pib);

	Common::HashMap<uint32, MediaEntry> _entries;
	Common::Array<Common::File *> _containers;
	Common::HashMap<uint32, PibList *> _cache;
};

} // End of namespace MSPAM

#endif // MSPAM_MEDIA_H
