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

// The __builtin__ module (CPython 2.2's bltinmodule.c)

namespace Yaga {
namespace Python {

static ObjRef builtinImport(Object *, Tuple *args, Dict *kw) {
	if (!checkArgs("__import__", args, 1, 4))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	Object *globals = args->_size > 1 ? args->get(1) : nullptr;
	Object *locals = args->_size > 2 ? args->get(2) : nullptr;
	Object *fromlist = args->_size > 3 ? args->get(3) : nullptr;
	return importModuleEx(name->_data, globals && isInstanceOf(globals, &g_dictType) ? (Dict *)globals : nullptr,
	                      locals && isInstanceOf(locals, &g_dictType) ? (Dict *)locals : nullptr, fromlist);
}

static ObjRef builtinAbs(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("abs", args))
		return ObjRef();
	return numberAbsolute(args->get(0));
}

static ObjRef builtinApply(Object *, Tuple *args, Dict *) {
	if (!checkArgs("apply", args, 1, 3))
		return ObjRef();
	Object *func = args->get(0);
	ObjRef alist;
	Object *kwdict = args->_size > 2 ? args->get(2) : nullptr;
	if (args->_size > 1) {
		Object *a = args->get(1);
		if (!isInstanceOf(a, &g_tupleType)) {
			alist = sequenceTuple(a);
			if (!alist) {
				if (errorMatches(g_excTypeError)) {
					clearError();
					setErrorString(g_excTypeError, "apply() arg 2 expect sequence");
				}
				return ObjRef();
			}
		} else {
			alist = a;
		}
	}
	if (kwdict && !isInstanceOf(kwdict, &g_dictType)) {
		setErrorString(g_excTypeError, "apply() arg 3 expected dictionary");
		return ObjRef();
	}
	if (!alist)
		alist = newTuple(0);
	return call(func, (Tuple *)alist.get(), (Dict *)kwdict);
}

static ObjRef builtinCallable(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("callable", args))
		return ObjRef();
	return newBool(callable(args->get(0)));
}

static ObjRef builtinChr(Object *, Tuple *args, Dict *) {
	int32 x;
	if (!checkArgs("chr", args, 1, 1) || !asInt(args->get(0), &x))
		return ObjRef();
	if (x < 0 || x >= 256) {
		setErrorString(g_excValueError, "chr() arg not in range(256)");
		return ObjRef();
	}
	return strFromCharacter((uint8)x);
}

static ObjRef builtinCmp(Object *, Tuple *args, Dict *) {
	if (!checkArgs("cmp", args, 2, 2))
		return ObjRef();
	int c = compare(args->get(0), args->get(1));
	if (errorOccurred())
		return ObjRef();
	return newInt(c);
}

static ObjRef builtinCoerce(Object *, Tuple *args, Dict *) {
	if (!checkArgs("coerce", args, 2, 2))
		return ObjRef();
	ObjRef a(args->get(0)), b(args->get(1));
	int r = numberCoerce(&a, &b);
	if (r < 0)
		return ObjRef();
	if (r > 0) {
		setErrorString(g_excTypeError, "number coercion failed");
		return ObjRef();
	}
	return tuplePack(2, a.get(), b.get());
}

static ObjRef builtinCompile(Object *, Tuple *args, Dict *) {
	if (!checkArgs("compile", args, 3, 5))
		return ObjRef();
	Str *src = asStr(args->get(0));
	Str *filename = src ? asStr(args->get(1)) : nullptr;
	Str *mode = filename ? asStr(args->get(2)) : nullptr;
	if (!mode)
		return ObjRef();
	int start;
	if (mode->equals("exec"))
		start = kCompileExec;
	else if (mode->equals("eval"))
		start = kCompileEval;
	else if (mode->equals("single"))
		start = kCompileSingle;
	else {
		setErrorString(g_excValueError, "compile() arg 3 must be 'exec' or 'eval' or 'single'");
		return ObjRef();
	}
	return compileString(src->_data, filename->_data, start);
}

static ObjRef builtinDelattr(Object *, Tuple *args, Dict *) {
	if (!checkArgs("delattr", args, 2, 2))
		return ObjRef();
	if (setAttr(args->get(0), (Str *)args->get(1), nullptr) < 0)
		return ObjRef();
	return ObjRef(g_none);
}


// dir() as CPython 2.2 computes it: a sorted list of attribute names
static int mergeDictKeys(Dict *result, Object *dict) {
	if (!dict || !isInstanceOf(dict, &g_dictType))
		return 0;
	int32 pos = 0;
	Object *k;
	while (((Dict *)dict)->next(&pos, &k, nullptr))
		if (result->setItem(k, g_none) < 0)
			return -1;
	return 0;
}

static int mergeClassDict(Dict *result, Object *cls) {
	ObjRef d = getAttrString(cls, "__dict__");
	if (!d)
		clearError();
	else if (mergeDictKeys(result, d.get()) < 0)
		return -1;
	ObjRef bases = getAttrString(cls, "__bases__");
	if (!bases) {
		clearError();
		return 0;
	}
	if (!isInstanceOf(bases.get(), &g_tupleType))
		return 0;
	Tuple *t = (Tuple *)bases.get();
	for (uint32 i = 0; i < t->_size; i++)
		if (mergeClassDict(result, t->get(i)) < 0)
			return -1;
	return 0;
}

static int mergeListAttr(Dict *result, Object *obj, const char *attrname) {
	ObjRef list = getAttrString(obj, attrname);
	if (!list) {
		clearError();
		return 0;
	}
	if (isInstanceOf(list.get(), &g_listType)) {
		List *l = (List *)list.get();
		for (uint32 i = 0; i < l->size(); i++)
			if (isInstanceOf(l->get(i), &g_strType) && result->setItem(l->get(i), g_none) < 0)
				return -1;
	}
	return 0;
}

static ObjRef builtinDir(Object *, Tuple *args, Dict *) {
	if (!checkArgs("dir", args, 0, 1))
		return ObjRef();
	ObjRef result;
	if (args->_size == 0) {
		ObjRef locals = getLocals();
		if (!locals)
			return ObjRef();
		result = callMethod(locals.get(), "keys", 0);
	} else {
		Object *arg = args->get(0);
		Ref<Dict> masterdict = newDict();
		if (isType(arg, &g_moduleType)) {
			ObjRef d = getAttrString(arg, "__dict__");
			if (!d)
				return ObjRef();
			result = callMethod(d.get(), "keys", 0);
		} else if (isInstanceOf(arg, &g_typeType) || isType(arg, &g_classType)) {
			if (mergeClassDict(masterdict.get(), arg) < 0)
				return ObjRef();
			result = dictKeys(masterdict.get());
		} else {
			ObjRef d = getAttrString(arg, "__dict__");
			if (!d)
				clearError();
			else if (mergeDictKeys(masterdict.get(), d.get()) < 0)
				return ObjRef();
			if (mergeListAttr(masterdict.get(), arg, "__members__") < 0 || mergeListAttr(masterdict.get(), arg, "__methods__") < 0)
				return ObjRef();
			ObjRef itsclass = getAttrString(arg, "__class__");
			if (!itsclass)
				clearError();
			else if (mergeClassDict(masterdict.get(), itsclass.get()) < 0)
				return ObjRef();
			result = dictKeys(masterdict.get());
		}
	}
	if (!result)
		return ObjRef();
	if (listSort((List *)result.get(), nullptr) < 0)
		return ObjRef();
	return result;
}

static ObjRef builtinDivmod(Object *, Tuple *args, Dict *) {
	if (!checkArgs("divmod", args, 2, 2))
		return ObjRef();
	return numberDivmod(args->get(0), args->get(1));
}

static ObjRef builtinEval(Object *, Tuple *args, Dict *) {
	if (!checkArgs("eval", args, 1, 3))
		return ObjRef();
	Object *cmd = args->get(0);
	Object *globals = args->_size > 1 ? args->get(1) : g_none;
	Object *locals = args->_size > 2 ? args->get(2) : g_none;
	ObjRef rg(globals), rl(locals);
	if (globals == g_none) {
		rg = getGlobals();
		if (locals == g_none)
			rl = getLocals();
	} else if (locals == g_none) {
		rl = rg;
	}
	if (!isInstanceOf(rg.get(), &g_dictType) || (rl && !isInstanceOf(rl.get(), &g_dictType))) {
		setErrorString(g_excTypeError, "eval() arg 2 must be a dict; arg 3 must be a mapping");
		return ObjRef();
	}
	Dict *g = (Dict *)rg.get();
	if (!g->getItemString("__builtins__"))
		g->setItemString("__builtins__", getBuiltins());
	ObjRef code;
	if (isType(cmd, &g_codeType)) {
		code = cmd;
	} else {
		Str *s = asStr(cmd);
		if (!s) {
			clearError();
			setErrorString(g_excTypeError, "eval() arg 1 must be a string or code object");
			return ObjRef();
		}
		const char *p = s->_data;
		while (*p == ' ' || *p == '\t')
			p++;
		code = compileString(p, "<string>", kCompileEval);
		if (!code)
			return ObjRef();
	}
	return evalCode((Code *)code.get(), g, (Dict *)rl.get());
}

static ObjRef builtinFilter(Object *, Tuple *args, Dict *) {
	if (!checkArgs("filter", args, 2, 2))
		return ObjRef();
	Object *func = args->get(0);
	Object *seq = args->get(1);
	bool isStr = isInstanceOf(seq, &g_strType), isTuple = isInstanceOf(seq, &g_tupleType);
	ObjRef it = getIter(seq);
	if (!it)
		return ObjRef();
	Ref<List> result = newList();
	for (;;) {
		ObjRef item = iterNext(it.get());
		if (!item) {
			if (errorOccurred())
				return ObjRef();
			break;
		}
		int ok;
		if (func == g_none) {
			ok = isTrue(item.get());
		} else {
			ObjRef good = callFunction(func, 1, item.get());
			if (!good)
				return ObjRef();
			ok = isTrue(good.get());
		}
		if (ok < 0)
			return ObjRef();
		if (ok)
			result->append(item.get());
	}
	if (isStr) {
		Ref<Str> empty = newStr("");
		return strJoin(empty.get(), result.get());
	}
	if (isTuple)
		return listAsTuple(result.get());
	return result;
}

static ObjRef builtinGetattr(Object *, Tuple *args, Dict *) {
	if (!checkArgs("getattr", args, 2, 3))
		return ObjRef();
	Object *name = args->get(1);
	if (isInstanceOf(name, &g_unicodeType)) {
		ObjRef s = unicodeToStr(name);
		if (!s)
			return ObjRef();
		ObjRef r = getAttr(args->get(0), (Str *)s.get());
		if (!r && args->_size > 2 && errorMatches(g_excAttributeError)) {
			clearError();
			return ObjRef(args->get(2));
		}
		return r;
	}
	if (!isInstanceOf(name, &g_strType)) {
		setErrorString(g_excTypeError, "attribute name must be string");
		return ObjRef();
	}
	ObjRef r = getAttr(args->get(0), (Str *)name);
	if (!r && args->_size > 2 && errorMatches(g_excAttributeError)) {
		clearError();
		return ObjRef(args->get(2));
	}
	return r;
}

static ObjRef builtinGlobals(Object *, Tuple *args, Dict *) {
	if (!checkArgs("globals", args, 0, 0))
		return ObjRef();
	Dict *g = getGlobals();
	return g ? ObjRef(g) : ObjRef(g_none);
}

static ObjRef builtinHasattr(Object *, Tuple *args, Dict *) {
	if (!checkArgs("hasattr", args, 2, 2))
		return ObjRef();
	Str *name = asStr(args->get(1));
	if (!name)
		return ObjRef();
	ObjRef r = getAttr(args->get(0), name);
	if (!r) {
		// Python 2.2 swallows any exception here
		clearError();
		return newBool(false);
	}
	return newBool(true);
}

static ObjRef builtinHash(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("hash", args))
		return ObjRef();
	int32 h;
	if (!hash(args->get(0), &h))
		return ObjRef();
	return newInt(h);
}

static ObjRef builtinHex(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("hex", args))
		return ObjRef();
	Object *v = args->get(0);
	NumberMethods *nb = v->_type->asNumber;
	if (!nb || !nb->toHex) {
		setErrorString(g_excTypeError, "hex() argument can't be converted to hex");
		return ObjRef();
	}
	return nb->toHex(v);
}

static ObjRef builtinOct(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("oct", args))
		return ObjRef();
	Object *v = args->get(0);
	NumberMethods *nb = v->_type->asNumber;
	if (!nb || !nb->toOct) {
		setErrorString(g_excTypeError, "oct() argument can't be converted to oct");
		return ObjRef();
	}
	return nb->toOct(v);
}

static ObjRef builtinId(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("id", args))
		return ObjRef();
	return newIntOrLong(objectAddress(args->get(0)));
}

static ObjRef builtinInput(Object *, Tuple *, Dict *) {
	setErrorString(g_excEOFError, "EOF when reading a line");
	return ObjRef();
}

static ObjRef builtinIntern(Object *, Tuple *args, Dict *) {
	if (!checkArgs("intern", args, 1, 1))
		return ObjRef();
	if (!isType(args->get(0), &g_strType)) {
		setErrorString(g_excTypeError, "can't intern subclass of string");
		return ObjRef();
	}
	return intern((Str *)args->get(0));
}

static ObjRef builtinIsinstance(Object *, Tuple *args, Dict *) {
	if (!checkArgs("isinstance", args, 2, 2))
		return ObjRef();
	bool r = isInstance(args->get(0), args->get(1));
	if (errorOccurred())
		return ObjRef();
	return newBool(r);
}

static ObjRef builtinIssubclass(Object *, Tuple *args, Dict *) {
	if (!checkArgs("issubclass", args, 2, 2))
		return ObjRef();
	bool r = isSubclass(args->get(0), args->get(1));
	if (errorOccurred())
		return ObjRef();
	return newBool(r);
}

static ObjRef builtinIter(Object *, Tuple *args, Dict *) {
	if (!checkArgs("iter", args, 1, 2))
		return ObjRef();
	if (args->_size == 1)
		return getIter(args->get(0));
	if (!callable(args->get(0))) {
		setErrorString(g_excTypeError, "iter(v, w): v must be callable");
		return ObjRef();
	}
	return ObjRef(new CallIter(args->get(0), args->get(1)));
}

static ObjRef builtinLen(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("len", args))
		return ObjRef();
	int32 n = length(args->get(0));
	if (n < 0 && errorOccurred())
		return ObjRef();
	return newInt(n);
}

static ObjRef builtinLocals(Object *, Tuple *args, Dict *) {
	if (!checkArgs("locals", args, 0, 0))
		return ObjRef();
	ObjRef l = getLocals();
	return l ? l : ObjRef(g_none);
}

static ObjRef builtinMap(Object *, Tuple *args, Dict *) {
	if (args->_size < 2) {
		setErrorString(g_excTypeError, "map() requires at least two args");
		return ObjRef();
	}
	Object *func = args->get(0);
	uint32 n = args->_size - 1;
	if (func == g_none && n == 1)
		return sequenceList(args->get(1));
	Common::Array<ObjRef> iters;
	for (uint32 i = 0; i < n; i++) {
		ObjRef it = getIter(args->get(i + 1));
		if (!it) {
			if (errorMatches(g_excTypeError)) {
				clearError();
				setErrorFormat(g_excTypeError, "argument %u to map() must support iteration", i + 2);
			}
			return ObjRef();
		}
		iters.push_back(it);
	}
	Ref<List> result = newList();
	for (;;) {
		Ref<Tuple> alist = newTuple(n);
		uint32 numactive = 0;
		for (uint32 j = 0; j < n; j++) {
			ObjRef item;
			if (iters[j]) {
				item = iterNext(iters[j].get());
				if (!item) {
					if (errorOccurred())
						return ObjRef();
					iters[j].reset();
				} else {
					numactive++;
				}
			}
			alist->set(j, item ? item.get() : g_none);
		}
		if (numactive == 0)
			break;
		ObjRef value;
		if (func == g_none) {
			if (n == 1)
				value = alist->get(0);
			else
				value = alist;
		} else {
			value = call(func, alist.get(), nullptr);
			if (!value)
				return ObjRef();
		}
		result->append(value.get());
	}
	return result;
}

static ObjRef minMax(Tuple *args, int op, const char *name) {
	Object *v;
	if (args->_size == 1)
		v = args->get(0);
	else if (args->_size == 0) {
		setErrorFormat(g_excTypeError, "%s expected 1 arguments, got 0", name);
		return ObjRef();
	} else
		v = args;
	ObjRef it = getIter(v);
	if (!it)
		return ObjRef();
	ObjRef w;
	for (;;) {
		ObjRef x = iterNext(it.get());
		if (!x) {
			if (errorOccurred())
				return ObjRef();
			break;
		}
		if (!w) {
			w = x;
		} else {
			int cmp = richCompareBool(x.get(), w.get(), op);
			if (cmp > 0)
				w = x;
			else if (cmp < 0)
				return ObjRef();
		}
	}
	if (!w)
		setErrorString(g_excValueError, "min() or max() arg is an empty sequence");
	return w;
}

static ObjRef builtinMin(Object *, Tuple *args, Dict *) { return minMax(args, kLT, "min"); }
static ObjRef builtinMax(Object *, Tuple *args, Dict *) { return minMax(args, kGT, "max"); }

static ObjRef builtinOrd(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("ord", args))
		return ObjRef();
	Object *o = args->get(0);
	if (isInstanceOf(o, &g_strType)) {
		Str *s = (Str *)o;
		if (s->_len == 1)
			return newInt((uint8)s->_data[0]);
		setErrorFormat(g_excTypeError, "ord() expected a character, but string of length %d found", s->_len);
		return ObjRef();
	}
	if (isInstanceOf(o, &g_unicodeType)) {
		Unicode *u = (Unicode *)o;
		if (u->_chars.size() == 1)
			return newInt(u->_chars[0]);
		setErrorFormat(g_excTypeError, "ord() expected a character, but string of length %d found", u->_chars.size());
		return ObjRef();
	}
	setErrorFormat(g_excTypeError, "ord() expected string of length 1, but %.200s found", typeName(o));
	return ObjRef();
}

static ObjRef builtinPow(Object *, Tuple *args, Dict *) {
	if (!checkArgs("pow", args, 2, 3))
		return ObjRef();
	return numberPower(args->get(0), args->get(1), args->_size > 2 ? args->get(2) : g_none);
}

static ObjRef builtinRange(Object *, Tuple *args, Dict *) {
	int32 ilow = 0, ihigh = 0, istep = 1;
	if (args->_size == 1) {
		if (!asInt(args->get(0), &ihigh)) {
			clearError();
			setErrorString(g_excTypeError, "range() integer end argument expected, got " );
			return ObjRef();
		}
	} else if (args->_size == 2 || args->_size == 3) {
		if (!asInt(args->get(0), &ilow) || !asInt(args->get(1), &ihigh))
			return ObjRef();
		if (args->_size == 3 && !asInt(args->get(2), &istep))
			return ObjRef();
	} else {
		setErrorString(g_excTypeError, "range() requires 1-3 int arguments");
		return ObjRef();
	}

	if (istep == 0) {
		setErrorString(g_excValueError, "range() arg 3 must not be zero");
		return ObjRef();
	}
	int64 n;
	if (istep > 0)
		n = ilow < ihigh ? ((int64)ihigh - ilow - 1) / istep + 1 : 0;
	else
		n = ilow > ihigh ? ((int64)ilow - ihigh - 1) / -(int64)istep + 1 : 0;
	if (n > 0x7fffffff) {
		setErrorString(g_excOverflowError, "range() result has too many items");
		return ObjRef();
	}
	Ref<List> l = newList();
	l->_items.reserve((uint32)n);
	int32 v = ilow;
	for (int64 i = 0; i < n; i++, v += istep) {
		Ref<Int> item = newInt(v);
		l->append(item.get());
	}
	return l;
}

static ObjRef builtinReduce(Object *, Tuple *args, Dict *) {
	if (!checkArgs("reduce", args, 2, 3))
		return ObjRef();
	Object *func = args->get(0);
	ObjRef result = args->_size > 2 ? ObjRef(args->get(2)) : ObjRef();
	ObjRef it = getIter(args->get(1));
	if (!it) {
		if (errorMatches(g_excTypeError)) {
			clearError();
			setErrorString(g_excTypeError, "reduce() arg 2 must support iteration");
		}
		return ObjRef();
	}
	for (;;) {
		ObjRef op2 = iterNext(it.get());
		if (!op2) {
			if (errorOccurred())
				return ObjRef();
			break;
		}
		if (!result)
			result = op2;
		else {
			result = callFunction(func, 2, result.get(), op2.get());
			if (!result)
				return ObjRef();
		}
	}
	if (!result)
		setErrorString(g_excTypeError, "reduce() of empty sequence with no initial value");
	return result;
}

static ObjRef builtinReload(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("reload", args))
		return ObjRef();
	Object *m = args->get(0);
	if (!isType(m, &g_moduleType)) {
		setErrorString(g_excTypeError, "reload() argument must be module");
		return ObjRef();
	}
	Object *name = ((Module *)m)->_dict->getItemString("__name__");
	if (!name || !isInstanceOf(name, &g_strType)) {
		setErrorString(g_excSystemError, "reload(): module has no name");
		return ObjRef();
	}
	getModuleDict()->delItem(name);
	clearError();
	return importModule(((Str *)name)->_data);
}

static ObjRef builtinRepr(Object *, Tuple *args, Dict *) {
	if (!checkOneArg("repr", args))
		return ObjRef();
	return repr(args->get(0));
}

static ObjRef builtinRound(Object *, Tuple *args, Dict *) {
	if (!checkArgs("round", args, 1, 2))
		return ObjRef();
	double x;
	int32 ndigits = 0;
	if (!asDouble(args->get(0), &x))
		return ObjRef();
	if (args->_size > 1 && !asInt(args->get(1), &ndigits))
		return ObjRef();
	double f = 1.0;
	int32 i = ndigits < 0 ? -ndigits : ndigits;
	while (--i >= 0)
		f = f * 10.0;
	if (ndigits < 0)
		x /= f;
	else
		x *= f;
	if (x >= 0.0)
		x = floor(x + 0.5);
	else
		x = ceil(x - 0.5);
	if (ndigits < 0)
		x *= f;
	else
		x /= f;
	return newFloat(x);
}

static ObjRef builtinSetattr(Object *, Tuple *args, Dict *) {
	if (!checkArgs("setattr", args, 3, 3))
		return ObjRef();
	if (setAttr(args->get(0), (Str *)args->get(1), args->get(2)) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef builtinUnichr(Object *, Tuple *args, Dict *) {
	int32 x;
	if (!checkArgs("unichr", args, 1, 1) || !asInt(args->get(0), &x))
		return ObjRef();
	if (x < 0 || x > 0xffff) {
		setErrorString(g_excValueError, "unichr() arg not in range(0x10000) (narrow Python build)");
		return ObjRef();
	}
	Unicode *u = new Unicode();
	u->_chars.push_back(x);
	return ObjRef(u);
}

static ObjRef builtinVars(Object *, Tuple *args, Dict *) {
	if (!checkArgs("vars", args, 0, 1))
		return ObjRef();
	if (args->_size == 0) {
		ObjRef l = getLocals();
		if (!l) {
			if (!errorOccurred())
				setErrorString(g_excSystemError, "no locals!?");
			return ObjRef();
		}
		return l;
	}
	ObjRef d = getAttrString(args->get(0), "__dict__");
	if (!d) {
		clearError();
		setErrorString(g_excTypeError, "vars() argument must have __dict__ attribute");
	}
	return d;
}

static ObjRef builtinZip(Object *, Tuple *args, Dict *) {
	if (args->_size == 0) {
		setErrorString(g_excTypeError, "zip() requires at least one sequence");
		return ObjRef();
	}
	Common::Array<ObjRef> iters;
	for (uint32 i = 0; i < args->_size; i++) {
		ObjRef it = getIter(args->get(i));
		if (!it) {
			if (errorMatches(g_excTypeError)) {
				clearError();
				setErrorFormat(g_excTypeError, "zip argument #%u must support iteration", i + 1);
			}
			return ObjRef();
		}
		iters.push_back(it);
	}
	Ref<List> result = newList();
	for (;;) {
		Ref<Tuple> next = newTuple(args->_size);
		for (uint32 j = 0; j < args->_size; j++) {
			ObjRef item = iterNext(iters[j].get());
			if (!item) {
				if (errorOccurred())
					return ObjRef();
				return result;
			}
			next->set(j, item.get());
		}
		result->append(next.get());
	}
}

static ObjRef builtinBool(Object *, Tuple *args, Dict *) {
	if (!checkArgs("bool", args, 0, 1))
		return ObjRef();
	if (args->_size == 0)
		return newBool(false);
	int t = isTrue(args->get(0));
	if (t < 0)
		return ObjRef();
	return newBool(t != 0);
}

// A read-only window onto a string, which is all the shipped scripts make
struct Buffer : public Object {
	ObjRef _base;
	int32 _offset, _size;
	Buffer(Object *base, int32 offset, int32 size) : Object(&g_bufferType), _base(base), _offset(offset), _size(size) {}
	Str *str() const { return (Str *)_base.get(); }
	int32 length() const {
		int32 avail = (int32)str()->_len - _offset;
		if (avail < 0)
			avail = 0;
		return _size < 0 || _size > avail ? avail : _size;
	}
};

Type g_bufferType("buffer", &g_objectType);

static int32 bufferLength(Object *o) {
	return ((Buffer *)o)->length();
}

static ObjRef bufferItem(Object *o, int32 i) {
	Buffer *b = (Buffer *)o;
	if (i < 0 || i >= b->length()) {
		setErrorString(g_excIndexError, "buffer index out of range");
		return ObjRef();
	}
	return strFromCharacter((uint8)b->str()->_data[b->_offset + i]);
}

static ObjRef bufferSlice(Object *o, int32 lo, int32 hi) {
	Buffer *b = (Buffer *)o;
	int32 n = b->length();
	if (lo < 0)
		lo = 0;
	if (hi > n)
		hi = n;
	if (hi < lo)
		hi = lo;
	return newStr(b->str()->_data + b->_offset + lo, hi - lo);
}

static ObjRef bufferStr(Object *o) {
	Buffer *b = (Buffer *)o;
	return newStr(b->str()->_data + b->_offset, b->length());
}

static ObjRef bufferRepr(Object *o) {
	Buffer *b = (Buffer *)o;
	return newStr(Common::String::format("<read-only buffer for 0x%08X, size %d, offset %d at 0x%08X>",
	                                     objectAddress(b->_base.get()), b->_size, b->_offset, objectAddress(o)));
}

static SequenceMethods g_bufferAsSequence;

static ObjRef builtinBuffer(Object *, Tuple *args, Dict *) {
	if (!checkArgs("buffer", args, 1, 3))
		return ObjRef();
	int32 offset = 0, size = -1;
	if (args->_size > 1 && !asInt(args->get(1), &offset))
		return ObjRef();
	if (args->_size > 2 && !asInt(args->get(2), &size))
		return ObjRef();
	Object *base = args->get(0);
	if (!isInstanceOf(base, &g_strType)) {
		setErrorString(g_excTypeError, "buffer object expected");
		return ObjRef();
	}
	if (offset < 0) {
		setErrorString(g_excValueError, "offset must be zero or positive");
		return ObjRef();
	}
	if (!(g_bufferType._flags & kTypeReady)) {
		g_bufferAsSequence.length = bufferLength;
		g_bufferAsSequence.item = bufferItem;
		g_bufferAsSequence.slice = bufferSlice;
		g_bufferType.asSequence = &g_bufferAsSequence;
		g_bufferType.str = bufferStr;
		g_bufferType.repr = bufferRepr;
		g_bufferType.getAttr = genericGetAttr;
		typeReady(&g_bufferType);
	}
	return ObjRef(new Buffer(base, offset, size));
}

static ObjRef builtinExecfile(Object *, Tuple *args, Dict *) {
	if (!checkArgs("execfile", args, 1, 3))
		return ObjRef();
	Str *filename = asStr(args->get(0));
	if (!filename)
		return ObjRef();
	ObjRef f = newFileObject(filename->_data, "r");
	if (!f)
		return ObjRef();
	ObjRef text = callMethod(f.get(), "read", 0);
	if (!text)
		return ObjRef();
	ObjRef code = compileString(((Str *)text.get())->_data, filename->_data, kCompileExec);
	if (!code)
		return ObjRef();
	Object *globals = args->_size > 1 ? args->get(1) : getGlobals();
	Object *locals = args->_size > 2 ? args->get(2) : globals;
	if (args->_size == 1)
		locals = getLocals().get();
	ObjRef keepLocals(locals);
	return evalCode((Code *)code.get(), (Dict *)globals, (Dict *)locals);
}

static const MethodDef g_builtinMethods[] = {
	{ "__import__", builtinImport, kMethVarArgs, nullptr },
	{ "abs", builtinAbs, kMethVarArgs, nullptr },
	{ "apply", builtinApply, kMethVarArgs, nullptr },
	{ "bool", builtinBool, kMethVarArgs, nullptr },
	{ "buffer", builtinBuffer, kMethVarArgs, nullptr },
	{ "callable", builtinCallable, kMethVarArgs, nullptr },
	{ "chr", builtinChr, kMethVarArgs, nullptr },
	{ "cmp", builtinCmp, kMethVarArgs, nullptr },
	{ "coerce", builtinCoerce, kMethVarArgs, nullptr },
	{ "compile", builtinCompile, kMethVarArgs, nullptr },
	{ "delattr", builtinDelattr, kMethVarArgs, nullptr },
	{ "dir", builtinDir, kMethVarArgs, nullptr },
	{ "divmod", builtinDivmod, kMethVarArgs, nullptr },
	{ "eval", builtinEval, kMethVarArgs, nullptr },
	{ "execfile", builtinExecfile, kMethVarArgs, nullptr },
	{ "filter", builtinFilter, kMethVarArgs, nullptr },
	{ "getattr", builtinGetattr, kMethVarArgs, nullptr },
	{ "globals", builtinGlobals, kMethVarArgs, nullptr },
	{ "hasattr", builtinHasattr, kMethVarArgs, nullptr },
	{ "hash", builtinHash, kMethVarArgs, nullptr },
	{ "hex", builtinHex, kMethVarArgs, nullptr },
	{ "id", builtinId, kMethVarArgs, nullptr },
	{ "input", builtinInput, kMethVarArgs, nullptr },
	{ "intern", builtinIntern, kMethVarArgs, nullptr },
	{ "isinstance", builtinIsinstance, kMethVarArgs, nullptr },
	{ "issubclass", builtinIssubclass, kMethVarArgs, nullptr },
	{ "iter", builtinIter, kMethVarArgs, nullptr },
	{ "len", builtinLen, kMethVarArgs, nullptr },
	{ "locals", builtinLocals, kMethVarArgs, nullptr },
	{ "map", builtinMap, kMethVarArgs, nullptr },
	{ "max", builtinMax, kMethVarArgs, nullptr },
	{ "min", builtinMin, kMethVarArgs, nullptr },
	{ "oct", builtinOct, kMethVarArgs, nullptr },
	{ "ord", builtinOrd, kMethVarArgs, nullptr },
	{ "pow", builtinPow, kMethVarArgs, nullptr },
	{ "range", builtinRange, kMethVarArgs, nullptr },
	{ "raw_input", builtinInput, kMethVarArgs, nullptr },
	{ "reduce", builtinReduce, kMethVarArgs, nullptr },
	{ "reload", builtinReload, kMethVarArgs, nullptr },
	{ "repr", builtinRepr, kMethVarArgs, nullptr },
	{ "round", builtinRound, kMethVarArgs, nullptr },
	{ "setattr", builtinSetattr, kMethVarArgs, nullptr },
	{ "unichr", builtinUnichr, kMethVarArgs, nullptr },
	{ "vars", builtinVars, kMethVarArgs, nullptr },
	{ "zip", builtinZip, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

void initBuiltins() {
	Ref<Module> mod = initModule("__builtin__", g_builtinMethods);
	Dict *d = mod->_dict.get();
	g_ts->builtins = d;
	d->setItemString("None", g_none);
	d->setItemString("Ellipsis", g_ellipsis);
	d->setItemString("NotImplemented", g_notImplemented);
	Ref<Int> zero = newInt(0), one = newInt(1);
	// Python 2.2.1 added True and False, as ints
	ObjRef f = newBool(false), t = newBool(true);
	d->setItemString("False", f.get());
	d->setItemString("True", t.get());
	d->setItemString("__debug__", one.get());
	struct { const char *name; Type *type; } types[] = {
		{ "classmethod", &g_classMethodType },
		{ "complex", &g_complexType },
		{ "dict", &g_dictType },
		{ "file", &g_fileType },
		{ "float", &g_floatType },
		{ "property", &g_propertyType },
		{ "int", &g_intType },
		{ "list", &g_listType },
		{ "long", &g_longType },
		{ "object", &g_objectType },
		{ "open", &g_fileType },
		{ "slice", &g_sliceType },
		{ "staticmethod", &g_staticMethodType },
		{ "str", &g_strType },
		{ "super", &g_superType },
		{ "tuple", &g_tupleType },
		{ "type", &g_typeType },
		{ "unicode", &g_unicodeType },
		{ "xrange", &g_xrangeType }
	};
	for (uint i = 0; i < ARRAYSIZE(types); i++)
		d->setItemString(types[i].name, types[i].type);
	// The exceptions
	Module *exc = (Module *)getModuleDict()->getItemString("exceptions");
	int32 pos = 0;
	Object *k, *v;
	while (exc->_dict->next(&pos, &k, &v)) {
		if (isType(v, &g_classType))
			d->setItem(k, v);
	}
}

} // End of namespace Python
} // End of namespace Yaga
