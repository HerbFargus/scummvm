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

#include "common/archive.h"
#include "common/compression/unzip.h"
#include "common/debug.h"

#include "common/stream.h"

#include "yaga/pyhost.h"
#include "yaga/resources.h"

namespace Yaga {

static bool readAll(Common::SeekableReadStream *s, Common::String &out) {
	if (!s)
		return false;
	out.clear();
	char buf[16384];
	uint32 n;
	while ((n = s->read(buf, sizeof(buf))) > 0)
		out += Common::String(buf, n);
	delete s;
	return true;
}

Resources::Resources(const Common::FSNode &atariRoot, const Common::String &gameDir, GameFileSystem *fs) : _fs(fs) {
	// The archives sit in the game's folder and in MaxFiles beside it
	Common::FSList children;
	if (atariRoot.getChildren(children, Common::FSNode::kListDirectoriesOnly)) {
		for (uint i = 0; i < children.size(); i++)
			if (children[i].getName().equalsIgnoreCase(gameDir))
				indexFolder(children[i]);
		for (uint i = 0; i < children.size(); i++)
			if (children[i].getName().equalsIgnoreCase("MaxFiles"))
				indexFolder(children[i]);
	}
	debug(1, "yaga: %u .he archives", _archives.size());
}

Resources::~Resources() {
	for (uint i = 0; i < _archives.size(); i++)
		delete _archives[i].zip;
}

void Resources::indexFolder(const Common::FSNode &dir) {
	Common::FSList files;
	if (!dir.getChildren(files, Common::FSNode::kListFilesOnly))
		return;
	for (uint i = 0; i < files.size(); i++) {
		Common::String name = files[i].getName();
		Common::String lower = name;
		lower.toLowercase();
		if (!lower.hasSuffix(".he"))
			continue;
		Common::String stem(lower.c_str(), lower.size() - 3);
		if (findArchive(stem))
			continue; // the first one found wins
		HeArchive a;
		a.stem = stem;
		a.zip = Common::makeZipArchive(files[i]);
		if (!a.zip) {
			warning("yaga: %s is not a zip archive", name.c_str());
			continue;
		}
		Common::ArchiveMemberList members;
		a.zip->listMembers(members);
		for (Common::ArchiveMemberList::const_iterator it = members.begin(); it != members.end(); ++it) {
			Common::String stored = (*it)->getPathInArchive().toString('/');
			Common::String key = stored;
			key.toLowercase();
			a.members[key] = stored;
		}
		debug(2, "yaga: %s has %u members", name.c_str(), a.members.size());
		_archives.push_back(a);
	}
}

Resources::HeArchive *Resources::findArchive(const Common::String &stem) {
	for (uint i = 0; i < _archives.size(); i++)
		if (_archives[i].stem == stem)
			return &_archives[i];
	return nullptr;
}

Common::String Resources::normalise(const Common::String &path) {
	Common::String p = path;
	for (uint i = 0; i < p.size(); i++)
		if (p[i] == '\\')
			p.setChar('/', i);
	while (p.hasPrefix("/"))
		p = Common::String(p.c_str() + 1);
	while (p.hasPrefix("./"))
		p = Common::String(p.c_str() + 2);
	return p;
}

bool Resources::read(const Common::String &path, Common::String &out) {
	Common::String p = normalise(path);
	Common::String lower = p;
	lower.toLowercase();
	// The game asks for .evt event streams; only the compiled .evb ones shipped
	if (lower.hasSuffix(".evt")) {
		if (read(Common::String(p.c_str(), p.size() - 4) + ".evb", out))
			return true;
	}
	const char *slash = strchr(lower.c_str(), '/');
	if (slash) {
		HeArchive *a = findArchive(Common::String(lower.c_str(), slash - lower.c_str()));
		if (a) {
			Common::String rest(slash + 1);
			if (a->members.contains(rest))
				return readAll(a->zip->createReadStreamForMember(Common::Path(a->members[rest], '/')), out);
		}
	}
	// A loose file in the game's folder
	return readAll(_fs->openForReading(p), out);
}

bool Resources::has(const Common::String &path) {
	Common::String p = normalise(path);
	Common::String lower = p;
	lower.toLowercase();
	const char *slash = strchr(lower.c_str(), '/');
	if (slash) {
		HeArchive *a = findArchive(Common::String(lower.c_str(), slash - lower.c_str()));
		if (a && a->members.contains(Common::String(slash + 1)))
			return true;
	}
	uint32 size, mtime;
	return _fs->stat(p, &size, &mtime) == 1;
}

} // End of namespace Yaga
