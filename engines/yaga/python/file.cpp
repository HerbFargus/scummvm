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

#include "common/memstream.h"
#include "common/stream.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/vm.h"

// File objects (open() and file()), over the VM's FileSystem. Files opened in
// text mode translate line endings as Windows did.

namespace Yaga {
namespace Python {

Type g_fileType("file", &g_objectType);

struct FileObject : public Object {
	Common::SeekableReadStream *_in;   // when reading
	Common::WriteStream *_out;         // when writing
	Common::MemoryReadWriteStream *_buffer; // "r+"/"w+" files are worked on in memory
	Ref<Str> _name, _mode;
	bool _binary, _closed;
	int _softspace;
	FileObject() : Object(&g_fileType), _in(nullptr), _out(nullptr), _buffer(nullptr), _binary(false), _closed(true), _softspace(0) {}
	~FileObject() override { closeFile(); }
	void closeFile() {
		delete _in;
		_in = nullptr;
		if (_out) {
			_out->finalize();
			delete _out;
			_out = nullptr;
		}
		_closed = true;
	}
};

static int fileError(const char *path, int errnum, const char *msg) {
	Ref<Int> e = newInt(errnum);
	Ref<Str> m = newStr(msg);
	Ref<Str> p = newStr(path);
	Ref<Tuple> args = tuplePack(3, e.get(), m.get(), p.get());
	setError(g_excIOError, args.get());
	return -1;
}

ObjRef newFileObject(const char *name, const char *mode) {
	FileSystem *fs = Interpreter::instance()->fileSystem();
	FileObject *f = new FileObject();
	ObjRef rf(f);
	f->_name = newStr(name);
	f->_mode = newStr(mode);
	f->_binary = strchr(mode, 'b') != nullptr;
	char kind = mode[0];
	bool plus = strchr(mode, '+') != nullptr;
	if (kind == 'r' && !plus) {
		f->_in = fs->openForReading(name);
		if (!f->_in) {
			fileError(name, 2, "No such file or directory");
			return ObjRef();
		}
	} else if (kind == 'w' || kind == 'a') {
		f->_out = fs->openForWriting(name, kind == 'a');
		if (!f->_out) {
			fileError(name, 13, "Permission denied");
			return ObjRef();
		}
	} else if (kind == 'r' && plus) {
		// Read and write: read it all, then write it back on close
		Common::SeekableReadStream *in = fs->openForReading(name);
		if (!in) {
			fileError(name, 2, "No such file or directory");
			return ObjRef();
		}
		delete in;
		setErrorString(g_excIOError, "files opened for update are not supported");
		return ObjRef();
	} else {
		setErrorFormat(g_excValueError, "mode string must begin with one of 'r', 'w', 'a' or 'U', not '%s'", mode);
		return ObjRef();
	}
	f->_closed = false;
	return rf;
}

static bool checkReadable(FileObject *f) {
	if (f->_closed) {
		setErrorString(g_excValueError, "I/O operation on closed file");
		return false;
	}
	if (!f->_in) {
		setError(g_excIOError, tuplePack(2, newInt(9).get(), newStr("Bad file descriptor").get()).get());
		return false;
	}
	return true;
}

static bool checkWritable(FileObject *f) {
	if (f->_closed) {
		setErrorString(g_excValueError, "I/O operation on closed file");
		return false;
	}
	if (!f->_out) {
		setError(g_excIOError, tuplePack(2, newInt(9).get(), newStr("Bad file descriptor").get()).get());
		return false;
	}
	return true;
}

// Reads one byte as the C runtime's text mode would: CR LF becomes LF, and
// ^Z ends the file
static int readChar(FileObject *f) {
	byte c;
	if (f->_in->read(&c, 1) != 1)
		return -1;
	if (f->_binary)
		return c;
	if (c == 0x1a) {
		f->_in->seek(-1, SEEK_CUR);
		return -1;
	}
	if (c == '\r') {
		byte next;
		if (f->_in->read(&next, 1) == 1) {
			if (next == '\n')
				return '\n';
			f->_in->seek(-1, SEEK_CUR);
		}
	}
	return c;
}

static Common::String readLine(FileObject *f, int32 limit) {
	Common::String line;
	while (limit < 0 || (int32)line.size() < limit) {
		int c = readChar(f);
		if (c < 0)
			break;
		line += (char)c;
		if (c == '\n')
			break;
	}
	return line;
}

#define FILE_SELF FileObject *self = (FileObject *)selfObj;

static ObjRef fileRead(Object *selfObj, Tuple *args, Dict *) {
	FILE_SELF
	int32 n = -1;
	if (!checkArgs("read", args, 0, 1))
		return ObjRef();
	if (args->_size && !asInt(args->get(0), &n))
		return ObjRef();
	if (!checkReadable(self))
		return ObjRef();
	if (self->_binary) {
		int64 avail = self->_in->size() - self->_in->pos();
		if (n < 0 || n > avail)
			n = (int32)avail;
		Ref<Str> s(new Str(n));
		uint32 got = self->_in->read(s->_data, n);
		if (got != (uint32)n)
			return newStr(s->_data, got);
		return s;
	}
	Common::String out;
	while (n < 0 || (int32)out.size() < n) {
		int c = readChar(self);
		if (c < 0)
			break;
		out += (char)c;
	}
	return newStr(out);
}

static ObjRef fileReadline(Object *selfObj, Tuple *args, Dict *) {
	FILE_SELF
	int32 n = -1;
	if (!checkArgs("readline", args, 0, 1))
		return ObjRef();
	if (args->_size && !asInt(args->get(0), &n))
		return ObjRef();
	if (!checkReadable(self))
		return ObjRef();
	if (n == 0)
		return newStr("");
	return newStr(readLine(self, n));
}

static ObjRef fileReadlines(Object *selfObj, Tuple *args, Dict *) {
	FILE_SELF
	if (!checkArgs("readlines", args, 0, 1))
		return ObjRef();
	if (!checkReadable(self))
		return ObjRef();
	Ref<List> l = newList();
	for (;;) {
		Common::String line = readLine(self, -1);
		if (line.empty())
			break;
		l->append(newStr(line).get());
	}
	return l;
}

static ObjRef fileWrite(Object *selfObj, Tuple *args, Dict *) {
	FILE_SELF
	if (!checkArgs("write", args, 1, 1))
		return ObjRef();
	Str *s = asStr(args->get(0));
	if (!s || !checkWritable(self))
		return ObjRef();
	if (self->_binary) {
		self->_out->write(s->_data, s->_len);
	} else {
		for (uint32 i = 0; i < s->_len; i++) {
			if (s->_data[i] == '\n')
				self->_out->writeByte('\r');
			self->_out->writeByte(s->_data[i]);
		}
	}
	return ObjRef(g_none);
}

static ObjRef fileWritelines(Object *selfObj, Tuple *args, Dict *kw) {
	if (!checkArgs("writelines", args, 1, 1))
		return ObjRef();
	ObjRef it = getIter(args->get(0));
	if (!it)
		return ObjRef();
	for (;;) {
		ObjRef line = iterNext(it.get());
		if (!line)
			return errorOccurred() ? ObjRef() : ObjRef(g_none);
		Ref<Tuple> a = tuplePack(1, line.get());
		if (!fileWrite(selfObj, a.get(), kw))
			return ObjRef();
	}
}

static ObjRef fileSeek(Object *selfObj, Tuple *args, Dict *) {
	FILE_SELF
	if (!checkArgs("seek", args, 1, 2))
		return ObjRef();
	int64 offset;
	int32 whence = 0;
	if (!longAsInt64(args->get(0), &offset)) {
		double d;
		clearError();
		if (!asDouble(args->get(0), &d))
			return ObjRef();
		offset = (int64)d;
	}
	if (args->_size > 1 && !asInt(args->get(1), &whence))
		return ObjRef();
	if (!checkReadable(self))
		return ObjRef();
	int w = whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET;
	if (!self->_in->seek(offset, w)) {
		setError(g_excIOError, tuplePack(2, newInt(22).get(), newStr("Invalid argument").get()).get());
		return ObjRef();
	}
	return ObjRef(g_none);
}

static ObjRef fileTell(Object *selfObj, Tuple *args, Dict *) {
	FILE_SELF
	if (!checkArgs("tell", args, 0, 0))
		return ObjRef();
	if (self->_closed) {
		setErrorString(g_excValueError, "I/O operation on closed file");
		return ObjRef();
	}
	if (self->_in)
		return newIntOrLong(self->_in->pos());
	return newIntOrLong(self->_out->pos());
}

static ObjRef fileClose(Object *selfObj, Tuple *args, Dict *) {
	FILE_SELF
	if (!checkArgs("close", args, 0, 0))
		return ObjRef();
	self->closeFile();
	return ObjRef(g_none);
}

static ObjRef fileFlush(Object *selfObj, Tuple *args, Dict *) {
	FILE_SELF
	if (self->_closed) {
		setErrorString(g_excValueError, "I/O operation on closed file");
		return ObjRef();
	}
	if (self->_out)
		self->_out->flush();
	return ObjRef(g_none);
}

static ObjRef fileIsatty(Object *, Tuple *, Dict *) {
	return newBool(false);
}

static ObjRef fileFileno(Object *selfObj, Tuple *, Dict *) {
	return newInt(3 + (objectAddress(selfObj) >> 4) % 1000);
}

static ObjRef fileXreadlines(Object *selfObj, Tuple *, Dict *) {
	return ObjRef(selfObj);
}

static ObjRef fileIter(Object *o) {
	return ObjRef(o);
}

static ObjRef fileIterNext(Object *o) {
	FileObject *f = (FileObject *)o;
	if (!checkReadable(f))
		return ObjRef();
	Common::String line = readLine(f, -1);
	if (line.empty())
		return ObjRef();
	return newStr(line);
}

static const MethodDef g_fileMethods[] = {
	{ "read", fileRead, kMethVarArgs, nullptr },
	{ "readline", fileReadline, kMethVarArgs, nullptr },
	{ "readlines", fileReadlines, kMethVarArgs, nullptr },
	{ "write", fileWrite, kMethVarArgs, nullptr },
	{ "writelines", fileWritelines, kMethVarArgs, nullptr },
	{ "seek", fileSeek, kMethVarArgs, nullptr },
	{ "tell", fileTell, kMethVarArgs, nullptr },
	{ "close", fileClose, kMethVarArgs, nullptr },
	{ "flush", fileFlush, kMethVarArgs, nullptr },
	{ "isatty", fileIsatty, kMethVarArgs, nullptr },
	{ "fileno", fileFileno, kMethVarArgs, nullptr },
	{ "xreadlines", fileXreadlines, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef fileGetName(Object *o) { return ObjRef(((FileObject *)o)->_name.get()); }
static ObjRef fileGetMode(Object *o) { return ObjRef(((FileObject *)o)->_mode.get()); }
static ObjRef fileGetClosed(Object *o) { return newBool(((FileObject *)o)->_closed); }
static ObjRef fileGetSoftspace(Object *o) { return newInt(((FileObject *)o)->_softspace); }
static int fileSetSoftspace(Object *o, Object *v) {
	int32 x;
	if (!v || !asInt(v, &x))
		return -1;
	((FileObject *)o)->_softspace = x;
	return 0;
}

static const GetSetDef g_fileGetSets[] = {
	{ "name", fileGetName, nullptr },
	{ "mode", fileGetMode, nullptr },
	{ "closed", fileGetClosed, nullptr },
	{ "softspace", fileGetSoftspace, fileSetSoftspace },
	{ nullptr, nullptr, nullptr }
};

static ObjRef fileRepr(Object *o) {
	FileObject *f = (FileObject *)o;
	return newStr(Common::String::format("<%s file '%s', mode '%s' at 0x%08X>", f->_closed ? "closed" : "open",
	                                     f->_name->_data, f->_mode->_data, objectAddress(o)));
}

static ObjRef fileNew(Type *, Tuple *args, Dict *kw) {
	if (!checkArgs("file", args, 1, 3))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	const char *mode = "r";
	if (args->_size > 1) {
		Str *m = asStr(args->get(1));
		if (!m)
			return ObjRef();
		mode = m->_data;
	}
	Common::String m(mode);
	// 'U' (universal newlines) arrived in 2.3; 't' is the default anyway
	return newFileObject(name->_data, m.c_str());
}

void initFileType() {
	g_fileType.methods = g_fileMethods;
	g_fileType.getSets = g_fileGetSets;
	g_fileType.repr = fileRepr;
	g_fileType.iter = fileIter;
	g_fileType.iterNext = fileIterNext;
	g_fileType.newObj = fileNew;
	g_fileType.getAttr = genericGetAttr;
	g_fileType.setAttr = genericSetAttr;
	g_fileType._flags |= kTypeBaseType;
}

} // End of namespace Python
} // End of namespace Yaga
