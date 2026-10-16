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

// The error state and the exceptions module (CPython 2.2's errors.c and
// exceptions.c). Exceptions are classic classes, as they were in 2.2.

namespace Yaga {
namespace Python {

Object *g_excException, *g_excStandardError, *g_excArithmeticError;
Object *g_excLookupError, *g_excAssertionError, *g_excAttributeError;
Object *g_excEOFError, *g_excFloatingPointError, *g_excEnvironmentError;
Object *g_excIOError, *g_excOSError, *g_excWindowsError, *g_excImportError;
Object *g_excIndexError, *g_excKeyError, *g_excKeyboardInterrupt;
Object *g_excMemoryError, *g_excNameError, *g_excOverflowError;
Object *g_excRuntimeError, *g_excNotImplementedError, *g_excSyntaxError;
Object *g_excIndentationError, *g_excTabError, *g_excReferenceError;
Object *g_excSystemError, *g_excSystemExit, *g_excTypeError;
Object *g_excUnboundLocalError, *g_excUnicodeError, *g_excValueError;
Object *g_excZeroDivisionError, *g_excStopIteration;
Object *g_excWarning, *g_excUserWarning, *g_excDeprecationWarning;
Object *g_excSyntaxWarning, *g_excOverflowWarning, *g_excRuntimeWarning;

// ---- Error state -------------------------------------------------------------------

void restoreError(Object *type, Object *value, Object *tb) {
	g_ts->curExcType = type;
	g_ts->curExcValue = value;
	g_ts->curExcTraceback = tb;
}

void setError(Object *type, Object *value) {
	restoreError(type, value, nullptr);
}

void setErrorString(Object *type, const char *msg) {
	Ref<Str> s = newStr(msg);
	setError(type, s.get());
}

void setErrorFormat(Object *type, const char *fmt, ...) {
	va_list va;
	va_start(va, fmt);
	Common::String s = Common::String::vformat(fmt, va);
	va_end(va);
	Ref<Str> str = newStr(s);
	setError(type, str.get());
}

bool errorOccurred() {
	return g_ts && g_ts->curExcType;
}

Object *errorType() {
	return g_ts->curExcType.get();
}

bool givenExceptionMatches(Object *err, Object *exc) {
	if (!err || !exc)
		return false;
	if (isInstanceOf(exc, &g_tupleType)) {
		Tuple *t = (Tuple *)exc;
		for (uint32 i = 0; i < t->_size; i++)
			if (givenExceptionMatches(err, t->get(i)))
				return true;
		return false;
	}
	if (isType(err, &g_instanceType))
		err = ((Instance *)err)->_class.get();
	if (isType(err, &g_classType) && isType(exc, &g_classType))
		return classIsSubclass(err, exc);
	return err == exc;
}

bool errorMatches(Object *exc) {
	return givenExceptionMatches(errorType(), exc);
}

void fetchError(ObjRef *type, ObjRef *value, ObjRef *tb) {
	*type = g_ts->curExcType;
	*value = g_ts->curExcValue;
	*tb = g_ts->curExcTraceback;
	g_ts->curExcType.reset();
	g_ts->curExcValue.reset();
	g_ts->curExcTraceback.reset();
}

void clearError() {
	g_ts->curExcType.reset();
	g_ts->curExcValue.reset();
	g_ts->curExcTraceback.reset();
}

void normalizeException(ObjRef *exc, ObjRef *val, ObjRef *tb) {
	ObjRef type = *exc;
	ObjRef value = *val;
	if (!type) {
		setErrorString(g_excSystemError, "PyErr_NormalizeException() called without exception");
		return;
	}
	if (!value)
		value = g_none;
	Object *inclass = nullptr;
	if (isType(value.get(), &g_instanceType))
		inclass = ((Instance *)value.get())->_class.get();
	if (isType(type.get(), &g_classType)) {
		if (!inclass || !classIsSubclass(inclass, type.get())) {
			Ref<Tuple> args;
			if (value.get() == g_none)
				args = newTuple(0);
			else if (isInstanceOf(value.get(), &g_tupleType))
				args = (Tuple *)value.get();
			else
				args = tuplePack(1, value.get());
			ObjRef res = call(type.get(), args.get(), nullptr);
			if (!res) {
				// Normalising raised a new exception: report that one instead
				ObjRef t2, v2, tb2;
				fetchError(&t2, &v2, &tb2);
				if (!tb2)
					tb2 = *tb;
				*exc = t2;
				*val = v2;
				*tb = tb2;
				normalizeException(exc, val, tb);
				return;
			}
			value = res;
		} else if (inclass != type.get()) {
			type = inclass;
		}
	}
	*exc = type;
	*val = value;
}

void setNoMemory() {
	setError(g_excMemoryError, g_none);
}

void badArgument() {
	setErrorString(g_excTypeError, "bad argument type for built-in operation");
}

void badInternalCall() {
	setErrorString(g_excSystemError, "bad argument to internal function");
}

// As PyErr_NewException: a classic class "module.Name", based on Exception
ObjRef newExceptionClass(const char *dottedName, Object *base) {
	const char *dot = strrchr(dottedName, '.');
	if (!dot) {
		badInternalCall();
		return ObjRef();
	}
	Ref<Dict> dict = newDict();
	dict->setItemString("__module__", newStr(dottedName, dot - dottedName).get());
	Ref<Tuple> bases = tuplePack(1, base ? base : g_excException);
	return newClassObj(bases.get(), dict.get(), newStr(dot + 1).get());
}

ObjRef newException(Object *type, const char *msg) {

	Ref<Str> s = newStr(msg);
	return callFunction(type, 1, s.get());
}

static Common::String safeStr(Object *o) {
	ObjRef s = str(o);
	if (!s) {
		clearError();
		return "<unprintable object>";
	}
	return ((Str *)s.get())->toString();
}

Common::String formatError() {
	ObjRef type, value, tb;
	fetchError(&type, &value, &tb);
	if (!type)
		return "";
	normalizeException(&type, &value, &tb);
	clearError();
	Common::String out = "Traceback (most recent call last):\n";
	for (Traceback *t = tb && isType(tb.get(), &g_tracebackType) ? (Traceback *)tb.get() : nullptr; t; t = t->_next.get()) {
		Code *co = t->_frame->_code.get();
		out += Common::String::format("  File \"%s\", line %d, in %s\n",
		                              co->_filename ? co->_filename->_data : "?", t->_lineno, co->_name ? co->_name->_data : "?");
	}
	Common::String name;
	if (isType(type.get(), &g_classType)) {
		ClassObj *c = (ClassObj *)type.get();
		Object *mod = c->_dict->getItemString("__module__");
		name = c->_name->toString();
		if (mod && isInstanceOf(mod, &g_strType) && !((Str *)mod)->equals("exceptions"))
			name = ((Str *)mod)->toString() + "." + name;
	} else {
		name = safeStr(type.get());
	}
	out += name;
	if (value && value.get() != g_none) {
		Common::String v = safeStr(value.get());
		if (!v.empty())
			out += ": " + v;
	}
	return out;
}

// ---- The exceptions module ----------------------------------------------------------

static ObjRef getArgs(Object *self) {
	return getAttrString(self, "args");
}

static ObjRef excInit(Object *, Tuple *args, Dict *) {
	if (args->_size < 1) {
		setErrorString(g_excTypeError, "unbound method must be called with instance as first argument");
		return ObjRef();
	}
	Object *self = args->get(0);
	Ref<Tuple> rest = newTuple(args->_size - 1);
	for (uint32 i = 1; i < args->_size; i++)
		rest->set(i - 1, args->get(i));
	if (setAttrString(self, "args", rest.get()) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef excStr(Object *, Tuple *args, Dict *) {
	if (!checkArgs("__str__", args, 1, 1))
		return ObjRef();
	ObjRef a = getArgs(args->get(0));
	if (!a)
		return ObjRef();
	int32 n = length(a.get());
	if (n < 0)
		return ObjRef();
	if (n == 0)
		return newStr("");
	if (n == 1) {
		ObjRef item = sequenceGetItem(a.get(), 0);
		if (!item)
			return ObjRef();
		return str(item.get());
	}
	return str(a.get());
}

static ObjRef excGetItem(Object *, Tuple *args, Dict *) {
	if (!checkArgs("__getitem__", args, 2, 2))
		return ObjRef();
	ObjRef a = getArgs(args->get(0));
	if (!a)
		return ObjRef();
	return getItem(a.get(), args->get(1));
}

static const MethodDef g_exceptionMethods[] = {
	{ "__getitem__", excGetItem, kMethVarArgs, nullptr },
	{ "__str__", excStr, kMethVarArgs, nullptr },
	{ "__init__", excInit, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef envInit(Object *, Tuple *args, Dict *) {
	if (args->_size < 1) {
		setErrorString(g_excTypeError, "unbound method must be called with instance as first argument");
		return ObjRef();
	}
	Object *self = args->get(0);
	Ref<Tuple> rest = newTuple(args->_size - 1);
	for (uint32 i = 1; i < args->_size; i++)
		rest->set(i - 1, args->get(i));
	if (setAttrString(self, "args", rest.get()) < 0 ||
	    setAttrString(self, "errno", g_none) < 0 ||
	    setAttrString(self, "strerror", g_none) < 0 ||
	    setAttrString(self, "filename", g_none) < 0)
		return ObjRef();
	if (rest->_size == 3) {
		// open() errors give a third argument, the filename
		if (setAttrString(self, "errno", rest->get(0)) < 0 ||
		    setAttrString(self, "strerror", rest->get(1)) < 0 ||
		    setAttrString(self, "filename", rest->get(2)) < 0)
			return ObjRef();
		Ref<Tuple> two = tuplePack(2, rest->get(0), rest->get(1));
		if (setAttrString(self, "args", two.get()) < 0)
			return ObjRef();
	} else if (rest->_size == 2) {
		if (setAttrString(self, "errno", rest->get(0)) < 0 ||
		    setAttrString(self, "strerror", rest->get(1)) < 0)
			return ObjRef();
	}
	return ObjRef(g_none);
}

static ObjRef envStr(Object *, Tuple *args, Dict *) {
	if (!checkArgs("__str__", args, 1, 1))
		return ObjRef();
	Object *self = args->get(0);
	ObjRef filename = getAttrString(self, "filename");
	ObjRef serrno = getAttrString(self, "errno");
	ObjRef strerror = getAttrString(self, "strerror");
	if (!filename || !serrno || !strerror)
		return ObjRef();
	if (filename.get() != g_none) {
		ObjRef fr = repr(filename.get());
		ObjRef es = str(serrno.get());
		ObjRef ss = str(strerror.get());
		if (!fr || !es || !ss)
			return ObjRef();
		return newStr(Common::String::format("[Errno %s] %s: %s", ((Str *)es.get())->_data, ((Str *)ss.get())->_data, ((Str *)fr.get())->_data));
	}
	if (isTrue(serrno.get()) > 0 && isTrue(strerror.get()) > 0) {
		ObjRef es = str(serrno.get());
		ObjRef ss = str(strerror.get());
		if (!es || !ss)
			return ObjRef();
		return newStr(Common::String::format("[Errno %s] %s", ((Str *)es.get())->_data, ((Str *)ss.get())->_data));
	}
	return excStr(nullptr, args, nullptr);
}

static const MethodDef g_environmentErrorMethods[] = {
	{ "__init__", envInit, kMethVarArgs, nullptr },
	{ "__str__", envStr, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef syntaxInit(Object *, Tuple *args, Dict *) {
	if (args->_size < 1) {
		setErrorString(g_excTypeError, "unbound method must be called with instance as first argument");
		return ObjRef();
	}
	Object *self = args->get(0);
	Ref<Tuple> rest = newTuple(args->_size - 1);
	for (uint32 i = 1; i < args->_size; i++)
		rest->set(i - 1, args->get(i));
	if (setAttrString(self, "args", rest.get()) < 0)
		return ObjRef();
	static const char *const fields[] = { "msg", "filename", "lineno", "offset", "text" };
	for (int i = 0; i < 5; i++)
		if (setAttrString(self, fields[i], g_none) < 0)
			return ObjRef();
	Ref<Int> zero = newInt(0);
	setAttrString(self, "print_file_and_line", g_none);
	if (rest->_size >= 1)
		setAttrString(self, "msg", rest->get(0));
	if (rest->_size == 2) {
		ObjRef info = sequenceTuple(rest->get(1));
		if (!info)
			return ObjRef();
		Tuple *t = (Tuple *)info.get();
		if (t->_size != 4) {
			setErrorString(g_excIndexError, "tuple index out of range");
			return ObjRef();
		}
		for (int i = 0; i < 4; i++)
			if (setAttrString(self, fields[i + 1], t->get(i)) < 0)
				return ObjRef();
	}
	return ObjRef(g_none);
}

static ObjRef syntaxStr(Object *, Tuple *args, Dict *) {
	if (!checkArgs("__str__", args, 1, 1))
		return ObjRef();
	Object *self = args->get(0);
	ObjRef msg = getAttrString(self, "msg");
	if (!msg)
		return ObjRef();
	ObjRef s = str(msg.get());
	if (!s)
		return ObjRef();
	ObjRef filename = getAttrString(self, "filename");
	ObjRef lineno = getAttrString(self, "lineno");
	clearError();
	bool haveFilename = filename && isInstanceOf(filename.get(), &g_strType);
	bool haveLineno = lineno && isInstanceOf(lineno.get(), &g_intType);
	if (!haveFilename && !haveLineno)
		return s;
	Common::String fn;
	if (haveFilename) {
		// Only the file's base name
		Common::String full = ((Str *)filename.get())->toString();
		const char *p = full.c_str();
		const char *last = p;
		for (const char *q = p; *q; q++)
			if (*q == '/' || *q == '\\')
				last = q + 1;
		fn = last;
	}
	if (haveFilename && haveLineno)
		return newStr(Common::String::format("%s (%s, line %d)", ((Str *)s.get())->_data, fn.c_str(), ((Int *)lineno.get())->_value));
	if (haveFilename)
		return newStr(Common::String::format("%s (%s)", ((Str *)s.get())->_data, fn.c_str()));
	return newStr(Common::String::format("%s (line %d)", ((Str *)s.get())->_data, ((Int *)lineno.get())->_value));
}

static const MethodDef g_syntaxErrorMethods[] = {
	{ "__init__", syntaxInit, kMethVarArgs, nullptr },
	{ "__str__", syntaxStr, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef systemExitInit(Object *, Tuple *args, Dict *) {
	if (args->_size < 1) {
		setErrorString(g_excTypeError, "unbound method must be called with instance as first argument");
		return ObjRef();
	}
	Object *self = args->get(0);
	Ref<Tuple> rest = newTuple(args->_size - 1);
	for (uint32 i = 1; i < args->_size; i++)
		rest->set(i - 1, args->get(i));
	if (setAttrString(self, "args", rest.get()) < 0)
		return ObjRef();
	Object *code = rest->_size == 0 ? g_none : rest->_size == 1 ? rest->get(0) : (Object *)rest.get();
	if (setAttrString(self, "code", code) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static const MethodDef g_systemExitMethods[] = {
	{ "__init__", systemExitInit, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

struct ExcDef {
	const char *name;
	Object **exc;
	Object **base;
	const MethodDef *methods;
};

static const ExcDef g_excDefs[] = {
	{ "Exception", &g_excException, nullptr, g_exceptionMethods },
	{ "StopIteration", &g_excStopIteration, &g_excException, nullptr },
	{ "StandardError", &g_excStandardError, &g_excException, nullptr },
	{ "ArithmeticError", &g_excArithmeticError, &g_excStandardError, nullptr },
	{ "LookupError", &g_excLookupError, &g_excStandardError, nullptr },
	{ "AssertionError", &g_excAssertionError, &g_excStandardError, nullptr },
	{ "AttributeError", &g_excAttributeError, &g_excStandardError, nullptr },
	{ "EOFError", &g_excEOFError, &g_excStandardError, nullptr },
	{ "FloatingPointError", &g_excFloatingPointError, &g_excArithmeticError, nullptr },
	{ "EnvironmentError", &g_excEnvironmentError, &g_excStandardError, g_environmentErrorMethods },
	{ "IOError", &g_excIOError, &g_excEnvironmentError, nullptr },
	{ "OSError", &g_excOSError, &g_excEnvironmentError, nullptr },
	{ "WindowsError", &g_excWindowsError, &g_excOSError, nullptr },
	{ "ImportError", &g_excImportError, &g_excStandardError, nullptr },
	{ "IndexError", &g_excIndexError, &g_excLookupError, nullptr },
	{ "KeyError", &g_excKeyError, &g_excLookupError, nullptr },
	{ "KeyboardInterrupt", &g_excKeyboardInterrupt, &g_excStandardError, nullptr },
	{ "MemoryError", &g_excMemoryError, &g_excStandardError, nullptr },
	{ "NameError", &g_excNameError, &g_excStandardError, nullptr },
	{ "OverflowError", &g_excOverflowError, &g_excArithmeticError, nullptr },
	{ "RuntimeError", &g_excRuntimeError, &g_excStandardError, nullptr },
	{ "NotImplementedError", &g_excNotImplementedError, &g_excRuntimeError, nullptr },
	{ "SyntaxError", &g_excSyntaxError, &g_excStandardError, g_syntaxErrorMethods },
	{ "IndentationError", &g_excIndentationError, &g_excSyntaxError, nullptr },
	{ "TabError", &g_excTabError, &g_excIndentationError, nullptr },
	{ "ReferenceError", &g_excReferenceError, &g_excStandardError, nullptr },
	{ "SystemError", &g_excSystemError, &g_excStandardError, nullptr },
	{ "SystemExit", &g_excSystemExit, &g_excException, g_systemExitMethods },
	{ "TypeError", &g_excTypeError, &g_excStandardError, nullptr },
	{ "UnboundLocalError", &g_excUnboundLocalError, &g_excNameError, nullptr },
	{ "UnicodeError", &g_excUnicodeError, &g_excValueError, nullptr },
	{ "ValueError", &g_excValueError, &g_excStandardError, nullptr },
	{ "ZeroDivisionError", &g_excZeroDivisionError, &g_excArithmeticError, nullptr },
	{ "Warning", &g_excWarning, &g_excException, nullptr },
	{ "UserWarning", &g_excUserWarning, &g_excWarning, nullptr },
	{ "DeprecationWarning", &g_excDeprecationWarning, &g_excWarning, nullptr },
	{ "SyntaxWarning", &g_excSyntaxWarning, &g_excWarning, nullptr },
	{ "OverflowWarning", &g_excOverflowWarning, &g_excWarning, nullptr },
	{ "RuntimeWarning", &g_excRuntimeWarning, &g_excWarning, nullptr },
	{ nullptr, nullptr, nullptr, nullptr }
};

void initExceptions() {
	Ref<Module> mod = newModule("exceptions");
	getModuleDict()->setItemString("exceptions", mod.get());
	Ref<Str> modName = newStr("exceptions");
	// UnicodeError's base comes later in the table
	for (int pass = 0; pass < 2; pass++) {
		for (const ExcDef *d = g_excDefs; d->name; d++) {
			if (*d->exc)
				continue;
			if (d->base && !*d->base)
				continue;
			Ref<Dict> dict = newDict();
			dict->setItemString("__module__", modName.get());
			Ref<Tuple> bases = d->base ? tuplePack(1, *d->base) : newTuple(0);
			Ref<Str> name = newStr(d->name);
			ClassObj *cls = new ClassObj();
			cls->_bases = bases;
			cls->_dict = dict;
			cls->_name = name;
			incRef(cls); // kept for good
			dict->setItemString("__doc__", g_none);
			for (const MethodDef *m = d->methods; m && m->name; m++) {
				ObjRef func(new BuiltinFunction(m, nullptr));
				ObjRef meth = newMethod(func.get(), nullptr, cls);
				dict->setItemString(m->name, meth.get());
			}
			*d->exc = cls;
			mod->_dict->setItemString(d->name, cls);
		}
	}
	// The class slots (__getattr__ and friends) are all empty for these
}

} // End of namespace Python
} // End of namespace Yaga
