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

#ifndef YAGA_PYTHON_VM_H
#define YAGA_PYTHON_VM_H

#include "common/fs.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

#include "yaga/python/object.h"

namespace Common {
class SeekableReadStream;
class WriteStream;
}

namespace Yaga {
namespace Python {

// A native module: the init function fills it in once it is imported
typedef void (*ModuleInitFunc)(Module *m);

// How the VM sees the game's files: Windows paths under a virtual install
// directory are mapped to the game folder ScummVM was pointed at, and files
// the game writes go to the save game directory.
class FileSystem {
public:
	virtual ~FileSystem() {}
	// Opens a file for reading; null if it does not exist
	virtual Common::SeekableReadStream *openForReading(const Common::String &path) = 0;
	// Opens a file for writing (replacing it, or appending to it)
	virtual Common::WriteStream *openForWriting(const Common::String &path, bool append) = 0;
	// 0 = missing, 1 = file, 2 = directory; size and modification time if found
	virtual int stat(const Common::String &path, uint32 *size, uint32 *mtime) = 0;
	virtual bool remove(const Common::String &path) = 0;
	virtual bool listDirectory(const Common::String &path, Common::Array<Common::String> &names) = 0;
	virtual Common::String currentDirectory() = 0;
};

// Where print and the logs go
class OutputSink {
public:
	virtual ~OutputSink() {}
	virtual void writeStdout(const char *s, uint32 len) = 0;
	virtual void writeStderr(const char *s, uint32 len) = 0;
};

class Interpreter {
public:
	Interpreter(FileSystem *fs, OutputSink *out);
	~Interpreter();

	// Registers a native module, importable as a built-in or (if extension is
	// set) through a .pyd/.dll file the game's importer finds
	void addNativeModule(const char *name, ModuleInitFunc init, bool extension);

	// Python 2.2 system state
	void setArgv(const Common::Array<Common::String> &argv);
	void setExecutable(const Common::String &path);
	void setPath(const Common::Array<Common::String> &path);

	// Runs source in __main__, reporting errors; false if an exception escaped
	bool runString(const char *source, const char *filename);
	// Runs a code object as a module (as PyImport_ExecCodeModule)
	bool execCodeModule(const char *name, Object *code);
	// Runs a code object in __main__
	bool runCode(Object *code);

	// The error that ended the last run, formatted as a traceback
	const Common::String &lastError() const { return _lastError; }
	// True if the last run ended with SystemExit (and its code)
	bool exited() const { return _exited; }
	int exitCode() const { return _exitCode; }

	FileSystem *fileSystem() { return _fs; }
	OutputSink *output() { return _out; }

	static Interpreter *instance() { return _instance; }

	struct NativeModule {
		ModuleInitFunc init;
		bool extension;
	};
	const Common::HashMap<Common::String, NativeModule> &nativeModules() const { return _natives; }

private:
	bool reportError();

	static Interpreter *_instance;
	FileSystem *_fs;
	OutputSink *_out;
	Common::HashMap<Common::String, NativeModule> _natives;
	Common::String _lastError;
	bool _exited;
	int _exitCode;
};

// Imports a native module by name, or returns null (no error) if there is none
ObjRef initNativeModule(const char *name);
bool isNativeModule(const char *name, bool *extension);

} // End of namespace Python
} // End of namespace Yaga

#endif // YAGA_PYTHON_VM_H
