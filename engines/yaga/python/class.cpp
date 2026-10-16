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


// Classic classes and their instances, as CPython 2.2's classobject.c. The
// special methods are looked up through the instance (and so through
// __getattr__) every time, exactly as there.

namespace Yaga {
namespace Python {

Type g_classType("class", &g_objectType);
Type g_instanceType("instance", &g_objectType);
Type g_methodType("instancemethod", &g_objectType);

static Str *g_strGetattr, *g_strSetattr, *g_strDelattr, *g_strInit, *g_strDel;
static Str *g_strName, *g_strModule, *g_strDoc;

static Str *cachedName(Str *&slot, const char *name) {
	if (!slot) {
		Ref<Str> s = internStr(name);
		slot = s.get();
		incRef(slot);
	}
	return slot;
}

#define NAME(var, text) cachedName(var, text)

// ---- Classes ---------------------------------------------------------------

ObjRef classLookup(ClassObj *cls, Str *name, ClassObj **found) {
	Object *v = cls->_dict->getItem(name);
	if (v) {
		*found = cls;
		return ObjRef(v);
	}
	Tuple *bases = cls->_bases.get();
	for (uint32 i = 0; i < bases->_size; i++) {
		Object *base = bases->get(i);
		if (!isType(base, &g_classType))
			continue;
		ObjRef r = classLookup((ClassObj *)base, name, found);
		if (r)
			return r;
	}
	return ObjRef();
}

static void setSlots(ClassObj *c) {
	ClassObj *dummy;
	c->_getattr = classLookup(c, NAME(g_strGetattr, "__getattr__"), &dummy);
	c->_setattr = classLookup(c, NAME(g_strSetattr, "__setattr__"), &dummy);
	c->_delattr = classLookup(c, NAME(g_strDelattr, "__delattr__"), &dummy);
}

ObjRef newClassObj(Tuple *bases, Dict *dict, Str *name) {
	if (!dict->getItem(NAME(g_strDoc, "__doc__")))
		dict->setItem(g_strDoc, g_none);
	if (!dict->getItem(NAME(g_strModule, "__module__"))) {
		Dict *globals = getGlobals();
		if (globals) {
			Object *modname = globals->getItem(NAME(g_strName, "__name__"));
			if (modname)
				dict->setItem(g_strModule, modname);
		}
	}
	Ref<Tuple> b(bases ? bases : newTuple(0).get());
	for (int32 i = b->_size; --i >= 0;) {
		if (!isType(b->get(i), &g_classType)) {
			Object *base = b->get(i);
			if (callable(base->_type)) {
				// A base with a __class__ that can be called makes a new-style class
				return callFunction(base->_type, 3, name, b.get(), dict);
			}
			setErrorString(g_excTypeError, "PyClass_New: base must be a class");
			return ObjRef();
		}
	}
	ClassObj *c = new ClassObj();
	ObjRef rc(c);
	c->_bases = b;
	c->_dict = dict;
	c->_name = name;
	setSlots(c);
	return rc;
}

static ObjRef classNew(Type *, Tuple *args, Dict *kw) {
	if (!noKeywords("class", kw) || !checkArgs("class", args, 3, 3))
		return ObjRef();
	if (!isInstanceOf(args->get(0), &g_strType) || !isInstanceOf(args->get(1), &g_tupleType) || !isInstanceOf(args->get(2), &g_dictType)) {
		setErrorString(g_excTypeError, "class() takes a string, a tuple and a dict");
		return ObjRef();
	}
	return newClassObj((Tuple *)args->get(1), (Dict *)args->get(2), (Str *)args->get(0));
}

static ObjRef classGetAttr(Object *o, Str *name) {
	ClassObj *c = (ClassObj *)o;
	const char *s = name->_data;
	if (s[0] == '_' && s[1] == '_') {
		if (name->equals("__dict__"))
			return ObjRef(c->_dict.get());
		if (name->equals("__bases__"))
			return ObjRef(c->_bases.get());
		if (name->equals("__name__"))
			return c->_name ? ObjRef(c->_name.get()) : ObjRef(g_none);
	}
	ClassObj *found;
	ObjRef v = classLookup(c, name, &found);
	if (!v) {
		setErrorFormat(g_excAttributeError, "class %.50s has no attribute '%.400s'", c->_name->_data, s);
		return ObjRef();
	}
	DescrGetFunc f = v->_type->descrGet;
	if (!f)
		return v;
	return f(v.get(), nullptr, c);
}

static const char *setDict(ClassObj *c, Object *v) {
	if (!v || !isInstanceOf(v, &g_dictType))
		return "__dict__ must be a dictionary object";
	c->_dict = (Dict *)v;
	return "";
}

static const char *setBases(ClassObj *c, Object *v) {
	if (!v || !isInstanceOf(v, &g_tupleType))
		return "__bases__ must be a tuple object";
	Tuple *t = (Tuple *)v;
	for (uint32 i = 0; i < t->_size; i++) {
		Object *x = t->get(i);
		if (!isType(x, &g_classType))
			return "__bases__ items must be classes";
		if (classIsSubclass(x, c))
			return "a __bases__ item causes an inheritance cycle";
	}
	c->_bases = t;
	return "";
}

static const char *setName(ClassObj *c, Object *v) {
	if (!v || !isInstanceOf(v, &g_strType))
		return "__name__ must be a string object";
	if (strlen(((Str *)v)->_data) != ((Str *)v)->_len)
		return "__name__ must not contain null bytes";
	c->_name = (Str *)v;
	return "";
}

static int classSetAttr(Object *o, Str *name, Object *v) {
	ClassObj *c = (ClassObj *)o;
	const char *s = name->_data;
	if (s[0] == '_' && s[1] == '_') {
		uint32 n = name->_len;
		if (s[n - 1] == '_' && s[n - 2] == '_') {
			const char *err = nullptr;
			if (name->equals("__dict__"))
				err = setDict(c, v);
			else if (name->equals("__bases__"))
				err = setBases(c, v);
			else if (name->equals("__name__"))
				err = setName(c, v);
			else if (name->equals("__getattr__"))
				c->_getattr = v;
			else if (name->equals("__setattr__"))
				c->_setattr = v;
			else if (name->equals("__delattr__"))
				c->_delattr = v;
			// For the last three, still set the attribute in __dict__
			if (err) {
				if (*err) {
					setErrorString(g_excTypeError, err);
					return -1;
				}
				setSlots(c);
				return 0;
			}
		}
	}
	if (!v) {
		int r = c->_dict->delItem(name);
		if (r < 0) {
			clearError();
			setErrorFormat(g_excAttributeError, "class %.50s has no attribute '%.400s'", c->_name->_data, s);
		}
		return r;
	}
	int r = c->_dict->setItem(name, v);
	if (r == 0 && s[0] == '_' && s[1] == '_')
		setSlots(c);
	return r;
}

static ObjRef classRepr(Object *o) {
	ClassObj *c = (ClassObj *)o;
	Object *mod = c->_dict->getItemString("__module__");
	const char *name = c->_name ? c->_name->_data : "?";
	if (!mod || !isInstanceOf(mod, &g_strType))
		return newStr(Common::String::format("<class ?.%s at 0x%08X>", name, objectAddress(o)));
	return newStr(Common::String::format("<class %s.%s at 0x%08X>", ((Str *)mod)->_data, name, objectAddress(o)));
}

static ObjRef classStr(Object *o) {
	ClassObj *c = (ClassObj *)o;
	Object *mod = c->_dict->getItemString("__module__");
	ObjRef name = c->_name ? ObjRef(c->_name.get()) : ObjRef(newStr("?"));
	if (!mod || !isInstanceOf(mod, &g_strType))
		return name;
	return newStr(Common::String::format("%s.%s", ((Str *)mod)->_data, ((Str *)name.get())->_data));
}

static ObjRef classCall(Object *o, Tuple *args, Dict *kw) {
	return newInstance(o, args, kw);
}

bool classIsSubclass(Object *cls, Object *base) {
	if (cls == base)
		return true;
	if (!isType(cls, &g_classType))
		return false;
	if (isType(base, &g_tupleType)) {
		Tuple *t = (Tuple *)base;
		for (uint32 i = 0; i < t->_size; i++)
			if (classIsSubclass(cls, t->get(i)))
				return true;
		return false;
	}
	Tuple *bases = ((ClassObj *)cls)->_bases.get();
	for (uint32 i = 0; i < bases->_size; i++)
		if (classIsSubclass(bases->get(i), base))
			return true;
	return false;
}

// ---- Instances -------------------------------------------------------------

Instance::Instance(ClassObj *cls, Dict *dict) : Object(&g_instanceType), _class(cls), _dict(dict) {
}

Instance::~Instance() {
}

static ObjRef instanceGetAttr2(Instance *inst, Str *name);

ObjRef newInstance(Object *klass, Object *argObj, Object *kwObj) {

	if (!isType(klass, &g_classType)) {
		badInternalCall();
		return ObjRef();
	}
	Tuple *arg = (Tuple *)argObj;
	Dict *kw = (Dict *)kwObj;
	Ref<Dict> dict = newDict();
	Instance *inst = new Instance((ClassObj *)klass, dict.get());
	ObjRef rinst(inst);
	// Not through __getattr__, as CPython's PyInstance_New
	ObjRef init = instanceGetAttr2(inst, NAME(g_strInit, "__init__"));
	if (!init) {
		if (errorOccurred())
			return ObjRef();
		if ((arg && arg->_size != 0) || (kw && kw->_used != 0)) {
			setErrorString(g_excTypeError, "this constructor takes no arguments");
			return ObjRef();
		}
		return rinst;
	}
	ObjRef res = call(init.get(), arg ? arg : newTuple(0).get(), kw);
	if (!res)
		return ObjRef();
	if (res.get() != g_none) {
		setErrorString(g_excTypeError, "__init__() should return None");
		return ObjRef();
	}
	return rinst;
}

// Returns null with no error set when missing
static ObjRef instanceGetAttr2(Instance *inst, Str *name) {
	Object *v = inst->_dict->getItem(name);
	if (v)
		return ObjRef(v);
	ClassObj *found;
	ObjRef r = classLookup(inst->_class.get(), name, &found);
	if (r) {
		DescrGetFunc f = r->_type->descrGet;
		if (f)
			return f(r.get(), inst, inst->_class.get());
	}
	return r;
}

static ObjRef instanceGetAttr1(Instance *inst, Str *name) {
	const char *s = name->_data;
	if (s[0] == '_' && s[1] == '_') {
		if (name->equals("__dict__"))
			return ObjRef(inst->_dict.get());
		if (name->equals("__class__"))
			return ObjRef(inst->_class.get());
	}
	ObjRef v = instanceGetAttr2(inst, name);
	if (!v && !errorOccurred())
		setErrorFormat(g_excAttributeError, "%.50s instance has no attribute '%.400s'", inst->_class->_name->_data, s);
	return v;
}

ObjRef instanceGetAttr(Object *o, Str *name) {
	Instance *inst = (Instance *)o;
	ObjRef res = instanceGetAttr1(inst, name);
	Object *func;
	if (!res && (func = inst->_class->_getattr.get())) {
		if (!errorMatches(g_excAttributeError))
			return ObjRef();
		clearError();
		ObjRef keep(func);
		return callFunction(func, 2, inst, name);
	}
	return res;
}

static int instanceSetAttr1(Instance *inst, Str *name, Object *v) {
	if (!v) {
		int r = inst->_dict->delItem(name);
		if (r < 0) {
			clearError();
			setErrorFormat(g_excAttributeError, "%.50s instance has no attribute '%.400s'", inst->_class->_name->_data, name->_data);
		}
		return r;
	}
	return inst->_dict->setItem(name, v);
}

static int instanceSetAttr(Object *o, Str *name, Object *v) {
	Instance *inst = (Instance *)o;
	const char *s = name->_data;
	if (s[0] == '_' && s[1] == '_') {
		if (name->equals("__dict__")) {
			if (!v || !isInstanceOf(v, &g_dictType)) {
				setErrorString(g_excTypeError, "__dict__ must be set to a dictionary");
				return -1;
			}
			inst->_dict = (Dict *)v;
			return 0;
		}
		if (name->equals("__class__")) {
			if (!v || !isType(v, &g_classType)) {
				setErrorString(g_excTypeError, "__class__ must be set to a class");
				return -1;
			}
			inst->_class = (ClassObj *)v;
			return 0;
		}
	}
	Object *func = v ? inst->_class->_setattr.get() : inst->_class->_delattr.get();
	if (!func)
		return instanceSetAttr1(inst, name, v);
	ObjRef keep(func);
	ObjRef res = v ? callFunction(func, 3, inst, name, v) : callFunction(func, 2, inst, name);
	return res ? 0 : -1;
}

// Looks up a special method through the instance; null + AttributeError when missing
static ObjRef special(Object *self, const char *name) {
	Ref<Str> n = internStr(name);
	return instanceGetAttr(self, n.get());
}

static ObjRef instanceRepr(Object *o) {
	Instance *inst = (Instance *)o;
	ObjRef func = special(o, "__repr__");
	if (!func) {
		if (!errorMatches(g_excAttributeError))
			return ObjRef();
		clearError();
		Str *cname = inst->_class->_name.get();
		Object *mod = inst->_class->_dict->getItemString("__module__");
		const char *cn = (cname && isInstanceOf(cname, &g_strType)) ? cname->_data : "?";
		if (!mod || !isInstanceOf(mod, &g_strType))
			return newStr(Common::String::format("<?.%s instance at 0x%08X>", cn, objectAddress(o)));
		return newStr(Common::String::format("<%s.%s instance at 0x%08X>", ((Str *)mod)->_data, cn, objectAddress(o)));
	}
	return callObject(func.get(), nullptr);
}

static ObjRef instanceStr(Object *o) {
	ObjRef func = special(o, "__str__");
	if (!func) {
		if (!errorMatches(g_excAttributeError))
			return ObjRef();
		clearError();
		return instanceRepr(o);
	}
	return callObject(func.get(), nullptr);
}

static bool instanceHash(Object *o, int32 *out) {
	ObjRef func = special(o, "__hash__");
	if (!func) {
		if (!errorMatches(g_excAttributeError))
			return false;
		clearError();
		// No __hash__: hashable by identity only without __eq__ and __cmp__
		func = special(o, "__eq__");
		if (!func) {
			if (!errorMatches(g_excAttributeError))
				return false;
			clearError();
			func = special(o, "__cmp__");
			if (!func) {
				if (!errorMatches(g_excAttributeError))
					return false;
				clearError();
				*out = hashPointer(o);
				return true;
			}
		}
		setErrorString(g_excTypeError, "unhashable instance");
		return false;
	}
	ObjRef res = callObject(func.get(), nullptr);
	if (!res)
		return false;
	if (!isInstanceOf(res.get(), &g_intType)) {
		setErrorString(g_excTypeError, "__hash__() should return an int");
		return false;
	}
	int32 x = ((Int *)res.get())->_value;
	*out = x == -1 ? -2 : x;
	return true;
}

static int32 instanceLength(Object *o) {
	ObjRef func = special(o, "__len__");
	if (!func)
		return -1;
	ObjRef res = callObject(func.get(), nullptr);
	if (!res)
		return -1;
	if (!isInstanceOf(res.get(), &g_intType)) {
		setErrorString(g_excTypeError, "__len__() should return an int");
		return -1;
	}
	int32 n = ((Int *)res.get())->_value;
	if (n < 0) {
		setErrorString(g_excValueError, "__len__() should return >= 0");
		return -1;
	}
	return n;
}

static ObjRef instanceSubscript(Object *o, Object *key) {
	ObjRef func = special(o, "__getitem__");
	if (!func)
		return ObjRef();
	return callFunction(func.get(), 1, key);
}

static int instanceAssSubscript(Object *o, Object *key, Object *value) {
	ObjRef func = special(o, value ? "__setitem__" : "__delitem__");
	if (!func)
		return -1;
	ObjRef res = value ? callFunction(func.get(), 2, key, value) : callFunction(func.get(), 1, key);
	return res ? 0 : -1;
}

static ObjRef instanceItem(Object *o, int32 i) {
	ObjRef func = special(o, "__getitem__");
	if (!func)
		return ObjRef();
	Ref<Int> idx = newInt(i);
	return callFunction(func.get(), 1, idx.get());
}

static ObjRef instanceSlice(Object *o, int32 i, int32 j) {
	ObjRef func = special(o, "__getslice__");
	if (!func) {
		if (!errorMatches(g_excAttributeError))
			return ObjRef();
		clearError();
		func = special(o, "__getitem__");
		if (!func)
			return ObjRef();
		Ref<Int> a = newInt(i), b = newInt(j);
		ObjRef sl(new Slice(a.get(), b.get(), g_none));
		return callFunction(func.get(), 1, sl.get());
	}
	Ref<Int> a = newInt(i), b = newInt(j);
	return callFunction(func.get(), 2, a.get(), b.get());
}

static int instanceAssItem(Object *o, int32 i, Object *v) {
	ObjRef func = special(o, v ? "__setitem__" : "__delitem__");
	if (!func)
		return -1;
	Ref<Int> idx = newInt(i);
	ObjRef res = v ? callFunction(func.get(), 2, idx.get(), v) : callFunction(func.get(), 1, idx.get());
	return res ? 0 : -1;
}

static int instanceAssSlice(Object *o, int32 i, int32 j, Object *v) {
	ObjRef func = special(o, v ? "__setslice__" : "__delslice__");
	Ref<Int> a = newInt(i), b = newInt(j);
	if (!func) {
		if (!errorMatches(g_excAttributeError))
			return -1;
		clearError();
		func = special(o, v ? "__setitem__" : "__delitem__");
		if (!func)
			return -1;
		ObjRef sl(new Slice(a.get(), b.get(), g_none));
		ObjRef res = v ? callFunction(func.get(), 2, sl.get(), v) : callFunction(func.get(), 1, sl.get());
		return res ? 0 : -1;
	}
	ObjRef res = v ? callFunction(func.get(), 3, a.get(), b.get(), v) : callFunction(func.get(), 2, a.get(), b.get());
	return res ? 0 : -1;
}

static int instanceContains(Object *o, Object *member) {
	ObjRef func = special(o, "__contains__");
	if (func) {
		ObjRef res = callFunction(func.get(), 1, member);
		if (!res)
			return -1;
		return isTrue(res.get());
	}
	if (!errorMatches(g_excAttributeError))
		return -1;
	clearError();
	// Search with the iterator, like _PySequence_IterSearch
	ObjRef it = getIter(o);
	if (!it)
		return -1;
	for (;;) {
		ObjRef item = iterNext(it.get());
		if (!item)
			return errorOccurred() ? -1 : 0;
		int c = richCompareBool(member, item.get(), kEQ);
		if (c != 0)
			return c;
	}
}

// Binary operators ----------------------------------------------------------

static ObjRef genericBinaryOp(Object *v, Object *w, const char *opname) {
	ObjRef func = getAttrString(v, opname);
	if (!func) {
		if (!errorMatches(g_excAttributeError))
			return ObjRef();
		clearError();
		return ObjRef(g_notImplemented);
	}
	return callFunction(func.get(), 1, w);
}

static ObjRef halfBinop(Object *v, Object *w, const char *opname, BinaryFunc thisfunc, bool swapped) {
	if (!isType(v, &g_instanceType))
		return ObjRef(g_notImplemented);
	ObjRef coercefunc = getAttrString(v, "__coerce__");
	if (!coercefunc) {
		if (!errorMatches(g_excAttributeError))
			return ObjRef();
		clearError();
		return genericBinaryOp(v, w, opname);
	}
	ObjRef coerced = callFunction(coercefunc.get(), 1, w);
	if (!coerced)
		return ObjRef();
	if (coerced.get() == g_none || coerced.get() == g_notImplemented)
		return genericBinaryOp(v, w, opname);
	if (!isInstanceOf(coerced.get(), &g_tupleType) || ((Tuple *)coerced.get())->_size != 2) {
		setErrorString(g_excTypeError, "coercion should return None or 2-tuple");
		return ObjRef();
	}
	Object *v1 = ((Tuple *)coerced.get())->get(0);
	Object *w1 = ((Tuple *)coerced.get())->get(1);
	if (v1->_type == v->_type && isType(v, &g_instanceType))
		return genericBinaryOp(v1, w1, opname);
	return swapped ? thisfunc(w1, v1) : thisfunc(v1, w1);
}

static ObjRef doBinop(Object *v, Object *w, const char *opname, const char *ropname, BinaryFunc thisfunc) {
	ObjRef r = halfBinop(v, w, opname, thisfunc, false);
	if (r && r.get() == g_notImplemented)
		r = halfBinop(w, v, ropname, thisfunc, true);
	return r;
}

static ObjRef doBinopInplace(Object *v, Object *w, const char *iopname, const char *opname, const char *ropname, BinaryFunc thisfunc) {
	ObjRef r = halfBinop(v, w, iopname, thisfunc, false);
	if (r && r.get() == g_notImplemented)
		r = doBinop(v, w, opname, ropname, thisfunc);
	return r;
}

#define BINARY(f, m, n) \
	static ObjRef f(Object *v, Object *w) { return doBinop(v, w, "__" m "__", "__r" m "__", n); }
#define BINARY_INPLACE(f, m, n) \
	static ObjRef f(Object *v, Object *w) { return doBinopInplace(v, w, "__i" m "__", "__" m "__", "__r" m "__", n); }

BINARY(instanceOr, "or", numberOr)
BINARY(instanceAnd, "and", numberAnd)
BINARY(instanceXor, "xor", numberXor)
BINARY(instanceLshift, "lshift", numberLshift)
BINARY(instanceRshift, "rshift", numberRshift)
BINARY(instanceAdd, "add", numberAdd)
BINARY(instanceSub, "sub", numberSubtract)
BINARY(instanceMul, "mul", numberMultiply)
BINARY(instanceDiv, "div", numberDivide)
BINARY(instanceMod, "mod", numberRemainder)
BINARY(instanceDivmod, "divmod", numberDivmod)
BINARY(instanceFloordiv, "floordiv", numberFloorDivide)
BINARY(instanceTruediv, "truediv", numberTrueDivide)
BINARY_INPLACE(instanceIor, "or", numberOr)
BINARY_INPLACE(instanceIand, "and", numberAnd)
BINARY_INPLACE(instanceIxor, "xor", numberXor)
BINARY_INPLACE(instanceIlshift, "lshift", numberLshift)
BINARY_INPLACE(instanceIrshift, "rshift", numberRshift)
BINARY_INPLACE(instanceIadd, "add", numberAdd)
BINARY_INPLACE(instanceIsub, "sub", numberSubtract)
BINARY_INPLACE(instanceImul, "mul", numberMultiply)
BINARY_INPLACE(instanceIdiv, "div", numberDivide)
BINARY_INPLACE(instanceImod, "mod", numberRemainder)
BINARY_INPLACE(instanceIfloordiv, "floordiv", numberFloorDivide)
BINARY_INPLACE(instanceItruediv, "truediv", numberTrueDivide)

static ObjRef binPowerFunc(Object *v, Object *w) {
	return numberPower(v, w, g_none);
}

static ObjRef binInplacePowerFunc(Object *v, Object *w) {
	return numberInplacePower(v, w, g_none);
}

static ObjRef instancePow(Object *v, Object *w, Object *z) {
	if (z == g_none)
		return doBinop(v, w, "__pow__", "__rpow__", binPowerFunc);
	ObjRef func = getAttrString(v, "__pow__");
	if (!func)
		return ObjRef();
	return callFunction(func.get(), 2, w, z);
}

static ObjRef instanceIpow(Object *v, Object *w, Object *z) {
	if (z == g_none)
		return doBinopInplace(v, w, "__ipow__", "__pow__", "__rpow__", binInplacePowerFunc);
	ObjRef func = getAttrString(v, "__ipow__");
	if (!func) {
		if (!errorMatches(g_excAttributeError))
			return ObjRef();
		clearError();
		return instancePow(v, w, z);
	}
	return callFunction(func.get(), 2, w, z);
}

static int instanceCoerce(ObjRef *pv, ObjRef *pw) {
	Object *v = pv->get();
	ObjRef coercefunc = getAttrString(v, "__coerce__");
	if (!coercefunc) {
		if (!errorMatches(g_excAttributeError))
			return -1;
		clearError();
		return 1;
	}
	ObjRef coerced = callFunction(coercefunc.get(), 1, pw->get());
	if (!coerced)
		return -1;
	if (coerced.get() == g_none || coerced.get() == g_notImplemented)
		return 1;
	if (!isInstanceOf(coerced.get(), &g_tupleType) || ((Tuple *)coerced.get())->_size != 2) {
		setErrorString(g_excTypeError, "coercion should return None or 2-tuple");
		return -1;
	}
	*pv = ObjRef(((Tuple *)coerced.get())->get(0));
	*pw = ObjRef(((Tuple *)coerced.get())->get(1));
	return 0;
}

#define UNARY(f, m) \
	static ObjRef f(Object *self) { \
		ObjRef func = special(self, m); \
		if (!func) \
			return ObjRef(); \
		return callObject(func.get(), nullptr); \
	}

UNARY(instanceNeg, "__neg__")
UNARY(instancePos, "__pos__")
UNARY(instanceAbs, "__abs__")
UNARY(instanceInvert, "__invert__")
UNARY(instanceInt, "__int__")
UNARY(instanceLong, "__long__")
UNARY(instanceFloat, "__float__")
UNARY(instanceOct, "__oct__")
UNARY(instanceHex, "__hex__")

static int instanceNonzero(Object *self) {
	ObjRef func = special(self, "__nonzero__");
	if (!func) {
		if (!errorMatches(g_excAttributeError))
			return -1;
		clearError();
		func = special(self, "__len__");
		if (!func) {
			if (!errorMatches(g_excAttributeError))
				return -1;
			clearError();
			return 1;
		}
	}
	ObjRef res = callObject(func.get(), nullptr);
	if (!res)
		return -1;
	if (!isInstanceOf(res.get(), &g_intType)) {
		setErrorString(g_excTypeError, "__nonzero__ should return an int");
		return -1;
	}
	int32 outcome = ((Int *)res.get())->_value;
	if (outcome < 0) {
		setErrorString(g_excValueError, "__nonzero__ should return >= 0");
		return -1;
	}
	return outcome > 0;
}

// Comparison ----------------------------------------------------------------

// Returns -1, 0, 1, 2 (no __cmp__) or -2 (error)
static int halfCmp(Object *v, Object *w) {
	ObjRef cmpFunc = getAttrString(v, "__cmp__");
	if (!cmpFunc) {
		if (!errorMatches(g_excAttributeError))
			return -2;
		clearError();
		return 2;
	}
	ObjRef result = callFunction(cmpFunc.get(), 1, w);
	if (!result)
		return -2;
	if (result.get() == g_notImplemented)
		return 2;
	int32 l;
	if (!asInt(result.get(), &l)) {
		clearError();
		setErrorString(g_excTypeError, "comparison did not return an int");
		return -2;
	}
	return l < 0 ? -1 : l > 0 ? 1 : 0;
}

static int instanceCompare(Object *v, Object *w) {
	ObjRef cv(v), cw(w);
	int c = numberCoerce(&cv, &cw);
	if (c < 0)
		return -2;
	if (c == 0) {
		if (!isType(cv.get(), &g_instanceType) && !isType(cw.get(), &g_instanceType)) {
			c = compare(cv.get(), cw.get());
			if (errorOccurred())
				return -2;
			return c < 0 ? -1 : c > 0 ? 1 : 0;
		}
	} else {
		cv = ObjRef(v);
		cw = ObjRef(w);
	}
	if (isType(cv.get(), &g_instanceType)) {
		c = halfCmp(cv.get(), cw.get());
		if (c <= 1)
			return c;
	}
	if (isType(cw.get(), &g_instanceType)) {
		c = halfCmp(cw.get(), cv.get());
		if (c <= 1) {
			if (c >= -1)
				c = -c;
			return c;
		}
	}
	return 2;
}

static const char *const g_opNames[] = { "__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__" };
static const int g_swapped[] = { kGT, kGE, kEQ, kNE, kLT, kLE };

static ObjRef halfRichCompare(Object *v, Object *w, int op) {
	Instance *inst = (Instance *)v;
	Ref<Str> name = internStr(g_opNames[op]);
	ObjRef method;
	if (!inst->_class->_getattr) {
		method = instanceGetAttr2(inst, name.get());
		if (!method) {
			if (errorOccurred())
				return ObjRef();
			return ObjRef(g_notImplemented);
		}
	} else {
		method = getAttr(v, name.get());
		if (!method) {
			if (!errorMatches(g_excAttributeError))
				return ObjRef();
			clearError();
			return ObjRef(g_notImplemented);
		}
	}
	return callFunction(method.get(), 1, w);
}

static ObjRef instanceRichCompare(Object *v, Object *w, int op) {
	if (isType(v, &g_instanceType)) {
		ObjRef res = halfRichCompare(v, w, op);
		if (!res || res.get() != g_notImplemented)
			return res;
	}
	if (isType(w, &g_instanceType)) {
		ObjRef res = halfRichCompare(w, v, g_swapped[op]);
		if (!res || res.get() != g_notImplemented)
			return res;
	}
	return ObjRef(g_notImplemented);
}

// Iteration and calls ---------------------------------------------------------

static ObjRef instanceGetIter(Object *self) {
	ObjRef func = special(self, "__iter__");
	if (func) {
		ObjRef res = callObject(func.get(), nullptr);
		if (!res)
			return ObjRef();
		if (!res->_type->iterNext) {
			setErrorFormat(g_excTypeError, "__iter__ returned non-iterator of type '%.100s'", typeName(res.get()));
			return ObjRef();
		}
		return res;
	}
	if (!errorMatches(g_excAttributeError))
		return ObjRef();
	clearError();
	func = special(self, "__getitem__");
	if (!func) {
		clearError();
		setErrorString(g_excTypeError, "iteration over non-sequence");
		return ObjRef();
	}
	return ObjRef(new SeqIter(self));
}

static ObjRef instanceIterNext(Object *self) {
	ObjRef func = special(self, "next");
	if (func) {
		ObjRef res = callObject(func.get(), nullptr);
		if (res)
			return res;
		if (errorMatches(g_excStopIteration))
			clearError();
		return ObjRef();
	}
	clearError();
	setErrorString(g_excTypeError, "instance has no next() method");
	return ObjRef();
}

static ObjRef instanceCall(Object *self, Tuple *args, Dict *kw) {
	ObjRef func = special(self, "__call__");
	if (!func) {
		Instance *inst = (Instance *)self;
		if (!errorMatches(g_excAttributeError))
			return ObjRef();
		clearError();
		setErrorFormat(g_excAttributeError, "%.200s instance has no __call__ method", inst->_class->_name->_data);
		return ObjRef();
	}
	return call(func.get(), args, kw);
}

// Objects with a __del__ method run it when their last reference goes, and
// may come back to life doing so (CPython's instance_dealloc and subtype_dealloc)
bool finalizeObject(Object *o) {
	if (!g_ts)
		return true;
	Str *name = NAME(g_strDel, "__del__");
	if (isType(o, &g_instanceType)) {
		ClassObj *found;
		if (!classLookup(((Instance *)o)->_class.get(), name, &found))
			return true;
	} else if (!lookupType(o->_type, name)) {
		return true;
	}
	o->_refs = 1;
	{
		ObjRef errType, errValue, errTb;
		fetchError(&errType, &errValue, &errTb);
		ObjRef bound;
		if (isType(o, &g_instanceType)) {
			bound = instanceGetAttr2((Instance *)o, name);
		} else {
			ObjRef f = lookupType(o->_type, name);
			if (f && f->_type->descrGet)
				bound = f->_type->descrGet(f.get(), o, o->_type);
		}
		ObjRef res;
		if (bound)
			res = callObject(bound.get(), nullptr);
		if (!res && errorOccurred()) {
			// Reported and ignored, as "Exception E: v in <bound method ...> ignored"
			ObjRef t, v, tb;
			fetchError(&t, &v, &tb);
			normalizeException(&t, &v, &tb);
			clearError();
			ObjRef r = repr(bound ? bound.get() : g_none);
			// As PyErr_WriteUnraisable: the type with str(), the value with repr()
			ObjRef ts = str(t.get());
			ObjRef vs = v && v.get() != g_none ? repr(v.get()) : ObjRef();
			clearError();
			Common::String msg = "Exception ";
			msg += ts ? ((Str *)ts.get())->toString() : "?";
			if (vs)
				msg += ": " + ((Str *)vs.get())->toString();
			msg += " in ";
			msg += r ? ((Str *)r.get())->toString() : "?";
			msg += " ignored\n";
			writeToStderr(msg.c_str());
		}
		restoreError(errType.get(), errValue.get(), errTb.get());
	}
	if (--o->_refs > 0)
		return false; // resurrected
	return true;
}

// ---- Methods -------------------------------------------------------------------

Method::Method(Object *func, Object *self, Object *cls) : Object(&g_methodType), _func(func), _self(self), _class(cls) {
}

ObjRef newMethod(Object *func, Object *self, Object *cls) {
	return ObjRef(new Method(func, self, cls));
}

static ObjRef methodCall(Object *o, Tuple *arg, Dict *kw) {
	Method *m = (Method *)o;
	Object *self = m->_self.get();
	Ref<Tuple> args(arg);
	if (!self) {
		// Unbound methods must be called with an instance of the class
		bool ok;
		if (arg->_size >= 1) {
			self = arg->get(0);
			ok = isInstance(self, m->_class.get());
			if (errorOccurred())
				return ObjRef();
		} else {
			ok = false;
		}
		if (!ok) {
			Object *func = m->_func.get();
			const char *fname = "?";
			ObjRef fn = getAttrString(func, "__name__");
			if (fn && isInstanceOf(fn.get(), &g_strType))
				fname = ((Str *)fn.get())->_data;
			else
				clearError();
			const char *cname = "?";
			if (m->_class && isType(m->_class.get(), &g_classType))
				cname = ((ClassObj *)m->_class.get())->_name->_data;
			else if (m->_class && isInstanceOf(m->_class.get(), &g_typeType))
				cname = ((Type *)m->_class.get())->_name.c_str();
			const char *gotName = "nothing";
			Common::String got;
			if (self) {
				if (isType(self, &g_instanceType))
					got = Common::String::format("%s instance", ((Instance *)self)->_class->_name->_data);
				else
					got = Common::String::format("%s instance", typeName(self));
				gotName = got.c_str();
			}
			setErrorFormat(g_excTypeError, "unbound method %s%s must be called with %s instance as first argument (got %s%s instead)",
			               fname, "()", cname, gotName, "");
			return ObjRef();
		}
	} else {
		Ref<Tuple> newArgs = newTuple(arg->_size + 1);
		newArgs->set(0, self);
		for (uint32 i = 0; i < arg->_size; i++)
			newArgs->set(i + 1, arg->get(i));
		args = newArgs;
	}
	return call(m->_func.get(), args.get(), kw);
}

static ObjRef methodDescrGet(Object *meth, Object *obj, Object *cls) {
	Method *m = (Method *)meth;
	// Already bound methods are left alone, as in CPython 2.2
	if (m->_self)
		return ObjRef(meth);
	if (m->_class && cls && !isSubclass(cls, m->_class.get())) {
		clearError();
		return ObjRef(meth);
	}
	if (obj == g_none)
		obj = nullptr;
	if (!cls)
		cls = obj->_type;
	return newMethod(m->_func.get(), obj, cls);
}

static ObjRef methodRepr(Object *o) {
	Method *m = (Method *)o;
	Common::String fname = "?", cname = "?";
	ObjRef fn = getAttrString(m->_func.get(), "__name__");
	if (fn && isInstanceOf(fn.get(), &g_strType))
		fname = ((Str *)fn.get())->toString();
	else
		clearError();
	if (m->_class) {
		ObjRef cn = getAttrString(m->_class.get(), "__name__");
		if (cn && isInstanceOf(cn.get(), &g_strType))
			cname = ((Str *)cn.get())->toString();
		else
			clearError();
	}
	if (!m->_self)
		return newStr(Common::String::format("<unbound method %s.%s>", cname.c_str(), fname.c_str()));
	ObjRef selfRepr = repr(m->_self.get());
	if (!selfRepr)
		return ObjRef();
	return newStr(Common::String::format("<bound method %s.%s of %s>", cname.c_str(), fname.c_str(), ((Str *)selfRepr.get())->_data));
}

static int methodCompare(Object *a, Object *b) {
	Method *x = (Method *)a, *y = (Method *)b;
	if (x->_self != y->_self)
		return objectAddress(x->_self ? x->_self.get() : g_none) < objectAddress(y->_self ? y->_self.get() : g_none) ? -1 : 1;
	return compare(x->_func.get(), y->_func.get());
}

static bool methodHash(Object *o, int32 *out) {
	Method *m = (Method *)o;
	int32 x, y;
	if (!hash(m->_self ? m->_self.get() : g_none, &x))
		return false;
	if (!hash(m->_func.get(), &y))
		return false;
	*out = x ^ y;
	return true;
}

static ObjRef methodGetAttr(Object *o, Str *name) {
	Method *m = (Method *)o;
	if (name->equals("im_func"))
		return m->_func;
	if (name->equals("im_self"))
		return m->_self ? m->_self : ObjRef(g_none);
	if (name->equals("im_class"))
		return m->_class ? m->_class : ObjRef(g_none);
	if (name->equals("__class__"))
		return ObjRef(&g_methodType);
	return getAttr(m->_func.get(), name);
}

static int methodSetAttr(Object *o, Str *name, Object *v) {
	setErrorString(g_excTypeError, "instance method attributes are read-only");
	return -1;
}

// ---- isinstance / issubclass -------------------------------------------------

static Ref<Tuple> abstractGetBases(Object *cls) {
	ObjRef bases = getAttrString(cls, "__bases__");
	if (!bases || !isInstanceOf(bases.get(), &g_tupleType)) {
		clearError();
		return Ref<Tuple>();
	}
	return bases.as<Tuple>();
}

static bool abstractIsSubclass(Object *derived, Object *cls) {
	if (derived == cls)
		return true;
	Ref<Tuple> bases = abstractGetBases(derived);
	if (!bases)
		return false;
	for (uint32 i = 0; i < bases->_size; i++)
		if (abstractIsSubclass(bases->get(i), cls))
			return true;
	return false;
}

bool isInstance(Object *inst, Object *cls) {
	if (isType(inst, &g_instanceType) && isType(cls, &g_classType))
		return classIsSubclass(((Instance *)inst)->_class.get(), cls);
	if (isInstanceOf(cls, &g_typeType))
		return isInstanceOf(inst, (Type *)cls);
	if (isType(cls, &g_tupleType)) {
		Tuple *t = (Tuple *)cls;
		for (uint32 i = 0; i < t->_size; i++) {
			if (isInstance(inst, t->get(i)))
				return true;
			if (errorOccurred())
				return false;
		}
		return false;
	}
	if (!abstractGetBases(cls)) {
		setErrorString(g_excTypeError, "isinstance() arg 2 must be a class, type, or tuple of classes and types");

		return false;
	}
	ObjRef icls = getAttrString(inst, "__class__");
	if (!icls) {
		clearError();
		return false;
	}
	return abstractIsSubclass(icls.get(), cls);
}

bool isSubclass(Object *derived, Object *cls) {
	if (isType(cls, &g_classType) && isType(derived, &g_classType))
		return classIsSubclass(derived, cls);
	if (isInstanceOf(derived, &g_typeType) && isInstanceOf(cls, &g_typeType))
		return isSubtype((Type *)derived, (Type *)cls);
	if (isType(cls, &g_tupleType)) {
		Tuple *t = (Tuple *)cls;
		for (uint32 i = 0; i < t->_size; i++)
			if (isSubclass(derived, t->get(i)))
				return true;
		return false;
	}
	if (!abstractGetBases(derived)) {
		setErrorString(g_excTypeError, "issubclass() arg 1 must be a class");
		return false;
	}
	if (!abstractGetBases(cls)) {
		setErrorString(g_excTypeError, "issubclass() arg 2 must be a class");
		return false;
	}
	return abstractIsSubclass(derived, cls);
}

static NumberMethods g_instanceAsNumber;
static SequenceMethods g_instanceAsSequence;
static MappingMethods g_instanceAsMapping;

void initClassTypes() {
	g_classType.getAttr = classGetAttr;
	g_classType.setAttr = classSetAttr;
	g_classType.repr = classRepr;
	g_classType.str = classStr;
	g_classType.call = classCall;
	g_classType.newObj = classNew;

	NumberMethods &n = g_instanceAsNumber;
	n.add = instanceAdd;
	n.subtract = instanceSub;
	n.multiply = instanceMul;
	n.divide = instanceDiv;
	n.remainder = instanceMod;
	n.divmod = instanceDivmod;
	n.power = instancePow;
	n.negative = instanceNeg;
	n.positive = instancePos;
	n.absolute = instanceAbs;
	n.nonzero = instanceNonzero;
	n.invert = instanceInvert;
	n.lshift = instanceLshift;
	n.rshift = instanceRshift;
	n.and_ = instanceAnd;
	n.xor_ = instanceXor;
	n.or_ = instanceOr;
	n.coerce = instanceCoerce;
	n.toInt = instanceInt;
	n.toLong = instanceLong;
	n.toFloat = instanceFloat;
	n.toOct = instanceOct;
	n.toHex = instanceHex;
	n.inplaceAdd = instanceIadd;
	n.inplaceSubtract = instanceIsub;
	n.inplaceMultiply = instanceImul;
	n.inplaceDivide = instanceIdiv;
	n.inplaceRemainder = instanceImod;
	n.inplacePower = instanceIpow;
	n.inplaceLshift = instanceIlshift;
	n.inplaceRshift = instanceIrshift;
	n.inplaceAnd = instanceIand;
	n.inplaceXor = instanceIxor;
	n.inplaceOr = instanceIor;
	n.floorDivide = instanceFloordiv;
	n.trueDivide = instanceTruediv;
	n.inplaceFloorDivide = instanceIfloordiv;
	n.inplaceTrueDivide = instanceItruediv;

	SequenceMethods &s = g_instanceAsSequence;
	s.length = instanceLength;
	s.item = instanceItem;
	s.slice = instanceSlice;
	s.assItem = instanceAssItem;
	s.assSlice = instanceAssSlice;
	s.contains = instanceContains;

	MappingMethods &m = g_instanceAsMapping;
	m.length = instanceLength;
	m.subscript = instanceSubscript;
	m.assSubscript = instanceAssSubscript;

	Type &t = g_instanceType;
	t._flags |= kTypeCheckTypes; // the instance slots do their own coercion
	t.asNumber = &n;

	t.asSequence = &s;
	t.asMapping = &m;
	t.repr = instanceRepr;
	t.str = instanceStr;
	t.hash = instanceHash;
	t.compare = instanceCompare;
	t.richCompare = instanceRichCompare;
	t.getAttr = instanceGetAttr;
	t.setAttr = instanceSetAttr;
	t.call = instanceCall;
	t.iter = instanceGetIter;
	t.iterNext = instanceIterNext;

	g_methodType.call = methodCall;
	g_methodType.descrGet = methodDescrGet;
	g_methodType.repr = methodRepr;
	g_methodType.compare = methodCompare;
	g_methodType.hash = methodHash;
	g_methodType.getAttr = methodGetAttr;
	g_methodType.setAttr = methodSetAttr;
}

} // End of namespace Python
} // End of namespace Yaga
