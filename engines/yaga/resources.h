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

#ifndef YAGA_RESOURCES_H
#define YAGA_RESOURCES_H

#include "common/archive.h"
#include "common/fs.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

namespace Yaga {

class GameFileSystem;

// The game's data as the engine addresses it: a path's first part names a
// .he archive (plain ZIP files, stored uncompressed), so data/scenes.xml is
// scenes.xml inside data.he. Anything not in an archive is a loose file in
// the game's folder. Names match without regard to case.
class Resources {
public:
	Resources(const Common::FSNode &atariRoot, const Common::String &gameDir, GameFileSystem *fs);
	~Resources();

	// The bytes at a game path; false if nothing is there
	bool read(const Common::String &path, Common::String &out);
	bool has(const Common::String &path);
	uint archiveCount() const { return _archives.size(); }

	// A replacement for a sound or a movie's soundtrack from the game's extra
	// path (ScummVM's "Extra path" option): the same path with any of these
	// extensions, such as talkies/sam/pj4pc_sam_00055.wav for the .mp3.
	// Null if there is none; extension gets the one found
	Common::SeekableReadStream *openAudioReplacement(const Common::String &path, Common::String &extension);

private:
	struct HeArchive {
		Common::String stem; // "rooms" for rooms.he
		Common::Archive *zip;
		Common::HashMap<Common::String, Common::String> members; // lower-case path -> stored path
	};

	void indexFolder(const Common::FSNode &dir);
	void indexReplacements(const Common::FSNode &dir, const Common::String &prefix, int depth);
	HeArchive *findArchive(const Common::String &stem);
	static Common::String normalise(const Common::String &path);

	Common::Array<HeArchive> _archives;
	GameFileSystem *_fs;
	Common::HashMap<Common::String, Common::FSNode> _replacements; // lower-case path -> file
};

} // End of namespace Yaga

#endif // YAGA_RESOURCES_H
