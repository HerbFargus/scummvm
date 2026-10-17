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
#include "common/memstream.h"
#include "common/textconsole.h"

#include "bigtuna/bitmap.h"
#include "bigtuna/detection.h"
#include "bigtuna/movie.h"
#include "bigtuna/score.h"

namespace BigTuna {

Movie::Movie(bool v1) : _v1(v1), _hasPalette(false) {
	memset(_palette, 0, sizeof(_palette));
}

Movie::~Movie() {
	for (auto &b : _bitmaps)
		delete b._value;
	for (auto &s : _scores)
		delete s._value;
}

bool Movie::open(const Common::String &name) {
	_name = name;
	if (!_file.open(Common::Path(name + ".im2")))
		return false;

	if (_file.readUint32LE() != MKTAG('I', 'M', '2', '0')) {
		warning("Movie::open(): %s is not an IM2 file", name.c_str());
		return false;
	}
	uint32 count = _file.readUint32LE();
	_file.seek(_file.size() - 9 * (int64)count);

	_entries.resize(count + 1);
	_entries[0].type = kResEmpty;
	_entries[0].size = _entries[0].offset = 0;
	for (uint i = 1; i <= count; i++) {
		_entries[i].type = _file.readByte();
		_entries[i].size = _file.readUint32LE();
		_entries[i].offset = _file.readUint32LE();
	}
	debugC(1, kDebugLoading, "Movie %s: %d resources", name.c_str(), count);

	if (getType(kResNumSymbols) == kResSymbols)
		readNames(kResNumSymbols, _symbols);
	if (!_v1 && getType(kResNumCastNames) == kResCastNames)
		readNames(kResNumCastNames, _castNames);
	readLabels();

	for (uint i = 1; i <= count && !_hasPalette; i++) {
		if (_entries[i].type == kResPalette)
			_hasPalette = loadPalette(i, _palette);
	}
	return true;
}

ResourceType Movie::getType(uint res) const {
	if (res >= _entries.size())
		return kResEmpty;
	return (ResourceType)_entries[res].type;
}

uint32 Movie::getSize(uint res) const {
	if (res >= _entries.size())
		return 0;
	return _entries[res].size;
}

Common::SeekableReadStream *Movie::getResource(uint res) {
	if (res >= _entries.size() || _entries[res].type == kResEmpty)
		return nullptr;
	_file.seek(_entries[res].offset);
	return _file.readStream(_entries[res].size);
}

Bitmap *Movie::getBitmap(uint res) {
	if (_bitmaps.contains(res))
		return _bitmaps[res];
	Bitmap *bitmap = nullptr;
	if (getType(res) == kResBitmap) {
		Common::SeekableReadStream *stream = getResource(res);
		bitmap = new Bitmap();
		if (!bitmap->load(*stream, _v1)) {
			warning("Movie %s: bad bitmap %d", _name.c_str(), res);
			delete bitmap;
			bitmap = nullptr;
		}
		delete stream;
	}
	_bitmaps[res] = bitmap;
	return bitmap;
}

Score *Movie::getScore(uint res) {
	if (_scores.contains(res))
		return _scores[res];
	Score *score = nullptr;
	if (getType(res) == kResScore) {
		Common::SeekableReadStream *stream = getResource(res);
		score = new Score();
		if (!score->load(*stream)) {
			warning("Movie %s: bad score %d", _name.c_str(), res);
			delete score;
			score = nullptr;
		}
		delete stream;
	}
	_scores[res] = score;
	return score;
}

bool Movie::loadPalette(uint res, byte *pal) {
	if (getType(res) != kResPalette || getSize(res) < 768)
		return false;
	_file.seek(_entries[res].offset);
	_file.read(pal, 768);
	return true;
}

// u32 count, count x { u32 name start, u32 value }, u32 names end, names
bool Movie::readNames(uint res, Common::Array<NameEntry> &names) {
	Common::SeekableReadStream *stream = getResource(res);
	if (!stream)
		return false;
	uint32 count = stream->readUint32LE();
	if (count > (uint32)stream->size() / 8) {
		warning("Movie %s: bad name table %d", _name.c_str(), res);
		delete stream;
		return false;
	}
	Common::Array<uint32> starts(count + 1);
	names.resize(count);
	for (uint i = 0; i < count; i++) {
		starts[i] = stream->readUint32LE();
		names[i].value = stream->readUint32LE();
	}
	starts[count] = stream->readUint32LE();
	for (uint i = 0; i < count; i++) {
		stream->seek(starts[i]);
		names[i].name = stream->readString(0, starts[i + 1] - starts[i]);
	}
	delete stream;
	return true;
}

void Movie::readLabels() {
	if (_v1) {
		// The first player keeps labels as score byte offsets
		Common::Array<NameEntry> names;
		if (getType(kResNumLabels) == kResCastNames && readNames(kResNumLabels, names)) {
			for (auto &n : names)
				_labels.push_back(Label{n.name, n.value});
		}
		return;
	}

	// Director VWLB layout, big-endian: count, (frame, offset) x count, total, names
	Common::SeekableReadStream *stream = getResource(kResNumLabels);
	if (!stream || getType(kResNumLabels) != kResLabels) {
		delete stream;
		return;
	}
	uint16 count = stream->readUint16BE();
	Common::Array<uint16> frames(count + 1), offsets(count + 1);
	for (uint i = 0; i <= count; i++) {
		frames[i] = stream->readUint16BE();
		offsets[i] = stream->readUint16BE();
	}
	uint32 base = stream->pos();
	for (uint i = 0; i < count; i++) {
		stream->seek(base + offsets[i]);
		_labels.push_back(Label{stream->readString(0, offsets[i + 1] - offsets[i]), frames[i]});
	}
	delete stream;
}

} // End of namespace BigTuna
