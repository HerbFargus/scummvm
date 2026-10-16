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
#include "common/debug.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "common/textconsole.h"

#include "yaga/pythontest.h"
#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/vm.h"

namespace Yaga {

using namespace Python;

namespace {

// Files are looked up relative to the test folder; writes are kept in memory
class TestFileSystem : public FileSystem {
public:
	explicit TestFileSystem(const Common::FSNode &root) : _root(root) {}

	Common::FSNode resolve(const Common::String &path) {
		Common::String p = path;
		for (uint i = 0; i < p.size(); i++)
			if (p[i] == '\\')
				p.setChar('/', i);
		Common::FSNode node = _root;
		Common::String part;
		for (uint i = 0; i <= p.size(); i++) {
			if (i == p.size() || p[i] == '/') {
				if (!part.empty() && part != ".")
					node = node.getChild(part);
				part.clear();
			} else {
				part += p[i];
			}
		}
		return node;
	}

	Common::SeekableReadStream *openForReading(const Common::String &path) override {
		if (_written.contains(path))
			return new Common::MemoryReadStream((const byte *)_written[path].c_str(), _written[path].size());
		Common::FSNode node = resolve(path);
		if (!node.exists() || node.isDirectory())
			return nullptr;
		return node.createReadStream();
	}

	class StringWriteStream : public Common::WriteStream {
	public:
		StringWriteStream(Common::String *target) : _target(target) {}
		uint32 write(const void *dataPtr, uint32 dataSize) override {
			*_target += Common::String((const char *)dataPtr, dataSize);
			return dataSize;
		}
		int64 pos() const override { return _target->size(); }
	private:
		Common::String *_target;
	};

	Common::WriteStream *openForWriting(const Common::String &path, bool append) override {
		if (!append)
			_written[path].clear();
		return new StringWriteStream(&_written[path]);
	}

	int stat(const Common::String &path, uint32 *size, uint32 *mtime) override {
		*mtime = 0;
		if (_written.contains(path)) {
			*size = _written[path].size();
			return 1;
		}
		Common::FSNode node = resolve(path);
		if (!node.exists())
			return 0;
		*size = 0;
		return node.isDirectory() ? 2 : 1;
	}

	bool remove(const Common::String &path) override {
		if (!_written.contains(path))
			return false;
		_written.erase(path);
		return true;
	}

	bool listDirectory(const Common::String &path, Common::Array<Common::String> &names) override {
		Common::FSNode node = resolve(path);
		Common::FSList list;
		if (!node.isDirectory() || !node.getChildren(list, Common::FSNode::kListAll))
			return false;
		for (uint i = 0; i < list.size(); i++)
			names.push_back(list[i].getName());
		return true;
	}

	Common::String currentDirectory() override {
		return ".";
	}

private:
	Common::FSNode _root;
	Common::HashMap<Common::String, Common::String> _written;
};

class BufferSink : public OutputSink {
public:
	Common::String _text;
	// At debug level 3 the output is also logged as it happens
	void writeStdout(const char *s, uint32 len) override {
		_text += Common::String(s, len);
		debugN(3, "%s", Common::String(s, len).c_str());
	}
	void writeStderr(const char *s, uint32 len) override {
		_text += Common::String(s, len);
		debugN(3, "%s", Common::String(s, len).c_str());
	}

};

} // End of anonymous namespace

void runPythonTests(const Common::String &dir, const Common::String &outDir) {
	Common::FSNode root(Common::Path(dir, Common::Path::kNativeSeparator));
	Common::FSNode out(Common::Path(outDir.empty() ? dir : outDir, Common::Path::kNativeSeparator));
	Common::FSList files;
	if (!root.getChildren(files, Common::FSNode::kListFilesOnly)) {
		warning("yaga python test: cannot list %s", dir.c_str());
		return;
	}
	Common::sort(files.begin(), files.end());
	TestFileSystem fs(root);
	BufferSink sink;
	Interpreter vm(&fs, &sink);
	Common::Array<Common::String> path;
	path.push_back("");
	path.push_back("lib");
	vm.setPath(path);

	int passed = 0, failed = 0;
	for (uint i = 0; i < files.size(); i++) {
		Common::String name = files[i].getName();
		if (!name.hasSuffix(".pyc"))
			continue;
		Common::String base(name.c_str(), name.size() - 4);
		Common::SeekableReadStream *in = files[i].createReadStream();
		if (!in)
			continue;
		Common::String data;
		while (!in->eos()) {
			char buf[4096];
			uint32 n = in->read(buf, sizeof(buf));
			data += Common::String(buf, n);
			if (n == 0)
				break;
		}
		delete in;
		sink._text.clear();
		// A fresh __main__ for each test
		getModuleDict()->delItem(newStr("__main__").get());
		clearError();
		Module *main = addModule("__main__");
		main->_dict->setItemString("__builtins__", getModuleDict()->getItemString("__builtin__") ?
		                           ((Module *)getModuleDict()->getItemString("__builtin__"))->_dict.get() : (Object *)g_none);
		main->_dict->setItemString("__file__", newStr(base + ".py").get());
		bool ok;
		if (data.size() < 8 || memcmp(data.c_str(), "\x2d\xed\x0d\x0a", 4) != 0) {
			sink._text += "bad .pyc header\n";
			ok = false;
		} else {
			ObjRef code = marshalLoads(data.c_str() + 8, data.size() - 8);
			if (!code) {
				sink._text += "marshal: " + formatError() + "\n";
				ok = false;
			} else {
				ok = vm.runCode(code.get()) || vm.exited();
			}
		}
		Common::WriteStream *w = out.getChild(base + ".out").createWriteStream();
		if (w) {
			w->write(sink._text.c_str(), sink._text.size());
			w->finalize();
			delete w;
		}
		if (ok)
			passed++;
		else
			failed++;
		debug("yaga python test: %s %s", base.c_str(), ok ? "ran" : "raised");
	}
	debug("yaga python test: %d ran, %d raised", passed, failed);
}

} // End of namespace Yaga
