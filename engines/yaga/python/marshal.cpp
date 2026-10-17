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

// Python 2.2's marshal format, in which the games' bytecode is stored

namespace Yaga {
namespace Python {

namespace {

struct Reader {
	const uint8 *p;
	const uint8 *end;
	bool eof;

	int byte() {
		if (p >= end) {
			eof = true;
			return -1;
		}
		return *p++;
	}
	int32 i16() {
		int32 x = byte();
		x |= byte() << 8;
		x |= -(x & 0x8000); // sign-extend
		return x;
	}
	int32 i32() {
		uint32 x = (uint32)byte();
		x |= (uint32)byte() << 8;
		x |= (uint32)byte() << 16;
		x |= (uint32)byte() << 24;
		return (int32)x;
	}
};

bool allNameChars(const Str *s) {
	for (uint32 i = 0; i < s->_len; i++) {
		char c = s->_data[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
			return false;
	}
	return true;
}

void internTuple(Tuple *t) {
	for (uint32 i = 0; t && i < t->_size; i++) {
		Object *o = t->get(i);
		if (isType(o, &g_strType)) {
			Ref<Str> s = intern((Str *)o);
			t->set(i, s.get());
		}
	}
}

ObjRef readObject(Reader &r) {
	int type = r.byte();
	switch (type) {
	case -1:
		setErrorString(g_excEOFError, "EOF read where object expected");
		return ObjRef();
	case '0':
		return ObjRef(); // NULL, only valid inside a dict
	case 'N':
		return ObjRef(g_none);
	case 'S':
		return ObjRef(g_excStopIteration);
	case '.':
		return ObjRef(g_ellipsis);
	case 'F':
		return newBool(false);
	case 'T':
		return newBool(true);
	case 'i':
		return newInt(r.i32());
	case 'I': {
		uint32 lo = (uint32)r.i32();
		int32 hi = r.i32();
		return newIntOrLong(((int64)hi << 32) | lo);
	}
	case 'l': {
		int32 n = r.i32();
		Long *l = new Long();
		ObjRef rl(l);
		uint32 size = n < 0 ? -n : n;
		l->_digits.resize(size);
		for (uint32 i = 0; i < size; i++) {
			int32 d = r.i16();
			if (d < 0 || d >= (1 << 15)) {
				setErrorString(g_excValueError, "bad marshal data");
				return ObjRef();
			}
			l->_digits[i] = (uint16)d;
		}
		l->_negative = n < 0;
		return longNormalize(l);
	}
	case 'f': {
		int n = r.byte();
		if (n < 0 || r.end - r.p < n) {
			setErrorString(g_excEOFError, "EOF read where object expected");
			return ObjRef();
		}
		Common::String s((const char *)r.p, n);
		r.p += n;
		return newFloat(strtod(s.c_str(), nullptr));
	}
	case 'x': {
		int n = r.byte();
		Common::String re((const char *)r.p, n);
		r.p += n;
		n = r.byte();
		Common::String im((const char *)r.p, n);
		r.p += n;
		return ObjRef(new Complex(strtod(re.c_str(), nullptr), strtod(im.c_str(), nullptr)));
	}
	case 's':
	case 't': {
		int32 n = r.i32();
		if (n < 0 || r.end - r.p < n) {
			setErrorString(g_excEOFError, "EOF read where object expected");
			return ObjRef();
		}
		Ref<Str> s = newStr((const char *)r.p, n);
		r.p += n;
		return s;
	}
	case 'u': {
		int32 n = r.i32();
		if (n < 0 || r.end - r.p < n) {
			setErrorString(g_excEOFError, "EOF read where object expected");
			return ObjRef();
		}
		Ref<Str> s = newStr((const char *)r.p, n);
		r.p += n;
		Ref<Str> enc = newStr("utf-8");
		return unicodeFromStr(s.get(), "utf-8", "strict");
	}
	case '(': {
		int32 n = r.i32();
		if (n < 0) {
			setErrorString(g_excValueError, "bad marshal data");
			return ObjRef();
		}
		Ref<Tuple> t = newTuple(n);
		for (int32 i = 0; i < n; i++) {
			ObjRef item = readObject(r);
			if (!item) {
				if (!errorOccurred())
					setErrorString(g_excTypeError, "NULL object in marshal data");
				return ObjRef();
			}
			t->set(i, item.get());
		}
		return t;
	}
	case '[': {
		int32 n = r.i32();
		if (n < 0) {
			setErrorString(g_excValueError, "bad marshal data");
			return ObjRef();
		}
		Ref<List> l = newList();
		for (int32 i = 0; i < n; i++) {
			ObjRef item = readObject(r);
			if (!item) {
				if (!errorOccurred())
					setErrorString(g_excTypeError, "NULL object in marshal data");
				return ObjRef();
			}
			l->append(item.get());
		}
		return l;
	}
	case '{': {
		Ref<Dict> d = newDict();
		for (;;) {
			ObjRef key = readObject(r);
			if (!key)
				break;
			ObjRef value = readObject(r);
			if (!value)
				break;
			d->setItem(key.get(), value.get());
		}
		if (errorOccurred())
			return ObjRef();
		return d;
	}
	case 'c': {
		Code *co = new Code();
		ObjRef rc(co);
		co->_argCount = r.i16();
		co->_nLocals = r.i16();
		co->_stackSize = r.i16();
		co->_flags = r.i16();
		ObjRef code = readObject(r);
		ObjRef consts = code ? readObject(r) : ObjRef();
		ObjRef names = consts ? readObject(r) : ObjRef();
		ObjRef varnames = names ? readObject(r) : ObjRef();
		ObjRef freevars = varnames ? readObject(r) : ObjRef();
		ObjRef cellvars = freevars ? readObject(r) : ObjRef();
		ObjRef filename = cellvars ? readObject(r) : ObjRef();
		ObjRef name = filename ? readObject(r) : ObjRef();
		if (!name)
			return ObjRef();
		co->_firstLineNo = r.i16();
		ObjRef lnotab = readObject(r);
		if (!lnotab)
			return ObjRef();
		if (!isType(code.get(), &g_strType) || !isType(consts.get(), &g_tupleType) || !isType(names.get(), &g_tupleType) ||
		    !isType(varnames.get(), &g_tupleType) || !isType(freevars.get(), &g_tupleType) ||
		    !isType(cellvars.get(), &g_tupleType) || !isType(name.get(), &g_strType) || !isType(filename.get(), &g_strType)) {
			setErrorString(g_excValueError, "bad marshal data");
			return ObjRef();
		}
		co->_code = (Str *)code.get();
		co->_consts = (Tuple *)consts.get();
		co->_names = (Tuple *)names.get();
		co->_varNames = (Tuple *)varnames.get();
		co->_freeVars = (Tuple *)freevars.get();
		co->_cellVars = (Tuple *)cellvars.get();
		co->_filename = (Str *)filename.get();
		co->_name = intern((Str *)name.get());
		co->_lnotab = isType(lnotab.get(), &g_strType) ? (Str *)lnotab.get() : nullptr;
		// As PyCode_New: intern the names, and string constants that look like names
		internTuple(co->_names.get());
		internTuple(co->_varNames.get());
		internTuple(co->_freeVars.get());
		internTuple(co->_cellVars.get());
		Tuple *ct = co->_consts.get();
		for (uint32 i = 0; i < ct->_size; i++) {
			Object *o = ct->get(i);
			if (isType(o, &g_strType) && allNameChars((Str *)o)) {
				Ref<Str> s = intern((Str *)o);
				ct->set(i, s.get());
			}
		}
		return rc;
	}
	default:
		setErrorString(g_excValueError, "bad marshal data");
		return ObjRef();
	}
}

struct Writer {
	Common::String out;
	bool error;
	void byte(int c) { out += (char)c; }
	void i16(int32 x) {
		byte(x & 0xff);
		byte((x >> 8) & 0xff);
	}
	void i32(int32 x) {
		byte(x & 0xff);
		byte((x >> 8) & 0xff);
		byte((x >> 16) & 0xff);
		byte((x >> 24) & 0xff);
	}
	void bytes(const char *s, uint32 n) { out += Common::String(s, n); }
};

void writeObject(Writer &w, Object *v) {
	if (!v) {
		w.byte('0');
	} else if (v == g_none) {
		w.byte('N');
	} else if (v == g_excStopIteration) {
		w.byte('S');
	} else if (v == g_ellipsis) {
		w.byte('.');
	} else if (isType(v, &g_intType)) {
		w.byte('i');
		w.i32(((Int *)v)->_value);
	} else if (isType(v, &g_longType)) {
		Long *l = (Long *)v;
		w.byte('l');
		int32 n = l->_digits.size();
		w.i32(l->_negative ? -n : n);
		for (int32 i = 0; i < n; i++)
			w.i16(l->_digits[i]);
	} else if (isType(v, &g_floatType)) {
		Common::String s = floatRepr(((Float *)v)->_value);
		w.byte('f');
		w.byte(s.size());
		w.bytes(s.c_str(), s.size());
	} else if (isType(v, &g_strType)) {
		Str *s = (Str *)v;
		w.byte('s');
		w.i32(s->_len);
		w.bytes(s->_data, s->_len);
	} else if (isType(v, &g_tupleType)) {
		Tuple *t = (Tuple *)v;
		w.byte('(');
		w.i32(t->_size);
		for (uint32 i = 0; i < t->_size; i++)
			writeObject(w, t->get(i));
	} else if (isType(v, &g_listType)) {
		List *l = (List *)v;
		w.byte('[');
		w.i32(l->size());
		for (uint32 i = 0; i < l->size(); i++)
			writeObject(w, l->get(i));
	} else if (isType(v, &g_dictType)) {
		Dict *d = (Dict *)v;
		w.byte('{');
		int32 pos = 0;
		Object *k, *val;
		while (d->next(&pos, &k, &val)) {
			writeObject(w, k);
			writeObject(w, val);
		}
		w.byte('0');
	} else if (isType(v, &g_codeType)) {
		Code *co = (Code *)v;
		w.byte('c');
		w.i16(co->_argCount);
		w.i16(co->_nLocals);
		w.i16(co->_stackSize);
		w.i16(co->_flags);
		writeObject(w, co->_code.get());
		writeObject(w, co->_consts.get());
		writeObject(w, co->_names.get());
		writeObject(w, co->_varNames.get());
		writeObject(w, co->_freeVars.get());
		writeObject(w, co->_cellVars.get());
		writeObject(w, co->_filename.get());
		writeObject(w, co->_name.get());
		w.i16(co->_firstLineNo);
		writeObject(w, co->_lnotab.get());
	} else if (isType(v, &g_unicodeType)) {
		ObjRef s = callMethod(v, "encode", 1, newStr("utf-8").get());
		if (!s) {
			w.error = true;
			return;
		}
		w.byte('u');
		w.i32(((Str *)s.get())->_len);
		w.bytes(((Str *)s.get())->_data, ((Str *)s.get())->_len);
	} else {
		w.error = true;
	}
}

} // End of anonymous namespace

ObjRef marshalLoads(const char *data, uint32 len, uint32 *consumed) {
	Reader r;
	r.p = (const uint8 *)data;
	r.end = r.p + len;
	r.eof = false;
	ObjRef o = readObject(r);
	if (!o && !errorOccurred())
		setErrorString(g_excTypeError, "NULL object in marshal data");
	if (consumed)
		*consumed = r.p - (const uint8 *)data;
	return o;
}

ObjRef marshalDumps(Object *o) {
	Writer w;
	w.error = false;
	writeObject(w, o);
	if (w.error) {
		setErrorString(g_excValueError, "unmarshallable object");
		return ObjRef();
	}
	return newStr(w.out);
}

// ---- The marshal module ----------------------------------------------------------

static ObjRef marshalLoadsFunc(Object *, Tuple *args, Dict *) {
	if (!checkArgs("loads", args, 1, 1))
		return ObjRef();
	Str *s = asStr(args->get(0));
	if (!s)
		return ObjRef();
	return marshalLoads(s->_data, s->_len);
}

static ObjRef marshalDumpsFunc(Object *, Tuple *args, Dict *) {
	if (!checkArgs("dumps", args, 1, 1))
		return ObjRef();
	return marshalDumps(args->get(0));
}

// load(file) reads from a file object: the whole remaining file is read, then
// the file is positioned just after the object
static ObjRef marshalLoad(Object *, Tuple *args, Dict *) {
	if (!checkArgs("load", args, 1, 1))
		return ObjRef();
	Object *f = args->get(0);
	ObjRef pos = callMethod(f, "tell", 0);
	if (!pos)
		return ObjRef();
	ObjRef data = callMethod(f, "read", 0);
	if (!data)
		return ObjRef();
	Str *s = asStr(data.get());
	if (!s)
		return ObjRef();
	uint32 used = 0;
	ObjRef result = marshalLoads(s->_data, s->_len, &used);
	if (!result)
		return ObjRef();
	int32 start;
	if (!asInt(pos.get(), &start))
		return ObjRef();
	Ref<Int> newPos = newInt(start + used);
	ObjRef r = callMethod(f, "seek", 1, newPos.get());
	if (!r)
		return ObjRef();
	return result;
}

static ObjRef marshalDump(Object *, Tuple *args, Dict *) {
	if (!checkArgs("dump", args, 2, 2))
		return ObjRef();
	ObjRef s = marshalDumps(args->get(0));
	if (!s)
		return ObjRef();
	ObjRef r = callMethod(args->get(1), "write", 1, s.get());
	if (!r)
		return ObjRef();
	return ObjRef(g_none);
}

static const MethodDef g_marshalMethods[] = {
	{ "dump", marshalDump, kMethVarArgs, nullptr },
	{ "load", marshalLoad, kMethVarArgs, nullptr },
	{ "dumps", marshalDumpsFunc, kMethVarArgs, nullptr },
	{ "loads", marshalLoadsFunc, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

void initMarshalModule() {
	initModule("marshal", g_marshalMethods);
}

} // End of namespace Python
} // End of namespace Yaga
