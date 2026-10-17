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

#ifndef BIGTUNA_IMP_H
#define BIGTUNA_IMP_H

#include "common/array.h"
#include "common/file.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"

namespace BigTuna {

class BigTunaEngine;
class Bitmap;

/**
 * A Just Me and My Dad .IMP file (the "IW" player in DAD.EXE): u16 0x0063,
 * u16 0x7AB7, u16 count, then count x { name in 11 bytes, u32 size, u32 offset }.
 * Records are named <name>.<type>: C palette, A film, P picture, S sound.
 */
class ImpFile {
public:
	~ImpFile();

	bool open(const Common::String &name);
	const Common::String &getName() const { return _name; }

	bool has(const Common::String &record) const { return _records.contains(record); }
	bool read(const Common::String &record, Common::Array<byte> &data);
	Bitmap *getPicture(const Common::String &name);
	Common::Array<Common::String> list() const;

private:
	struct Entry {
		uint32 size;
		uint32 offset;
	};

	Common::String _name;
	Common::File _file;
	Common::HashMap<Common::String, Entry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _records;
	Common::HashMap<Common::String, Bitmap *, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _pictures;
};

enum ImpRecordType {
	kImpClear = 0,
	kImpBox = 1,
	kImpPicture = 2,
	kImpText = 3,
	kImpSound = 4,
	kImpGo = 5,
	kImpLabel = 6,
	kImpButton = 7,
	kImpValue = 8,
	kImpClicks = 9,
	kImpVolume = 10,
	kImpMovie = 11,
	kImpSetVariable = 12
};

// A film record: 19 bytes, duration in 1/60 s (non-zero ends a frame)
struct ImpRecord {
	byte type;
	byte channel;
	byte duration;
	byte transition;
	int16 a, b, c;
	Common::String name;
};

/**
 * Plays a film: records are applied in order; a record with a duration ends
 * the frame, which is drawn, waited for (taking clicks on buttons), and then
 * any pending go to a label or another film happens.
 */
class ImpPlayer {
public:
	ImpPlayer(BigTunaEngine *vm);
	~ImpPlayer();

	void run(const Common::String &start);
	bool loadFilm(const Common::String &name);
	bool goTo(const Common::String &target);
	void draw(bool present = true);

	const Common::String &getFilmName() const;
	uint getPosition() const { return _pos; }
	Common::Array<Common::Point> getButtonPoints() const;
	// Every film: decode every picture and step every record without waiting
	void sweep();

	enum {
		kStageX = 64,
		kStageY = 48,
		kStageW = 512,
		kStageH = 384
	};

private:
	struct Cell {
		Cell() : active(false), bitmap(nullptr), x(0), y(0), layer(0) {}
		bool active;
		Common::String picture;
		Bitmap *bitmap;
		int x, y;
		int layer;
	};

	struct SoundSlot {
		SoundSlot() : play(false), loop(false), volume(false), level(0), fade(0) {}
		bool play;
		Common::String name;
		bool loop;
		bool volume;
		int level;
		int fade;
	};

	void apply(uint index, bool seeking);
	void rewind();
	void endFrame(uint index);
	void waitFrame(uint32 duration);
	void flushSounds();
	int channelAt(int x, int y) const;
	int top(const Cell &c) const;
	Common::Array<uint> drawOrder() const;
	Common::String variable(const Common::String &name) const;
	void playMovie(const Common::String &name, int x, int y);
	void updateCursor();

	BigTunaEngine *_vm;
	ImpFile *_file;
	Common::Array<ImpRecord> _records;
	uint _channels;
	uint _pos;
	uint32 _prevDuration;
	Common::Array<Cell> _cells;
	Common::Array<uint> _buttons;
	bool _clicks;
	Common::String _target;
	SoundSlot _slots[2];
	Common::HashMap<Common::String, Common::String, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _variables;
	bool _quit;
};

} // End of namespace BigTuna

#endif // BIGTUNA_IMP_H
