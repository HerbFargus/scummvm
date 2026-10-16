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
#include "yaga/python/eval.h"
#include "yaga/python/vm.h"

// The sys module, and the console streams behind sys.stdout and sys.stderr

namespace Yaga {
namespace Python {

// A native stream object for stdout/stderr: write() goes to the output sink
struct ConsoleStream : public Object {
	bool _stderr;
	int _softspace;
	explicit ConsoleStream(Type *t, bool err) : Object(t), _stderr(err), _softspace(0) {}
};

static Type g_consoleStreamType("file", &g_objectType);

static ObjRef streamWrite(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("write", args, 1, 1))
		return ObjRef();
	ObjRef s(args->get(0));
	if (isInstanceOf(s.get(), &g_unicodeType)) {
		s = unicodeToStr(s.get());
		if (!s)
			return ObjRef();
	}
	Str *str = asStr(s.get());
	if (!str)
		return ObjRef();
	ConsoleStream *cs = (ConsoleStream *)self;
	OutputSink *out = Interpreter::instance()->output();
	if (cs->_stderr)
		out->writeStderr(str->_data, str->_len);
	else
		out->writeStdout(str->_data, str->_len);
	return ObjRef(g_none);
}

static ObjRef streamFlush(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static ObjRef streamWritelines(Object *self, Tuple *args, Dict *kw) {
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
		if (!streamWrite(self, a.get(), kw))
			return ObjRef();
	}
}

static ObjRef streamIsatty(Object *, Tuple *, Dict *) {
	return newBool(false);
}

static ObjRef streamGetSoftspace(Object *o) {
	return newInt(((ConsoleStream *)o)->_softspace);
}

static int streamSetSoftspace(Object *o, Object *v) {
	int32 x;
	if (!v || !asInt(v, &x))
		return -1;
	((ConsoleStream *)o)->_softspace = x;
	return 0;
}

static ObjRef streamGetName(Object *o) {
	return newStr(((ConsoleStream *)o)->_stderr ? "<stderr>" : "<stdout>");
}

static ObjRef streamGetMode(Object *) {
	return newStr("w");
}

static ObjRef streamGetClosed(Object *) {
	return newBool(false);
}

static const MethodDef g_streamMethods[] = {
	{ "write", streamWrite, kMethVarArgs, nullptr },
	{ "writelines", streamWritelines, kMethVarArgs, nullptr },
	{ "flush", streamFlush, kMethVarArgs, nullptr },
	{ "isatty", streamIsatty, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_streamGetSets[] = {
	{ "softspace", streamGetSoftspace, streamSetSoftspace },
	{ "name", streamGetName, nullptr },
	{ "mode", streamGetMode, nullptr },
	{ "closed", streamGetClosed, nullptr },
	{ nullptr, nullptr, nullptr }
};

static ObjRef streamRepr(Object *o) {
	ConsoleStream *cs = (ConsoleStream *)o;
	return newStr(Common::String::format("<open file '%s', mode 'w' at 0x%08X>", cs->_stderr ? "<stderr>" : "<stdout>", objectAddress(o)));
}

// ---- Access from C++ ---------------------------------------------------------------

ObjRef sysGetObject(const char *name) {
	if (!g_ts->sysDict)
		return ObjRef();
	Object *v = g_ts->sysDict->getItemString(name);
	return v ? ObjRef(v) : ObjRef();
}

int sysSetObject(const char *name, Object *value) {
	if (!g_ts->sysDict)
		return -1;
	if (!value) {
		int r = g_ts->sysDict->delItem(newStr(name).get());
		clearError();
		return r;
	}
	return g_ts->sysDict->setItemString(name, value);
}

int fileWriteString(const char *s, Object *f) {
	if (!f) {
		setErrorString(g_excTypeError, "writeobject with NULL file");
		return -1;
	}
	Ref<Str> str = newStr(s);
	ObjRef r = callMethod(f, "write", 1, str.get());
	return r ? 0 : -1;
}

int fileWriteObject(Object *v, Object *f, bool raw) {
	if (!f) {
		setErrorString(g_excTypeError, "writeobject with NULL file");
		return -1;
	}
	ObjRef writer = getAttrString(f, "write");
	if (!writer)
		return -1;
	ObjRef value = raw ? str(v) : repr(v);
	if (!value)
		return -1;
	ObjRef r = callFunction(writer.get(), 1, value.get());
	return r ? 0 : -1;
}

int fileSoftSpace(Object *f, int newflag) {
	int oldflag = 0;
	if (!f)
		return 0;
	ObjRef errType, errValue, errTb;
	ObjRef v = getAttrString(f, "softspace");
	if (!v)
		clearError();
	else if (isInstanceOf(v.get(), &g_intType))
		oldflag = ((Int *)v.get())->_value;
	Ref<Int> nv = newInt(newflag);
	if (setAttrString(f, "softspace", nv.get()) != 0)
		clearError();
	return oldflag;
}

void writeToStdout(const char *s) {
	ObjRef out = sysGetObject("stdout");
	if (out && fileWriteString(s, out.get()) == 0)
		return;
	clearError();
	Interpreter::instance()->output()->writeStdout(s, strlen(s));
}

void writeToStderr(const char *s) {
	ObjRef err = sysGetObject("stderr");
	if (err && fileWriteString(s, err.get()) == 0)
		return;
	clearError();
	Interpreter::instance()->output()->writeStderr(s, strlen(s));
}

// ---- sys functions -----------------------------------------------------------------

static ObjRef sysExcInfo(Object *, Tuple *args, Dict *) {
	if (!checkArgs("exc_info", args, 0, 0))
		return ObjRef();
	Object *t = g_ts->excType ? g_ts->excType.get() : g_none;
	Object *v = g_ts->excValue ? g_ts->excValue.get() : g_none;
	Object *tb = g_ts->excTraceback ? g_ts->excTraceback.get() : g_none;
	return tuplePack(3, t, v, tb);
}

static ObjRef sysExit(Object *, Tuple *args, Dict *) {
	if (!checkArgs("exit", args, 0, 1))
		return ObjRef();
	setError(g_excSystemExit, args->_size ? args->get(0) : g_none);
	return ObjRef();
}

static ObjRef sysGetRecursionLimit(Object *, Tuple *, Dict *) {
	return newInt(g_ts->recursionLimit);
}

static ObjRef sysSetRecursionLimit(Object *, Tuple *args, Dict *) {
	int32 n;
	if (!checkArgs("setrecursionlimit", args, 1, 1) || !asInt(args->get(0), &n))
		return ObjRef();
	if (n <= 0) {
		setErrorString(g_excValueError, "recursion limit must be positive");
		return ObjRef();
	}
	g_ts->recursionLimit = n;
	return ObjRef(g_none);
}

static ObjRef sysGetRefCount(Object *, Tuple *args, Dict *) {
	if (!checkArgs("getrefcount", args, 1, 1))
		return ObjRef();
	return newInt(args->get(0)->_refs);
}

static ObjRef sysGetFrame(Object *, Tuple *args, Dict *) {
	int32 depth = 0;
	if (!checkArgs("_getframe", args, 0, 1))
		return ObjRef();
	if (args->_size && !asInt(args->get(0), &depth))
		return ObjRef();
	Frame *f = g_ts->frame;
	while (depth > 0 && f) {
		f = f->_back.get();
		--depth;
	}
	if (!f) {
		setErrorString(g_excValueError, "call stack is not deep enough");
		return ObjRef();
	}
	return ObjRef(f);
}

static ObjRef sysGetDefaultEncoding(Object *, Tuple *, Dict *) {
	return newStr("ascii");
}

static ObjRef sysSetCheckInterval(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static ObjRef sysDisplayHook(Object *, Tuple *args, Dict *) {
	if (!checkArgs("displayhook", args, 1, 1))
		return ObjRef();
	Object *o = args->get(0);
	if (o == g_none)
		return ObjRef(g_none);
	ObjRef out = sysGetObject("stdout");
	if (!out) {
		setErrorString(g_excRuntimeError, "lost sys.stdout");
		return ObjRef();
	}
	if (fileWriteObject(o, out.get(), false) < 0 || fileWriteString("\n", out.get()) < 0)
		return ObjRef();
	Ref<Module> b((Module *)getModuleDict()->getItemString("__builtin__"));
	if (b)
		b->_dict->setItemString("_", o);
	return ObjRef(g_none);
}

static ObjRef sysExceptHook(Object *, Tuple *args, Dict *) {
	if (!checkArgs("excepthook", args, 3, 3))
		return ObjRef();
	restoreError(args->get(0), args->get(1), args->get(2) == g_none ? nullptr : args->get(2));
	Common::String msg = formatError();
	writeToStderr((msg + "\n").c_str());
	return ObjRef(g_none);
}

static const MethodDef g_sysMethods[] = {
	{ "exc_info", sysExcInfo, kMethVarArgs, nullptr },
	{ "exit", sysExit, kMethVarArgs, nullptr },
	{ "getrecursionlimit", sysGetRecursionLimit, kMethVarArgs, nullptr },
	{ "setrecursionlimit", sysSetRecursionLimit, kMethVarArgs, nullptr },
	{ "getrefcount", sysGetRefCount, kMethVarArgs, nullptr },
	{ "_getframe", sysGetFrame, kMethVarArgs, nullptr },
	{ "getdefaultencoding", sysGetDefaultEncoding, kMethVarArgs, nullptr },
	{ "setcheckinterval", sysSetCheckInterval, kMethVarArgs, nullptr },
	{ "displayhook", sysDisplayHook, kMethVarArgs, nullptr },
	{ "excepthook", sysExceptHook, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

void initSysModule(const Common::Array<Common::String> &builtinNames) {
	g_consoleStreamType.methods = g_streamMethods;
	g_consoleStreamType.getSets = g_streamGetSets;
	g_consoleStreamType.repr = streamRepr;
	g_consoleStreamType.getAttr = genericGetAttr;
	g_consoleStreamType.setAttr = genericSetAttr;
	typeReady(&g_consoleStreamType);

	Ref<Module> sys = initModule("sys", g_sysMethods);
	Dict *d = sys->_dict.get();
	g_ts->sysDict = d;
	d->setItemString("modules", g_ts->modules.get());
	ObjRef out(new ConsoleStream(&g_consoleStreamType, false));
	ObjRef err(new ConsoleStream(&g_consoleStreamType, true));
	d->setItemString("stdout", out.get());
	d->setItemString("stderr", err.get());
	d->setItemString("stdin", out.get());
	d->setItemString("__stdout__", out.get());
	d->setItemString("__stderr__", err.get());
	d->setItemString("__stdin__", out.get());
	ObjRef dh(d->getItemString("displayhook"));
	ObjRef eh(d->getItemString("excepthook"));
	d->setItemString("__displayhook__", dh.get());
	d->setItemString("__excepthook__", eh.get());
	moduleAddString(sys.get(), "version", "2.2.3 (#42, May 30 2003, 18:12:08) [MSC 32 bit (Intel)]");
	moduleAddInt(sys.get(), "hexversion", 0x020203f0);
	Ref<Int> v2 = newInt(2), v3 = newInt(3), zero = newInt(0);
	Ref<Str> final = newStr("final");
	Ref<Tuple> vi = tuplePack(5, v2.get(), v2.get(), v3.get(), final.get(), zero.get());
	d->setItemString("version_info", vi.get());
	moduleAddInt(sys.get(), "api_version", 1011);
	moduleAddString(sys.get(), "copyright", "Copyright (c) 2001, 2002, 2003 Python Software Foundation.\nAll Rights Reserved.");
	moduleAddString(sys.get(), "platform", "win32");
	moduleAddString(sys.get(), "byteorder", "little");
	moduleAddInt(sys.get(), "maxint", 0x7fffffff);
	moduleAddInt(sys.get(), "maxunicode", 0xffff);
	moduleAddString(sys.get(), "winver", "2.2");
	moduleAddInt(sys.get(), "dllhandle", 0x1e000000);
	moduleAddString(sys.get(), "prefix", "");
	moduleAddString(sys.get(), "exec_prefix", "");
	moduleAddString(sys.get(), "executable", "");
	Ref<List> argv = newList();
	d->setItemString("argv", argv.get());
	Ref<List> path = newList();
	d->setItemString("path", path.get());
	Ref<Tuple> names = newTuple(builtinNames.size());
	for (uint32 i = 0; i < builtinNames.size(); i++)
		names->set(i, newStr(builtinNames[i]).get());
	d->setItemString("builtin_module_names", names.get());
	Ref<List> warnoptions = newList();
	d->setItemString("warnoptions", warnoptions.get());
}

} // End of namespace Python
} // End of namespace Yaga
