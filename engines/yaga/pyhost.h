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

#ifndef YAGA_PYHOST_H
#define YAGA_PYHOST_H

#include "common/fs.h"
#include "common/str.h"

#include "yaga/python/vm.h"

namespace Yaga {

// The games' files as their scripts see them: Windows paths under the
// install folder (C:\Program Files\Atari\<game>), mapped onto the folder
// ScummVM was pointed at. Files the game writes go to the save game folder.
class GameFileSystem : public Python::FileSystem {
public:
	GameFileSystem(const Common::FSNode &atariRoot, const Common::String &gameDir, const Common::String &target);

	const Common::String &homePath() const { return _home; }
	Common::String virtualPath(const Common::String &relative) const;

	Common::SeekableReadStream *openForReading(const Common::String &path) override;
	Common::WriteStream *openForWriting(const Common::String &path, bool append) override;
	int stat(const Common::String &path, uint32 *size, uint32 *mtime) override;
	bool remove(const Common::String &path) override;
	bool listDirectory(const Common::String &path, Common::Array<Common::String> &names) override;
	Common::String currentDirectory() override { return _home; }

private:
	// Splits a path into its parts below the install root; false if outside it
	bool relativeParts(const Common::String &path, Common::Array<Common::String> &parts) const;
	Common::FSNode findNode(const Common::Array<Common::String> &parts) const;
	Common::String saveName(const Common::Array<Common::String> &parts) const;

	Common::FSNode _root;
	Common::String _rootPath; // C:\Program Files\Atari
	Common::String _home;     // _rootPath\<game folder>
	Common::String _target;
};

// Where the scripts' output goes: the debug log
class LogSink : public Python::OutputSink {
public:
	void writeStdout(const char *s, uint32 len) override;
	void writeStderr(const char *s, uint32 len) override;
private:
	void write(const char *s, uint32 len, Common::String &pending);
	Common::String _out, _err;
};

// Registers tracing stand-ins for the yaga* engine modules: every attribute,
// call and assignment the scripts make on them is logged (debug channel
// "Script"), and they answer with more stand-ins
void registerEngineStubs(Python::Interpreter *vm);

// Runs the game's executable as McMillan Installer's launcher does: the
// bootstrap modules, then the bytecode archive on sys.path, then the scripts.
// Returns false (with the error logged) if a script raised.
bool runLauncher(Python::Interpreter *vm, GameFileSystem *fs, const Common::String &exeName);

} // End of namespace Yaga

#endif // YAGA_PYHOST_H
