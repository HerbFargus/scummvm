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
#include "common/system.h"
#include "common/textconsole.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/eval.h"
#include "yaga/python/vm.h"

// The built-in modules of python22.dll that the games' scripts use

namespace Yaga {
namespace Python {

void initMarshalModule();
void initStructModule(Module *m);
void initSreModule(Module *m);
void initCPickleModule(Module *m);
void initCStringIOModule(Module *m);

// ---- imp -------------------------------------------------------------------------

enum {
	kSearchError = 0, kPySource = 1, kPyCompiled = 2, kCExtension = 3,
	kPyResource = 4, kPkgDirectory = 5, kCBuiltin = 6, kPyFrozen = 7
};

static ObjRef impGetMagic(Object *, Tuple *, Dict *) {
	return newStr("\x2d\xed\x0d\x0a", 4);
}

static ObjRef impGetSuffixes(Object *, Tuple *, Dict *) {
	static const struct { const char *suffix, *mode; int type; } suffixes[] = {
		{ ".pyd", "rb", kCExtension },
		{ ".dll", "rb", kCExtension },
		{ ".py", "r", kPySource },
		{ ".pyw", "r", kPySource },
		{ ".pyc", "rb", kPyCompiled }
	};
	Ref<List> l = newList();
	for (uint i = 0; i < ARRAYSIZE(suffixes); i++) {
		Ref<Tuple> t = tuplePack(3, newStr(suffixes[i].suffix).get(), newStr(suffixes[i].mode).get(), newInt(suffixes[i].type).get());
		l->append(t.get());
	}
	return l;
}

static ObjRef impNewModule(Object *, Tuple *args, Dict *) {
	if (!checkArgs("new_module", args, 1, 1))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	return newModule(name->_data);
}

static bool isBuiltinName(const char *name) {
	ObjRef names = sysGetObject("builtin_module_names");
	if (!names || !isInstanceOf(names.get(), &g_tupleType))
		return false;
	Tuple *t = (Tuple *)names.get();
	for (uint32 i = 0; i < t->_size; i++)
		if (((Str *)t->get(i))->equals(name))
			return true;
	return false;
}

static ObjRef impIsBuiltin(Object *, Tuple *args, Dict *) {
	if (!checkArgs("is_builtin", args, 1, 1))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	if (!isBuiltinName(name->_data))
		return newInt(0);
	// sys and __builtin__ cannot be initialised again
	if (name->equals("sys") || name->equals("__builtin__") || name->equals("__main__"))
		return newInt(-1);
	return newInt(1);
}

static ObjRef impIsFrozen(Object *, Tuple *args, Dict *) {
	if (!checkArgs("is_frozen", args, 1, 1))
		return ObjRef();
	return newInt(0);
}

static ObjRef impInitBuiltin(Object *, Tuple *args, Dict *) {
	if (!checkArgs("init_builtin", args, 1, 1))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	Object *existing = getModuleDict()->getItem(name);
	if (existing)
		return ObjRef(existing);
	if (!isBuiltinName(name->_data))
		return ObjRef(g_none);
	ObjRef m = initNativeModule(name->_data);
	if (!m) {
		if (!errorOccurred())
			setErrorFormat(g_excImportError, "No module named %.200s", name->_data);
		return ObjRef();
	}
	return m;
}

static ObjRef impInitFrozen(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static ObjRef loadDynamic(const char *name, const char *path) {
	bool extension = false;
	if (!isNativeModule(name, &extension) || !extension) {
		setErrorString(g_excImportError, "DLL load failed: The specified module could not be found.");
		return ObjRef();
	}
	ObjRef m = initNativeModule(name);
	if (!m)
		return ObjRef();
	setAttrString(m.get(), "__file__", newStr(path).get());
	return m;
}

static ObjRef impLoadDynamic(Object *, Tuple *args, Dict *) {
	if (!checkArgs("load_dynamic", args, 2, 3))
		return ObjRef();
	Str *name = asStr(args->get(0));
	Str *path = name ? asStr(args->get(1)) : nullptr;
	if (!path)
		return ObjRef();
	return loadDynamic(name->_data, path->_data);
}

static ObjRef loadSource(const char *name, Object *file, const char *path) {
	ObjRef f(file);
	if (!f || f.get() == g_none) {
		f = newFileObject(path, "r");
		if (!f)
			return ObjRef();
	}
	ObjRef text = callMethod(f.get(), "read", 0);
	if (!text)
		return ObjRef();
	ObjRef code = compileString(((Str *)text.get())->_data, path, kCompileExec);
	if (!code)
		return ObjRef();
	return execCodeModule(name, (Code *)code.get());
}

static ObjRef loadCompiled(const char *name, Object *file, const char *path) {
	ObjRef f(file);
	if (!f || f.get() == g_none) {
		f = newFileObject(path, "rb");
		if (!f)
			return ObjRef();
	}
	ObjRef data = callMethod(f.get(), "read", 0);
	if (!data)
		return ObjRef();
	Str *s = (Str *)data.get();
	if (s->_len < 8 || memcmp(s->_data, "\x2d\xed\x0d\x0a", 4) != 0) {
		setErrorFormat(g_excImportError, "Bad magic number in %.200s", path);
		return ObjRef();
	}
	ObjRef code = marshalLoads(s->_data + 8, s->_len - 8);
	if (!code)
		return ObjRef();
	if (!isType(code.get(), &g_codeType)) {
		setErrorFormat(g_excImportError, "Non-code object in %.200s", path);
		return ObjRef();
	}
	return execCodeModule(name, (Code *)code.get());
}

static ObjRef impLoadModule(Object *, Tuple *args, Dict *) {
	if (!checkArgs("load_module", args, 4, 4))
		return ObjRef();
	Str *name = asStr(args->get(0));
	Str *path = name ? asStr(args->get(2)) : nullptr;
	if (!path)
		return ObjRef();
	Object *info = args->get(3);
	ObjRef typeObj = sequenceGetItem(info, 2);
	int32 type;
	if (!typeObj || !asInt(typeObj.get(), &type))
		return ObjRef();
	switch (type) {
	case kPySource:
		return loadSource(name->_data, args->get(1), path->_data);
	case kPyCompiled:
		return loadCompiled(name->_data, args->get(1), path->_data);
	case kCExtension:
		return loadDynamic(name->_data, path->_data);
	case kCBuiltin: {
		Ref<Tuple> a = tuplePack(1, name);
		ObjRef m = impInitBuiltin(nullptr, a.get(), nullptr);
		if (m && m.get() == g_none) {
			setErrorFormat(g_excImportError, "No module named %.200s", name->_data);
			return ObjRef();
		}
		return m;
	}
	case kPyFrozen:
		setErrorFormat(g_excImportError, "No frozen submodule named %.200s", name->_data);
		return ObjRef();
	default:
		setErrorFormat(g_excImportError, "Don't know how to import %.200s (type code %d)", name->_data, type);
		return ObjRef();
	}
}

static ObjRef impLoadSourceFunc(Object *, Tuple *args, Dict *) {
	if (!checkArgs("load_source", args, 2, 3))
		return ObjRef();
	Str *name = asStr(args->get(0));
	Str *path = name ? asStr(args->get(1)) : nullptr;
	if (!path)
		return ObjRef();
	return loadSource(name->_data, args->_size > 2 ? args->get(2) : nullptr, path->_data);
}

static ObjRef impLoadCompiledFunc(Object *, Tuple *args, Dict *) {
	if (!checkArgs("load_compiled", args, 2, 3))
		return ObjRef();
	Str *name = asStr(args->get(0));
	Str *path = name ? asStr(args->get(1)) : nullptr;
	if (!path)
		return ObjRef();
	return loadCompiled(name->_data, args->_size > 2 ? args->get(2) : nullptr, path->_data);
}

static ObjRef impLockHeld(Object *, Tuple *, Dict *) {
	return newBool(false);
}

static ObjRef impNone(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static ObjRef impFindModule(Object *, Tuple *args, Dict *) {
	if (!checkArgs("find_module", args, 1, 2))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	if (args->_size < 2 || args->get(1) == g_none) {
		if (isBuiltinName(name->_data))
			return tuplePack(3, g_none, name, tuplePack(3, newStr("").get(), newStr("").get(), newInt(kCBuiltin).get()).get());
	}
	setErrorFormat(g_excImportError, "No module named %.200s", name->_data);
	return ObjRef();
}

static const MethodDef g_impMethods[] = {
	{ "get_magic", impGetMagic, kMethVarArgs, nullptr },
	{ "get_suffixes", impGetSuffixes, kMethVarArgs, nullptr },
	{ "new_module", impNewModule, kMethVarArgs, nullptr },
	{ "is_builtin", impIsBuiltin, kMethVarArgs, nullptr },
	{ "is_frozen", impIsFrozen, kMethVarArgs, nullptr },
	{ "init_builtin", impInitBuiltin, kMethVarArgs, nullptr },
	{ "init_frozen", impInitFrozen, kMethVarArgs, nullptr },
	{ "load_dynamic", impLoadDynamic, kMethVarArgs, nullptr },
	{ "load_module", impLoadModule, kMethVarArgs, nullptr },
	{ "load_source", impLoadSourceFunc, kMethVarArgs, nullptr },
	{ "load_compiled", impLoadCompiledFunc, kMethVarArgs, nullptr },
	{ "find_module", impFindModule, kMethVarArgs, nullptr },
	{ "lock_held", impLockHeld, kMethVarArgs, nullptr },
	{ "acquire_lock", impNone, kMethVarArgs, nullptr },
	{ "release_lock", impNone, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void initImp(Module *m) {
	for (const MethodDef *d = g_impMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	moduleAddInt(m, "SEARCH_ERROR", kSearchError);
	moduleAddInt(m, "PY_SOURCE", kPySource);
	moduleAddInt(m, "PY_COMPILED", kPyCompiled);
	moduleAddInt(m, "C_EXTENSION", kCExtension);
	moduleAddInt(m, "PY_RESOURCE", kPyResource);
	moduleAddInt(m, "PKG_DIRECTORY", kPkgDirectory);
	moduleAddInt(m, "C_BUILTIN", kCBuiltin);
	moduleAddInt(m, "PY_FROZEN", kPyFrozen);
}

// ---- nt and errno -------------------------------------------------------------------

static Type g_statResultType("nt.stat_result", &g_tupleType);

static ObjRef osError(const char *path, int err, const char *msg) {
	Ref<Tuple> args = path ? tuplePack(3, newInt(err).get(), newStr(msg).get(), newStr(path).get())
	                       : tuplePack(2, newInt(err).get(), newStr(msg).get());
	setError(g_excOSError, args.get());
	return ObjRef();
}

#define STAT_FIELD(fname, index) \
	static ObjRef fname(Object *o) { return ObjRef(((Tuple *)o)->get(index)); }
STAT_FIELD(statMode, 0)
STAT_FIELD(statIno, 1)
STAT_FIELD(statDev, 2)
STAT_FIELD(statNlink, 3)
STAT_FIELD(statUid, 4)
STAT_FIELD(statGid, 5)
STAT_FIELD(statSize, 6)
STAT_FIELD(statAtime, 7)
STAT_FIELD(statMtime, 8)
STAT_FIELD(statCtime, 9)

static const GetSetDef g_statGetSets[] = {
	{ "st_mode", statMode, nullptr }, { "st_ino", statIno, nullptr }, { "st_dev", statDev, nullptr },
	{ "st_nlink", statNlink, nullptr }, { "st_uid", statUid, nullptr }, { "st_gid", statGid, nullptr },
	{ "st_size", statSize, nullptr }, { "st_atime", statAtime, nullptr }, { "st_mtime", statMtime, nullptr },
	{ "st_ctime", statCtime, nullptr }, { nullptr, nullptr, nullptr }
};

static ObjRef ntStat(Object *, Tuple *args, Dict *) {
	if (!checkArgs("stat", args, 1, 1))
		return ObjRef();
	Str *path = asStr(args->get(0));
	if (!path)
		return ObjRef();
	uint32 size = 0, mtime = 0;
	int kind = Interpreter::instance()->fileSystem()->stat(path->_data, &size, &mtime);
	if (kind == 0)
		return osError(path->_data, 2, "No such file or directory");
	if (!mtime)
		mtime = 1060300800; // August 2003
	int32 mode;
	if (kind == 2) {
		mode = 040777;
	} else {
		mode = 0100666;
		Common::String p(path->_data);
		p.toLowercase();
		if (p.hasSuffix(".exe") || p.hasSuffix(".bat") || p.hasSuffix(".com"))
			mode = 0100777;
	}
	Tuple *t = new Tuple(10);
	ObjRef rt(t);
	t->_type = &g_statResultType;
	t->set(0, newInt(mode).get());
	t->set(1, newInt(0).get());
	t->set(2, newInt(2).get());
	t->set(3, newInt(1).get());
	t->set(4, newInt(0).get());
	t->set(5, newInt(0).get());
	t->set(6, newInt(size).get());
	t->set(7, newInt(mtime).get());
	t->set(8, newInt(mtime).get());
	t->set(9, newInt(mtime).get());
	return rt;
}

static ObjRef ntGetcwd(Object *, Tuple *, Dict *) {
	return newStr(Interpreter::instance()->fileSystem()->currentDirectory());
}

static ObjRef ntListdir(Object *, Tuple *args, Dict *) {
	if (!checkArgs("listdir", args, 1, 1))
		return ObjRef();
	Str *path = asStr(args->get(0));
	if (!path)
		return ObjRef();
	Common::Array<Common::String> names;
	if (!Interpreter::instance()->fileSystem()->listDirectory(path->_data, names))
		return osError(path->_data, 3, "The system cannot find the path specified");
	Ref<List> l = newList();
	for (uint i = 0; i < names.size(); i++)
		l->append(newStr(names[i]).get());
	return l;
}

static ObjRef ntRemove(Object *, Tuple *args, Dict *) {
	if (!checkArgs("remove", args, 1, 1))
		return ObjRef();
	Str *path = asStr(args->get(0));
	if (!path)
		return ObjRef();
	if (!Interpreter::instance()->fileSystem()->remove(path->_data))
		return osError(path->_data, 2, "No such file or directory");
	return ObjRef(g_none);
}

static ObjRef ntAccess(Object *, Tuple *args, Dict *) {
	if (!checkArgs("access", args, 2, 2))
		return ObjRef();
	Str *path = asStr(args->get(0));
	if (!path)
		return ObjRef();
	uint32 size, mtime;
	return newBool(Interpreter::instance()->fileSystem()->stat(path->_data, &size, &mtime) != 0);
}

static ObjRef ntUnsupported(Object *, Tuple *, Dict *) {
	return osError(nullptr, 13, "Permission denied");
}

static ObjRef ntGetpid(Object *, Tuple *, Dict *) {
	return newInt(1000);
}

static ObjRef ntExit(Object *, Tuple *args, Dict *) {
	setError(g_excSystemExit, args->_size ? args->get(0) : g_none);
	return ObjRef();
}

static ObjRef ntStrerror(Object *, Tuple *args, Dict *) {
	int32 code;
	if (!checkArgs("strerror", args, 1, 1) || !asInt(args->get(0), &code))
		return ObjRef();
	switch (code) {
	case 2: return newStr("No such file or directory");
	case 13: return newStr("Permission denied");
	case 17: return newStr("File exists");
	default: return newStr("Unknown error");
	}
}

static ObjRef ntTimes(Object *, Tuple *, Dict *) {
	double t = g_system->getMillis() / 1000.0;
	return tuplePack(5, newFloat(t).get(), newFloat(0.0).get(), newFloat(0.0).get(), newFloat(0.0).get(), newFloat(0.0).get());
}

static const MethodDef g_ntMethods[] = {
	{ "stat", ntStat, kMethVarArgs, nullptr },
	{ "lstat", ntStat, kMethVarArgs, nullptr },
	{ "getcwd", ntGetcwd, kMethVarArgs, nullptr },
	{ "listdir", ntListdir, kMethVarArgs, nullptr },
	{ "remove", ntRemove, kMethVarArgs, nullptr },
	{ "unlink", ntRemove, kMethVarArgs, nullptr },
	{ "access", ntAccess, kMethVarArgs, nullptr },
	{ "mkdir", ntUnsupported, kMethVarArgs, nullptr },
	{ "rmdir", ntUnsupported, kMethVarArgs, nullptr },
	{ "rename", ntUnsupported, kMethVarArgs, nullptr },
	{ "chdir", ntUnsupported, kMethVarArgs, nullptr },
	{ "chmod", ntUnsupported, kMethVarArgs, nullptr },
	{ "system", ntUnsupported, kMethVarArgs, nullptr },
	{ "utime", ntUnsupported, kMethVarArgs, nullptr },
	{ "popen", ntUnsupported, kMethVarArgs, nullptr },
	{ "getpid", ntGetpid, kMethVarArgs, nullptr },
	{ "_exit", ntExit, kMethVarArgs, nullptr },
	{ "strerror", ntStrerror, kMethVarArgs, nullptr },
	{ "times", ntTimes, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void initNt(Module *m) {
	for (const MethodDef *d = g_ntMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	moduleAddObject(m, "error", g_excOSError);
	Ref<Dict> env = newDict();
	env->setItemString("PATH", newStr("C:\\WINDOWS\\system32;C:\\WINDOWS").get());
	env->setItemString("SYSTEMROOT", newStr("C:\\WINDOWS").get());
	env->setItemString("WINDIR", newStr("C:\\WINDOWS").get());
	env->setItemString("TEMP", newStr("C:\\WINDOWS\\TEMP").get());
	env->setItemString("TMP", newStr("C:\\WINDOWS\\TEMP").get());
	moduleAddObject(m, "environ", env.get());
	moduleAddObject(m, "stat_result", &g_statResultType);
}

static void initErrno(Module *m) {
	static const struct { const char *name; int value; } codes[] = {
		{ "EPERM", 1 }, { "ENOENT", 2 }, { "ESRCH", 3 }, { "EINTR", 4 }, { "EIO", 5 }, { "ENXIO", 6 },
		{ "E2BIG", 7 }, { "ENOEXEC", 8 }, { "EBADF", 9 }, { "ECHILD", 10 }, { "EAGAIN", 11 }, { "ENOMEM", 12 },
		{ "EACCES", 13 }, { "EFAULT", 14 }, { "EBUSY", 16 }, { "EEXIST", 17 }, { "EXDEV", 18 }, { "ENODEV", 19 },
		{ "ENOTDIR", 20 }, { "EISDIR", 21 }, { "EINVAL", 22 }, { "ENFILE", 23 }, { "EMFILE", 24 }, { "ENOTTY", 25 },
		{ "EFBIG", 27 }, { "ENOSPC", 28 }, { "ESPIPE", 29 }, { "EROFS", 30 }, { "EMLINK", 31 }, { "EPIPE", 32 },
		{ "EDOM", 33 }, { "ERANGE", 34 }, { "EDEADLK", 36 }, { "ENAMETOOLONG", 38 }, { "ENOLCK", 39 },
		{ "ENOSYS", 40 }, { "ENOTEMPTY", 41 }
	};
	Ref<Dict> errorcode = newDict();
	for (uint i = 0; i < ARRAYSIZE(codes); i++) {
		moduleAddInt(m, codes[i].name, codes[i].value);
		errorcode->setItem(newInt(codes[i].value).get(), newStr(codes[i].name).get());
	}
	moduleAddObject(m, "errorcode", errorcode.get());
}

// ---- time ------------------------------------------------------------------------

static uint32 g_clockStart = 0;
static bool g_clockStarted = false;

static ObjRef timeClock(Object *, Tuple *, Dict *) {
	uint32 now = g_system->getMillis();
	if (!g_clockStarted) {
		g_clockStarted = true;
		g_clockStart = now;
		return newFloat(0.0);
	}
	return newFloat((now - g_clockStart) / 1000.0);
}

static double nowSeconds() {
	TimeDate td;
	g_system->getTimeAndDate(td);
	// Days since 1970 for the date, then the time of day
	int y = td.tm_year + 1900, mo = td.tm_mon + 1, d = td.tm_mday;
	int a = (14 - mo) / 12;
	int yy = y + 4800 - a, mm = mo + 12 * a - 3;
	int64 jdn = d + (153 * mm + 2) / 5 + 365LL * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
	int64 days = jdn - 2440588;
	double secs = (double)(days * 86400 + td.tm_hour * 3600 + td.tm_min * 60 + td.tm_sec);
	return secs + (g_system->getMillis() % 1000) / 1000.0;
}

static ObjRef timeTime(Object *, Tuple *, Dict *) {
	return newFloat(nowSeconds());
}

static ObjRef timeSleep(Object *, Tuple *args, Dict *) {
	double secs;
	if (!checkArgs("sleep", args, 1, 1) || !asDouble(args->get(0), &secs))
		return ObjRef();
	if (secs > 0)
		g_system->delayMillis((uint32)(secs * 1000));
	return ObjRef(g_none);
}

static Ref<Tuple> makeTimeTuple(int64 t) {
	int64 days = t / 86400;
	int64 rem = t % 86400;
	if (rem < 0) {
		rem += 86400;
		days--;
	}
	int hour = (int)(rem / 3600), min = (int)(rem % 3600 / 60), sec = (int)(rem % 60);
	int wday = (int)((days + 3) % 7); // 1970-01-01 was a Thursday; Monday == 0
	if (wday < 0)
		wday += 7;
	// Civil date from days (Howard Hinnant's algorithm)
	int64 z = days + 719468;
	int64 era = (z >= 0 ? z : z - 146096) / 146097;
	int64 doe = z - era * 146097;
	int64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64 y = yoe + era * 400;
	int64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64 mp = (5 * doy + 2) / 153;
	int d = (int)(doy - (153 * mp + 2) / 5 + 1);
	int m = (int)(mp < 10 ? mp + 3 : mp - 9);
	if (m <= 2)
		y++;
	static const int cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
	bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
	int yday = cum[m - 1] + d + (leap && m > 2 ? 1 : 0);
	Ref<Tuple> tt = newTuple(9);
	int vals[9] = { (int)y, m, d, hour, min, sec, wday, yday, 0 };
	for (int i = 0; i < 9; i++)
		tt->set(i, newInt(vals[i]).get());
	return tt;
}

static ObjRef timeLocaltime(Object *, Tuple *args, Dict *) {
	if (!checkArgs("localtime", args, 0, 1))
		return ObjRef();
	double t;
	if (args->_size && args->get(0) != g_none) {
		if (!asDouble(args->get(0), &t))
			return ObjRef();
	} else {
		t = nowSeconds();
	}
	return makeTimeTuple((int64)t);
}

static const char *const g_dayNames[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static const char *const g_monthNames[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static ObjRef formatAsctime(Tuple *tt) {
	int32 v[9];
	for (int i = 0; i < 9; i++)
		if (!asInt(tt->get(i), &v[i]))
			return ObjRef();
	return newStr(Common::String::format("%s %s %2d %02d:%02d:%02d %d", g_dayNames[v[6] % 7], g_monthNames[(v[1] + 11) % 12],
	                                     v[2], v[3], v[4], v[5], v[0]));
}

static ObjRef timeCtime(Object *, Tuple *args, Dict *) {
	if (!checkArgs("ctime", args, 0, 1))
		return ObjRef();
	double t = nowSeconds();
	if (args->_size && !asDouble(args->get(0), &t))
		return ObjRef();
	Ref<Tuple> tt = makeTimeTuple((int64)t);
	return formatAsctime(tt.get());
}

static ObjRef timeAsctime(Object *, Tuple *args, Dict *) {
	if (!checkArgs("asctime", args, 0, 1))
		return ObjRef();
	Ref<Tuple> tt;
	if (args->_size) {
		ObjRef t = sequenceTuple(args->get(0));
		if (!t)
			return ObjRef();
		tt = (Tuple *)t.get();
		if (tt->_size != 9) {
			setErrorString(g_excTypeError, "argument must be 9-item sequence");
			return ObjRef();
		}
	} else {
		tt = makeTimeTuple((int64)nowSeconds());
	}
	return formatAsctime(tt.get());
}

static ObjRef timeMktime(Object *, Tuple *args, Dict *) {
	if (!checkArgs("mktime", args, 1, 1))
		return ObjRef();
	ObjRef t = sequenceTuple(args->get(0));
	if (!t)
		return ObjRef();
	Tuple *tt = (Tuple *)t.get();
	int32 v[6];
	for (int i = 0; i < 6; i++)
		if (!asInt(tt->get(i), &v[i]))
			return ObjRef();
	int y = v[0], mo = v[1], d = v[2];
	int a = (14 - mo) / 12;
	int yy = y + 4800 - a, mm = mo + 12 * a - 3;
	int64 jdn = d + (153 * mm + 2) / 5 + 365LL * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
	int64 days = jdn - 2440588;
	return newFloat((double)(days * 86400 + v[3] * 3600 + v[4] * 60 + v[5]));
}

static ObjRef timeStrftime(Object *, Tuple *args, Dict *) {
	if (!checkArgs("strftime", args, 1, 2))
		return ObjRef();
	Str *fmt = asStr(args->get(0));
	if (!fmt)
		return ObjRef();
	Ref<Tuple> tt;
	if (args->_size > 1) {
		ObjRef t = sequenceTuple(args->get(1));
		if (!t)
			return ObjRef();
		tt = (Tuple *)t.get();
	} else {
		tt = makeTimeTuple((int64)nowSeconds());
	}
	int32 v[9];
	for (int i = 0; i < 9; i++)
		if (!asInt(tt->get(i), &v[i]))
			return ObjRef();
	Common::String out;
	for (const char *p = fmt->_data; *p; p++) {
		if (*p != '%' || !p[1]) {
			out += *p;
			continue;
		}
		switch (*++p) {
		case 'Y': out += Common::String::format("%d", v[0]); break;
		case 'y': out += Common::String::format("%02d", v[0] % 100); break;
		case 'm': out += Common::String::format("%02d", v[1]); break;
		case 'd': out += Common::String::format("%02d", v[2]); break;
		case 'H': out += Common::String::format("%02d", v[3]); break;
		case 'I': out += Common::String::format("%02d", (v[3] + 11) % 12 + 1); break;
		case 'M': out += Common::String::format("%02d", v[4]); break;
		case 'S': out += Common::String::format("%02d", v[5]); break;
		case 'p': out += v[3] < 12 ? "AM" : "PM"; break;
		case 'a': out += g_dayNames[v[6] % 7]; break;
		case 'b': out += g_monthNames[(v[1] + 11) % 12]; break;
		case 'j': out += Common::String::format("%03d", v[7]); break;
		case '%': out += '%'; break;
		default: out += '%'; out += *p; break;
		}
	}
	return newStr(out);
}

static const MethodDef g_timeMethods[] = {
	{ "clock", timeClock, kMethVarArgs, nullptr },
	{ "time", timeTime, kMethVarArgs, nullptr },
	{ "sleep", timeSleep, kMethVarArgs, nullptr },
	{ "localtime", timeLocaltime, kMethVarArgs, nullptr },
	{ "gmtime", timeLocaltime, kMethVarArgs, nullptr },
	{ "ctime", timeCtime, kMethVarArgs, nullptr },
	{ "asctime", timeAsctime, kMethVarArgs, nullptr },
	{ "mktime", timeMktime, kMethVarArgs, nullptr },
	{ "strftime", timeStrftime, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void initTime(Module *m) {
	for (const MethodDef *d = g_timeMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	moduleAddInt(m, "timezone", 0);
	moduleAddInt(m, "altzone", 0);
	moduleAddInt(m, "daylight", 0);
	Ref<Tuple> tzname = tuplePack(2, newStr("GMT").get(), newStr("GMT").get());
	moduleAddObject(m, "tzname", tzname.get());
}

// ---- math ------------------------------------------------------------------------

static bool isFiniteValue(double x) {
	return x == x && x - x == 0.0;
}

static ObjRef mathResult(double x, double input) {
	if (x != x && input == input) {
		setErrorString(g_excValueError, "math domain error");
		return ObjRef();
	}
	if (!isFiniteValue(x) && isFiniteValue(input)) {
		setErrorString(g_excOverflowError, "math range error");
		return ObjRef();
	}
	return newFloat(x);
}

#define MATH1(fname, cfunc, domainCheck) \
	static ObjRef fname(Object *, Tuple *args, Dict *) { \
		double x; \
		if (!checkArgs(#cfunc, args, 1, 1) || !asDouble(args->get(0), &x)) \
			return ObjRef(); \
		if (domainCheck) { \
			setErrorString(g_excValueError, "math domain error"); \
			return ObjRef(); \
		} \
		return mathResult(cfunc(x), x); \
	}

MATH1(mathAcos, acos, (x < -1.0 || x > 1.0))
MATH1(mathAsin, asin, (x < -1.0 || x > 1.0))
MATH1(mathAtan, atan, false)
MATH1(mathCeil, ceil, false)
MATH1(mathCos, cos, false)
MATH1(mathCosh, cosh, false)
MATH1(mathExp, exp, false)
MATH1(mathFabs, fabs, false)
MATH1(mathFloor, floor, false)
MATH1(mathLog10, log10, (x <= 0.0))
MATH1(mathSin, sin, false)
MATH1(mathSinh, sinh, false)
MATH1(mathSqrt, sqrt, (x < 0.0))
MATH1(mathTan, tan, false)
MATH1(mathTanh, tanh, false)

static ObjRef mathLog(Object *, Tuple *args, Dict *) {
	double x;
	if (!checkArgs("log", args, 1, 2) || !asDouble(args->get(0), &x))
		return ObjRef();
	if (x <= 0.0) {
		setErrorString(g_excValueError, "math domain error");
		return ObjRef();
	}
	double r = log(x);
	if (args->_size > 1) {
		double b;
		if (!asDouble(args->get(1), &b))
			return ObjRef();
		if (b <= 0.0) {
			setErrorString(g_excValueError, "math domain error");
			return ObjRef();
		}
		r /= log(b);
	}
	return newFloat(r);
}

static ObjRef mathAtan2(Object *, Tuple *args, Dict *) {
	double y, x;
	if (!checkArgs("atan2", args, 2, 2) || !asDouble(args->get(0), &y) || !asDouble(args->get(1), &x))
		return ObjRef();
	return newFloat(atan2(y, x));
}

static ObjRef mathFmod(Object *, Tuple *args, Dict *) {
	double x, y;
	if (!checkArgs("fmod", args, 2, 2) || !asDouble(args->get(0), &x) || !asDouble(args->get(1), &y))
		return ObjRef();
	if (y == 0.0) {
		setErrorString(g_excValueError, "math domain error");
		return ObjRef();
	}
	return newFloat(fmod(x, y));
}

static ObjRef mathHypot(Object *, Tuple *args, Dict *) {
	double x, y;
	if (!checkArgs("hypot", args, 2, 2) || !asDouble(args->get(0), &x) || !asDouble(args->get(1), &y))
		return ObjRef();
	return mathResult(sqrt(x * x + y * y), x);
}

static ObjRef mathPow(Object *, Tuple *args, Dict *) {
	double x, y;
	if (!checkArgs("pow", args, 2, 2) || !asDouble(args->get(0), &x) || !asDouble(args->get(1), &y))
		return ObjRef();
	if (x < 0.0 && y != floor(y)) {
		setErrorString(g_excValueError, "math domain error");
		return ObjRef();
	}
	return mathResult(pow(x, y), x);
}

static ObjRef mathFrexp(Object *, Tuple *args, Dict *) {
	double x;
	if (!checkArgs("frexp", args, 1, 1) || !asDouble(args->get(0), &x))
		return ObjRef();
	int e;
	double m = frexp(x, &e);
	return tuplePack(2, newFloat(m).get(), newInt(e).get());
}

static ObjRef mathLdexp(Object *, Tuple *args, Dict *) {
	double x;
	int32 e;
	if (!checkArgs("ldexp", args, 2, 2) || !asDouble(args->get(0), &x) || !asInt(args->get(1), &e))
		return ObjRef();
	return mathResult(ldexp(x, e), x);
}

static ObjRef mathModf(Object *, Tuple *args, Dict *) {
	double x;
	if (!checkArgs("modf", args, 1, 1) || !asDouble(args->get(0), &x))
		return ObjRef();
	double ip;
	double fp = modf(x, &ip);
	return tuplePack(2, newFloat(fp).get(), newFloat(ip).get());
}

static ObjRef mathDegrees(Object *, Tuple *args, Dict *) {
	double x;
	if (!checkArgs("degrees", args, 1, 1) || !asDouble(args->get(0), &x))
		return ObjRef();
	return newFloat(x * 180.0 / 3.14159265358979323846);
}

static ObjRef mathRadians(Object *, Tuple *args, Dict *) {
	double x;
	if (!checkArgs("radians", args, 1, 1) || !asDouble(args->get(0), &x))
		return ObjRef();
	return newFloat(x * 3.14159265358979323846 / 180.0);
}

static const MethodDef g_mathMethods[] = {
	{ "acos", mathAcos, kMethVarArgs, nullptr }, { "asin", mathAsin, kMethVarArgs, nullptr },
	{ "atan", mathAtan, kMethVarArgs, nullptr }, { "atan2", mathAtan2, kMethVarArgs, nullptr },
	{ "ceil", mathCeil, kMethVarArgs, nullptr }, { "cos", mathCos, kMethVarArgs, nullptr },
	{ "cosh", mathCosh, kMethVarArgs, nullptr }, { "degrees", mathDegrees, kMethVarArgs, nullptr },
	{ "exp", mathExp, kMethVarArgs, nullptr }, { "fabs", mathFabs, kMethVarArgs, nullptr },
	{ "floor", mathFloor, kMethVarArgs, nullptr }, { "fmod", mathFmod, kMethVarArgs, nullptr },
	{ "frexp", mathFrexp, kMethVarArgs, nullptr }, { "hypot", mathHypot, kMethVarArgs, nullptr },
	{ "ldexp", mathLdexp, kMethVarArgs, nullptr }, { "log", mathLog, kMethVarArgs, nullptr },
	{ "log10", mathLog10, kMethVarArgs, nullptr }, { "modf", mathModf, kMethVarArgs, nullptr },
	{ "pow", mathPow, kMethVarArgs, nullptr }, { "radians", mathRadians, kMethVarArgs, nullptr },
	{ "sin", mathSin, kMethVarArgs, nullptr }, { "sinh", mathSinh, kMethVarArgs, nullptr },
	{ "sqrt", mathSqrt, kMethVarArgs, nullptr }, { "tan", mathTan, kMethVarArgs, nullptr },
	{ "tanh", mathTanh, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void initMath(Module *m) {
	for (const MethodDef *d = g_mathMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	moduleAddObject(m, "pi", newFloat(3.14159265358979323846).get());
	moduleAddObject(m, "e", newFloat(2.71828182845904523536).get());
}

// ---- strop -------------------------------------------------------------------------

static ObjRef stropMaketrans(Object *, Tuple *args, Dict *) {
	if (!checkArgs("maketrans", args, 2, 2))
		return ObjRef();
	Str *from = asStr(args->get(0));
	Str *to = from ? asStr(args->get(1)) : nullptr;
	if (!to)
		return ObjRef();
	if (from->_len != to->_len) {
		setErrorString(g_excValueError, "maketrans arguments must have same length");
		return ObjRef();
	}
	Ref<Str> r(new Str(256));
	for (int i = 0; i < 256; i++)
		r->_data[i] = (char)i;
	for (uint32 i = 0; i < from->_len; i++)
		r->_data[(uint8)from->_data[i]] = to->_data[i];
	return r;
}

// The rest of strop forwards to the str methods it was the fast version of
static ObjRef stropForward(Tuple *args, const char *method, uint32 minArgs) {
	if (args->_size < minArgs) {
		setErrorString(g_excTypeError, "not enough arguments");
		return ObjRef();
	}
	Str *s = asStr(args->get(0));
	if (!s)
		return ObjRef();
	ObjRef meth = getAttrString(s, method);
	if (!meth)
		return ObjRef();
	Ref<Tuple> rest = newTuple(args->_size - 1);
	for (uint32 i = 1; i < args->_size; i++)
		rest->set(i - 1, args->get(i));
	return call(meth.get(), rest.get(), nullptr);
}

#define STROP(fname, method) \
	static ObjRef fname(Object *, Tuple *args, Dict *) { return stropForward(args, method, 1); }
STROP(stropLower, "lower")
STROP(stropUpper, "upper")
STROP(stropSwapcase, "swapcase")
STROP(stropCapitalize, "capitalize")
STROP(stropStrip, "strip")
STROP(stropLstrip, "lstrip")
STROP(stropRstrip, "rstrip")
STROP(stropSplit, "split")
STROP(stropFind, "find")
STROP(stropRfind, "rfind")
STROP(stropCount, "count")
STROP(stropReplace, "replace")
STROP(stropTranslate, "translate")
STROP(stropExpandtabs, "expandtabs")

static ObjRef stropJoin(Object *, Tuple *args, Dict *) {
	if (!checkArgs("join", args, 1, 2))
		return ObjRef();
	Ref<Str> sep = args->_size > 1 ? Ref<Str>(asStr(args->get(1))) : newStr(" ");
	if (!sep)
		return ObjRef();
	return strJoin(sep.get(), args->get(0));
}

static ObjRef stropAtoi(Object *, Tuple *args, Dict *) {
	if (!checkArgs("atoi", args, 1, 2))
		return ObjRef();
	Str *s = asStr(args->get(0));
	int32 base = 10;
	if (!s || (args->_size > 1 && !asInt(args->get(1), &base)))
		return ObjRef();
	return intFromString(s->_data, base, true);
}

static ObjRef stropAtol(Object *, Tuple *args, Dict *) {
	if (!checkArgs("atol", args, 1, 2))
		return ObjRef();
	Str *s = asStr(args->get(0));
	int32 base = 10;
	if (!s || (args->_size > 1 && !asInt(args->get(1), &base)))
		return ObjRef();
	return longFromString(s->_data, base, true);
}

static ObjRef stropAtof(Object *, Tuple *args, Dict *) {
	if (!checkArgs("atof", args, 1, 1))
		return ObjRef();
	Str *s = asStr(args->get(0));
	if (!s)
		return ObjRef();
	return floatFromString(s);
}

static const MethodDef g_stropMethods[] = {
	{ "maketrans", stropMaketrans, kMethVarArgs, nullptr },
	{ "lower", stropLower, kMethVarArgs, nullptr }, { "upper", stropUpper, kMethVarArgs, nullptr },
	{ "swapcase", stropSwapcase, kMethVarArgs, nullptr }, { "capitalize", stropCapitalize, kMethVarArgs, nullptr },
	{ "strip", stropStrip, kMethVarArgs, nullptr }, { "lstrip", stropLstrip, kMethVarArgs, nullptr },
	{ "rstrip", stropRstrip, kMethVarArgs, nullptr }, { "split", stropSplit, kMethVarArgs, nullptr },
	{ "splitfields", stropSplit, kMethVarArgs, nullptr }, { "find", stropFind, kMethVarArgs, nullptr },
	{ "rfind", stropRfind, kMethVarArgs, nullptr }, { "count", stropCount, kMethVarArgs, nullptr },
	{ "replace", stropReplace, kMethVarArgs, nullptr }, { "translate", stropTranslate, kMethVarArgs, nullptr },
	{ "expandtabs", stropExpandtabs, kMethVarArgs, nullptr }, { "join", stropJoin, kMethVarArgs, nullptr },
	{ "joinfields", stropJoin, kMethVarArgs, nullptr }, { "atoi", stropAtoi, kMethVarArgs, nullptr },
	{ "atol", stropAtol, kMethVarArgs, nullptr }, { "atof", stropAtof, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void initStrop(Module *m) {
	for (const MethodDef *d = g_stropMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	// The C locale's character classes
	moduleAddString(m, "whitespace", "\t\n\x0b\x0c\r ");
	moduleAddString(m, "lowercase", "abcdefghijklmnopqrstuvwxyz");
	moduleAddString(m, "uppercase", "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

// ---- gc ----------------------------------------------------------------------------

static int32 g_gcDebug = 0;
static bool g_gcEnabled = true;
static int32 g_gcThreshold[3] = { 700, 10, 10 };

static ObjRef gcCollect(Object *, Tuple *, Dict *) { return newInt(0); }
static ObjRef gcEnable(Object *, Tuple *, Dict *) { g_gcEnabled = true; return ObjRef(g_none); }
static ObjRef gcDisable(Object *, Tuple *, Dict *) { g_gcEnabled = false; return ObjRef(g_none); }
static ObjRef gcIsEnabled(Object *, Tuple *, Dict *) { return newBool(g_gcEnabled); }
static ObjRef gcGetDebug(Object *, Tuple *, Dict *) { return newInt(g_gcDebug); }

static ObjRef gcSetDebug(Object *, Tuple *args, Dict *) {
	if (!checkArgs("set_debug", args, 1, 1) || !asInt(args->get(0), &g_gcDebug))
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef gcGetThreshold(Object *, Tuple *, Dict *) {
	return tuplePack(3, newInt(g_gcThreshold[0]).get(), newInt(g_gcThreshold[1]).get(), newInt(g_gcThreshold[2]).get());
}

static ObjRef gcSetThreshold(Object *, Tuple *args, Dict *) {
	for (uint32 i = 0; i < args->_size && i < 3; i++)
		if (!asInt(args->get(i), &g_gcThreshold[i]))
			return ObjRef();
	return ObjRef(g_none);
}

static ObjRef gcGetReferrers(Object *, Tuple *, Dict *) {
	return newList();
}

static const MethodDef g_gcMethods[] = {
	{ "collect", gcCollect, kMethVarArgs, nullptr }, { "enable", gcEnable, kMethVarArgs, nullptr },
	{ "disable", gcDisable, kMethVarArgs, nullptr }, { "isenabled", gcIsEnabled, kMethVarArgs, nullptr },
	{ "get_debug", gcGetDebug, kMethVarArgs, nullptr }, { "set_debug", gcSetDebug, kMethVarArgs, nullptr },
	{ "get_threshold", gcGetThreshold, kMethVarArgs, nullptr }, { "set_threshold", gcSetThreshold, kMethVarArgs, nullptr },
	{ "get_referrers", gcGetReferrers, kMethVarArgs, nullptr }, { "get_objects", gcGetReferrers, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void initGc(Module *m) {
	for (const MethodDef *d = g_gcMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	moduleAddObject(m, "garbage", newList().get());
	moduleAddInt(m, "DEBUG_STATS", 1);
	moduleAddInt(m, "DEBUG_COLLECTABLE", 2);
	moduleAddInt(m, "DEBUG_UNCOLLECTABLE", 4);
	moduleAddInt(m, "DEBUG_INSTANCES", 8);
	moduleAddInt(m, "DEBUG_OBJECTS", 16);
	moduleAddInt(m, "DEBUG_SAVEALL", 32);
	moduleAddInt(m, "DEBUG_LEAK", 2 | 4 | 8 | 16 | 32);
}

// ---- thread (one thread only) ------------------------------------------------------------

struct Lock : public Object {
	bool _locked;
	explicit Lock(Type *t) : Object(t), _locked(false) {}
};

static Type g_lockType("lock", &g_objectType);

static ObjRef lockAcquire(Object *self, Tuple *args, Dict *) {
	Lock *l = (Lock *)self;
	int32 wait = 1;
	if (args->_size && !asInt(args->get(0), &wait))
		return ObjRef();
	if (l->_locked) {
		if (!wait)
			return newBool(false);
		// Waiting would deadlock with a single thread
		setErrorString(g_excRuntimeError, "deadlock: lock is already held");
		return ObjRef();
	}
	l->_locked = true;
	return args->_size ? newBool(true) : ObjRef(g_none);
}

static ObjRef lockRelease(Object *self, Tuple *, Dict *) {
	Lock *l = (Lock *)self;
	if (!l->_locked) {
		setErrorString(g_excRuntimeError, "release unlocked lock");
		return ObjRef();
	}
	l->_locked = false;
	return ObjRef(g_none);
}

static ObjRef lockLocked(Object *self, Tuple *, Dict *) {
	return newBool(((Lock *)self)->_locked);
}

static const MethodDef g_lockMethods[] = {
	{ "acquire", lockAcquire, kMethVarArgs, nullptr }, { "acquire_lock", lockAcquire, kMethVarArgs, nullptr },
	{ "release", lockRelease, kMethVarArgs, nullptr }, { "release_lock", lockRelease, kMethVarArgs, nullptr },
	{ "locked", lockLocked, kMethVarArgs, nullptr }, { "locked_lock", lockLocked, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef threadAllocateLock(Object *, Tuple *, Dict *) {
	return ObjRef(new Lock(&g_lockType));
}

static ObjRef threadGetIdent(Object *, Tuple *, Dict *) {
	return newInt(1);
}

static ObjRef threadStartNew(Object *, Tuple *, Dict *) {
	setErrorString(g_excRuntimeError, "can't start new thread");
	return ObjRef();
}

static const MethodDef g_threadMethods[] = {
	{ "allocate_lock", threadAllocateLock, kMethVarArgs, nullptr }, { "allocate", threadAllocateLock, kMethVarArgs, nullptr },
	{ "get_ident", threadGetIdent, kMethVarArgs, nullptr }, { "start_new_thread", threadStartNew, kMethVarArgs, nullptr },
	{ "start_new", threadStartNew, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void initThread(Module *m) {
	g_lockType.methods = g_lockMethods;
	g_lockType.getAttr = genericGetAttr;
	typeReady(&g_lockType);
	for (const MethodDef *d = g_threadMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	moduleAddObject(m, "LockType", &g_lockType);
	moduleAddObject(m, "error", g_excRuntimeError);
}

// ---- _codecs -------------------------------------------------------------------------

static ObjRef codecsRegister(Object *, Tuple *args, Dict *) {
	if (!checkArgs("register", args, 1, 1))
		return ObjRef();
	if (!callable(args->get(0))) {
		setErrorString(g_excTypeError, "argument must be callable");
		return ObjRef();
	}
	if (!g_ts->codecSearchPath) {
		Ref<Dict> d = newDict();
		g_ts->codecSearchPath = d;
	}
	ObjRef path(g_ts->codecSearchPath->getItemString("path"));
	if (!path) {
		path = newList();
		g_ts->codecSearchPath->setItemString("path", path.get());
	}
	((List *)path.get())->append(args->get(0));
	return ObjRef(g_none);
}

static ObjRef codecsLookup(Object *, Tuple *args, Dict *) {
	if (!checkArgs("lookup", args, 1, 1))
		return ObjRef();
	Str *enc = asStr(args->get(0));
	if (!enc)
		return ObjRef();
	Common::String norm = enc->toString();
	norm.toLowercase();
	for (uint i = 0; i < norm.size(); i++)
		if (norm[i] == ' ')
			norm.setChar('_', i);
	if (!g_ts->codecSearchPath)
		g_ts->codecSearchPath = newDict();
	Object *cache = g_ts->codecSearchPath->getItemString("cache");
	if (!cache) {
		g_ts->codecSearchPath->setItemString("cache", newDict().get());
		cache = g_ts->codecSearchPath->getItemString("cache");
	}
	Ref<Str> key = newStr(norm);
	Object *hit = ((Dict *)cache)->getItem(key.get());
	if (hit)
		return ObjRef(hit);
	if (!g_ts->codecSearchPath->getItemString("path")) {
		// Importing encodings registers its search function
		ObjRef m = importModule("encodings");
		if (!m)
			clearError();
	}
	Object *path = g_ts->codecSearchPath->getItemString("path");
	if (path) {
		List *l = (List *)path;
		for (uint32 i = 0; i < l->size(); i++) {
			ObjRef r = callFunction(l->get(i), 1, key.get());
			if (!r)
				return ObjRef();
			if (r.get() == g_none)
				continue;
			((Dict *)cache)->setItem(key.get(), r.get());
			return r;
		}
	}
	if (!path || ((List *)path)->size() == 0)
		setErrorString(g_excLookupError, "no codec search functions registered: can't find encoding");
	else
		setErrorFormat(g_excLookupError, "unknown encoding: %s", enc->_data);

	return ObjRef();

}

static ObjRef codecDecode(Tuple *args, const char *encoding) {
	if (!checkArgs("decode", args, 1, 2))
		return ObjRef();
	const char *errors = nullptr;
	if (args->_size > 1 && args->get(1) != g_none) {
		Str *e = asStr(args->get(1));
		if (!e)
			return ObjRef();
		errors = e->_data;
	}
	ObjRef u = unicodeFromStr(args->get(0), encoding, errors);
	if (!u)
		return ObjRef();
	int32 n = length(args->get(0));
	return tuplePack(2, u.get(), newInt(n).get());
}

static ObjRef codecEncode(Tuple *args, const char *encoding) {
	if (!checkArgs("encode", args, 1, 2))
		return ObjRef();
	ObjRef u = unicodeFromStr(args->get(0), nullptr, nullptr);
	if (!u)
		return ObjRef();
	Ref<Tuple> a = args->_size > 1 && args->get(1) != g_none ? tuplePack(2, newStr(encoding).get(), args->get(1)) : tuplePack(1, newStr(encoding).get());
	ObjRef meth = getAttrString(u.get(), "encode");
	if (!meth)
		return ObjRef();
	ObjRef s = call(meth.get(), a.get(), nullptr);
	if (!s)
		return ObjRef();
	return tuplePack(2, s.get(), newInt(length(u.get())).get());
}

#define CODEC(dec, enc, name) \
	static ObjRef dec(Object *, Tuple *args, Dict *) { return codecDecode(args, name); } \
	static ObjRef enc(Object *, Tuple *args, Dict *) { return codecEncode(args, name); }
CODEC(codecAsciiDecode, codecAsciiEncode, "ascii")
CODEC(codecLatin1Decode, codecLatin1Encode, "latin-1")
CODEC(codecUtf8Decode, codecUtf8Encode, "utf-8")
CODEC(codecMbcsDecode, codecMbcsEncode, "mbcs")

// UTF-16: byteorder -1 little-endian, 1 big-endian, 0 native (little) with a BOM
static ObjRef utf16Encode(Tuple *args, int defaultOrder) {
	if (!checkArgs("utf_16_encode", args, 1, 3))
		return ObjRef();
	ObjRef u = unicodeFromStr(args->get(0), nullptr, nullptr);
	if (!u)
		return ObjRef();
	int32 order = defaultOrder;
	if (args->_size > 2 && !asInt(args->get(2), &order))
		return ObjRef();
	Common::String out;
	bool big = order > 0;
	if (order == 0) {
		out += (char)0xff;
		out += (char)0xfe;
	}
	const Common::Array<uint32> &chars = ((Unicode *)u.get())->_chars;
	for (uint32 i = 0; i < chars.size(); i++) {
		uint32 c = chars[i];
		char hi = (char)((c >> 8) & 0xff), lo = (char)(c & 0xff);
		out += big ? hi : lo;
		out += big ? lo : hi;
	}
	return tuplePack(2, newStr(out).get(), newInt(chars.size()).get());
}

static ObjRef utf16Decode(Tuple *args, int defaultOrder) {
	if (!checkArgs("utf_16_decode", args, 1, 3))
		return ObjRef();
	Str *s = asStr(args->get(0));
	if (!s)
		return ObjRef();
	int32 order = defaultOrder;
	if (args->_size > 2 && !asInt(args->get(2), &order))
		return ObjRef();
	const uint8 *p = (const uint8 *)s->_data;
	uint32 i = 0;
	bool big = order > 0;
	if (order == 0 && s->_len >= 2) {
		if (p[0] == 0xff && p[1] == 0xfe) {
			big = false;
			i = 2;
		} else if (p[0] == 0xfe && p[1] == 0xff) {
			big = true;
			i = 2;
		}
	}
	if ((s->_len - i) % 2) {
		setErrorString(g_excUnicodeError, "UTF-16 decoding error: truncated data");
		return ObjRef();
	}
	Unicode *u = new Unicode();
	ObjRef ru(u);
	for (; i + 1 < s->_len; i += 2)
		u->_chars.push_back(big ? (p[i] << 8) | p[i + 1] : p[i] | (p[i + 1] << 8));
	return tuplePack(2, ru.get(), newInt(s->_len).get());
}

static ObjRef codecUtf16Encode(Object *, Tuple *a, Dict *) { return utf16Encode(a, 0); }
static ObjRef codecUtf16LeEncode(Object *, Tuple *a, Dict *) { return utf16Encode(a, -1); }
static ObjRef codecUtf16BeEncode(Object *, Tuple *a, Dict *) { return utf16Encode(a, 1); }
static ObjRef codecUtf16Decode(Object *, Tuple *a, Dict *) { return utf16Decode(a, 0); }
static ObjRef codecUtf16LeDecode(Object *, Tuple *a, Dict *) { return utf16Decode(a, -1); }
static ObjRef codecUtf16BeDecode(Object *, Tuple *a, Dict *) { return utf16Decode(a, 1); }

static ObjRef codecCharmapDecode(Object *, Tuple *args, Dict *) {
	if (!checkArgs("charmap_decode", args, 1, 3))
		return ObjRef();
	Str *s = asStr(args->get(0));
	if (!s)
		return ObjRef();
	Object *mapping = args->_size > 2 ? args->get(2) : g_none;
	Unicode *u = new Unicode();
	ObjRef ru(u);
	for (uint32 i = 0; i < s->_len; i++) {
		uint8 c = (uint8)s->_data[i];
		if (mapping == g_none) {
			u->_chars.push_back(c);
			continue;
		}
		ObjRef key = newInt(c);
		ObjRef v = getItem(mapping, key.get());
		if (!v) {
			if (!errorMatches(g_excLookupError))
				return ObjRef();
			clearError();
			u->_chars.push_back(c);
			continue;
		}
		if (v.get() == g_none) {
			setErrorString(g_excUnicodeError, "charmap decoding error: character maps to <undefined>");
			return ObjRef();
		}
		if (isInstanceOf(v.get(), &g_intType))
			u->_chars.push_back(((Int *)v.get())->_value);
		else if (isInstanceOf(v.get(), &g_unicodeType))
			for (uint32 k = 0; k < ((Unicode *)v.get())->_chars.size(); k++)
				u->_chars.push_back(((Unicode *)v.get())->_chars[k]);
	}
	return tuplePack(2, ru.get(), newInt(s->_len).get());
}

static ObjRef codecCharmapEncode(Object *, Tuple *args, Dict *) {
	if (!checkArgs("charmap_encode", args, 1, 3))
		return ObjRef();
	ObjRef u = unicodeFromStr(args->get(0), nullptr, nullptr);
	if (!u)
		return ObjRef();
	Object *mapping = args->_size > 2 ? args->get(2) : g_none;
	Common::String out;
	const Common::Array<uint32> &chars = ((Unicode *)u.get())->_chars;
	for (uint32 i = 0; i < chars.size(); i++) {
		if (mapping == g_none) {
			out += (char)chars[i];
			continue;
		}
		ObjRef key = newInt(chars[i]);
		ObjRef v = getItem(mapping, key.get());
		if (!v || v.get() == g_none) {
			clearError();
			setErrorString(g_excUnicodeError, "charmap encoding error: character maps to <undefined>");
			return ObjRef();
		}
		if (isInstanceOf(v.get(), &g_intType))
			out += (char)((Int *)v.get())->_value;
		else if (isInstanceOf(v.get(), &g_strType))
			out += ((Str *)v.get())->toString();
	}
	return tuplePack(2, newStr(out).get(), newInt(chars.size()).get());
}

static const MethodDef g_codecsMethods[] = {
	{ "register", codecsRegister, kMethVarArgs, nullptr }, { "lookup", codecsLookup, kMethVarArgs, nullptr },
	{ "ascii_decode", codecAsciiDecode, kMethVarArgs, nullptr }, { "ascii_encode", codecAsciiEncode, kMethVarArgs, nullptr },
	{ "latin_1_decode", codecLatin1Decode, kMethVarArgs, nullptr }, { "latin_1_encode", codecLatin1Encode, kMethVarArgs, nullptr },
	{ "utf_8_decode", codecUtf8Decode, kMethVarArgs, nullptr }, { "utf_8_encode", codecUtf8Encode, kMethVarArgs, nullptr },
	{ "mbcs_decode", codecMbcsDecode, kMethVarArgs, nullptr }, { "mbcs_encode", codecMbcsEncode, kMethVarArgs, nullptr },
	{ "charmap_decode", codecCharmapDecode, kMethVarArgs, nullptr }, { "charmap_encode", codecCharmapEncode, kMethVarArgs, nullptr },
	{ "utf_16_encode", codecUtf16Encode, kMethVarArgs, nullptr }, { "utf_16_decode", codecUtf16Decode, kMethVarArgs, nullptr },
	{ "utf_16_le_encode", codecUtf16LeEncode, kMethVarArgs, nullptr }, { "utf_16_le_decode", codecUtf16LeDecode, kMethVarArgs, nullptr },
	{ "utf_16_be_encode", codecUtf16BeEncode, kMethVarArgs, nullptr }, { "utf_16_be_decode", codecUtf16BeDecode, kMethVarArgs, nullptr },

	{ nullptr, nullptr, 0, nullptr }
};

static void initCodecs(Module *m) {
	for (const MethodDef *d = g_codecsMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
}

// ---- imageop -------------------------------------------------------------------------

static Object *g_imageopError = nullptr;

static bool imageopCheck(Str *image, int32 size, int32 x, int32 y) {
	if (size != 1 && size != 2 && size != 4) {
		setErrorString(g_imageopError, "Size should be 1, 2 or 4");
		return false;
	}
	if ((int64)image->_len != (int64)size * x * y) {
		setErrorString(g_imageopError, "String has incorrect length");
		return false;
	}
	return true;
}

// Nearest-neighbour scaling, as imageop.scale
static ObjRef imageopScale(Object *, Tuple *args, Dict *) {
	if (!checkArgs("scale", args, 6, 6))
		return ObjRef();
	Str *image = asStr(args->get(0));
	int32 size, x, y, newx, newy;
	if (!image || !asInt(args->get(1), &size) || !asInt(args->get(2), &x) || !asInt(args->get(3), &y) ||
	    !asInt(args->get(4), &newx) || !asInt(args->get(5), &newy))
		return ObjRef();
	if (!imageopCheck(image, size, x, y))
		return ObjRef();
	if (newx < 0 || newy < 0) {
		setErrorString(g_excSystemError, "Negative size passed to PyString_FromStringAndSize");
		return ObjRef();
	}
	Ref<Str> r(new Str((uint32)(newx * newy * size)));
	char *out = r->_data;
	for (int32 iy = 0; iy < newy; iy++) {
		for (int32 ix = 0; ix < newx; ix++) {
			int32 oix = ix * x / newx;
			int32 oiy = iy * y / newy;
			memcpy(out, image->_data + (oiy * x + oix) * size, size);
			out += size;
		}
	}
	return r;
}

// Cropping, with pixels outside the image set to 0, as imageop.crop
static ObjRef imageopCrop(Object *, Tuple *args, Dict *) {
	if (!checkArgs("crop", args, 8, 8))
		return ObjRef();
	Str *image = asStr(args->get(0));
	int32 size, x, y, x1, y1, x2, y2;
	if (!image || !asInt(args->get(1), &size) || !asInt(args->get(2), &x) || !asInt(args->get(3), &y) ||
	    !asInt(args->get(4), &x1) || !asInt(args->get(5), &y1) || !asInt(args->get(6), &x2) || !asInt(args->get(7), &y2))
		return ObjRef();
	if (!imageopCheck(image, size, x, y))
		return ObjRef();
	int32 xstep = x1 < x2 ? 1 : -1;
	int32 ystep = y1 < y2 ? 1 : -1;
	int32 nlen = (ABS(x2 - x1) + 1) * (ABS(y2 - y1) + 1) * size;
	Ref<Str> r(new Str((uint32)nlen));
	char *out = r->_data;
	for (int32 iy = y1; iy != y2 + ystep; iy += ystep) {
		for (int32 ix = x1; ix != x2 + xstep; ix += xstep) {
			if (iy < 0 || ix < 0 || iy >= y || ix >= x)
				memset(out, 0, size);
			else
				memcpy(out, image->_data + (iy * x + ix) * size, size);
			out += size;
		}
	}
	return r;
}

static const MethodDef g_imageopMethods[] = {
	{ "scale", imageopScale, kMethVarArgs, nullptr },
	{ "crop", imageopCrop, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void initImageop(Module *m) {
	ObjRef error = newExceptionClass("imageop.error", nullptr);
	if (g_imageopError)
		decRef(g_imageopError);
	g_imageopError = error.get();
	incRef(g_imageopError);
	moduleAddObject(m, "error", error.get());
	for (const MethodDef *d = g_imageopMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
}

static void initMarshalNative(Module *) {
	initMarshalModule();
}

static void initImpNative(Module *m) {
	initImp(m);
}

void registerZlibModule(Interpreter *vm);

void initStandardModules(Interpreter *vm) {
	registerZlibModule(vm);
	vm->addNativeModule("imageop", initImageop, false);


	vm->addNativeModule("imp", initImpNative, false);
	vm->addNativeModule("marshal", initMarshalNative, false);
	vm->addNativeModule("nt", initNt, false);
	vm->addNativeModule("errno", initErrno, false);
	vm->addNativeModule("time", initTime, false);
	vm->addNativeModule("math", initMath, false);
	vm->addNativeModule("strop", initStrop, false);
	vm->addNativeModule("gc", initGc, false);
	vm->addNativeModule("thread", initThread, false);
	vm->addNativeModule("_codecs", initCodecs, false);
	vm->addNativeModule("struct", initStructModule, false);
	vm->addNativeModule("_sre", initSreModule, true);
	vm->addNativeModule("cPickle", initCPickleModule, false);
	vm->addNativeModule("cStringIO", initCStringIOModule, false);

	g_statResultType.getSets = g_statGetSets;
	g_statResultType.getAttr = genericGetAttr;
	typeReady(&g_statResultType);
}

} // End of namespace Python
} // End of namespace Yaga
