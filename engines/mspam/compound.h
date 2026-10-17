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

#ifndef MSPAM_COMPOUND_H
#define MSPAM_COMPOUND_H

#include "common/array.h"
#include "common/str.h"
#include "common/stream.h"

namespace MSPAM {

/**
 * Read-only reader for OLE2 compound files (structured storage), enough for
 * the title's .TDF: FAT, DIFAT, mini-FAT and the directory tree.
 */
class CompoundFile {
public:
	struct Entry {
		Common::String name;
		byte type;     ///< 1 = storage, 2 = stream, 5 = root
		uint32 left, right, child;
		byte clsid[16];
		uint32 start;
		uint32 size;
	};

	CompoundFile();

	bool open(Common::SeekableReadStream *stream);
	void close();

	/** Finds an entry by a '/'-separated path below the root (case-insensitive), -1 if missing */
	int findEntry(const Common::String &path) const;
	const Entry *getEntry(int index) const;
	/** Names of the children of a storage, in directory order */
	Common::Array<Common::String> listChildren(const Common::String &path) const;
	/** Reads a whole stream into memory; nullptr if the path isn't a stream */
	Common::SeekableReadStream *openStream(const Common::String &path) const;

private:
	uint32 nextSector(uint32 sector) const;
	uint32 nextMiniSector(uint32 sector) const;
	bool readChain(uint32 start, Common::Array<byte> &out) const;
	int findChild(uint32 storage, const Common::String &name) const;
	void collectChildren(uint32 node, Common::Array<uint32> &out, int depth) const;

	Common::Array<byte> _data;
	uint32 _sectorSize;
	uint32 _miniSectorSize;
	uint32 _miniCutoff;
	Common::Array<uint32> _fat;
	Common::Array<uint32> _miniFat;
	Common::Array<byte> _miniStream;
	Common::Array<Entry> _entries;
};

} // End of namespace MSPAM

#endif // MSPAM_COMPOUND_H
