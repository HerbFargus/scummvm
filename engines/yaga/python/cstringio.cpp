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

#include "yaga/python/object.h"
#include "yaga/python/internal.h"

// The cStringIO module of Python 2.2: StringO (output) and StringI (input)
// objects, which cPickle also reads and writes directly.

namespace Yaga {
namespace Python {

static Type g_stringOType("cStringIO.StringO", &g_objectType);
static Type g_stringIType("cStringIO.StringI", &g_objectType);

struct StringIO : public Object {
	Common::Array<char> _buf; // output data (may run past _size)
	Ref<Str> _in;             // input data
	bool _closed;
	int32 _pos, _size;
	int32 _softspace;
	explicit StringIO(Type *t) : Object(t), _closed(false), _pos(0), _size(0), _softspace(0) {}
	bool isOutput() const { return _type == &g_stringOType; }
	const char *data() const {
		if (isOutput())
			return _buf.empty() ? "" : &_buf[0];
		return _in->_data;
	}
};

static bool openCheck(StringIO *s) {
	if (s->_closed) {
		setErrorString(g_excValueError, "I/O operation on closed file");
		return false;
	}
	return true;
}

// The C-level interface, as PycStringIO_CAPI

bool isStringIOOutput(Object *o) {
	return isType(o, &g_stringOType);
}

bool isStringIOInput(Object *o) {
	return isType(o, &g_stringIType);
}

// Returns the number of bytes available (up to n; all if n < 0), or -1
int32 stringIORead(Object *o, const char **out, int32 n) {
	StringIO *s = (StringIO *)o;
	if (!openCheck(s))
		return -1;
	int32 l = s->_size - s->_pos;
	if (n < 0 || n > l) {
		n = l;
		if (n < 0)
			n = 0;
	}
	*out = s->data() + s->_pos;
	s->_pos += n;
	return n;
}

int32 stringIOReadline(Object *o, const char **out) {
	StringIO *s = (StringIO *)o;
	if (!openCheck(s))
		return -1;
	const char *d = s->data();
	int32 i = s->_pos;
	while (i < s->_size && d[i] != '\n')
		i++;
	if (i < s->_size)
		i++;
	int32 l = i - s->_pos;
	if (l < 0)
		l = 0;
	*out = d + s->_pos;
	s->_pos += l;
	return l;
}

int32 stringIOWrite(Object *o, const char *c, int32 l) {
	StringIO *s = (StringIO *)o;
	if (!openCheck(s))
		return -1;
	int32 newl = s->_pos + l;
	if (newl > (int32)s->_buf.size())
		s->_buf.resize(newl);
	if (l)
		memcpy(&s->_buf[s->_pos], c, l);
	s->_pos += l;
	if (s->_size < s->_pos)
		s->_size = s->_pos;
	return l;
}

ObjRef stringIOGetValue(Object *o) {
	StringIO *s = (StringIO *)o;
	if (!openCheck(s))
		return ObjRef();
	return newStr(s->data(), s->_size);
}

ObjRef newStringIOOutput() {
	StringIO *s = new StringIO(&g_stringOType);
	ObjRef r(s);
	s->_buf.reserve(128);
	return r;
}

ObjRef newStringIOInput(Object *o) {
	Ref<Str> data;
	if (isInstanceOf(o, &g_strType)) {
		data = (Str *)o;
	} else if (isInstanceOf(o, &g_unicodeType)) {
		// Its buffer: the UCS-2 data
		Unicode *u = (Unicode *)o;
		Common::String raw;
		for (uint32 i = 0; i < u->_chars.size(); i++) {
			raw += (char)(u->_chars[i] & 0xff);
			raw += (char)((u->_chars[i] >> 8) & 0xff);
		}
		data = newStr(raw);
	} else {
		setErrorFormat(g_excTypeError, "expected read buffer, %.200s found", typeName(o));
		return ObjRef();
	}
	StringIO *s = new StringIO(&g_stringIType);
	ObjRef r(s);
	s->_in = data;
	s->_size = data->_len;
	return r;
}

// ---- Methods ----------------------------------------------------------------

#define IO_SELF StringIO *self = (StringIO *)selfObj;

static bool optionalInt(const char *fname, Tuple *args, int32 *out) {
	if (!checkArgs(fname, args, 0, 1))
		return false;
	return !args->_size || asInt(args->get(0), out);
}

static ObjRef ioFlush(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	if (!openCheck(self) || !checkArgs("flush", args, 0, 0))
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef ioGetValue(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	if (!openCheck(self) || !checkArgs("getval", args, 0, 1))
		return ObjRef();
	int32 n = self->_size;
	if (args->_size) {
		int t = isTrue(args->get(0));
		if (t < 0)
			return ObjRef();
		if (t)
			n = MIN(self->_pos, self->_size);
	}
	return newStr(self->data(), n);
}

static ObjRef ioIsatty(Object *, Tuple *args, Dict *) {
	if (!checkArgs("isatty", args, 0, 0))
		return ObjRef();
	return newInt(0);
}

static ObjRef ioRead(Object *selfObj, Tuple *args, Dict *) {
	int32 n = -1;
	if (!optionalInt("read", args, &n))
		return ObjRef();
	const char *out;
	n = stringIORead(selfObj, &out, n);
	if (n < 0)
		return ObjRef();
	return newStr(out, n);
}

static ObjRef ioReadline(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	int32 m = -1;
	if (!optionalInt("readline", args, &m))
		return ObjRef();
	const char *out;
	int32 n = stringIOReadline(selfObj, &out);
	if (n < 0)
		return ObjRef();
	if (m >= 0 && m < n) {
		self->_pos -= n - m;
		n = m;
	}
	return newStr(out, n);
}

static ObjRef ioReadlines(Object *selfObj, Tuple *args, Dict *) {
	int32 hint = 0, length = 0;
	if (!optionalInt("readlines", args, &hint))
		return ObjRef();
	Ref<List> result = newList();
	for (;;) {
		const char *out;
		int32 n = stringIOReadline(selfObj, &out);
		if (n < 0)
			return ObjRef();
		if (n == 0)
			break;
		result->append(newStr(out, n).get());
		length += n;
		if (hint > 0 && length >= hint)
			break;
	}
	return result;
}

static ObjRef ioReset(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	if (!openCheck(self) || !checkArgs("reset", args, 0, 0))
		return ObjRef();
	self->_pos = 0;
	return ObjRef(g_none);
}

static ObjRef ioTell(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	if (!openCheck(self) || !checkArgs("tell", args, 0, 0))
		return ObjRef();
	return newInt(self->_pos);
}

static ObjRef ioTruncate(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	int32 pos = -1;
	if (!openCheck(self) || !optionalInt("truncate", args, &pos))
		return ObjRef();
	if (pos < 0)
		pos = self->_pos;
	if (self->_size > pos)
		self->_size = pos;
	return ObjRef(g_none);
}

static bool seekArgs(StringIO *self, Tuple *args, int32 *position) {
	int32 mode = 0;
	if (!openCheck(self) || !checkArgs("seek", args, 1, 2) || !asInt(args->get(0), position))
		return false;
	if (args->_size > 1 && !asInt(args->get(1), &mode))
		return false;
	if (mode == 2)
		*position += self->_size;
	else if (mode == 1)
		*position += self->_pos;
	return true;
}

static ObjRef oSeek(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	int32 position;
	if (!seekArgs(self, args, &position))
		return ObjRef();
	if (position < 0)
		position = 0;
	if (position > (int32)self->_buf.size())
		self->_buf.resize(position);
	self->_pos = position;
	// The gap past the end reads as zeros
	for (int32 i = self->_size; i < position; i++)
		self->_buf[i] = 0;
	return ObjRef(g_none);
}

static ObjRef iSeek(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	int32 position;
	if (!seekArgs(self, args, &position))
		return ObjRef();
	if (position < 0)
		position = 0;
	self->_pos = position;
	return ObjRef(g_none);
}

static ObjRef oWrite(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("write", args, 1, 1))
		return ObjRef();
	Object *o = args->get(0);
	ObjRef s(o);
	if (isInstanceOf(o, &g_unicodeType)) {
		s = unicodeToStr(o);
		if (!s)
			return ObjRef();
	} else if (!isInstanceOf(o, &g_strType)) {
		setErrorFormat(g_excTypeError, "write() argument 1 must be string or read-only buffer, not %.50s", typeName(o));
		return ObjRef();
	}
	Str *str = (Str *)s.get();
	if (stringIOWrite(selfObj, str->_data, str->_len) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef oWritelines(Object *selfObj, Tuple *args, Dict *kw) {
	if (!checkArgs("writelines", args, 1, 1))
		return ObjRef();
	if (length(args->get(0)) < 0)
		return ObjRef();
	Ref<Str> empty = newStr("");
	ObjRef joined = strJoin(empty.get(), args->get(0));
	if (!joined)
		return ObjRef();
	Ref<Tuple> a = tuplePack(1, joined.get());
	return oWrite(selfObj, a.get(), kw);
}

static ObjRef ioClose(Object *selfObj, Tuple *args, Dict *) {
	IO_SELF
	if (!checkArgs("close", args, 0, 0))
		return ObjRef();
	self->_closed = true;
	self->_buf.clear();
	self->_in.reset();
	self->_pos = self->_size = 0;
	return ObjRef(g_none);
}

#define COMMON_METHODS \
	{ "flush", ioFlush, kMethVarArgs, "flush(): does nothing." }, \
	{ "getvalue", ioGetValue, kMethVarArgs, "getvalue([use_pos]) -- Get the string value.\nIf use_pos is specified and is a true value, then the string returned\nwill include only the text up to the current file position.\n" }, \
	{ "isatty", ioIsatty, kMethVarArgs, "isatty(): always returns 0" }, \
	{ "read", ioRead, kMethVarArgs, "read([s]) -- Read s characters, or the rest of the string" }, \
	{ "readline", ioReadline, kMethVarArgs, "readline() -- Read one line" }, \
	{ "readlines", ioReadlines, kMethVarArgs, "readlines() -- Read all lines" }, \
	{ "reset", ioReset, kMethVarArgs, "reset() -- Reset the file position to the beginning" }, \
	{ "tell", ioTell, kMethVarArgs, "tell() -- get the current position." }, \
	{ "truncate", ioTruncate, kMethVarArgs, "truncate(): truncate the file at the current position." },

static const char *const g_closeDoc = "close(): explicitly release resources held.";
static const char *const g_seekDoc = "seek(position)       -- set the current position\nseek(position, mode) -- mode 0: absolute; 1: relative; 2: relative to EOF";

static const MethodDef g_oMethods[] = {
	COMMON_METHODS
	{ "close", ioClose, kMethVarArgs, g_closeDoc },
	{ "seek", oSeek, kMethVarArgs, g_seekDoc },
	{ "write", oWrite, kMethVarArgs, "write(s) -- Write a string to the file\n\nNote (hack:) writing None resets the buffer" },
	{ "writelines", oWritelines, kMethVarArgs, "writelines(sequence_of_strings): write each string" },
	{ nullptr, nullptr, 0, nullptr }
};

static const MethodDef g_iMethods[] = {
	COMMON_METHODS
	{ "close", ioClose, kMethVarArgs, g_closeDoc },
	{ "seek", iSeek, kMethVarArgs, g_seekDoc },
	{ nullptr, nullptr, 0, nullptr }
};

// Attributes come from the method table only, as Py_FindMethod
static ObjRef findMethod(Object *o, const MethodDef *methods, Str *name) {
	if (name->equals("__methods__")) {
		Ref<List> l = newList();
		for (const MethodDef *d = methods; d->name; d++)
			l->append(newStr(d->name).get());
		if (listSort(l.get(), nullptr) < 0)
			return ObjRef();
		return l;
	}
	if (name->equals("__doc__")) {
		Object *doc = o->_type->_dict->getItemString("__doc__");
		return ObjRef(doc ? doc : g_none);
	}
	for (const MethodDef *d = methods; d->name; d++)
		if (name->equals(d->name))
			return ObjRef(new BuiltinFunction(d, o));
	setErrorString(g_excAttributeError, name->_data);
	return ObjRef();
}

static ObjRef oGetAttr(Object *o, Str *name) {
	if (name->equals("softspace"))
		return newInt(((StringIO *)o)->_softspace);
	return findMethod(o, g_oMethods, name);
}

static int oSetAttr(Object *o, Str *name, Object *value) {
	if (!name->equals("softspace")) {
		setErrorString(g_excAttributeError, name->_data);
		return -1;
	}
	int32 x;
	if (!value || !asInt(value, &x))
		return -1;
	((StringIO *)o)->_softspace = x;
	return 0;
}

static ObjRef iGetAttr(Object *o, Str *name) {
	return findMethod(o, g_iMethods, name);
}

// Iterating over an input object calls readline until it returns ''
static ObjRef iIter(Object *o) {
	ObjRef readline = getAttrString(o, "readline");
	if (!readline)
		return ObjRef();
	Ref<Str> empty = newStr("");
	return ObjRef(new CallIter(readline.get(), empty.get()));
}

static ObjRef cStringIONew(Object *, Tuple *args, Dict *) {
	if (!checkArgs("StringIO", args, 0, 1))
		return ObjRef();
	if (args->_size)
		return newStringIOInput(args->get(0));
	return newStringIOOutput();
}

static const MethodDef g_cStringIOMethods[] = {
	{ "StringIO", cStringIONew, kMethVarArgs, "StringIO([s]) -- Return a StringIO-like stream for reading or writing" },
	{ nullptr, nullptr, 0, nullptr }
};

static void readyType(Type *t, const char *doc) {
	if (t->_flags & kTypeReady)
		return;
	t->_dict = newDict();
	Ref<Str> d = newStr(doc);
	t->_dict->setItemString("__doc__", d.get());
	typeReady(t);
}

void initStringIOTypes() {
	g_stringOType.getAttr = oGetAttr;
	g_stringOType.setAttr = oSetAttr;
	readyType(&g_stringOType, "Simple type for output to strings.");
	g_stringIType.getAttr = iGetAttr;
	g_stringIType.iter = iIter;
	readyType(&g_stringIType, "Simple type for treating strings as input file streams");
	g_stringIType.setAttr = nullptr; // read-only attributes

}

void initCStringIOModule(Module *m) {
	initStringIOTypes();
	Ref<Str> modName = newStr("cStringIO");
	for (const MethodDef *d = g_cStringIOMethods; d->name; d++) {
		BuiltinFunction *f = new BuiltinFunction(d, nullptr);
		ObjRef rf(f);
		f->_module = modName;
		moduleAddObject(m, d->name, f);
	}
	moduleAddString(m, "__doc__", "A simple fast partial StringIO replacement.\n\nThis module provides a simple useful replacement for\n"
		"the StringIO module that is written in C.  It does not provide the\nfull generality of StringIO, but it provides enough for most\n"
		"applications and is especially useful in conjunction with the\npickle module.\n\nUsage:\n\n  from cStringIO import StringIO\n\n"
		"  an_output_stream=StringIO()\n  an_output_stream.write(some_stuff)\n  ...\n  value=an_output_stream.getvalue()\n\n"
		"  an_input_stream=StringIO(a_string)\n  spam=an_input_stream.readline()\n  spam=an_input_stream.read(5)\n"
		"  an_input_stream.seek(0)           # OK, start over\n  spam=an_input_stream.read()       # and read it all\n  \n"
		"If someone else wants to provide a more complete implementation,\ngo for it. :-)  \n\n"
		"cStringIO.c,v 1.29 1999/06/15 14:10:27 jim Exp\n");
	moduleAddObject(m, "InputType", &g_stringIType);
	moduleAddObject(m, "OutputType", &g_stringOType);
}

} // End of namespace Python
} // End of namespace Yaga
