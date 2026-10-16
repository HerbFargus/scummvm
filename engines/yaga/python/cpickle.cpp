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

#include "common/util.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"

// The cPickle module of Python 2.2.3. The games save with it, so the bytes
// written must match CPython's, including its memo: like cPickle, an object
// is only given a PUT when its reference count says it might be shared.

namespace Yaga {
namespace Python {

// From cstringio.cpp
void initStringIOTypes();
bool isStringIOOutput(Object *o);
bool isStringIOInput(Object *o);
int32 stringIORead(Object *o, const char **out, int32 n);
int32 stringIOReadline(Object *o, const char **out);
int32 stringIOWrite(Object *o, const char *c, int32 l);
ObjRef stringIOGetValue(Object *o);
ObjRef newStringIOOutput();
ObjRef newStringIOInput(Object *o);

enum {
	kMark = '(', kStop = '.', kPop = '0', kPopMark = '1', kDup = '2', kFloat = 'F', kBinFloat = 'G',
	kInt = 'I', kBinInt = 'J', kBinInt1 = 'K', kLong = 'L', kBinInt2 = 'M', kNone = 'N',
	kPersId = 'P', kBinPersId = 'Q', kReduce = 'R', kString = 'S', kBinString = 'T',
	kShortBinString = 'U', kUnicode = 'V', kBinUnicode = 'X', kAppend = 'a', kBuild = 'b',
	kGlobal = 'c', kDict = 'd', kEmptyDict = '}', kAppends = 'e', kGet = 'g', kBinGet = 'h',
	kInst = 'i', kLongBinGet = 'j', kList = 'l', kEmptyList = ']', kObj = 'o', kPut = 'p',
	kBinPut = 'q', kLongBinPut = 'r', kSetItem = 's', kTuple = 't', kEmptyTuple = ')', kSetItems = 'u'
};

enum {
	kWriteBufSize = 256,
	kFastLimit = 50
};

// Module state, set up when cPickle is imported
static Object *g_pickleError = nullptr, *g_picklingError = nullptr, *g_unpickleableError = nullptr;
static Object *g_unpicklingError = nullptr, *g_badPickleGet = nullptr;
static Object *g_dispatchTable = nullptr, *g_safeConstructors = nullptr;

static Type g_picklerType("cPickle.Pickler", &g_objectType);
static Type g_unpicklerType("cPickle.Unpickler", &g_objectType);

// Calls func(o) through a reused one-item tuple, as cPickle's ARG_TUP: the
// tuple keeps o alive until the next call, which shows in reference counts
static ObjRef callArgTup(Ref<Tuple> &arg, Object *func, Object *o) {
	if (!arg)
		arg = newTuple(1);
	arg->set(0, o);
	ObjRef r = call(func, arg.get());
	if (arg && arg->_refs > 1)
		arg.reset();
	return r;
}

// "fmt" % args, as an error of the given class
static void errFormat(Object *exc, const char *fmt, Object *a, Object *b = nullptr, Object *c = nullptr) {
	Ref<Tuple> args = c ? tuplePack(3, a, b, c) : b ? tuplePack(2, a, b) : tuplePack(1, a);
	Ref<Str> f = newStr(fmt);
	ObjRef msg = strFormat(f.get(), args.get());
	if (msg)
		setError(exc, msg.get());
}

static Ref<Int> objectId(Object *o) {
	return newInt((int32)objectAddress(o));
}

// ---- Pickler ------------------------------------------------------------------------

enum {
	kWriteFile, kWriteStringIO, kWriteNone, kWriteOther
};

struct Pickler : public Object {
	ObjRef _file;
	Ref<List> _pdata; // a list-based pickler's output
	ObjRef _write;
	int _writeKind;
	Common::Array<char> _writeBuf;
	Ref<Dict> _memo;
	ObjRef _persFunc, _instPersFunc;
	Ref<Tuple> _arg;
	int32 _bin, _fast, _fastContainer;
	Ref<Dict> _fastMemo;
	ObjRef _dispatchTable;
	Pickler() : Object(&g_picklerType), _writeKind(kWriteOther), _bin(0), _fast(0), _fastContainer(0) {}
};

static int writeOther(Pickler *p, const char *s, int32 n) {
	ObjRef pyStr;
	if (!s) {
		if (p->_writeBuf.empty())
			return 0;
		pyStr = newStr(&p->_writeBuf[0], p->_writeBuf.size());
	} else {
		if (!p->_writeBuf.empty() && n + (int32)p->_writeBuf.size() > kWriteBufSize) {
			if (writeOther(p, nullptr, 0) < 0)
				return -1;
		}
		if (n <= kWriteBufSize) {
			for (int32 i = 0; i < n; i++)
				p->_writeBuf.push_back(s[i]);
			return n;
		}
		pyStr = newStr(s, n);
	}
	if (p->_write) {
		ObjRef junk = callArgTup(p->_arg, p->_write.get(), pyStr.get());
		if (!junk)
			return -1;
	} else {
		p->_pdata->append(pyStr.get());
	}
	p->_writeBuf.clear();
	return n;
}

// Writes n bytes (s null flushes); -1 on error
static int writeBytes(Pickler *p, const char *s, int32 n) {
	switch (p->_writeKind) {
	case kWriteFile: {
		if (!s)
			return 0;
		Ref<Str> str = newStr(s, n);
		ObjRef r = callFunction(p->_write.get(), 1, str.get());
		return r ? n : -1;
	}
	case kWriteStringIO:
		if (!s)
			return 0;
		return stringIOWrite(p->_file.get(), s, n);
	case kWriteNone:
		return s ? n : 0;
	default:
		return writeOther(p, s, n);
	}
}

static int writeChar(Pickler *p, char c) {
	return writeBytes(p, &c, 1);
}

static int writeString(Pickler *p, const Common::String &s) {
	return writeBytes(p, s.c_str(), s.size());
}

static bool fastSaveEnter(Pickler *p, Object *obj) {
	if (++p->_fastContainer >= kFastLimit) {
		if (!p->_fastMemo)
			p->_fastMemo = newDict();
		Ref<Int> key = objectId(obj);
		if (p->_fastMemo->getItem(key.get())) {
			setErrorFormat(g_excValueError, "fast mode: can't pickle cyclic objects including object type %s at 0x%08X",
			               typeName(obj), objectAddress(obj));
			p->_fastContainer = -1;
			return false;
		}
		if (p->_fastMemo->setItem(key.get(), g_none) < 0) {
			p->_fastContainer = -1;
			return false;
		}
	}
	return true;
}

static bool fastSaveLeave(Pickler *p, Object *obj) {
	if (p->_fastContainer-- >= kFastLimit) {
		Ref<Int> key = objectId(obj);
		if (p->_fastMemo->delItem(key.get()) < 0) {
			p->_fastContainer = -1;
			return false;
		}
	}
	return true;
}

static int memoGet(Pickler *p, Object *id) {
	Object *mv = p->_memo->getItem(id);
	if (!mv) {
		if (!errorOccurred())
			setError(g_excKeyError, id);
		return -1;
	}
	if (!isInstanceOf(mv, &g_tupleType) || ((Tuple *)mv)->_size < 1) {
		badInternalCall();
		return -1;
	}
	Object *value = ((Tuple *)mv)->get(0);
	if (!isInstanceOf(value, &g_intType)) {
		setErrorString(g_picklingError, "no int where int expected in memo");
		return -1;
	}
	int32 c = ((Int *)value)->_value;
	if (!p->_bin)
		return writeString(p, Common::String::format("%c%d\n", kGet, c));
	if (p->_pdata) {
		if (writeOther(p, nullptr, 0) < 0)
			return -1;
		p->_pdata->append(mv);
		return 0;
	}
	char s[5];
	if (c < 256) {
		s[0] = kBinGet;
		s[1] = (char)(c & 0xff);
		return writeBytes(p, s, 2);
	}
	s[0] = kLongBinGet;
	for (int i = 0; i < 4; i++)
		s[i + 1] = (char)((c >> (i * 8)) & 0xff);
	return writeBytes(p, s, 5);
}

static int put2(Pickler *p, Object *ob) {
	if (p->_fast)
		return 0;
	int32 n = p->_memo->_used + 1;
	Ref<Int> id = objectId(ob);
	Ref<Int> memoLen = newInt(n);
	Ref<Tuple> t = tuplePack(2, memoLen.get(), ob);
	if (p->_memo->setItem(id.get(), t.get()) < 0)
		return -1;
	if (!p->_bin)
		return writeString(p, Common::String::format("%c%d\n", kPut, n)) < 0 ? -1 : 0;
	if (p->_pdata) {
		if (writeOther(p, nullptr, 0) < 0)
			return -1;
		p->_pdata->append(memoLen.get());
		return 0;
	}
	char s[5];
	int len;
	if (n >= 256) {
		s[0] = kLongBinPut;
		for (int i = 0; i < 4; i++)
			s[i + 1] = (char)((n >> (i * 8)) & 0xff);
		len = 5;
	} else {
		s[0] = kBinPut;
		s[1] = (char)n;
		len = 2;
	}
	return writeBytes(p, s, len) < 0 ? -1 : 0;
}

static int put(Pickler *p, Object *ob) {
	if (ob->_refs < 2 || p->_fast)
		return 0;
	return put2(p, ob);
}

static int save(Pickler *p, Object *args, bool persSave);

static int saveInt(Pickler *p, Object *args) {
	int32 l = ((Int *)args)->_value;
	if (!p->_bin)
		return writeString(p, Common::String::format("%c%d\n", kInt, l));
	char s[5];
	s[1] = (char)(l & 0xff);
	s[2] = (char)((l >> 8) & 0xff);
	s[3] = (char)((l >> 16) & 0xff);
	s[4] = (char)((l >> 24) & 0xff);
	if (s[4] == 0 && s[3] == 0) {
		if (s[2] == 0) {
			s[0] = kBinInt1;
			return writeBytes(p, s, 2);
		}
		s[0] = kBinInt2;
		return writeBytes(p, s, 3);
	}
	s[0] = kBinInt;
	return writeBytes(p, s, 5);
}

static int saveLong(Pickler *p, Object *args) {
	ObjRef r = longFormat(args, 10, true);
	if (!r)
		return -1;
	Str *s = (Str *)r.get();
	if (writeChar(p, kLong) < 0 || writeBytes(p, s->_data, s->_len) < 0 || writeChar(p, '\n') < 0)
		return -1;
	return 0;
}

static int saveFloat(Pickler *p, Object *args) {
	double x = ((Float *)args)->_value;
	if (!p->_bin)
		return writeString(p, Common::String::format("%c", kFloat) + formatDouble(x, 17, 'g') + "\n");
	uint64 bits;
	if (x != x || x - x != x - x) {
		// MSVC's frexp gives 0.5, -1 for these, so cPickle wrote 0.25
		memcpy(&bits, &x, 8);
		bits = (bits & 0x8000000000000000ULL) | 0x3fd0000000000000ULL;
	} else if (x == 0.0) {
		bits = 0; // -0.0 too: the sign comes from x < 0
	} else {
		memcpy(&bits, &x, 8);
	}
	char s[9];
	s[0] = kBinFloat;
	for (int i = 0; i < 8; i++)
		s[i + 1] = (char)((bits >> ((7 - i) * 8)) & 0xff);
	return writeBytes(p, s, 9);
}

static int saveString(Pickler *p, Object *args, bool doPut) {
	Str *str = (Str *)args;
	int32 size = str->_len;
	if (!p->_bin) {
		ObjRef r = strRepr(str);
		if (!r)
			return -1;
		Str *rs = (Str *)r.get();
		if (writeChar(p, kString) < 0 || writeBytes(p, rs->_data, rs->_len) < 0 || writeChar(p, '\n') < 0)
			return -1;
	} else {
		char s[5];
		int len;
		if (size < 256) {
			s[0] = kShortBinString;
			s[1] = (char)size;
			len = 2;
		} else {
			s[0] = kBinString;
			for (int i = 1; i < 5; i++)
				s[i] = (char)((size >> ((i - 1) * 8)) & 0xff);
			len = 5;
		}
		if (writeBytes(p, s, len) < 0)
			return -1;
		if (size > 128 && p->_pdata) {
			if (writeOther(p, nullptr, 0) < 0)
				return -1;
			p->_pdata->append(args);
		} else if (writeBytes(p, str->_data, size) < 0) {
			return -1;
		}
	}
	if (doPut && put(p, args) < 0)
		return -1;
	return 0;
}

// Raw-unicode-escape, plus backslash and newline, as cPickle's own encoder;
// characters past the BMP become surrogate pairs, as in a UCS-2 build
static void appendUnicodeEscape(Common::String &out, uint32 ch) {
	static const char hexDigits[] = "0123456789ABCDEF";
	out += '\\';
	out += 'u';
	for (int shift = 12; shift >= 0; shift -= 4)
		out += hexDigits[(ch >> shift) & 0xf];
}

static int saveUnicode(Pickler *p, Object *args, bool doPut) {
	Unicode *u = (Unicode *)args;
	if (!p->_bin) {
		Common::String out;
		out += (char)kUnicode;
		for (uint32 i = 0; i < u->_chars.size(); i++) {
			uint32 ch = u->_chars[i];
			if (ch >= 0x10000) {
				appendUnicodeEscape(out, 0xd800 + ((ch - 0x10000) >> 10));
				appendUnicodeEscape(out, 0xdc00 + ((ch - 0x10000) & 0x3ff));
			} else if (ch >= 256 || ch == '\\' || ch == '\n') {
				appendUnicodeEscape(out, ch);
			} else {
				out += (char)ch;
			}
		}
		out += '\n';
		if (writeString(p, out) < 0)
			return -1;
	} else {
		Ref<Str> enc = newStr("utf-8");
		ObjRef r = callMethod(args, "encode", 1, enc.get());
		if (!r)
			return -1;
		Str *s = (Str *)r.get();
		char c[5];
		c[0] = kBinUnicode;
		for (int i = 1; i < 5; i++)
			c[i] = (char)((s->_len >> ((i - 1) * 8)) & 0xff);
		if (writeBytes(p, c, 5) < 0 || writeBytes(p, s->_data, s->_len) < 0)
			return -1;
	}
	if (doPut && put(p, args) < 0)
		return -1;
	return 0;
}

static int saveTuple(Pickler *p, Object *args) {
	Tuple *t = (Tuple *)args;
	if (writeChar(p, kMark) < 0)
		return -1;
	uint32 len = t->_size;
	for (uint32 i = 0; i < len; i++)
		if (save(p, t->get(i), false) < 0)
			return -1;
	Ref<Int> id = objectId(args);
	if (len && p->_memo->getItem(id.get())) {
		// The tuple was reached again through its own items
		if (p->_bin) {
			if (writeChar(p, kPopMark) < 0)
				return -1;
		} else {
			for (uint32 i = 0; i <= len; i++)
				if (writeChar(p, kPop) < 0)
					return -1;
		}
		return memoGet(p, id.get()) < 0 ? -1 : 0;
	}
	if (writeChar(p, kTuple) < 0)
		return -1;
	return put(p, args);
}

static int saveList(Pickler *p, Object *args) {
	List *l = (List *)args;
	int res = -1;
	if (p->_fast && !fastSaveEnter(p, args))
		goto finally;
	{
		if (p->_bin) {
			if (writeChar(p, kEmptyList) < 0)
				goto finally;
		} else if (writeChar(p, kMark) < 0 || writeChar(p, kList) < 0) {
			goto finally;
		}
		uint32 len = l->size();
		if ((len == 0 ? put(p, args) : put2(p, args)) < 0)
			goto finally;
		bool usingAppends = p->_bin && len > 1;
		if (usingAppends && writeChar(p, kMark) < 0)
			goto finally;
		for (uint32 i = 0; i < l->size() && i < len; i++) {
			if (save(p, l->get(i), false) < 0)
				goto finally;
			if (!usingAppends && writeChar(p, kAppend) < 0)
				goto finally;
		}
		if (usingAppends && writeChar(p, kAppends) < 0)
			goto finally;
		res = 0;
	}
finally:
	if (p->_fast && !fastSaveLeave(p, args))
		res = -1;
	return res;
}

static int saveDict(Pickler *p, Object *args) {
	Dict *d = (Dict *)args;
	int res = -1;
	if (p->_fast && !fastSaveEnter(p, args))
		goto finally;
	{
		if (p->_bin) {
			if (writeChar(p, kEmptyDict) < 0)
				goto finally;
		} else if (writeChar(p, kMark) < 0 || writeChar(p, kDict) < 0) {
			goto finally;
		}
		int32 len = d->_used;
		if ((len == 0 ? put(p, args) : put2(p, args)) < 0)
			goto finally;
		bool usingSetItems = p->_bin && d->_used > 1;
		if (usingSetItems && writeChar(p, kMark) < 0)
			goto finally;
		int32 pos = 0;
		Object *key, *value;
		while (d->next(&pos, &key, &value)) {
			if (save(p, key, false) < 0 || save(p, value, false) < 0)
				goto finally;
			if (!usingSetItems && writeChar(p, kSetItem) < 0)
				goto finally;
		}
		if (usingSetItems && writeChar(p, kSetItems) < 0)
			goto finally;
		res = 0;
	}
finally:
	if (p->_fast && !fastSaveLeave(p, args))
		res = -1;
	return res;
}

// The module a class or function lives in, as cPickle's whichmodule
static ObjRef whichModule(Object *global, Str *globalName) {
	ObjRef module = getAttrString(global, "__module__");
	if (module)
		return module;
	clearError();
	ObjRef modules = sysGetObject("modules");
	if (!modules)
		return ObjRef();
	if (isInstanceOf(modules.get(), &g_dictType)) {
		Dict *d = (Dict *)modules.get();
		int32 pos = 0;
		Object *name, *mod;
		while (d->next(&pos, &name, &mod)) {
			if (isInstanceOf(name, &g_strType) && ((Str *)name)->equals("__main__"))
				continue;
			ObjRef attr = getAttr(mod, globalName);
			if (!attr) {
				clearError();
				continue;
			}
			if (attr.get() == global)
				return ObjRef(name);
		}
	}
	return newStr("__main__");
}

static int saveGlobal(Pickler *p, Object *args, Object *name) {
	ObjRef globalName(name);
	if (!globalName) {
		globalName = getAttrString(args, "__name__");
		if (!globalName)
			return -1;
	}
	if (!isInstanceOf(globalName.get(), &g_strType)) {
		setErrorFormat(g_excTypeError, "expected string or Unicode object, %.200s found", typeName(globalName.get()));
		return -1;
	}
	ObjRef module = whichModule(args, (Str *)globalName.get());
	if (!module)
		return -1;
	if (!isInstanceOf(module.get(), &g_strType)) {
		setErrorFormat(g_excTypeError, "expected string or Unicode object, %.200s found", typeName(module.get()));
		return -1;
	}
	Str *moduleStr = (Str *)module.get();
	Str *nameStr = (Str *)globalName.get();
	ObjRef mod = importModule(moduleStr->_data);
	if (!mod) {
		clearError();
		errFormat(g_picklingError, "Can't pickle %s: it's not found as %s.%s", args, module.get(), globalName.get());
		return -1;
	}
	Dict *modDict = mod->getDict();
	Object *klass = modDict ? modDict->getItemString(nameStr->_data) : nullptr;
	if (!klass) {
		clearError();
		errFormat(g_picklingError, "Can't pickle %s: it's not found as %s.%s", args, module.get(), globalName.get());
		return -1;
	}
	if (klass != args) {
		errFormat(g_picklingError, "Can't pickle %s: it's not the same object as %s.%s", args, module.get(), globalName.get());
		return -1;
	}
	if (writeChar(p, kGlobal) < 0 || writeBytes(p, moduleStr->_data, moduleStr->_len) < 0 || writeChar(p, '\n') < 0 ||
	    writeBytes(p, nameStr->_data, nameStr->_len) < 0 || writeChar(p, '\n') < 0)
		return -1;
	return put(p, args);
}

static int saveInst(Pickler *p, Object *args) {
	int res = -1;
	ObjRef cls, getInitArgs, classArgs, getState, state;
	if (p->_fast && !fastSaveEnter(p, args))
		goto finally;
	if (writeChar(p, kMark) < 0)
		goto finally;
	cls = getAttrString(args, "__class__");
	if (!cls)
		goto finally;
	if (p->_bin && save(p, cls.get(), false) < 0)
		goto finally;
	getInitArgs = getAttrString(args, "__getinitargs__");
	if (getInitArgs) {
		classArgs = call(getInitArgs.get(), newTuple(0).get());
		if (!classArgs)
			goto finally;
		int32 len = length(classArgs.get());
		if (len < 0)
			goto finally;
		for (int32 i = 0; i < len; i++) {
			ObjRef element = sequenceGetItem(classArgs.get(), i);
			if (!element || save(p, element.get(), false) < 0)
				goto finally;
		}
	} else {
		clearError();
	}
	if (!p->_bin) {
		if (!isType(cls.get(), &g_classType)) {
			setErrorString(g_picklingError, "class has no name");
			goto finally;
		}
		Str *name = ((ClassObj *)cls.get())->_name.get();
		ObjRef module = whichModule(cls.get(), name);
		if (!module)
			goto finally;
		if (!isInstanceOf(module.get(), &g_strType)) {
			setErrorFormat(g_excTypeError, "expected string or Unicode object, %.200s found", typeName(module.get()));
			goto finally;
		}
		Str *moduleStr = (Str *)module.get();
		if (writeChar(p, kInst) < 0 || writeBytes(p, moduleStr->_data, moduleStr->_len) < 0 || writeChar(p, '\n') < 0 ||
		    writeBytes(p, name->_data, name->_len) < 0 || writeChar(p, '\n') < 0)
			goto finally;
	} else if (writeChar(p, kObj) < 0) {
		goto finally;
	}
	getState = getAttrString(args, "__getstate__");
	if (getState) {
		state = call(getState.get(), newTuple(0).get());
		if (!state)
			goto finally;
	} else {
		clearError();
		state = getAttrString(args, "__dict__");
		if (!state) {
			clearError();
			res = 0;
			goto finally;
		}
	}
	if ((!isInstanceOf(state.get(), &g_dictType) ? put2(p, args) : put(p, args)) < 0)
		goto finally;
	if (save(p, state.get(), false) < 0)
		goto finally;
	if (writeChar(p, kBuild) < 0)
		goto finally;
	res = 0;
finally:
	if (p->_fast && !fastSaveLeave(p, args))
		res = -1;
	return res;
}

static int savePers(Pickler *p, Object *args, Object *f) {
	ObjRef pid = callArgTup(p->_arg, f, args);
	if (!pid)
		return -1;
	if (pid.get() == g_none)
		return 0;
	if (!p->_bin) {
		if (!isInstanceOf(pid.get(), &g_strType)) {
			setErrorString(g_picklingError, "persistent id must be string");
			return -1;
		}
		Str *s = (Str *)pid.get();
		if (writeChar(p, kPersId) < 0 || writeBytes(p, s->_data, s->_len) < 0 || writeChar(p, '\n') < 0)
			return -1;
		return 1;
	}
	if (save(p, pid.get(), true) < 0)
		return -1;
	return writeChar(p, kBinPersId) < 0 ? -1 : 1;
}

static int saveReduce(Pickler *p, Object *callable, Object *tup, Object *state, Object *ob) {
	if (save(p, callable, false) < 0 || save(p, tup, false) < 0 || writeChar(p, kReduce) < 0)
		return -1;
	if (ob) {
		if (((state && !isInstanceOf(state, &g_dictType)) ? put2(p, ob) : put(p, ob)) < 0)
			return -1;
	}
	if (state) {
		if (save(p, state, false) < 0 || writeChar(p, kBuild) < 0)
			return -1;
	}
	return 0;
}

static int save(Pickler *p, Object *args, bool persSave) {
	if (!persSave && p->_persFunc) {
		int tmp = savePers(p, args, p->_persFunc.get());
		if (tmp != 0)
			return tmp;
	}
	if (args == g_none)
		return writeChar(p, kNone);

	Type *type = args->_type;
	if (type == &g_intType)
		return saveInt(p, args);
	if (type == &g_longType)
		return saveLong(p, args);
	if (type == &g_floatType)
		return saveFloat(p, args);
	if (type == &g_tupleType && ((Tuple *)args)->_size == 0)
		return p->_bin ? writeChar(p, kEmptyTuple) : saveTuple(p, args);
	if (type == &g_strType && ((Str *)args)->_len < 2)
		return saveString(p, args, false);
	if (type == &g_unicodeType && ((Unicode *)args)->_chars.size() < 2)
		return saveUnicode(p, args, false);

	if (args->_refs > 1) {
		Ref<Int> id = objectId(args);
		if (p->_memo->getItem(id.get()))
			return memoGet(p, id.get()) < 0 ? -1 : 0;
		if (errorOccurred())
			return -1;
	}

	if (type == &g_strType)
		return saveString(p, args, true);
	if (type == &g_unicodeType)
		return saveUnicode(p, args, true);
	if (type == &g_tupleType)
		return saveTuple(p, args);
	if (type == &g_typeType)
		return saveGlobal(p, args, nullptr);
	if (type == &g_listType)
		return saveList(p, args);
	if (type == &g_dictType)
		return saveDict(p, args);
	if (type == &g_instanceType)
		return saveInst(p, args);
	if (type == &g_classType || type == &g_functionType || type == &g_builtinFunctionType)
		return saveGlobal(p, args, nullptr);

	if (!persSave && p->_instPersFunc) {
		int tmp = savePers(p, args, p->_instPersFunc.get());
		if (tmp != 0)
			return tmp;
	}

	if (isSubtype(type, &g_typeType))
		return saveGlobal(p, args, nullptr);

	ObjRef reduce, t;
	Object *table = p->_dispatchTable.get();
	Object *reducer = table && isInstanceOf(table, &g_dictType) ? ((Dict *)table)->getItem(type) : nullptr;
	if (reducer) {
		reduce = reducer;
		t = callArgTup(p->_arg, reduce.get(), args);
		if (!t)
			return -1;
	} else {
		clearError();
		reduce = getAttrString(args, "__reduce__");
		if (reduce) {
			t = call(reduce.get(), newTuple(0).get());
			if (!t)
				return -1;
		} else {
			clearError();
		}
	}

	if (t) {
		if (isInstanceOf(t.get(), &g_strType))
			return saveGlobal(p, args, t.get());
		if (!isInstanceOf(t.get(), &g_tupleType)) {
			errFormat(g_picklingError, "Value returned by %s must be a tuple", reduce.get());
			return -1;
		}
		Tuple *tt = (Tuple *)t.get();
		if (tt->_size != 2 && tt->_size != 3) {
			errFormat(g_picklingError, "tuple returned by %s must contain only two or three elements", reduce.get());
			return -1;
		}
		Object *argTup = tt->get(1);
		if (!isInstanceOf(argTup, &g_tupleType) && argTup != g_none) {
			errFormat(g_picklingError, "Second element of tuple returned by %s must be a tuple", reduce.get());
			return -1;
		}
		return saveReduce(p, tt->get(0), argTup, tt->_size > 2 ? tt->get(2) : nullptr, args);
	}

	setError(g_unpickleableError, args);
	return -1;
}

static int dump(Pickler *p, Object *args) {
	if (save(p, args, false) < 0)
		return -1;
	if (writeChar(p, kStop) < 0)
		return -1;
	if (writeBytes(p, nullptr, 0) < 0)
		return -1;
	return 0;
}

// Assembles a list-based pickle: puts are kept only where a get uses them
static ObjRef picklerGetValue(Pickler *p, bool clear) {
	if (!p->_pdata) {
		setErrorString(g_picklingError, "Attempt to getvalue() a non-list-based pickler");
		return ObjRef();
	}
	if (writeOther(p, nullptr, 0) < 0)
		return ObjRef();
	List *data = p->_pdata.get();
	int32 lm = p->_memo->_used + 1;
	Common::Array<bool> haveGet;
	haveGet.resize(lm);
	for (int32 i = 0; i < lm; i++)
		haveGet[i] = false;
	for (int32 i = data->size(); --i >= 0;) {
		Object *k = data->get(i);
		if (isInstanceOf(k, &g_strType))
			continue;
		if (isInstanceOf(k, &g_intType)) {
			int32 ik = ((Int *)k)->_value;
			if (ik >= lm || ik <= 0) {
				setErrorString(g_picklingError, "Invalid get data");
				return ObjRef();
			}
			continue;
		}
		if (!isInstanceOf(k, &g_tupleType) || ((Tuple *)k)->_size != 2 || !isInstanceOf(((Tuple *)k)->get(0), &g_intType)) {
			setErrorString(g_picklingError, "Unexpected data in internal list");
			return ObjRef();
		}
		int32 ik = ((Int *)((Tuple *)k)->get(0))->_value;
		if (ik >= lm || ik <= 0) {
			setErrorString(g_picklingError, "Invalid get data");
			return ObjRef();
		}
		haveGet[ik] = true;
	}
	Common::String out;
	for (uint32 i = 0; i < data->size(); i++) {
		Object *k = data->get(i);
		if (isInstanceOf(k, &g_strType)) {
			out += Common::String(((Str *)k)->_data, ((Str *)k)->_len);
			continue;
		}
		bool isGet = isInstanceOf(k, &g_tupleType);
		int32 ik = isGet ? ((Int *)((Tuple *)k)->get(0))->_value : ((Int *)k)->_value;
		if (!isGet && !haveGet[ik])
			continue;
		if (ik < 256) {
			out += (char)(isGet ? kBinGet : kBinPut);
			out += (char)(ik & 0xff);
		} else {
			out += (char)(isGet ? kLongBinGet : kLongBinPut);
			for (int b = 0; b < 4; b++)
				out += (char)((ik >> (b * 8)) & 0xff);
		}
	}
	if (clear) {
		p->_memo->clear();
		data->clear();
	}
	return newStr(out);
}

static ObjRef newPickler(Object *file, int32 bin) {
	Pickler *p = new Pickler();
	ObjRef rp(p);
	p->_bin = bin;
	p->_memo = newDict();
	p->_dispatchTable = g_dispatchTable;
	if (!file) {
		p->_pdata = newList();
		p->_writeKind = kWriteOther;
		return rp;
	}
	p->_file = file;
	if (isInstanceOf(file, &g_fileType)) {
		ObjRef closed = getAttrString(file, "closed");
		int c = closed ? isTrue(closed.get()) : -1;
		if (c != 0) {
			if (c > 0)
				setErrorString(g_excValueError, "I/O operation on closed file");
			return ObjRef();
		}
		p->_write = getAttrString(file, "write");
		if (!p->_write)
			return ObjRef();
		p->_writeKind = kWriteFile;
	} else if (isStringIOOutput(file)) {
		p->_writeKind = kWriteStringIO;
	} else if (file == g_none) {
		p->_writeKind = kWriteNone;
	} else {
		p->_writeKind = kWriteOther;
		p->_write = getAttrString(file, "write");
		if (!p->_write) {
			clearError();
			setErrorString(g_excTypeError, "argument must have 'write' attribute");
			return ObjRef();
		}
	}
	return rp;
}

// An optional int argument, as PyArg_ParseTuple's "i"
static bool intArg(Object *o, int32 *out) {
	if (isInstanceOf(o, &g_intType) || isInstanceOf(o, &g_longType) || isInstanceOf(o, &g_floatType))
		return asInt(o, out);
	setErrorString(g_excTypeError, "an integer is required");
	return false;
}

#define PICKLER_SELF Pickler *self = (Pickler *)selfObj;

static ObjRef picklerDump(Object *selfObj, Tuple *args, Dict *) {
	PICKLER_SELF
	int32 get = 0;
	if (!checkArgs("dump", args, 1, 2))
		return ObjRef();
	if (args->_size > 1 && !intArg(args->get(1), &get))
		return ObjRef();
	if (dump(self, args->get(0)) < 0)
		return ObjRef();
	if (get)
		return picklerGetValue(self, true);
	return ObjRef(selfObj);
}

static ObjRef picklerClearMemo(Object *selfObj, Tuple *args, Dict *) {
	PICKLER_SELF
	if (!checkArgs("clear_memo", args, 0, 0))
		return ObjRef();
	self->_memo->clear();
	return ObjRef(g_none);
}

static ObjRef picklerGetValueMethod(Object *selfObj, Tuple *args, Dict *) {
	PICKLER_SELF
	int32 clear = 1;
	if (!checkArgs("getvalue", args, 0, 1))
		return ObjRef();
	if (args->_size && !intArg(args->get(0), &clear))
		return ObjRef();
	return picklerGetValue(self, clear != 0);
}

static const MethodDef g_picklerMethods[] = {
	{ "dump", picklerDump, kMethVarArgs, "dump(object) --Write an object in pickle format to the object's pickle stream\n" },
	{ "clear_memo", picklerClearMemo, kMethVarArgs, "clear_memo() -- Clear the picklers memo" },
	{ "getvalue", picklerGetValueMethod, kMethVarArgs, "getvalue() -- Finish picking a list-based pickle" },
	{ nullptr, nullptr, 0, nullptr }
};

static int noDelete(Object *v) {
	if (!v) {
		setErrorString(g_excTypeError, "attribute deletion is not supported");
		return -1;
	}
	return 0;
}

static ObjRef picklerGetPersFunc(Object *o) {
	Pickler *p = (Pickler *)o;
	if (!p->_persFunc) {
		setErrorString(g_excAttributeError, "persistent_id");
		return ObjRef();
	}
	return p->_persFunc;
}

static int picklerSetPersFunc(Object *o, Object *v) {
	if (noDelete(v) < 0)
		return -1;
	((Pickler *)o)->_persFunc = v;
	return 0;
}

static ObjRef picklerGetInstPersFunc(Object *) {
	setErrorString(g_excTypeError, "attribute 'inst_persistent_id' of 'cPickle.Pickler' objects is not readable");
	return ObjRef();
}

static int picklerSetInstPersFunc(Object *o, Object *v) {
	if (noDelete(v) < 0)
		return -1;
	((Pickler *)o)->_instPersFunc = v;
	return 0;
}

static ObjRef picklerGetMemo(Object *o) {
	return ObjRef(((Pickler *)o)->_memo.get());
}

static int picklerSetMemo(Object *o, Object *v) {
	if (noDelete(v) < 0)
		return -1;
	if (!isInstanceOf(v, &g_dictType)) {
		setErrorString(g_excTypeError, "memo must be a dictionary");
		return -1;
	}
	((Pickler *)o)->_memo = (Dict *)v;
	return 0;
}

static ObjRef picklerGetPicklingError(Object *) {
	return ObjRef(g_picklingError);
}

static bool memberInt(Object *v, int32 *out) {
	if (!v) {
		setErrorString(g_excTypeError, "can't delete numeric/char attribute");
		return false;
	}
	if (!isInstanceOf(v, &g_intType) && !isInstanceOf(v, &g_longType)) {
		setErrorString(g_excTypeError, "an integer is required");
		return false;
	}
	return asInt(v, out);
}

static ObjRef picklerGetBinary(Object *o) { return newInt(((Pickler *)o)->_bin); }
static int picklerSetBinary(Object *o, Object *v) { return memberInt(v, &((Pickler *)o)->_bin) ? 0 : -1; }
static ObjRef picklerGetFast(Object *o) { return newInt(((Pickler *)o)->_fast); }
static int picklerSetFast(Object *o, Object *v) { return memberInt(v, &((Pickler *)o)->_fast) ? 0 : -1; }

static const GetSetDef g_picklerGetSets[] = {
	{ "persistent_id", picklerGetPersFunc, picklerSetPersFunc },
	{ "inst_persistent_id", picklerGetInstPersFunc, picklerSetInstPersFunc },
	{ "memo", picklerGetMemo, picklerSetMemo },
	{ "PicklingError", picklerGetPicklingError, nullptr },
	{ "binary", picklerGetBinary, picklerSetBinary },
	{ "fast", picklerGetFast, picklerSetFast },
	{ nullptr, nullptr, nullptr }
};

// ---- Unpickler ----------------------------------------------------------------------

enum {
	kReadFile, kReadStringIO, kReadOther
};

struct Unpickler : public Object {
	ObjRef _file;
	int _readKind;
	ObjRef _read, _readline;
	Ref<Dict> _memo;
	Ref<List> _stack;
	Common::Array<int32> _marks;
	ObjRef _persFunc, _findClass;
	Ref<Tuple> _arg;
	ObjRef _lastString;
	Common::Array<char> _pad;
	Common::Array<char> _lineBuf; // as readline_file's buffer
	bool _feof;
	ObjRef _safeConstructors;
	Unpickler() : Object(&g_unpicklerType), _readKind(kReadOther), _feof(false) {}
};

static int32 stackLength(Unpickler *u) {
	return u->_stack->size();
}

static void stackPush(Unpickler *u, Object *o) {
	u->_stack->append(o);
}

static ObjRef stackPop(Unpickler *u) {
	List *s = u->_stack.get();
	if (!s->size()) {
		setErrorString(g_unpicklingError, "bad pickle data");
		return ObjRef();
	}
	Object *o = s->_items.back();
	s->_items.pop_back();
	ObjRef r(o);
	decRef(o);
	return r;
}

static void stackClear(Unpickler *u, int32 from) {
	List *s = u->_stack.get();
	if (from < 0)
		from = 0;
	while ((int32)s->size() > from) {
		Object *o = s->_items.back();
		s->_items.pop_back();
		decRef(o);
	}
}

static Ref<Tuple> stackPopTuple(Unpickler *u, int32 start) {
	int32 l = stackLength(u) - start;
	if (l < 0) {
		badInternalCall();
		return Ref<Tuple>();
	}
	Ref<Tuple> t = newTuple(l);
	for (int32 i = 0; i < l; i++)
		t->set(i, u->_stack->get(start + i));
	stackClear(u, start);
	return t;
}

static Ref<List> stackPopList(Unpickler *u, int32 start) {
	Ref<List> l = newList();
	for (int32 i = start; i < stackLength(u); i++)
		l->append(u->_stack->get(i));
	stackClear(u, start);
	return l;
}

static int stackUnderflow() {
	setErrorString(g_unpicklingError, "unpickling stack underflow");
	return -1;
}

static int badReadline() {
	setErrorString(g_unpicklingError, "pickle data was truncated");
	return -1;
}

static int32 marker(Unpickler *u) {
	if (u->_marks.empty()) {
		setErrorString(g_unpicklingError, "could not find MARK");
		return -1;
	}
	int32 m = u->_marks.back();
	u->_marks.pop_back();
	return m;
}

// Reads n bytes; -1 on error
static int32 readBytes(Unpickler *u, const char **s, int32 n) {
	if (u->_readKind == kReadStringIO) {
		if (stringIORead(u->_file.get(), s, n) != n) {
			clearError();
			setError(g_excEOFError, nullptr);
			return -1;
		}
		return n;
	}
	ObjRef str;
	if (u->_readKind == kReadFile) {
		if (n < 0) {
			u->_feof = true;
			setError(g_excEOFError, nullptr);
			return -1;
		}
		Ref<Int> count = newInt(n);
		str = callFunction(u->_read.get(), 1, count.get());
	} else {
		Ref<Int> count = newInt(n);
		str = callArgTup(u->_arg, u->_read.get(), count.get());
	}
	if (!str)
		return -1;
	if (!isInstanceOf(str.get(), &g_strType)) {
		setErrorFormat(g_excTypeError, "expected string or Unicode object, %.200s found", typeName(str.get()));
		return -1;
	}
	u->_lastString = str;
	Str *st = (Str *)str.get();
	if (u->_readKind == kReadFile) {
		if ((int32)st->_len != n) {
			u->_feof = true;
			setError(g_excEOFError, nullptr);
			return -1;
		}
		*s = st->_data;
		return n;
	}
	// cPickle took the bytes without checking the length
	if ((int32)st->_len >= n) {
		*s = st->_data;
		return n;
	}
	u->_pad.resize(MAX<int32>(n, 1));
	for (int32 i = 0; i < n; i++)
		u->_pad[i] = i < (int32)st->_len ? st->_data[i] : 0;
	*s = &u->_pad[0];
	return n;
}

// cPickle's readline_file: getc() into a reused buffer. At the end of the
// file it stores the EOF character (0xff) and returns a byte left over from
// an earlier line; truncated pickles fail the way they did in CPython.
static int32 readLineFile(Unpickler *u, const char **s) {
	if (u->_lineBuf.empty())
		u->_lineBuf.resize(40, 0);
	ObjRef line;
	const char *src = nullptr;
	int32 srcLen = 0, srcPos = 0;
	int32 i = 0;
	for (;;) {
		for (; i < (int32)u->_lineBuf.size() - 1; i++) {
			if (!u->_feof && !line) {
				line = call(u->_readline.get(), newTuple(0).get());
				if (!line)
					return -1;
				if (!isInstanceOf(line.get(), &g_strType)) {
					setErrorFormat(g_excTypeError, "expected string or Unicode object, %.200s found", typeName(line.get()));
					return -1;
				}
				src = ((Str *)line.get())->_data;
				srcLen = ((Str *)line.get())->_len;
			}
			bool done = u->_feof;
			if (!done) {
				char c;
				if (srcPos < srcLen) {
					c = src[srcPos++];
				} else {
					c = (char)0xff;
					u->_feof = true;
				}
				u->_lineBuf[i] = c;
				done = c == '\n';
			}
			if (done) {
				u->_lineBuf[i + 1] = '\0';
				*s = &u->_lineBuf[0];
				return i + 1;
			}
		}
		u->_lineBuf.resize(u->_lineBuf.size() * 2, 0);
	}
}

// Reads a line (with its newline, if any); returns its length or -1
static int32 readLine(Unpickler *u, const char **s) {
	if (u->_readKind == kReadStringIO)
		return stringIOReadline(u->_file.get(), s);
	if (u->_readKind == kReadFile)
		return readLineFile(u, s);
	ObjRef str = call(u->_readline.get(), newTuple(0).get());
	if (!str)
		return -1;
	if (!isInstanceOf(str.get(), &g_strType)) {
		setErrorFormat(g_excTypeError, "expected string or Unicode object, %.200s found", typeName(str.get()));
		return -1;
	}
	u->_lastString = str;
	*s = ((Str *)str.get())->_data;
	return ((Str *)str.get())->_len;
}

static int32 calcBinInt(const char *s, int x) {
	uint32 l = 0;
	for (int i = 0; i < x; i++)
		l |= (uint32)(uint8)s[i] << (i * 8);
	return (int32)l;
}

// strtol(s, &end, 0) as MSVC's: false on overflow
static bool parseLong(const char *s, const char **end, int32 *out) {
	const char *p = s;
	while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
		p++;
	bool neg = false;
	if (*p == '+' || *p == '-')
		neg = *p++ == '-';
	int base = 10;
	if (p[0] == '0') {
		bool hex = (p[1] == 'x' || p[1] == 'X') &&
		           ((p[2] >= '0' && p[2] <= '9') || (p[2] >= 'a' && p[2] <= 'f') || (p[2] >= 'A' && p[2] <= 'F'));
		if (hex) {
			base = 16;
			p += 2;
		} else {
			base = 8;
		}
	}
	const char *digits = p;
	uint64 v = 0;
	bool overflow = false;
	for (;; p++) {
		int d;
		if (*p >= '0' && *p <= '9')
			d = *p - '0';
		else if (*p >= 'a' && *p <= 'z')
			d = *p - 'a' + 10;
		else if (*p >= 'A' && *p <= 'Z')
			d = *p - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		v = v * base + d;
		if (v > 0x80000000ULL)
			overflow = true;
	}
	if (p == digits) {
		*end = s;
		*out = 0;
		return true;
	}
	*end = p;
	if (overflow || (!neg && v > 0x7fffffffULL))
		return false;
	*out = neg ? (int32)(0 - (uint32)v) : (int32)v;
	return true;
}

static int loadNone(Unpickler *u) {
	stackPush(u, g_none);
	return 0;
}

static int loadInt(Unpickler *u) {
	const char *s;
	int32 len = readLine(u, &s);
	if (len < 0)
		return -1;
	if (len < 2)
		return badReadline();
	Common::String line(s, len);
	const char *end;
	int32 l;
	ObjRef v;
	if (!parseLong(line.c_str(), &end, &l) || *end != '\n' || end[1] != '\0') {
		v = longFromString(line.c_str(), 0, true);
		if (!v) {
			clearError();
			setErrorString(g_excValueError, "could not convert string to int");
			return -1;
		}
	} else {
		v = newInt(l);
	}
	stackPush(u, v.get());
	return 0;
}

static int loadBinIntN(Unpickler *u, int n) {
	const char *s;
	if (readBytes(u, &s, n) < 0)
		return -1;
	Ref<Int> v = newInt(calcBinInt(s, n));
	stackPush(u, v.get());
	return 0;
}

static int loadLong(Unpickler *u) {
	const char *s;
	int32 len = readLine(u, &s);
	if (len < 0)
		return -1;
	if (len < 2)
		return badReadline();
	Common::String line(s, len);
	ObjRef v = longFromString(line.c_str(), 0, true);
	if (!v)
		return -1;
	stackPush(u, v.get());
	return 0;
}

static int loadFloat(Unpickler *u) {
	const char *s;
	int32 len = readLine(u, &s);
	if (len < 0)
		return -1;
	if (len < 2)
		return badReadline();
	Common::String line(s, len);
	const char *p = line.c_str();
	while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
		p++;
	if (*p == '+' || *p == '-')
		p++;
	// MSVC's strtod knows neither inf, nan nor hex
	bool ok = (*p >= '0' && *p <= '9') || *p == '.';
	for (const char *q = p; *q; q++)
		if (*q == 'x' || *q == 'X')
			ok = false;
	double d = 0.0;
	if (ok) {
		char *end;
		d = strtod(line.c_str(), &end);
		ok = end[0] == '\n' && end[1] == '\0' && d - d == 0.0;
	}
	if (!ok) {
		setErrorString(g_excValueError, "could not convert string to float");
		return -1;
	}
	Ref<Float> v = newFloat(d);
	stackPush(u, v.get());
	return 0;
}

static int loadBinFloat(Unpickler *u) {
	const char *s;
	if (readBytes(u, &s, 8) < 0)
		return -1;
	const uint8 *p = (const uint8 *)s;
	int sign = (p[0] >> 7) & 1;
	int e = ((p[0] & 0x7f) << 4) | ((p[1] >> 4) & 0xf);
	uint32 fhi = ((p[1] & 0xf) << 24) | (p[2] << 16) | (p[3] << 8) | p[4];
	uint32 flo = (p[5] << 16) | (p[6] << 8) | p[7];
	double x = (double)fhi + (double)flo / 16777216.0;
	x /= 268435456.0;
	if (e == 0) {
		e = -1022;
	} else {
		x += 1.0;
		e -= 1023;
	}
	x = ldexp(x, e);
	if (sign)
		x = -x;
	Ref<Float> v = newFloat(x);
	stackPush(u, v.get());
	return 0;
}

static bool isHexDigit(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int hexValue(char c) {
	return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

// Decodes a quoted string literal (already checked to be one), as eval()
static ObjRef evalStringLiteral(const char *s, int32 len) {
	char q = s[0];
	Common::String out;
	int32 i = 1;
	for (; i < len && s[i] != q; i++) {
		if (s[i] != '\\') {
			out += s[i];
			continue;
		}
		i++;
		char c = s[i];
		switch (c) {
		case '\n': break;
		case '\\': out += '\\'; break;
		case '\'': out += '\''; break;
		case '"': out += '"'; break;
		case 'b': out += '\b'; break;
		case 'f': out += '\014'; break;
		case 't': out += '\t'; break;
		case 'n': out += '\n'; break;
		case 'r': out += '\r'; break;
		case 'v': out += '\013'; break;
		case 'a': out += '\007'; break;
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
			int v = c - '0';
			if (s[i + 1] >= '0' && s[i + 1] <= '7') {
				v = v * 8 + (s[++i] - '0');
				if (s[i + 1] >= '0' && s[i + 1] <= '7')
					v = v * 8 + (s[++i] - '0');
			}
			out += (char)v;
			break;
		}
		case 'x':
			if (isHexDigit(s[i + 1]) && isHexDigit(s[i + 2])) {
				out += (char)(hexValue(s[i + 1]) * 16 + hexValue(s[i + 2]));
				i += 2;
				break;
			}
			setErrorString(g_excValueError, "invalid \\x escape");
			return ObjRef();
		default:
			out += '\\';
			i--;
			break;
		}
	}
	return newStr(out);
}

static int loadString(Unpickler *u) {
	const char *s;
	int32 len = readLine(u, &s);
	if (len < 0)
		return -1;
	if (len < 2)
		return badReadline();
	Common::String line(s, len);
	const char *str = line.c_str();
	// Only a lone, properly quoted literal is evaluated
	char q = *str;
	bool secure = q == '"' || q == '\'';
	if (secure) {
		const char *p;
		int nslash = 0;
		for (p = str + 1; *p; p++) {
			if (*p == q && nslash % 2 == 0)
				break;
			if (*p == '\\')
				nslash++;
			else
				nslash = 0;
		}
		if (*p == q) {
			for (p++; *p; p++)
				if (*(const uint8 *)p > ' ')
					secure = false;
		} else {
			secure = false;
		}
	}
	if (!secure) {
		setErrorString(g_excValueError, "insecure string pickle");
		return -1;
	}
	ObjRef v = evalStringLiteral(str, strlen(str));
	if (!v)
		return -1;
	stackPush(u, v.get());
	return 0;
}

static int loadBinString(Unpickler *u, int lenBytes) {
	const char *s;
	if (readBytes(u, &s, lenBytes) < 0)
		return -1;
	int32 l = lenBytes == 1 ? (uint8)s[0] : calcBinInt(s, 4);
	if (readBytes(u, &s, l) < 0)
		return -1;
	Ref<Str> v = newStr(s, l);
	stackPush(u, v.get());
	return 0;
}

// As Python 2.2's raw-unicode-escape codec
static ObjRef decodeRawUnicodeEscape(const char *s, int32 len) {
	Ref<Unicode> u(new Unicode());
	const char *end = s + len;
	while (s < end) {
		if (*s != '\\') {
			u->_chars.push_back((uint8)*s++);
			continue;
		}
		const char *bs = s;
		while (s < end && *s == '\\')
			u->_chars.push_back((uint8)*s++);
		if (((s - bs) & 1) == 0 || s >= end || *s != 'u')
			continue;
		u->_chars.pop_back();
		s++;
		uint32 x = 0;
		for (int i = 0; i < 4; i++) {
			if (s + i >= end || !isHexDigit(s[i])) {
				setErrorString(g_excUnicodeError, "Unicode-Escape decoding error: truncated \\uXXXX");
				return ObjRef();
			}
			x = (x << 4) + hexValue(s[i]);
		}
		s += 4;
		u->_chars.push_back(x);
	}
	return u;
}

// As Python 2.2's UTF-8 decoder (strict)
static ObjRef decodeUtf8(const char *str, int32 len) {
	Ref<Unicode> u(new Unicode());
	const uint8 *s = (const uint8 *)str, *e = s + len;
	const char *errmsg = nullptr;
	while (s < e) {
		uint32 ch = *s;
		if (ch < 0x80) {
			u->_chars.push_back(ch);
			s++;
			continue;
		}
		int n = ch < 0xc0 ? 0 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : ch < 0xf8 ? 4 : ch < 0xfc ? 5 : ch < 0xfe ? 6 : 0;
		if (s + n > e) {
			errmsg = "unexpected end of data";
			break;
		}
		if (n == 0) {
			errmsg = "unexpected code byte";
			break;
		}
		if (n > 4) {
			errmsg = "unsupported Unicode code range";
			break;
		}
		bool valid = true;
		for (int i = 1; i < n; i++)
			if ((s[i] & 0xc0) != 0x80)
				valid = false;
		if (!valid) {
			errmsg = "invalid data";
			break;
		}
		if (n == 2) {
			ch = ((s[0] & 0x1f) << 6) + (s[1] & 0x3f);
			if (ch < 0x80) {
				errmsg = "illegal encoding";
				break;
			}
		} else if (n == 3) {
			ch = ((s[0] & 0x0f) << 12) + ((s[1] & 0x3f) << 6) + (s[2] & 0x3f);
			if (ch < 0x800 || (ch >= 0xd800 && ch < 0xe000)) {
				errmsg = "illegal encoding";
				break;
			}
		} else {
			ch = ((s[0] & 0x7) << 18) + ((s[1] & 0x3f) << 12) + ((s[2] & 0x3f) << 6) + (s[3] & 0x3f);
			if (ch < 0x10000 || ch > 0x10ffff) {
				errmsg = "illegal encoding";
				break;
			}
		}
		u->_chars.push_back(ch);
		s += n;
	}
	if (errmsg) {
		setErrorFormat(g_excUnicodeError, "UTF-8 decoding error: %s", errmsg);
		return ObjRef();
	}
	return u;
}

static int loadUnicode(Unpickler *u) {
	const char *s;
	int32 len = readLine(u, &s);
	if (len < 0)
		return -1;
	if (len < 1)
		return badReadline();
	ObjRef v = decodeRawUnicodeEscape(s, len - 1);
	if (!v)
		return -1;
	stackPush(u, v.get());
	return 0;
}

static int loadBinUnicode(Unpickler *u) {
	const char *s;
	if (readBytes(u, &s, 4) < 0)
		return -1;
	int32 l = calcBinInt(s, 4);
	if (readBytes(u, &s, l) < 0)
		return -1;
	ObjRef v = decodeUtf8(s, l);
	if (!v)
		return -1;
	stackPush(u, v.get());
	return 0;
}

static int loadTuple(Unpickler *u) {
	int32 i = marker(u);
	if (i < 0)
		return -1;
	Ref<Tuple> t = stackPopTuple(u, i);
	if (!t)
		return -1;
	stackPush(u, t.get());
	return 0;
}

static int loadList(Unpickler *u) {
	int32 i = marker(u);
	if (i < 0)
		return -1;
	Ref<List> l = stackPopList(u, i);
	stackPush(u, l.get());
	return 0;
}

static int loadDict(Unpickler *u) {
	int32 i = marker(u);
	if (i < 0)
		return -1;
	int32 j = stackLength(u);
	Ref<Dict> d = newDict();
	for (int32 k = i + 1; k < j; k += 2)
		if (d->setItem(u->_stack->get(k - 1), u->_stack->get(k)) < 0)
			return -1;
	stackClear(u, i);
	stackPush(u, d.get());
	return 0;
}

// Makes an instance or calls a constructor, as cPickle's Instance_New
static ObjRef instanceNew(Unpickler *u, Object *cls, Object *args) {
	ObjRef r;
	if (isType(cls, &g_classType)) {
		int32 l = length(args);
		if (l >= 0) {
			bool raw = false;
			if (l == 0) {
				ObjRef gia = getAttrString(cls, "__getinitargs__");
				if (!gia) {
					clearError();
					raw = true;
				}
			}
			if (raw) {
				Ref<Dict> d = newDict();
				return ObjRef(new Instance((ClassObj *)cls, d.get()));
			}
			if (!isInstanceOf(args, &g_tupleType))
				setErrorString(g_excTypeError, "argument list must be a tuple");
			else
				r = newInstance(cls, args, nullptr);
			if (r)
				return r;
		}
	} else {
		Object *safe = u->_safeConstructors.get();
		bool hasKey = false;
		if (safe) {
			ObjRef v = getItem(safe, cls);
			hasKey = v;
			clearError();
		}
		if (!hasKey) {
			ObjRef s = getAttrString(cls, "__safe_for_unpickling__");
			int t = s ? isTrue(s.get()) : 0;
			if (t <= 0) {
				clearError();
				errFormat(g_unpicklingError, "%s is not safe for unpickling", cls);
				return ObjRef();
			}
		}
		if (args == g_none) {
			ObjRef basicNew = getAttrString(cls, "__basicnew__");
			if (!basicNew)
				return ObjRef();
			r = call(basicNew.get(), newTuple(0).get());
			if (r)
				return r;
		}
		if (!isInstanceOf(args, &g_tupleType)) {
			clearError();
			setErrorString(g_excTypeError, "argument list must be a tuple");
		} else {
			r = call(cls, (Tuple *)args);
			if (r)
				return r;
		}
	}
	ObjRef tp, v, tb;
	fetchError(&tp, &v, &tb);
	if (v)
		v = tuplePack(3, v.get(), cls, args);
	restoreError(tp.get(), v.get(), tb.get());
	return ObjRef();
}

static int loadObj(Unpickler *u) {
	int32 i = marker(u);
	if (i < 0)
		return -1;
	Ref<Tuple> tup = stackPopTuple(u, i + 1);
	if (!tup)
		return -1;
	ObjRef cls = stackPop(u);
	if (!cls)
		return -1;
	ObjRef obj = instanceNew(u, cls.get(), tup.get());
	if (!obj)
		return -1;
	stackPush(u, obj.get());
	return 0;
}

static ObjRef findClass(Unpickler *u, Str *moduleName, Str *globalName) {
	if (u->_findClass) {
		if (u->_findClass.get() == g_none) {
			setErrorString(g_unpicklingError, "Global and instance pickles are not supported.");
			return ObjRef();
		}
		return callFunction(u->_findClass.get(), 2, moduleName, globalName);
	}
	ObjRef modules = sysGetObject("modules");
	if (!modules)
		return ObjRef();
	Object *module = isInstanceOf(modules.get(), &g_dictType) ? ((Dict *)modules.get())->getItem(moduleName) : nullptr;
	if (!module) {
		ObjRef m = importModule(moduleName->_data);
		if (!m)
			return ObjRef();
		return getAttr(m.get(), globalName);
	}
	return getAttr(module, globalName);
}

// Reads the module and name lines of INST and GLOBAL
static ObjRef readGlobal(Unpickler *u, bool noload) {
	const char *s;
	int32 len = readLine(u, &s);
	if (len < 0)
		return ObjRef();
	if (len < 2) {
		badReadline();
		return ObjRef();
	}
	Ref<Str> moduleName = newStr(s, len - 1);
	len = readLine(u, &s);
	if (len < 0)
		return ObjRef();
	if (len < 2) {
		badReadline();
		return ObjRef();
	}
	Ref<Str> className = newStr(s, len - 1);
	if (noload)
		return ObjRef(g_none);
	return findClass(u, moduleName.get(), className.get());
}

static int loadInst(Unpickler *u) {
	int32 i = marker(u);
	if (i < 0)
		return -1;
	ObjRef cls = readGlobal(u, false);
	if (!cls)
		return -1;
	Ref<Tuple> tup = stackPopTuple(u, i);
	if (!tup)
		return -1;
	ObjRef obj = instanceNew(u, cls.get(), tup.get());
	if (!obj)
		return -1;
	stackPush(u, obj.get());
	return 0;
}

static int loadGlobal(Unpickler *u) {
	ObjRef cls = readGlobal(u, false);
	if (!cls)
		return -1;
	stackPush(u, cls.get());
	return 0;
}

static int pushPersistent(Unpickler *u, Object *pid) {
	if (!u->_persFunc) {
		setErrorString(g_unpicklingError, "A load persistent id instruction was encountered,\n"
		               "but no persistent_load function was specified.");
		return -1;
	}
	ObjRef v(pid);
	if (isInstanceOf(u->_persFunc.get(), &g_listType)) {
		((List *)u->_persFunc.get())->append(pid);
	} else {
		v = callArgTup(u->_arg, u->_persFunc.get(), pid);
		if (!v)
			return -1;
	}
	stackPush(u, v.get());
	return 0;
}

static int loadPersId(Unpickler *u) {
	if (!u->_persFunc)
		return pushPersistent(u, nullptr);
	const char *s;
	int32 len = readLine(u, &s);
	if (len < 0)
		return -1;
	if (len < 2)
		return badReadline();
	Ref<Str> pid = newStr(s, len - 1);
	return pushPersistent(u, pid.get());
}

static int loadBinPersId(Unpickler *u) {
	if (!u->_persFunc)
		return pushPersistent(u, nullptr);
	ObjRef pid = stackPop(u);
	if (!pid)
		return -1;
	return pushPersistent(u, pid.get());
}

static int loadPop(Unpickler *u) {
	int32 len = stackLength(u);
	if (len <= 0)
		return stackUnderflow();
	if (!u->_marks.empty() && u->_marks.back() == len)
		u->_marks.pop_back();
	else
		stackClear(u, len - 1);
	return 0;
}

static int loadPopMark(Unpickler *u) {
	int32 i = marker(u);
	if (i < 0)
		return -1;
	stackClear(u, i);
	return 0;
}

static int loadDup(Unpickler *u) {
	int32 len = stackLength(u);
	if (len <= 0)
		return stackUnderflow();
	stackPush(u, u->_stack->get(len - 1));
	return 0;
}

static int pushMemo(Unpickler *u, Object *key) {
	Object *value = u->_memo->getItem(key);
	if (!value) {
		if (!errorOccurred())
			setError(g_badPickleGet, key);
		return -1;
	}
	stackPush(u, value);
	return 0;
}

static int loadGet(Unpickler *u) {
	const char *s;
	int32 len = readLine(u, &s);
	if (len < 0)
		return -1;
	if (len < 2)
		return badReadline();
	Ref<Str> key = newStr(s, len - 1);
	return pushMemo(u, key.get());
}

static int loadBinGet(Unpickler *u, int n) {
	const char *s;
	if (readBytes(u, &s, n) < 0)
		return -1;
	Ref<Int> key = newInt(n == 1 ? (uint8)s[0] : calcBinInt(s, 4));
	return pushMemo(u, key.get());
}

static int loadPut(Unpickler *u) {
	const char *s;
	int32 l = readLine(u, &s);
	if (l < 0)
		return -1;
	if (l < 2)
		return badReadline();
	int32 len = stackLength(u);
	if (!len)
		return stackUnderflow();
	Ref<Str> key = newStr(s, l - 1);
	return u->_memo->setItem(key.get(), u->_stack->get(len - 1));
}

static int loadBinPut(Unpickler *u, int n) {
	const char *s;
	if (readBytes(u, &s, n) < 0)
		return -1;
	int32 len = stackLength(u);
	if (len <= 0)
		return stackUnderflow();
	Ref<Int> key = newInt(n == 1 ? (uint8)s[0] : calcBinInt(s, 4));
	return u->_memo->setItem(key.get(), u->_stack->get(len - 1));
}

static int doAppend(Unpickler *u, int32 x) {
	int32 len = stackLength(u);
	if (!(len >= x && x > 0))
		return stackUnderflow();
	if (len == x)
		return 0;
	Object *list = u->_stack->get(x - 1);
	if (isInstanceOf(list, &g_listType)) {
		Ref<List> slice = stackPopList(u, x);
		int32 listLen = ((List *)list)->size();
		return sequenceSetSlice(list, listLen, listLen, slice.get());
	}
	ObjRef appendMethod = getAttrString(list, "append");
	if (!appendMethod)
		return -1;
	for (int32 i = x; i < len; i++) {
		ObjRef junk = callArgTup(u->_arg, appendMethod.get(), u->_stack->get(i));
		if (!junk) {
			stackClear(u, i + 1);
			stackClear(u, x);
			return -1;
		}
	}
	stackClear(u, x);
	return 0;
}

static int doSetItems(Unpickler *u, int32 x) {
	int32 len = stackLength(u);
	if (!(len >= x && x > 0))
		return stackUnderflow();
	Object *dict = u->_stack->get(x - 1);
	int r = 0;
	for (int32 i = x + 1; i < len; i += 2) {
		if (setItem(dict, u->_stack->get(i - 1), u->_stack->get(i)) < 0) {
			r = -1;
			break;
		}
	}
	stackClear(u, x);
	return r;
}

static int loadBuild(Unpickler *u) {
	if (stackLength(u) < 2)
		return stackUnderflow();
	ObjRef value = stackPop(u);
	if (!value)
		return -1;
	Object *inst = u->_stack->get(stackLength(u) - 1);
	ObjRef setState = getAttrString(inst, "__setstate__");
	if (setState) {
		ObjRef junk = callArgTup(u->_arg, setState.get(), value.get());
		return junk ? 0 : -1;
	}
	clearError();
	ObjRef instDict = getAttrString(inst, "__dict__");
	if (!instDict)
		return -1;
	if (isInstanceOf(value.get(), &g_dictType)) {
		Dict *d = (Dict *)value.get();
		int32 pos = 0;
		Object *k, *v;
		while (d->next(&pos, &k, &v))
			if (setItem(instDict.get(), k, v) < 0)
				return -1;
	}
	return 0;
}

static int loadReduce(Unpickler *u) {
	ObjRef argTup = stackPop(u);
	if (!argTup)
		return -1;
	ObjRef callable = stackPop(u);
	if (!callable)
		return -1;
	ObjRef obj = instanceNew(u, callable.get(), argTup.get());
	if (!obj)
		return -1;
	stackPush(u, obj.get());
	return 0;
}

// noload: steps through a pickle without making instances or importing
static int noloadInst(Unpickler *u) {
	int32 i = marker(u);
	if (i < 0)
		return -1;
	stackClear(u, i);
	if (!readGlobal(u, true))
		return -1;
	stackPush(u, g_none);
	return 0;
}

static int noloadGlobal(Unpickler *u) {
	if (!readGlobal(u, true))
		return -1;
	stackPush(u, g_none);
	return 0;
}

static int noloadObj(Unpickler *u) {
	int32 i = marker(u);
	if (i < 0)
		return -1;
	stackClear(u, i + 1);
	return 0;
}

static int noloadReduce(Unpickler *u) {
	if (stackLength(u) < 2)
		return stackUnderflow();
	stackClear(u, stackLength(u) - 2);
	stackPush(u, g_none);
	return 0;
}

static int noloadBuild(Unpickler *u) {
	if (stackLength(u) < 1)
		return stackUnderflow();
	stackClear(u, stackLength(u) - 1);
	return 0;
}

static ObjRef unpicklerLoad(Unpickler *u, bool noload) {
	u->_marks.clear();
	stackClear(u, 0);
	for (;;) {
		const char *s;
		if (readBytes(u, &s, 1) < 0)
			break;
		int r;
		char op = s[0];
		switch (op) {
		case kNone: r = loadNone(u); break;
		case kBinInt: r = loadBinIntN(u, 4); break;
		case kBinInt1: r = loadBinIntN(u, 1); break;
		case kBinInt2: r = loadBinIntN(u, 2); break;
		case kInt: r = loadInt(u); break;
		case kLong: r = loadLong(u); break;
		case kFloat: r = loadFloat(u); break;
		case kBinFloat: r = loadBinFloat(u); break;
		case kBinString: r = loadBinString(u, 4); break;
		case kShortBinString: r = loadBinString(u, 1); break;
		case kString: r = loadString(u); break;
		case kUnicode: r = loadUnicode(u); break;
		case kBinUnicode: r = loadBinUnicode(u); break;
		case kEmptyTuple: {
			Ref<Tuple> t = newTuple(0);
			stackPush(u, t.get());
			r = 0;
			break;
		}
		case kTuple: r = loadTuple(u); break;
		case kEmptyList: {
			Ref<List> l = newList();
			stackPush(u, l.get());
			r = 0;
			break;
		}
		case kList: r = loadList(u); break;
		case kEmptyDict: {
			Ref<Dict> d = newDict();
			stackPush(u, d.get());
			r = 0;
			break;
		}
		case kDict: r = loadDict(u); break;
		case kObj: r = noload ? noloadObj(u) : loadObj(u); break;
		case kInst: r = noload ? noloadInst(u) : loadInst(u); break;
		case kGlobal: r = noload ? noloadGlobal(u) : loadGlobal(u); break;
		case kAppend: r = doAppend(u, stackLength(u) - 1); break;
		case kAppends: r = doAppend(u, marker(u)); break;
		case kBuild: r = noload ? noloadBuild(u) : loadBuild(u); break;
		case kDup: r = loadDup(u); break;
		case kBinGet: r = loadBinGet(u, 1); break;
		case kLongBinGet: r = loadBinGet(u, 4); break;
		case kGet: r = loadGet(u); break;
		case kMark:
			u->_marks.push_back(stackLength(u));
			r = 0;
			break;
		case kBinPut: r = loadBinPut(u, 1); break;
		case kLongBinPut: r = loadBinPut(u, 4); break;
		case kPut: r = loadPut(u); break;
		case kPop: r = loadPop(u); break;
		case kPopMark: r = loadPopMark(u); break;
		case kSetItem: r = doSetItems(u, stackLength(u) - 2); break;
		case kSetItems: r = doSetItems(u, marker(u)); break;
		case kStop: r = 1; break;
		case kPersId: r = loadPersId(u); break;
		case kBinPersId: r = loadBinPersId(u); break;
		case kReduce: r = noload ? noloadReduce(u) : loadReduce(u); break;
		default: {
			Ref<Str> key = newStr(&op, 1);
			errFormat(g_unpicklingError, "invalid load key, '%s'.", key.get());
			return ObjRef();
		}
		}
		if (r != 0)
			break;
	}
	if (errorOccurred()) {
		if (errorType() == g_excEOFError) {
			clearError();
			setError(g_excEOFError, nullptr);
		}
		return ObjRef();
	}
	return stackPop(u);
}

static ObjRef newUnpickler(Object *f) {
	Unpickler *u = new Unpickler();
	ObjRef ru(u);
	u->_file = f;
	u->_memo = newDict();
	u->_stack = newList();
	u->_safeConstructors = g_safeConstructors;
	if (isInstanceOf(f, &g_fileType)) {
		ObjRef closed = getAttrString(f, "closed");
		int c = closed ? isTrue(closed.get()) : -1;
		if (c != 0) {
			if (c > 0)
				setErrorString(g_excValueError, "I/O operation on closed file");
			return ObjRef();
		}
		u->_readKind = kReadFile;
	} else if (isStringIOInput(f)) {
		u->_readKind = kReadStringIO;
		return ru;
	} else {
		u->_readKind = kReadOther;
	}
	u->_readline = getAttrString(f, "readline");
	if (u->_readline)
		u->_read = getAttrString(f, "read");
	if (!u->_readline || !u->_read) {
		clearError();
		setErrorString(g_excTypeError, "argument must have 'read' and 'readline' attributes");
		return ObjRef();
	}
	return ru;
}

static ObjRef unpicklerLoadMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("load", args, 0, 0))
		return ObjRef();
	return unpicklerLoad((Unpickler *)selfObj, false);
}

static ObjRef unpicklerNoloadMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("noload", args, 0, 0))
		return ObjRef();
	return unpicklerLoad((Unpickler *)selfObj, true);
}

static const MethodDef g_unpicklerMethods[] = {
	{ "load", unpicklerLoadMethod, kMethVarArgs, "load() -- Load a pickle" },
	{ "noload", unpicklerNoloadMethod, kMethVarArgs,
	  "noload() -- not load a pickle, but go through most of the motions\n\n"
	  "This function can be used to read past a pickle without instantiating\n"
	  "any objects or importing any modules.  It can also be used to find all\n"
	  "persistent references without instantiating any objects or importing\n"
	  "any modules.\n" },
	{ nullptr, nullptr, 0, nullptr }
};

// Unpicklers have attributes rather than descriptors, as in 2.2
static ObjRef unpicklerGetAttr(Object *o, Str *name) {
	Unpickler *u = (Unpickler *)o;
	ObjRef v;
	if (name->equals("persistent_load"))
		v = u->_persFunc;
	else if (name->equals("find_global"))
		v = u->_findClass;
	else if (name->equals("memo"))
		v = u->_memo.get();
	else if (name->equals("UnpicklingError"))
		v = g_unpicklingError;
	else if (name->equals("__methods__")) {
		Ref<List> l = newList();
		for (const MethodDef *d = g_unpicklerMethods; d->name; d++)
			l->append(newStr(d->name).get());
		if (listSort(l.get(), nullptr) < 0)
			return ObjRef();
		return l;
	} else if (name->equals("__doc__")) {
		Object *doc = o->_type->_dict->getItemString("__doc__");
		return ObjRef(doc ? doc : g_none);
	} else {
		for (const MethodDef *d = g_unpicklerMethods; d->name; d++)
			if (name->equals(d->name))
				return ObjRef(new BuiltinFunction(d, o));
	}
	if (!v)
		setErrorString(g_excAttributeError, name->_data);
	return v;
}

static int unpicklerSetAttr(Object *o, Str *name, Object *value) {
	Unpickler *u = (Unpickler *)o;
	if (name->equals("persistent_load")) {
		u->_persFunc = value;
		return 0;
	}
	if (name->equals("find_global")) {
		u->_findClass = value;
		return 0;
	}
	if (noDelete(value) < 0)
		return -1;
	if (name->equals("memo")) {
		if (!isInstanceOf(value, &g_dictType)) {
			setErrorString(g_excTypeError, "memo must be a dictionary");
			return -1;
		}
		u->_memo = (Dict *)value;
		return 0;
	}
	setErrorString(g_excAttributeError, name->_data);
	return -1;
}

// ---- Module functions ---------------------------------------------------------------

static ObjRef cpmPickler(Object *, Tuple *args, Dict *) {
	int32 bin = 1;
	if (args->_size <= 1) {
		if (!args->_size || intArg(args->get(0), &bin))
			return newPickler(nullptr, bin);
		clearError();
	}
	bin = 0;
	if (!checkArgs("Pickler", args, 1, 2))
		return ObjRef();
	if (args->_size > 1 && !intArg(args->get(1), &bin))
		return ObjRef();
	return newPickler(args->get(0), bin);
}

static ObjRef cpmUnpickler(Object *, Tuple *args, Dict *) {
	if (!checkArgs("Unpickler", args, 1, 1))
		return ObjRef();
	return newUnpickler(args->get(0));
}

static ObjRef cpmDump(Object *, Tuple *args, Dict *) {
	int32 bin = 0;
	// Its format string has no name
	if (args->_size < 2 || args->_size > 3) {
		setErrorFormat(g_excTypeError, "function takes at %s %d arguments (%d given)", args->_size < 2 ? "least" : "most",
		               args->_size < 2 ? 2 : 3, args->_size);
		return ObjRef();
	}
	if (args->_size > 2 && !intArg(args->get(2), &bin))
		return ObjRef();
	ObjRef p = newPickler(args->get(1), bin);
	if (!p || dump((Pickler *)p.get(), args->get(0)) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef cpmDumps(Object *, Tuple *args, Dict *) {
	int32 bin = 0;
	if (!checkArgs("dumps", args, 1, 2))
		return ObjRef();
	if (args->_size > 1 && !intArg(args->get(1), &bin))
		return ObjRef();
	ObjRef file = newStringIOOutput();
	ObjRef p = newPickler(file.get(), bin);
	if (!p || dump((Pickler *)p.get(), args->get(0)) < 0)
		return ObjRef();
	return stringIOGetValue(file.get());
}

static ObjRef cpmLoad(Object *, Tuple *args, Dict *) {
	if (!checkArgs("load", args, 1, 1))
		return ObjRef();
	ObjRef u = newUnpickler(args->get(0));
	if (!u)
		return ObjRef();
	return unpicklerLoad((Unpickler *)u.get(), false);
}

static ObjRef cpmLoads(Object *, Tuple *args, Dict *) {
	if (!checkArgs("loads", args, 1, 1))
		return ObjRef();
	if (!isInstanceOf(args->get(0), &g_strType)) {
		setErrorFormat(g_excTypeError, "loads() argument 1 must be string, not %.50s", typeName(args->get(0)));
		return ObjRef();
	}
	ObjRef file = newStringIOInput(args->get(0));
	if (!file)
		return ObjRef();
	ObjRef u = newUnpickler(file.get());
	if (!u)
		return ObjRef();
	return unpicklerLoad((Unpickler *)u.get(), false);
}

#define BINARY_DOC \
	"If the optional argument, binary, is provided and is true, then the\n" \
	"pickle will be written in binary format, which is more space and\n" \
	"computationally efficient. \n"

static const MethodDef g_cPickleMethods[] = {
	{ "dump", cpmDump, kMethVarArgs, "dump(object, file, [binary]) --Write an object in pickle format to the given file\n\n" BINARY_DOC },
	{ "dumps", cpmDumps, kMethVarArgs, "dumps(object, [binary]) --Return a string containing an object in pickle format\n\n" BINARY_DOC },
	{ "load", cpmLoad, kMethVarArgs, "load(file) -- Load a pickle from the given file" },
	{ "loads", cpmLoads, kMethVarArgs, "loads(string) -- Load a pickle from the given string" },
	{ "Pickler", cpmPickler, kMethVarArgs, "Pickler(file, [binary]) -- Create a pickler\n\n"
	  "If the optional argument, binary, is provided and is true, then\n"
	  "pickles will be written in binary format, which is more space and\n"
	  "computationally efficient. \n" },
	{ "Unpickler", cpmUnpickler, kMethVarArgs, "Unpickler(file) -- Create an unpickler" },
	{ nullptr, nullptr, 0, nullptr }
};

// As PyErr_NewException, with methods defined by Python source
static Object *newExceptionClass(Module *m, const char *name, Object *base, const char *source) {
	Ref<Dict> dict = newDict();
	if (source) {
		ObjRef code = compileString(source, "<string>", kCompileExec);
		if (!code)
			return nullptr;
		ObjRef r = evalCode((Code *)code.get(), m->_dict.get(), dict.get());
		if (!r)
			return nullptr;
	}
	Ref<Str> modName = newStr("cPickle");
	dict->setItemString("__module__", modName.get());
	Ref<Tuple> bases = tuplePack(1, base ? base : g_excException);
	Ref<Str> className = newStr(name);
	ObjRef cls = newClassObj(bases.get(), dict.get(), className.get());
	if (!cls)
		return nullptr;
	moduleAddObject(m, name, cls.get());
	incRef(cls.get()); // held by the module state
	return cls.get();
}

static void readyType(Type *t, const char *doc) {
	if (t->_flags & kTypeReady)
		return;
	t->_dict = newDict();
	Ref<Str> d = newStr(doc);
	t->_dict->setItemString("__doc__", d.get());
	typeReady(t);
}

void initCPickleModule(Module *m) {
	initStringIOTypes();
	g_picklerType.methods = g_picklerMethods;
	g_picklerType.getSets = g_picklerGetSets;
	g_picklerType.getAttr = genericGetAttr;
	g_picklerType.setAttr = genericSetAttr;
	readyType(&g_picklerType, "Objects that know how to pickle objects\n");
	g_unpicklerType.getAttr = unpicklerGetAttr;
	g_unpicklerType.setAttr = unpicklerSetAttr;
	readyType(&g_unpicklerType, "Objects that know how to unpickle");

	Ref<Str> modName = newStr("cPickle");
	for (const MethodDef *d = g_cPickleMethods; d->name; d++) {
		BuiltinFunction *f = new BuiltinFunction(d, nullptr);
		ObjRef rf(f);
		f->_module = modName;
		moduleAddObject(m, d->name, f);
	}
	moduleAddString(m, "__doc__", "C implementation and optimization of the Python pickle module\n\n"
		"cPickle.c,v 1.71 1999/07/11 13:30:34 jim Exp\n");
	moduleAddString(m, "__version__", "1.71");
	moduleAddString(m, "format_version", "1.3");
	Ref<List> formats = newList();
	formats->append(newStr("1.0").get());
	formats->append(newStr("1.1").get());
	formats->append(newStr("1.2").get());
	moduleAddObject(m, "compatible_formats", formats.get());

	ObjRef builtins = importModule("__builtin__");
	if (!builtins)
		return;
	moduleAddObject(m, "__builtins__", builtins.get());
	ObjRef copyReg = importModule("copy_reg");
	if (!copyReg)
		return;
	ObjRef table = getAttrString(copyReg.get(), "dispatch_table");
	if (!table)
		return;
	ObjRef safe = getAttrString(copyReg.get(), "safe_constructors");
	if (!safe)
		return;
	g_dispatchTable = table.release();
	g_safeConstructors = safe.release();

	g_pickleError = newExceptionClass(m, "PickleError", nullptr,
		"def __init__(self, *args): self.args=args\n\n"
		"def __str__(self):\n"
		"  return self.args and ('%s' % self.args[0]) or '(what)'\n");
	if (!g_pickleError)
		return;
	g_picklingError = newExceptionClass(m, "PicklingError", g_pickleError, nullptr);
	if (!g_picklingError)
		return;
	g_unpickleableError = newExceptionClass(m, "UnpickleableError", g_picklingError,
		"def __init__(self, *args): self.args=args\n\n"
		"def __str__(self):\n"
		"  a=self.args\n"
		"  a=a and type(a[0]) or '(what)'\n"
		"  return 'Cannot pickle %s objects' % a\n");
	if (!g_unpickleableError)
		return;
	g_unpicklingError = newExceptionClass(m, "UnpicklingError", g_pickleError, nullptr);
	if (!g_unpicklingError)
		return;
	// A string exception in 2.2
	g_badPickleGet = newStr("cPickle.BadPickleGet").release();
	moduleAddObject(m, "BadPickleGet", g_badPickleGet);

	ObjRef cStringIO = importModule("cStringIO");
	if (!cStringIO)
		return;
}

} // End of namespace Python
} // End of namespace Yaga
