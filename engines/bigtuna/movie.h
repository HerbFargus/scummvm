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

#ifndef BIGTUNA_MOVIE_H
#define BIGTUNA_MOVIE_H

#include "common/array.h"
#include "common/file.h"
#include "common/hashmap.h"
#include "common/str.h"

namespace BigTuna {

class Bitmap;
class Score;

enum ResourceType {
	kResEmpty = 0,
	kResScript = 1,
	kResSymbols = 2,
	kResCastNames = 3,
	kResScore = 4,
	kResPalette = 5,
	kResBitmap = 6,
	kResSound = 7,
	kResLabels = 9
};

// Fixed resource numbers
enum {
	kResNumScore = 1,
	kResNumSymbols = 2,
	kResNumLabels = 3,
	kResNumCastNames = 4
};

// Lingo member N is resource N + 7
enum {
	kMemberBase = 7
};

struct NameEntry {
	Common::String name;
	uint32 value;
};

struct Label {
	Common::String name;
	uint32 value; // frame number, or a score byte offset in the first player
};

/**
 * An .IM2 movie: "IM20", entry count, data, then a directory of
 * { u8 type, u32 size, u32 offset } at the end of the file.
 * Resource numbers are 1-based; the player keeps a dummy entry 0.
 */
class Movie {
public:
	Movie(bool v1);
	~Movie();

	bool open(const Common::String &name);

	const Common::String &getName() const { return _name; }
	bool isV1() const { return _v1; }

	uint getResourceCount() const { return _entries.size(); }
	ResourceType getType(uint res) const;
	uint32 getSize(uint res) const;
	Common::SeekableReadStream *getResource(uint res);

	Bitmap *getBitmap(uint res);
	Score *getScore(uint res);

	bool hasPalette() const { return _hasPalette; }
	const byte *getPalette() const { return _palette; }
	bool loadPalette(uint res, byte *pal);

	const Common::Array<NameEntry> &getSymbols() const { return _symbols; }
	const Common::Array<NameEntry> &getCastNames() const { return _castNames; }
	const Common::Array<Label> &getLabels() const { return _labels; }

private:
	struct Entry {
		byte type;
		uint32 size;
		uint32 offset;
	};

	bool readNames(uint res, Common::Array<NameEntry> &names);
	void readLabels();

	bool _v1;
	Common::String _name;
	Common::File _file;
	Common::Array<Entry> _entries;
	Common::HashMap<uint, Bitmap *> _bitmaps;
	Common::HashMap<uint, Score *> _scores;
	Common::Array<NameEntry> _symbols;
	Common::Array<NameEntry> _castNames;
	Common::Array<Label> _labels;
	byte _palette[256 * 3];
	bool _hasPalette;
};

} // End of namespace BigTuna

#endif // BIGTUNA_MOVIE_H
