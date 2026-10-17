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

#include "mspam/compound.h"
#include "mspam/detection.h"

namespace MSPAM {

enum {
	kEndOfChain = 0xFFFFFFFE,
	kFreeSector = 0xFFFFFFFF,
	kNoStream = 0xFFFFFFFF,
	kMaxChain = 0x100000
};

static const byte kSignature[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };

CompoundFile::CompoundFile() : _sectorSize(512), _miniSectorSize(64), _miniCutoff(4096) {
}

void CompoundFile::close() {
	_data.clear();
	_fat.clear();
	_miniFat.clear();
	_miniStream.clear();
	_entries.clear();
}

uint32 CompoundFile::nextSector(uint32 sector) const {
	return sector < _fat.size() ? _fat[sector] : (uint32)kEndOfChain;
}

uint32 CompoundFile::nextMiniSector(uint32 sector) const {
	return sector < _miniFat.size() ? _miniFat[sector] : (uint32)kEndOfChain;
}

bool CompoundFile::readChain(uint32 start, Common::Array<byte> &out) const {
	out.clear();
	uint32 count = 0;
	for (uint32 s = start; s != kEndOfChain && s != kFreeSector; s = nextSector(s)) {
		uint32 offset = (s + 1) * _sectorSize;
		if (offset + _sectorSize > _data.size() || ++count > kMaxChain)
			return false;
		uint32 old = out.size();
		out.resize(old + _sectorSize);
		memcpy(&out[old], &_data[offset], _sectorSize);
	}
	return true;
}

bool CompoundFile::open(Common::SeekableReadStream *stream) {
	close();
	if (!stream)
		return false;
	_data.resize(stream->size());
	stream->read(_data.data(), _data.size());
	delete stream;
	if (_data.size() < 512 || memcmp(_data.data(), kSignature, 8) != 0)
		return false;

	const byte *h = _data.data();
	_sectorSize = 1 << READ_LE_UINT16(h + 0x1E);
	_miniSectorSize = 1 << READ_LE_UINT16(h + 0x20);
	uint32 numFatSectors = READ_LE_UINT32(h + 0x2C);
	uint32 dirStart = READ_LE_UINT32(h + 0x30);
	_miniCutoff = READ_LE_UINT32(h + 0x38);
	uint32 miniFatStart = READ_LE_UINT32(h + 0x3C);
	uint32 difatStart = READ_LE_UINT32(h + 0x44);
	uint32 numDifat = READ_LE_UINT32(h + 0x48);
	if (_sectorSize < 128 || _sectorSize > 4096)
		return false;

	// The DIFAT lists the FAT sectors: 109 in the header, then chained DIFAT sectors
	Common::Array<uint32> fatSectors;
	for (uint i = 0; i < 109 && fatSectors.size() < numFatSectors; i++)
		fatSectors.push_back(READ_LE_UINT32(h + 0x4C + 4 * i));
	uint32 perSector = _sectorSize / 4;
	for (uint32 d = difatStart, n = 0; n < numDifat && d < kEndOfChain - 2; n++) {
		uint32 offset = (d + 1) * _sectorSize;
		if (offset + _sectorSize > _data.size())
			return false;
		for (uint32 i = 0; i < perSector - 1 && fatSectors.size() < numFatSectors; i++)
			fatSectors.push_back(READ_LE_UINT32(&_data[offset + 4 * i]));
		d = READ_LE_UINT32(&_data[offset + 4 * (perSector - 1)]);
	}
	for (uint32 s : fatSectors) {
		uint32 offset = (s + 1) * _sectorSize;
		if (offset + _sectorSize > _data.size())
			return false;
		for (uint32 i = 0; i < perSector; i++)
			_fat.push_back(READ_LE_UINT32(&_data[offset + 4 * i]));
	}

	Common::Array<byte> buf;
	if (miniFatStart != kEndOfChain && miniFatStart != kFreeSector) {
		if (!readChain(miniFatStart, buf))
			return false;
		for (uint32 i = 0; i + 4 <= buf.size(); i += 4)
			_miniFat.push_back(READ_LE_UINT32(&buf[i]));
	}

	if (!readChain(dirStart, buf))
		return false;
	for (uint32 o = 0; o + 128 <= buf.size(); o += 128) {
		Entry e;
		for (uint i = 0; i < 62; i += 2) {
			uint16 c = READ_LE_UINT16(&buf[o + i]);
			if (!c)
				break;
			e.name += c < 0x80 ? (char)c : '?';
		}
		e.type = buf[o + 0x42];
		e.left = READ_LE_UINT32(&buf[o + 0x44]);
		e.right = READ_LE_UINT32(&buf[o + 0x48]);
		e.child = READ_LE_UINT32(&buf[o + 0x4C]);
		memcpy(e.clsid, &buf[o + 0x50], 16);
		e.start = READ_LE_UINT32(&buf[o + 0x74]);
		e.size = READ_LE_UINT32(&buf[o + 0x78]);
		_entries.push_back(e);
	}
	if (_entries.empty() || _entries[0].type != 5)
		return false;

	// The root entry's chain holds the mini stream
	if (_entries[0].start != kEndOfChain && !readChain(_entries[0].start, _miniStream))
		return false;
	debugC(1, kDebugLoading, "Compound file: %d entries, %d FAT sectors, mini stream %d bytes",
		_entries.size(), (int)numFatSectors, _miniStream.size());
	return true;
}

const CompoundFile::Entry *CompoundFile::getEntry(int index) const {
	return index >= 0 && index < (int)_entries.size() ? &_entries[index] : nullptr;
}

// Siblings form a red-black tree through left/right; an in-order walk gives directory order
void CompoundFile::collectChildren(uint32 node, Common::Array<uint32> &out, int depth) const {
	if (node == kNoStream || node >= _entries.size() || depth > 64)
		return;
	collectChildren(_entries[node].left, out, depth + 1);
	out.push_back(node);
	collectChildren(_entries[node].right, out, depth + 1);
}

int CompoundFile::findChild(uint32 storage, const Common::String &name) const {
	Common::Array<uint32> children;
	collectChildren(_entries[storage].child, children, 0);
	for (uint32 c : children) {
		if (_entries[c].name.equalsIgnoreCase(name))
			return c;
	}
	return -1;
}

int CompoundFile::findEntry(const Common::String &path) const {
	if (_entries.empty())
		return -1;
	int node = 0;
	Common::String rest = path;
	while (!rest.empty()) {
		size_t slash = rest.findFirstOf('/');
		Common::String part = slash == Common::String::npos ? rest : rest.substr(0, slash);
		rest = slash == Common::String::npos ? Common::String() : rest.substr(slash + 1);
		if (part.empty())
			continue;
		node = findChild(node, part);
		if (node < 0)
			return -1;
	}
	return node;
}

Common::Array<Common::String> CompoundFile::listChildren(const Common::String &path) const {
	Common::Array<Common::String> names;
	int node = findEntry(path);
	if (node < 0)
		return names;
	Common::Array<uint32> children;
	collectChildren(_entries[node].child, children, 0);
	for (uint32 c : children)
		names.push_back(_entries[c].name);
	return names;
}

Common::SeekableReadStream *CompoundFile::openStream(const Common::String &path) const {
	int node = findEntry(path);
	if (node < 0 || _entries[node].type != 2)
		return nullptr;
	const Entry &e = _entries[node];
	byte *out = (byte *)malloc(MAX<uint32>(e.size, 1));
	bool mini = e.size < _miniCutoff;
	const Common::Array<byte> &source = mini ? _miniStream : _data;
	uint32 unit = mini ? _miniSectorSize : _sectorSize;
	uint32 done = 0, count = 0;
	for (uint32 s = e.start; done < e.size && s != kEndOfChain && ++count < kMaxChain;
			s = mini ? nextMiniSector(s) : nextSector(s)) {
		uint32 offset = mini ? s * unit : (s + 1) * unit;
		uint32 n = MIN(unit, e.size - done);
		if (offset + n > source.size())
			break;
		memcpy(out + done, &source[offset], n);
		done += n;
	}
	if (done != e.size) {
		warning("Compound file stream '%s' is truncated (%d of %d bytes)", path.c_str(), done, e.size);
		free(out);
		return nullptr;
	}
	return new Common::MemoryReadStream(out, e.size, DisposeAfterUse::YES);
}

} // End of namespace MSPAM
