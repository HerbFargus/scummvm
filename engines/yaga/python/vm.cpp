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

#include "common/stream.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/eval.h"
#include "yaga/python/vm.h"

// The interpreter as a whole, and the import system (CPython 2.2's import.c)

namespace Yaga {
namespace Python {

Interpreter *Interpreter::_instance = nullptr;

void initBuiltins();
void initSysModule(const Common::Array<Common::String> &builtinNames);
void initStandardModules(Interpreter *vm);
void typeInitAll();

// The modules compiled into the games' python22.dll
static const char *const g_builtinModuleNames[] = {
	"__builtin__", "__main__", "_codecs", "_locale", "_weakref", "array", "audioop", "binascii",
	"cPickle", "cStringIO", "cmath", "errno", "exceptions", "gc", "imageop", "imp", "marshal",
	"math", "md5", "msvcrt", "new", "nt", "operator", "pcre", "regex", "rgbimg", "rotor", "sha",
	"signal", "strop", "struct", "sys", "thread", "time", "xreadlines", "xxsubtype"
};

Interpreter::Interpreter(FileSystem *fs, OutputSink *out) : _fs(fs), _out(out), _exited(false), _exitCode(0) {
	_instance = this;
	g_ts = new ThreadState();
	g_ts->modules = newDict();
	typeInitAll();
	Common::Array<Common::String> names;
	for (uint i = 0; i < ARRAYSIZE(g_builtinModuleNames); i++)
		names.push_back(g_builtinModuleNames[i]);
	initExceptions();
	initSysModule(names);
	initBuiltins();
	Module *main = addModule("__main__");
	main->_dict->setItemString("__builtins__", g_ts->builtins.get());
	initStandardModules(this);
}

Interpreter::~Interpreter() {
	// Objects are not torn down one by one: the interpreter lives as long as the engine
	_instance = nullptr;
}

void Interpreter::addNativeModule(const char *name, ModuleInitFunc init, bool extension) {
	NativeModule m;
	m.init = init;
	m.extension = extension;
	_natives[name] = m;
}

bool isNativeModule(const char *name, bool *extension) {
	const Common::HashMap<Common::String, Interpreter::NativeModule> &natives = Interpreter::instance()->nativeModules();
	Common::HashMap<Common::String, Interpreter::NativeModule>::const_iterator it = natives.find(name);
	if (it == natives.end())
		return false;
	if (extension)
		*extension = it->_value.extension;
	return true;
}

ObjRef initNativeModule(const char *name) {
	Object *existing = getModuleDict()->getItemString(name);
	if (existing)
		return ObjRef(existing);
	const Common::HashMap<Common::String, Interpreter::NativeModule> &natives = Interpreter::instance()->nativeModules();
	Common::HashMap<Common::String, Interpreter::NativeModule>::const_iterator it = natives.find(name);
	if (it == natives.end())
		return ObjRef();
	Module *m = addModule(name);
	ObjRef rm(m);
	it->_value.init(m);
	if (errorOccurred()) {
		getModuleDict()->delItem(newStr(name).get());
		return ObjRef();
	}
	return rm;
}

void Interpreter::setArgv(const Common::Array<Common::String> &argv) {
	Ref<List> l = newList();
	for (uint i = 0; i < argv.size(); i++)
		l->append(newStr(argv[i]).get());
	sysSetObject("argv", l.get());
}

void Interpreter::setExecutable(const Common::String &path) {
	sysSetObject("executable", newStr(path).get());
}

void Interpreter::setPath(const Common::Array<Common::String> &path) {
	Ref<List> l = newList();
	for (uint i = 0; i < path.size(); i++)
		l->append(newStr(path[i]).get());
	sysSetObject("path", l.get());
}

bool Interpreter::reportError() {
	_exited = false;
	if (errorMatches(g_excSystemExit)) {
		ObjRef type, value, tb;
		fetchError(&type, &value, &tb);
		normalizeException(&type, &value, &tb);
		clearError();
		_exited = true;
		_exitCode = 0;
		ObjRef code;
		if (value && isType(value.get(), &g_instanceType)) {
			code = getAttrString(value.get(), "code");
			clearError();
		} else {
			code = value;
		}
		if (code && code.get() != g_none) {
			if (isInstanceOf(code.get(), &g_intType)) {
				_exitCode = ((Int *)code.get())->_value;
			} else {
				ObjRef s = str(code.get());
				if (s)
					writeToStderr((((Str *)s.get())->toString() + "\n").c_str());
				clearError();
				_exitCode = 1;
			}
		}
		_lastError = Common::String::format("SystemExit(%d)", _exitCode);
		return false;
	}
	_lastError = formatError();
	writeToStderr((_lastError + "\n").c_str());
	return false;
}

bool Interpreter::runString(const char *source, const char *filename) {
	ObjRef code = compileString(source, filename, kCompileExec);
	if (!code)
		return reportError();
	return runCode(code.get());
}

bool Interpreter::runCode(Object *code) {
	Module *main = addModule("__main__");
	Dict *d = main->_dict.get();
	if (!d->getItemString("__builtins__"))
		d->setItemString("__builtins__", g_ts->builtins.get());
	ObjRef r = evalCode((Code *)code, d, d);
	if (!r)
		return reportError();
	return true;
}

bool Interpreter::execCodeModule(const char *name, Object *code) {
	ObjRef m = Python::execCodeModule(name, (Code *)code);
	if (!m)
		return reportError();
	return true;
}

// ---- Imports -------------------------------------------------------------------

Dict *getModuleDict() {
	return g_ts->modules.get();
}

Module *addModule(const char *name) {
	Dict *modules = getModuleDict();
	Object *m = modules->getItemString(name);
	if (m && isType(m, &g_moduleType))
		return (Module *)m;
	Ref<Module> nm = newModule(name);
	modules->setItemString(name, nm.get());
	return nm.get(); // sys.modules holds the reference
}

ObjRef execCodeModule(const char *name, Code *co) {
	Dict *modules = getModuleDict();
	Module *m = addModule(name);
	ObjRef rm(m);
	Dict *d = m->_dict.get();
	if (!d->getItemString("__builtins__"))
		d->setItemString("__builtins__", getBuiltins());
	d->setItemString("__file__", co->_filename.get());
	ObjRef v = evalCode(co, d, d);
	if (!v)
		return ObjRef();
	Object *result = modules->getItemString(name);
	if (!result) {
		setErrorFormat(g_excImportError, "Loaded module %.200s not found in sys.modules", name);
		return ObjRef();
	}
	return ObjRef(result);
}

// The default importer knows the native modules and anything already in
// sys.modules; the games install their own importer (iu.py) over it
static ObjRef findAndLoad(const char *fullname, const char *subname, Object *path) {
	bool extension;
	if (!path && isNativeModule(subname, &extension) && !extension) {
		ObjRef m = initNativeModule(subname);
		if (m)
			return m;
		if (errorOccurred())
			return ObjRef();
	}
	return ObjRef(g_none);
}

static ObjRef importSubmodule(Object *mod, const char *subname, const char *fullname) {
	Dict *modules = getModuleDict();
	Object *m = modules->getItemString(fullname);
	if (m)
		return ObjRef(m);
	ObjRef path;
	if (mod != g_none) {
		path = getAttrString(mod, "__path__");
		if (!path) {
			clearError();
			return ObjRef(g_none);
		}
	}
	ObjRef r = findAndLoad(fullname, subname, path.get());
	if (!r)
		return ObjRef();
	if (r.get() != g_none && mod != g_none) {
		if (setAttrString(mod, subname, r.get()) < 0)
			return ObjRef();
	}
	return r;
}

static Object *getParent(Dict *globals, Common::String &buf) {
	buf.clear();
	if (!globals)
		return g_none;
	Object *modname = globals->getItemString("__name__");
	if (!modname || !isInstanceOf(modname, &g_strType))
		return g_none;
	Object *modpath = globals->getItemString("__path__");
	if (modpath) {
		buf = ((Str *)modname)->toString();
	} else {
		Common::String start = ((Str *)modname)->toString();
		const char *lastdot = strrchr(start.c_str(), '.');
		if (!lastdot)
			return g_none;
		buf = Common::String(start.c_str(), lastdot - start.c_str());
	}
	Object *parent = getModuleDict()->getItemString(buf.c_str());
	if (!parent) {
		buf.clear();
		return g_none;
	}
	return parent;
}

static ObjRef loadNext(Object *mod, Object *altmod, const char **pname, Common::String &buf) {
	const char *name = *pname;
	const char *dot = strchr(name, '.');
	uint32 len;
	if (!dot) {
		*pname = nullptr;
		len = strlen(name);
	} else {
		*pname = dot + 1;
		len = dot - name;
	}
	if (len == 0) {
		setErrorString(g_excValueError, "Empty module name");
		return ObjRef();
	}
	Common::String part(name, len);
	Common::String saved = buf;
	if (!buf.empty())
		buf += '.';
	buf += part;
	ObjRef result = importSubmodule(mod, part.c_str(), buf.c_str());
	if (result && result.get() == g_none && altmod != mod) {
		result = importSubmodule(altmod, part.c_str(), part.c_str());
		if (result && result.get() != g_none) {
			// Mark the miss in the package so it is not looked up again
			getModuleDict()->setItemString(buf.c_str(), g_none);
			buf = part;
		}
	}
	if (!result)
		return ObjRef();
	if (result.get() == g_none) {
		setErrorFormat(g_excImportError, "No module named %.200s", part.c_str());
		return ObjRef();
	}
	return result;
}

static bool ensureFromlist(Object *mod, Object *fromlist, Common::String &buf, bool recursive) {
	if (!hasAttrString(mod, "__path__"))
		return true;
	for (int32 i = 0;; i++) {
		ObjRef item = sequenceGetItem(fromlist, i);
		if (!item) {
			if (errorMatches(g_excIndexError)) {
				clearError();
				return true;
			}
			return false;
		}
		if (!isInstanceOf(item.get(), &g_strType)) {
			setErrorString(g_excTypeError, "Item in ``from list'' not a string");
			return false;
		}
		Str *s = (Str *)item.get();
		if (s->_data[0] == '*') {
			if (recursive)
				continue;
			ObjRef all = getAttrString(mod, "__all__");
			if (!all)
				clearError();
			else if (!ensureFromlist(mod, all.get(), buf, true))
				return false;
			continue;
		}
		ObjRef has = getAttr(mod, s);
		if (!has) {
			clearError();
			Common::String full = buf + "." + s->toString();
			ObjRef submod = importSubmodule(mod, s->_data, full.c_str());
			if (!submod)
				return false;
		}
	}
}

ObjRef importModuleEx(const char *name, Dict *globals, Dict *locals, Object *fromlist) {
	Common::String buf;
	Object *parent = getParent(globals, buf);
	const char *rest = name;
	ObjRef head = loadNext(parent, g_none, &rest, buf);
	if (!head)
		return ObjRef();
	ObjRef tail = head;
	while (rest) {
		ObjRef next = loadNext(tail.get(), tail.get(), &rest, buf);
		if (!next)
			return ObjRef();
		tail = next;
	}
	if (fromlist && (fromlist == g_none || isTrue(fromlist) <= 0)) {
		clearError();
		fromlist = nullptr;
	}
	if (!fromlist)
		return head;
	if (!ensureFromlist(tail.get(), fromlist, buf, false))
		return ObjRef();
	return tail;
}

// import name, through __builtin__.__import__ as the bytecode does
ObjRef importModule(const char *name) {
	Dict *builtins = getBuiltins();
	Object *importFunc = builtins ? builtins->getItemString("__import__") : nullptr;
	if (!importFunc)
		return importModuleEx(name, nullptr, nullptr, nullptr);
	ObjRef keep(importFunc);
	Ref<Str> n = newStr(name);
	Dict *globals = getGlobals();
	Ref<Tuple> fromlist = tuplePack(1, newStr("__doc__").get());
	ObjRef m = callFunction(importFunc, 4, n.get(), globals ? (Object *)globals : g_none, globals ? (Object *)globals : g_none, fromlist.get());
	return m;
}

} // End of namespace Python
} // End of namespace Yaga
