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

// New-style types, as CPython 2.2's typeobject.c: type objects, 'object',
// class statements that derive from types, slot dispatch to special methods,
// super(), and the descriptors that expose native methods and slots.

namespace Yaga {
namespace Python {

Type g_typeType("type", &g_objectType);
Type g_objectType("object", nullptr);
Type g_superType("super", &g_objectType);
Type g_methodDescrType("method_descriptor", &g_objectType);
Type g_getSetDescrType("getset_descriptor", &g_objectType);
Type g_wrapperDescrType("wrapper_descriptor", &g_objectType);
static Type g_methodWrapperType("method-wrapper", &g_objectType);

Type::Type(const char *name, Type *base) : Object(&g_typeType), _name(name), _flags(0), _base(base),
	repr(nullptr), str(nullptr), hash(nullptr), compare(nullptr), richCompare(nullptr),
	getAttr(nullptr), setAttr(nullptr), call(nullptr), iter(nullptr), iterNext(nullptr),
	descrGet(nullptr), descrSet(nullptr), init(nullptr), newObj(nullptr),
	asNumber(nullptr), asSequence(nullptr), asMapping(nullptr), methods(nullptr), getSets(nullptr) {
	// Built-in types live for good
	_refs = 0x40000000;
}

Type::~Type() {
	if (_flags & kTypeHeap) {
		delete asNumber;
		delete asSequence;
		delete asMapping;
	}
}

// ---- Descriptors ---------------------------------------------------------------

MethodDescr::MethodDescr(const MethodDef *def, Type *owner) : Object(&g_methodDescrType), _def(def), _owner(owner) {
}

GetSetDescr::GetSetDescr(const GetSetDef *def, Type *owner) : Object(&g_getSetDescrType), _def(def), _owner(owner) {
}

WrapperDescr::WrapperDescr(const char *name, WrapperFunc wrapper, void *wrapped, Type *owner) : Object(&g_wrapperDescrType),
	_name(name), _wrapper(wrapper), _wrapped(wrapped), _owner(owner) {
}

struct MethodWrapper : public Object {
	Ref<WrapperDescr> _descr;
	ObjRef _self;
	MethodWrapper(WrapperDescr *d, Object *self) : Object(&g_methodWrapperType), _descr(d), _self(self) {}
};

static bool descrCheck(Object *descr, Type *owner, const char *name, Object *obj) {
	if (!isInstanceOf(obj, owner)) {
		setErrorFormat(g_excTypeError, "descriptor '%.200s' for '%.100s' objects doesn't apply to '%.100s' object",
		               name, owner->_name.c_str(), typeName(obj));
		return false;
	}
	return true;
}

static ObjRef methodDescrGet(Object *d, Object *obj, Object *type) {
	MethodDescr *m = (MethodDescr *)d;
	if (m->_def->flags & kMethClass) {
		Object *t = type ? type : obj->_type;
		return ObjRef(new BuiltinFunction(m->_def, t));
	}
	if (m->_def->flags & kMethStatic)
		return ObjRef(new BuiltinFunction(m->_def, nullptr));
	if (!obj || obj == g_none)
		return ObjRef(d);
	if (!descrCheck(d, m->_owner, m->_def->name, obj))
		return ObjRef();
	return ObjRef(new BuiltinFunction(m->_def, obj));
}

static ObjRef methodDescrCall(Object *d, Tuple *args, Dict *kw) {
	MethodDescr *m = (MethodDescr *)d;
	if (args->_size < 1) {
		setErrorFormat(g_excTypeError, "descriptor '%.300s' of '%.100s' object needs an argument", m->_def->name, m->_owner->_name.c_str());
		return ObjRef();
	}
	Object *self = args->get(0);
	if (!isInstanceOf(self, m->_owner)) {
		setErrorFormat(g_excTypeError, "descriptor '%.200s' requires a '%.100s' object but received a '%.100s'",
		               m->_def->name, m->_owner->_name.c_str(), typeName(self));
		return ObjRef();
	}
	Ref<Tuple> rest = newTuple(args->_size - 1);
	for (uint32 i = 1; i < args->_size; i++)
		rest->set(i - 1, args->get(i));
	if (!(m->_def->flags & kMethKeywords) && kw && kw->_used) {
		setErrorFormat(g_excTypeError, "%.200s() takes no keyword arguments", m->_def->name);
		return ObjRef();
	}
	return m->_def->func(self, rest.get(), kw);
}

static ObjRef methodDescrRepr(Object *d) {
	MethodDescr *m = (MethodDescr *)d;
	return newStr(Common::String::format("<method '%s' of '%s' objects>", m->_def->name, m->_owner->_name.c_str()));
}

static ObjRef getSetDescrGet(Object *d, Object *obj, Object *) {
	GetSetDescr *g = (GetSetDescr *)d;
	if (!obj || obj == g_none)
		return ObjRef(d);
	if (!descrCheck(d, g->_owner, g->_def->name, obj))
		return ObjRef();
	if (!g->_def->get) {
		setErrorFormat(g_excAttributeError, "attribute '%.300s' of '%.100s' objects is not readable", g->_def->name, g->_owner->_name.c_str());
		return ObjRef();
	}
	return g->_def->get(obj);
}

static int getSetDescrSet(Object *d, Object *obj, Object *value) {
	GetSetDescr *g = (GetSetDescr *)d;
	if (!descrCheck(d, g->_owner, g->_def->name, obj))
		return -1;
	if (!g->_def->set) {
		setErrorFormat(g_excAttributeError, "attribute '%.300s' of '%.100s' objects is not writable", g->_def->name, g->_owner->_name.c_str());
		return -1;
	}
	return g->_def->set(obj, value);
}

static ObjRef getSetDescrRepr(Object *d) {
	GetSetDescr *g = (GetSetDescr *)d;
	return newStr(Common::String::format("<attribute '%s' of '%s' objects>", g->_def->name, g->_owner->_name.c_str()));
}

static ObjRef wrapperDescrGet(Object *d, Object *obj, Object *) {
	WrapperDescr *w = (WrapperDescr *)d;
	if (!obj || obj == g_none)
		return ObjRef(d);
	if (!descrCheck(d, w->_owner, w->_name, obj))
		return ObjRef();
	return ObjRef(new MethodWrapper(w, obj));
}

static ObjRef wrapperDescrCall(Object *d, Tuple *args, Dict *kw) {
	WrapperDescr *w = (WrapperDescr *)d;
	if (args->_size < 1) {
		setErrorFormat(g_excTypeError, "descriptor '%.300s' of '%.100s' object needs an argument", w->_name, w->_owner->_name.c_str());
		return ObjRef();
	}
	Object *self = args->get(0);
	if (!isInstanceOf(self, w->_owner)) {
		setErrorFormat(g_excTypeError, "descriptor '%.200s' requires a '%.100s' object but received a '%.100s'",
		               w->_name, w->_owner->_name.c_str(), typeName(self));
		return ObjRef();
	}
	if (!noKeywords(w->_name, kw) && strcmp(w->_name, "__init__") != 0)
		return ObjRef();
	Ref<Tuple> rest = newTuple(args->_size - 1);
	for (uint32 i = 1; i < args->_size; i++)
		rest->set(i - 1, args->get(i));
	return w->_wrapper(self, rest.get(), w->_wrapped);
}

static ObjRef wrapperDescrRepr(Object *d) {
	WrapperDescr *w = (WrapperDescr *)d;
	return newStr(Common::String::format("<slot wrapper '%s' of '%s' objects>", w->_name, w->_owner->_name.c_str()));
}

static ObjRef methodWrapperCall(Object *o, Tuple *args, Dict *kw) {
	MethodWrapper *m = (MethodWrapper *)o;
	if (kw && kw->_used && strcmp(m->_descr->_name, "__init__") != 0) {
		setErrorFormat(g_excTypeError, "wrapper %s doesn't take keyword arguments", m->_descr->_name);
		return ObjRef();
	}
	return m->_descr->_wrapper(m->_self.get(), args, m->_descr->_wrapped);
}

static ObjRef methodWrapperRepr(Object *o) {
	MethodWrapper *m = (MethodWrapper *)o;
	return newStr(Common::String::format("<method-wrapper object at 0x%08X>", objectAddress(o)));
	(void)m;
}

// ---- Slot wrappers (what int.__add__ and friends call) -----------------------------

static bool wrapArgs(Tuple *args, uint32 n, const char *name) {
	if (args->_size == n)
		return true;
	setErrorFormat(g_excTypeError, "expected %u argument%s, got %u", n, n == 1 ? "" : "s", args->_size);
	(void)name;
	return false;
}

static ObjRef wrapUnary(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 0, ""))
		return ObjRef();
	return ((UnaryFunc)f)(self);
}

static ObjRef wrapBinary(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	return ((BinaryFunc)f)(self, args->get(0));
}

static ObjRef wrapBinaryR(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	return ((BinaryFunc)f)(args->get(0), self);
}

static ObjRef wrapTernary(Object *self, Tuple *args, void *f) {
	if (args->_size < 1 || args->_size > 2) {
		setErrorString(g_excTypeError, "expected 1 or 2 arguments");
		return ObjRef();
	}
	return ((TernaryFunc)f)(self, args->get(0), args->_size > 1 ? args->get(1) : g_none);
}

static ObjRef wrapTernaryR(Object *self, Tuple *args, void *f) {
	if (args->_size < 1 || args->_size > 2) {
		setErrorString(g_excTypeError, "expected 1 or 2 arguments");
		return ObjRef();
	}
	return ((TernaryFunc)f)(args->get(0), self, args->_size > 1 ? args->get(1) : g_none);
}

static ObjRef wrapInquiry(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 0, ""))
		return ObjRef();
	int r = ((InquiryFunc)f)(self);
	if (r < 0 && errorOccurred())
		return ObjRef();
	return newInt(r);
}

static ObjRef wrapLen(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 0, ""))
		return ObjRef();
	int32 r = ((LenFunc)f)(self);
	if (r < 0 && errorOccurred())
		return ObjRef();
	return newInt(r);
}

static ObjRef wrapReprFunc(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 0, ""))
		return ObjRef();
	return ((ReprFunc)f)(self);
}

static ObjRef wrapHash(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 0, ""))
		return ObjRef();
	int32 h;
	if (!((HashFunc)f)(self, &h))
		return ObjRef();
	return newInt(h);
}

static ObjRef wrapCall(Object *self, Tuple *args, void *f) {
	return ((CallFunc)f)(self, args, nullptr);
}

static ObjRef wrapCmp(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	Object *other = args->get(0);
	if (other->_type->compare != (CmpFunc)f && !isSubtype(other->_type, self->_type)) {
		setErrorString(g_excTypeError, "__cmp__ requires objects of the same type");
		return ObjRef();
	}
	int r = ((CmpFunc)f)(self, other);
	if (errorOccurred())
		return ObjRef();
	return newInt(r);
}

#define RICHCMP_WRAPPER(name, op) \
	static ObjRef name(Object *self, Tuple *args, void *f) { \
		if (!wrapArgs(args, 1, "")) \
			return ObjRef(); \
		return ((RichCmpFunc)f)(self, args->get(0), op); \
	}
RICHCMP_WRAPPER(wrapLt, kLT)
RICHCMP_WRAPPER(wrapLe, kLE)
RICHCMP_WRAPPER(wrapEq, kEQ)
RICHCMP_WRAPPER(wrapNe, kNE)
RICHCMP_WRAPPER(wrapGt, kGT)
RICHCMP_WRAPPER(wrapGe, kGE)

static ObjRef wrapGetAttr(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	return ((GetAttrFunc)f)(self, name);
}

static ObjRef wrapSetAttr(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 2, ""))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	if (((SetAttrFunc)f)(self, name, args->get(1)) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapDelAttr(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	if (((SetAttrFunc)f)(self, name, nullptr) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapNext(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 0, ""))
		return ObjRef();
	ObjRef r = ((UnaryFunc)f)(self);
	if (!r && !errorOccurred())
		setError(g_excStopIteration, g_none);
	return r;
}

static ObjRef wrapDescrGetW(Object *self, Tuple *args, void *f) {
	if (args->_size < 1 || args->_size > 2) {
		setErrorString(g_excTypeError, "expected 1 or 2 arguments");
		return ObjRef();
	}
	Object *obj = args->get(0);
	Object *type = args->_size > 1 ? args->get(1) : nullptr;
	if (obj == g_none)
		obj = nullptr;
	if (type == g_none)
		type = nullptr;
	return ((DescrGetFunc)f)(self, obj, type);
}

static ObjRef wrapDescrSetW(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 2, ""))
		return ObjRef();
	if (((DescrSetFunc)f)(self, args->get(0), args->get(1)) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapInit(Object *self, Tuple *args, void *f) {
	if (((InitFunc)f)(self, args, nullptr) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapIndexArg(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	int32 i;
	if (!asInt(args->get(0), &i))
		return ObjRef();
	return ((IntArgFunc)f)(self, i);
}

static ObjRef wrapItem(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	int32 i;
	if (!asInt(args->get(0), &i))
		return ObjRef();
	if (i < 0 && self->_type->asSequence && self->_type->asSequence->length) {
		int32 n = self->_type->asSequence->length(self);
		if (n < 0)
			return ObjRef();
		i += n;
	}
	return ((IntArgFunc)f)(self, i);
}

static ObjRef wrapSlice(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 2, ""))
		return ObjRef();
	int32 i, j;
	if (!asInt(args->get(0), &i) || !asInt(args->get(1), &j))
		return ObjRef();
	return ((IntIntArgFunc)f)(self, i, j);
}

static ObjRef wrapSqSetItem(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 2, ""))
		return ObjRef();
	int32 i;
	if (!asInt(args->get(0), &i))
		return ObjRef();
	if (((IntObjArgProc)f)(self, i, args->get(1)) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapSqDelItem(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	int32 i;
	if (!asInt(args->get(0), &i))
		return ObjRef();
	if (((IntObjArgProc)f)(self, i, nullptr) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapSetSlice(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 3, ""))
		return ObjRef();
	int32 i, j;
	if (!asInt(args->get(0), &i) || !asInt(args->get(1), &j))
		return ObjRef();
	if (((IntIntObjArgProc)f)(self, i, j, args->get(2)) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapDelSlice(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 2, ""))
		return ObjRef();
	int32 i, j;
	if (!asInt(args->get(0), &i) || !asInt(args->get(1), &j))
		return ObjRef();
	if (((IntIntObjArgProc)f)(self, i, j, nullptr) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapContains(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	int r = ((ObjObjProc)f)(self, args->get(0));
	if (r < 0)
		return ObjRef();
	return newInt(r);
}

static ObjRef wrapObjObjArg(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 2, ""))
		return ObjRef();
	if (((ObjObjArgProc)f)(self, args->get(0), args->get(1)) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapDelItem(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	if (((ObjObjArgProc)f)(self, args->get(0), nullptr) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef wrapCoerce(Object *self, Tuple *args, void *f) {
	if (!wrapArgs(args, 1, ""))
		return ObjRef();
	ObjRef a(self), b(args->get(0));
	int r = ((CoerceFunc)f)(&a, &b);
	if (r < 0)
		return ObjRef();
	if (r > 0)
		return ObjRef(g_notImplemented);
	return tuplePack(2, a.get(), b.get());
}

// ---- Slot definitions --------------------------------------------------------------

// Where a slot lives inside a Type: in the type itself or one of its method tables
enum SlotTable {
	kTableType, kTableNumber, kTableSequence, kTableMapping
};

struct SlotDef {
	const char *name;
	SlotTable table;
	size_t offset;
	void *slotFunc; // the dispatcher used when a Python class defines the method
	WrapperFunc wrapper;
};

#define TOFF(field) SlotTable(kTableType), ((size_t)&((Type *)nullptr)->field)
#define NOFF(field) SlotTable(kTableNumber), ((size_t)&((NumberMethods *)nullptr)->field)
#define SOFF(field) SlotTable(kTableSequence), ((size_t)&((SequenceMethods *)nullptr)->field)
#define MOFF(field) SlotTable(kTableMapping), ((size_t)&((MappingMethods *)nullptr)->field)

static void **slotPtr(Type *t, const SlotDef *d, bool create) {
	char *base = nullptr;
	switch (d->table) {
	case kTableType:
		base = (char *)t;
		break;
	case kTableNumber:
		if (!t->asNumber && create)
			t->asNumber = new NumberMethods();
		base = (char *)t->asNumber;
		break;
	case kTableSequence:
		if (!t->asSequence && create)
			t->asSequence = new SequenceMethods();
		base = (char *)t->asSequence;
		break;
	case kTableMapping:
		if (!t->asMapping && create)
			t->asMapping = new MappingMethods();
		base = (char *)t->asMapping;
		break;
	}
	if (!base)
		return nullptr;
	return (void **)(base + d->offset);
}

// Calls a special method looked up on the object's type, as CPython's call_method
static ObjRef lookupMaybe(Object *self, const char *name) {
	Ref<Str> n = internStr(name);
	ObjRef res = lookupType(self->_type, n.get());
	if (res) {
		DescrGetFunc f = res->_type->descrGet;
		if (f)
			return f(res.get(), self, self->_type);
	}
	return res;
}

static ObjRef lookupMethod(Object *self, const char *name) {
	ObjRef r = lookupMaybe(self, name);
	if (!r && !errorOccurred())
		setErrorString(g_excAttributeError, name);
	return r;
}

static ObjRef callMethodV(Object *self, const char *name, uint32 n, Object *a = nullptr, Object *b = nullptr) {
	ObjRef func = lookupMethod(self, name);
	if (!func)
		return ObjRef();
	if (n == 0)
		return callObject(func.get(), nullptr);
	if (n == 1)
		return callFunction(func.get(), 1, a);
	return callFunction(func.get(), 2, a, b);
}

// Like callMethodV, but a missing method gives NotImplemented
static ObjRef callMaybe(Object *self, const char *name, Object *a, Object *b = nullptr) {
	ObjRef func = lookupMaybe(self, name);
	if (!func) {
		if (errorOccurred())
			return ObjRef();
		return ObjRef(g_notImplemented);
	}
	if (b)
		return callFunction(func.get(), 2, a, b);
	return callFunction(func.get(), 1, a);
}

#define SLOT0(fname, opname) \
	static ObjRef fname(Object *self) { return callMethodV(self, opname, 0); }
#define SLOT1(fname, opname) \
	static ObjRef fname(Object *self, Object *other) { return callMethodV(self, opname, 1, other); }

SLOT0(slotNbNegative, "__neg__")
SLOT0(slotNbPositive, "__pos__")
SLOT0(slotNbAbsolute, "__abs__")
SLOT0(slotNbInvert, "__invert__")
SLOT0(slotNbInt, "__int__")
SLOT0(slotNbLong, "__long__")
SLOT0(slotNbFloat, "__float__")
SLOT0(slotNbOct, "__oct__")
SLOT0(slotNbHex, "__hex__")
SLOT1(slotNbInplaceAdd, "__iadd__")
SLOT1(slotNbInplaceSubtract, "__isub__")
SLOT1(slotNbInplaceMultiply, "__imul__")
SLOT1(slotNbInplaceDivide, "__idiv__")
SLOT1(slotNbInplaceRemainder, "__imod__")
SLOT1(slotNbInplaceLshift, "__ilshift__")
SLOT1(slotNbInplaceRshift, "__irshift__")
SLOT1(slotNbInplaceAnd, "__iand__")
SLOT1(slotNbInplaceXor, "__ixor__")
SLOT1(slotNbInplaceOr, "__ior__")
SLOT1(slotNbInplaceFloorDivide, "__ifloordiv__")
SLOT1(slotNbInplaceTrueDivide, "__itruediv__")

static ObjRef slotNbInplacePower(Object *self, Object *a, Object *) {
	return callMethodV(self, "__ipow__", 1, a);
}

// The binary operator dispatcher for Python classes (CPython's SLOT1BINFULL)
#define SLOT1BIN(fname, slotfield, opname, ropname) \
	static ObjRef fname(Object *self, Object *other) { \
		bool doOther = self->_type != other->_type && other->_type->asNumber && \
		               other->_type->asNumber->slotfield == fname; \
		if (self->_type->asNumber && self->_type->asNumber->slotfield == fname) { \
			if (doOther && isSubtype(other->_type, self->_type)) { \
				ObjRef r = callMaybe(other, ropname, self); \
				if (!r || r.get() != g_notImplemented) \
					return r; \
				doOther = false; \
			} \
			ObjRef r = callMaybe(self, opname, other); \
			if (!r || r.get() != g_notImplemented || other->_type == self->_type) \
				return r; \
		} \
		if (doOther) \
			return callMaybe(other, ropname, self); \
		return ObjRef(g_notImplemented); \
	}

SLOT1BIN(slotNbAdd, add, "__add__", "__radd__")
SLOT1BIN(slotNbSubtract, subtract, "__sub__", "__rsub__")
SLOT1BIN(slotNbMultiply, multiply, "__mul__", "__rmul__")
SLOT1BIN(slotNbDivide, divide, "__div__", "__rdiv__")
SLOT1BIN(slotNbRemainder, remainder, "__mod__", "__rmod__")
SLOT1BIN(slotNbDivmod, divmod, "__divmod__", "__rdivmod__")
SLOT1BIN(slotNbLshift, lshift, "__lshift__", "__rlshift__")
SLOT1BIN(slotNbRshift, rshift, "__rshift__", "__rrshift__")
SLOT1BIN(slotNbAnd, and_, "__and__", "__rand__")
SLOT1BIN(slotNbXor, xor_, "__xor__", "__rxor__")
SLOT1BIN(slotNbOr, or_, "__or__", "__ror__")
SLOT1BIN(slotNbFloorDivide, floorDivide, "__floordiv__", "__rfloordiv__")
SLOT1BIN(slotNbTrueDivide, trueDivide, "__truediv__", "__rtruediv__")

static ObjRef slotNbPowerBinary(Object *self, Object *other);
SLOT1BIN(slotNbPowerBinary, divmod, "__pow__", "__rpow__")

static ObjRef slotNbPower(Object *self, Object *other, Object *modulus) {
	if (modulus == g_none)
		return slotNbPowerBinary(self, other);
	if (self->_type->asNumber && self->_type->asNumber->power == slotNbPower)
		return callMaybe(self, "__pow__", other, modulus);
	return ObjRef(g_notImplemented);
}

static int slotNbNonzero(Object *self) {
	ObjRef func = lookupMaybe(self, "__nonzero__");
	if (!func) {
		if (errorOccurred())
			return -1;
		func = lookupMaybe(self, "__len__");
		if (!func)
			return errorOccurred() ? -1 : 1;
	}
	ObjRef res = callObject(func.get(), nullptr);
	if (!res)
		return -1;
	return isTrue(res.get());
}

static int slotNbCoerce(ObjRef *pa, ObjRef *pb) {
	Object *self = pa->get(), *other = pb->get();
	if (self->_type->asNumber && self->_type->asNumber->coerce == slotNbCoerce) {
		ObjRef r = callMaybe(self, "__coerce__", other);
		if (!r)
			return -1;
		if (r.get() != g_notImplemented) {
			if (!isInstanceOf(r.get(), &g_tupleType) || ((Tuple *)r.get())->_size != 2) {
				setErrorString(g_excTypeError, "__coerce__ didn't return a 2-tuple");
				return -1;
			}
			*pa = ObjRef(((Tuple *)r.get())->get(0));
			*pb = ObjRef(((Tuple *)r.get())->get(1));
			return 0;
		}
	}
	if (other->_type->asNumber && other->_type->asNumber->coerce == slotNbCoerce) {
		ObjRef r = callMaybe(other, "__coerce__", self);
		if (!r)
			return -1;
		if (r.get() != g_notImplemented) {
			if (!isInstanceOf(r.get(), &g_tupleType) || ((Tuple *)r.get())->_size != 2) {
				setErrorString(g_excTypeError, "__coerce__ didn't return a 2-tuple");
				return -1;
			}
			*pa = ObjRef(((Tuple *)r.get())->get(1));
			*pb = ObjRef(((Tuple *)r.get())->get(0));
			return 0;
		}
	}
	return 1;
}

static int32 slotSqLength(Object *self) {
	ObjRef res = callMethodV(self, "__len__", 0);
	if (!res)
		return -1;
	int32 len;
	if (!asInt(res.get(), &len))
		return -1;
	if (len < 0) {
		setErrorString(g_excValueError, "__len__() should return >= 0");
		return -1;
	}
	return len;
}

static ObjRef slotSqItem(Object *self, int32 i) {
	Ref<Int> idx = newInt(i);
	return callMethodV(self, "__getitem__", 1, idx.get());
}

static ObjRef slotSqSlice(Object *self, int32 i, int32 j) {
	Ref<Int> a = newInt(i), b = newInt(j);
	return callMethodV(self, "__getslice__", 2, a.get(), b.get());
}

static int slotSqAssItem(Object *self, int32 i, Object *value) {
	Ref<Int> idx = newInt(i);
	ObjRef r = value ? callMethodV(self, "__setitem__", 2, idx.get(), value) : callMethodV(self, "__delitem__", 1, idx.get());
	return r ? 0 : -1;
}

static int slotSqAssSlice(Object *self, int32 i, int32 j, Object *value) {
	Ref<Int> a = newInt(i), b = newInt(j);
	ObjRef r;
	if (value) {
		ObjRef func = lookupMethod(self, "__setslice__");
		if (!func)
			return -1;
		r = callFunction(func.get(), 3, a.get(), b.get(), value);
	} else {
		r = callMethodV(self, "__delslice__", 2, a.get(), b.get());
	}
	return r ? 0 : -1;
}

static int slotSqContains(Object *self, Object *value) {
	ObjRef func = lookupMaybe(self, "__contains__");
	if (func) {
		ObjRef res = callFunction(func.get(), 1, value);
		if (!res)
			return -1;
		return isTrue(res.get());
	}
	if (errorOccurred())
		return -1;
	ObjRef it = getIter(self);
	if (!it)
		return -1;
	for (;;) {
		ObjRef item = iterNext(it.get());
		if (!item)
			return errorOccurred() ? -1 : 0;
		int c = richCompareBool(value, item.get(), kEQ);
		if (c != 0)
			return c;
	}
}

static ObjRef slotMpSubscript(Object *self, Object *key) {
	return callMethodV(self, "__getitem__", 1, key);
}

static int slotMpAssSubscript(Object *self, Object *key, Object *value) {
	ObjRef r = value ? callMethodV(self, "__setitem__", 2, key, value) : callMethodV(self, "__delitem__", 1, key);
	return r ? 0 : -1;
}

static int halfCompare(Object *self, Object *other) {
	ObjRef func = lookupMaybe(self, "__cmp__");
	if (func) {
		ObjRef res = callFunction(func.get(), 1, other);
		if (!res)
			return -2;
		if (res.get() != g_notImplemented) {
			int32 c;
			if (!asInt(res.get(), &c))
				return -2;
			return c < 0 ? -1 : c > 0 ? 1 : 0;
		}
	} else if (errorOccurred()) {
		return -2;
	}
	return 2;
}

int slotCompare(Object *self, Object *other) {
	int c;
	if (self->_type->compare == slotCompare) {
		c = halfCompare(self, other);
		if (c <= 1)
			return c;
	}
	if (other->_type->compare == slotCompare) {
		c = halfCompare(other, self);
		if (c < -1)
			return -2;
		if (c <= 1)
			return -c;
	}
	uint32 a = objectAddress(self), b = objectAddress(other);
	return a < b ? -1 : a > b ? 1 : 0;
}

static ObjRef slotTpRepr(Object *self) {
	ObjRef func = lookupMaybe(self, "__repr__");
	if (func)
		return callObject(func.get(), nullptr);
	if (errorOccurred())
		return ObjRef();
	return newStr(Common::String::format("<%s object at 0x%08X>", typeName(self), objectAddress(self)));
}

static ObjRef slotTpStr(Object *self) {
	ObjRef func = lookupMaybe(self, "__str__");
	if (func)
		return callObject(func.get(), nullptr);
	if (errorOccurred())
		return ObjRef();
	return slotTpRepr(self);
}

static bool slotTpHash(Object *self, int32 *out) {
	ObjRef func = lookupMaybe(self, "__hash__");
	if (func) {
		ObjRef res = callObject(func.get(), nullptr);
		if (!res)
			return false;
		int32 h;
		if (!asInt(res.get(), &h))
			return false;
		*out = h == -1 ? -2 : h;
		return true;
	}
	clearError();
	func = lookupMaybe(self, "__eq__");
	if (!func) {
		clearError();
		func = lookupMaybe(self, "__cmp__");
	}
	if (func) {
		setErrorString(g_excTypeError, "unhashable type");
		return false;
	}
	clearError();
	*out = hashPointer(self);
	return true;
}

static ObjRef slotTpCall(Object *self, Tuple *args, Dict *kw) {
	ObjRef meth = lookupMethod(self, "__call__");
	if (!meth)
		return ObjRef();
	return call(meth.get(), args, kw);
}

// Plain dispatch to __getattribute__
static ObjRef slotTpGetattro(Object *self, Str *name) {
	return callMethodV(self, "__getattribute__", 1, name);
}

static ObjRef slotTpGetattrHook(Object *self, Str *name) {
	Type *tp = self->_type;
	Ref<Str> getattrStr = internStr("__getattr__");
	Ref<Str> getattributeStr = internStr("__getattribute__");
	ObjRef getattr = lookupType(tp, getattrStr.get());
	if (!getattr)
		return slotTpGetattro(self, name);
	ObjRef getattribute = lookupType(tp, getattributeStr.get());
	ObjRef res;
	if (!getattribute || (isType(getattribute.get(), &g_wrapperDescrType) &&
	                      ((WrapperDescr *)getattribute.get())->_wrapped == (void *)genericGetAttr))
		res = genericGetAttr(self, name);
	else
		res = callFunction(getattribute.get(), 2, self, name);
	if (!res && errorMatches(g_excAttributeError)) {
		clearError();
		res = callFunction(getattr.get(), 2, self, name);
	}
	return res;
}

static int slotTpSetattro(Object *self, Str *name, Object *value) {
	ObjRef r = value ? callMethodV(self, "__setattr__", 2, name, value) : callMethodV(self, "__delattr__", 1, name);
	return r ? 0 : -1;
}

static const char *const g_richOpNames[] = { "__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__" };

static ObjRef halfRichCompareSlot(Object *self, Object *other, int op) {
	ObjRef func = lookupMaybe(self, g_richOpNames[op]);
	if (!func) {
		if (errorOccurred())
			return ObjRef();
		return ObjRef(g_notImplemented);
	}
	return callFunction(func.get(), 1, other);
}

static ObjRef slotTpRichCompare(Object *self, Object *other, int op);

static const int g_swappedRich[] = { kGT, kGE, kEQ, kNE, kLT, kLE };

static ObjRef slotTpRichCompare(Object *self, Object *other, int op) {
	if (self->_type->richCompare == slotTpRichCompare) {
		ObjRef res = halfRichCompareSlot(self, other, op);
		if (!res || res.get() != g_notImplemented)
			return res;
	}
	if (other->_type->richCompare == slotTpRichCompare) {
		ObjRef res = halfRichCompareSlot(other, self, g_swappedRich[op]);
		if (!res || res.get() != g_notImplemented)
			return res;
	}
	return ObjRef(g_notImplemented);
}

static ObjRef slotTpIter(Object *self) {
	ObjRef func = lookupMaybe(self, "__iter__");
	if (func)
		return callObject(func.get(), nullptr);
	if (errorOccurred())
		return ObjRef();
	func = lookupMaybe(self, "__getitem__");
	if (!func) {
		setErrorString(g_excTypeError, "iteration over non-sequence");
		return ObjRef();
	}
	return ObjRef(new SeqIter(self));
}

static ObjRef slotTpIternext(Object *self) {
	ObjRef r = callMethodV(self, "next", 0);
	if (!r && errorMatches(g_excStopIteration))
		clearError();
	return r;
}

static ObjRef slotTpDescrGet(Object *self, Object *obj, Object *type) {
	ObjRef get = lookupType(self->_type, internStr("__get__").get());
	if (!get) {
		// Avoid further slowdowns
		return ObjRef(self);
	}
	return callFunction(get.get(), 3, self, obj ? obj : g_none, type ? type : g_none);
}

static int slotTpDescrSet(Object *self, Object *target, Object *value) {
	ObjRef r = value ? callMethodV(self, "__set__", 2, target, value) : callMethodV(self, "__delete__", 1, target);
	return r ? 0 : -1;
}

static int slotTpInit(Object *self, Tuple *args, Dict *kw) {
	ObjRef meth = lookupMethod(self, "__init__");
	if (!meth)
		return -1;
	ObjRef res = call(meth.get(), args, kw);
	if (!res)
		return -1;
	if (res.get() != g_none) {
		setErrorString(g_excTypeError, "__init__() should return None");
		return -1;
	}
	return 0;
}

static ObjRef slotTpNew(Type *type, Tuple *args, Dict *kw) {
	ObjRef func = getAttrString(type, "__new__");
	if (!func)
		return ObjRef();
	Ref<Tuple> newArgs = newTuple(args->_size + 1);
	newArgs->set(0, type);
	for (uint32 i = 0; i < args->_size; i++)
		newArgs->set(i + 1, args->get(i));
	return call(func.get(), newArgs.get(), kw);
}

static ObjRef wrapNewPlaceholder(Object *, Tuple *, void *) { return ObjRef(); }

static const SlotDef g_slotDefs[] = {
	{ "__len__", SlotTable(kTableSequence), ((size_t)&((SequenceMethods *)nullptr)->length), (void *)slotSqLength, wrapLen },
	{ "__add__", SOFF(concat), nullptr, wrapBinary },
	{ "__mul__", SOFF(repeat), nullptr, wrapIndexArg },
	{ "__rmul__", SOFF(repeat), nullptr, wrapIndexArg },
	{ "__getitem__", SOFF(item), (void *)slotSqItem, wrapItem },
	{ "__getslice__", SOFF(slice), (void *)slotSqSlice, wrapSlice },
	{ "__setitem__", SOFF(assItem), (void *)slotSqAssItem, wrapSqSetItem },
	{ "__delitem__", SOFF(assItem), (void *)slotSqAssItem, wrapSqDelItem },
	{ "__setslice__", SOFF(assSlice), (void *)slotSqAssSlice, wrapSetSlice },
	{ "__delslice__", SOFF(assSlice), (void *)slotSqAssSlice, wrapDelSlice },
	{ "__contains__", SOFF(contains), (void *)slotSqContains, wrapContains },
	{ "__iadd__", SOFF(inplaceConcat), nullptr, wrapBinary },
	{ "__imul__", SOFF(inplaceRepeat), nullptr, wrapIndexArg },

	{ "__len__", MOFF(length), (void *)slotSqLength, wrapLen },
	{ "__getitem__", MOFF(subscript), (void *)slotMpSubscript, wrapBinary },
	{ "__setitem__", MOFF(assSubscript), (void *)slotMpAssSubscript, wrapObjObjArg },
	{ "__delitem__", MOFF(assSubscript), (void *)slotMpAssSubscript, wrapDelItem },

	{ "__add__", NOFF(add), (void *)slotNbAdd, wrapBinary },
	{ "__radd__", NOFF(add), (void *)slotNbAdd, wrapBinaryR },
	{ "__sub__", NOFF(subtract), (void *)slotNbSubtract, wrapBinary },
	{ "__rsub__", NOFF(subtract), (void *)slotNbSubtract, wrapBinaryR },
	{ "__mul__", NOFF(multiply), (void *)slotNbMultiply, wrapBinary },
	{ "__rmul__", NOFF(multiply), (void *)slotNbMultiply, wrapBinaryR },
	{ "__div__", NOFF(divide), (void *)slotNbDivide, wrapBinary },
	{ "__rdiv__", NOFF(divide), (void *)slotNbDivide, wrapBinaryR },
	{ "__mod__", NOFF(remainder), (void *)slotNbRemainder, wrapBinary },
	{ "__rmod__", NOFF(remainder), (void *)slotNbRemainder, wrapBinaryR },
	{ "__divmod__", NOFF(divmod), (void *)slotNbDivmod, wrapBinary },
	{ "__rdivmod__", NOFF(divmod), (void *)slotNbDivmod, wrapBinaryR },
	{ "__pow__", NOFF(power), (void *)slotNbPower, wrapTernary },
	{ "__rpow__", NOFF(power), (void *)slotNbPower, wrapTernaryR },
	{ "__neg__", NOFF(negative), (void *)slotNbNegative, wrapUnary },
	{ "__pos__", NOFF(positive), (void *)slotNbPositive, wrapUnary },
	{ "__abs__", NOFF(absolute), (void *)slotNbAbsolute, wrapUnary },
	{ "__nonzero__", NOFF(nonzero), (void *)slotNbNonzero, wrapInquiry },
	{ "__invert__", NOFF(invert), (void *)slotNbInvert, wrapUnary },
	{ "__lshift__", NOFF(lshift), (void *)slotNbLshift, wrapBinary },
	{ "__rlshift__", NOFF(lshift), (void *)slotNbLshift, wrapBinaryR },
	{ "__rshift__", NOFF(rshift), (void *)slotNbRshift, wrapBinary },
	{ "__rrshift__", NOFF(rshift), (void *)slotNbRshift, wrapBinaryR },
	{ "__and__", NOFF(and_), (void *)slotNbAnd, wrapBinary },
	{ "__rand__", NOFF(and_), (void *)slotNbAnd, wrapBinaryR },
	{ "__xor__", NOFF(xor_), (void *)slotNbXor, wrapBinary },
	{ "__rxor__", NOFF(xor_), (void *)slotNbXor, wrapBinaryR },
	{ "__or__", NOFF(or_), (void *)slotNbOr, wrapBinary },
	{ "__ror__", NOFF(or_), (void *)slotNbOr, wrapBinaryR },
	{ "__coerce__", NOFF(coerce), (void *)slotNbCoerce, wrapCoerce },
	{ "__int__", NOFF(toInt), (void *)slotNbInt, wrapUnary },
	{ "__long__", NOFF(toLong), (void *)slotNbLong, wrapUnary },
	{ "__float__", NOFF(toFloat), (void *)slotNbFloat, wrapUnary },
	{ "__oct__", NOFF(toOct), (void *)slotNbOct, wrapUnary },
	{ "__hex__", NOFF(toHex), (void *)slotNbHex, wrapUnary },
	{ "__iadd__", NOFF(inplaceAdd), (void *)slotNbInplaceAdd, wrapBinary },
	{ "__isub__", NOFF(inplaceSubtract), (void *)slotNbInplaceSubtract, wrapBinary },
	{ "__imul__", NOFF(inplaceMultiply), (void *)slotNbInplaceMultiply, wrapBinary },
	{ "__idiv__", NOFF(inplaceDivide), (void *)slotNbInplaceDivide, wrapBinary },
	{ "__imod__", NOFF(inplaceRemainder), (void *)slotNbInplaceRemainder, wrapBinary },
	{ "__ipow__", NOFF(inplacePower), (void *)slotNbInplacePower, wrapBinary },
	{ "__ilshift__", NOFF(inplaceLshift), (void *)slotNbInplaceLshift, wrapBinary },
	{ "__irshift__", NOFF(inplaceRshift), (void *)slotNbInplaceRshift, wrapBinary },
	{ "__iand__", NOFF(inplaceAnd), (void *)slotNbInplaceAnd, wrapBinary },
	{ "__ixor__", NOFF(inplaceXor), (void *)slotNbInplaceXor, wrapBinary },
	{ "__ior__", NOFF(inplaceOr), (void *)slotNbInplaceOr, wrapBinary },
	{ "__floordiv__", NOFF(floorDivide), (void *)slotNbFloorDivide, wrapBinary },
	{ "__rfloordiv__", NOFF(floorDivide), (void *)slotNbFloorDivide, wrapBinaryR },
	{ "__truediv__", NOFF(trueDivide), (void *)slotNbTrueDivide, wrapBinary },
	{ "__rtruediv__", NOFF(trueDivide), (void *)slotNbTrueDivide, wrapBinaryR },
	{ "__ifloordiv__", NOFF(inplaceFloorDivide), (void *)slotNbInplaceFloorDivide, wrapBinary },
	{ "__itruediv__", NOFF(inplaceTrueDivide), (void *)slotNbInplaceTrueDivide, wrapBinary },

	{ "__str__", TOFF(str), (void *)slotTpStr, wrapReprFunc },
	{ "__repr__", TOFF(repr), (void *)slotTpRepr, wrapReprFunc },
	{ "__cmp__", TOFF(compare), (void *)slotCompare, wrapCmp },
	{ "__hash__", TOFF(hash), (void *)slotTpHash, wrapHash },
	{ "__call__", TOFF(call), (void *)slotTpCall, wrapCall },
	{ "__getattribute__", TOFF(getAttr), (void *)slotTpGetattrHook, wrapGetAttr },
	{ "__getattr__", TOFF(getAttr), (void *)slotTpGetattrHook, nullptr },
	{ "__setattr__", TOFF(setAttr), (void *)slotTpSetattro, wrapSetAttr },
	{ "__delattr__", TOFF(setAttr), (void *)slotTpSetattro, wrapDelAttr },
	{ "__lt__", TOFF(richCompare), (void *)slotTpRichCompare, wrapLt },
	{ "__le__", TOFF(richCompare), (void *)slotTpRichCompare, wrapLe },
	{ "__eq__", TOFF(richCompare), (void *)slotTpRichCompare, wrapEq },
	{ "__ne__", TOFF(richCompare), (void *)slotTpRichCompare, wrapNe },
	{ "__gt__", TOFF(richCompare), (void *)slotTpRichCompare, wrapGt },
	{ "__ge__", TOFF(richCompare), (void *)slotTpRichCompare, wrapGe },
	{ "__iter__", TOFF(iter), (void *)slotTpIter, wrapUnary },
	{ "next", TOFF(iterNext), (void *)slotTpIternext, wrapNext },
	{ "__get__", TOFF(descrGet), (void *)slotTpDescrGet, wrapDescrGetW },
	{ "__set__", TOFF(descrSet), (void *)slotTpDescrSet, wrapDescrSetW },
	{ "__delete__", TOFF(descrSet), (void *)slotTpDescrSet, nullptr },
	{ "__init__", TOFF(init), (void *)slotTpInit, wrapInit },
	{ "__new__", TOFF(newObj), (void *)slotTpNew, wrapNewPlaceholder },
	{ nullptr, kTableType, 0, nullptr, nullptr }
};

// Adds wrapper descriptors for the slots a native type defines itself
static void addOperators(Type *t) {
	Dict *dict = t->_dict.get();
	for (const SlotDef *d = g_slotDefs; d->name; d++) {
		if (!d->wrapper || d->wrapper == wrapNewPlaceholder)
			continue;
		void **ptr = slotPtr(t, d, false);
		if (!ptr || !*ptr)
			continue;
		if (dict->getItemString(d->name))
			continue;
		ObjRef w(new WrapperDescr(d->name, d->wrapper, *ptr, t));
		dict->setItemString(d->name, w.get());
	}
}

// Points each slot of a Python class at the right function, as CPython's update_one_slot:
// a native implementation when the method found is that native slot's wrapper,
// otherwise the generic dispatcher for the special method
static void updateSlots(Type *t) {
	for (const SlotDef *d = g_slotDefs; d->name;) {
		void **ptr = slotPtr(t, d, true);
		void *generic = nullptr, *specific = nullptr;
		bool useGeneric = false;
		const SlotDef *p = d;
		do {
			Ref<Str> name = internStr(p->name);
			ObjRef descr = lookupType(t, name.get());
			if (!descr) {
				p++;
				continue;
			}
			if (isType(descr.get(), &g_wrapperDescrType) && ((WrapperDescr *)descr.get())->_wrapper == p->wrapper &&
			    slotPtr(((WrapperDescr *)descr.get())->_owner, p, false)) {
				if (specific && specific != ((WrapperDescr *)descr.get())->_wrapped)
					useGeneric = true;
				specific = ((WrapperDescr *)descr.get())->_wrapped;
			} else if (p->name[2] == 'n' && strcmp(p->name, "__new__") == 0 && isType(descr.get(), &g_builtinFunctionType)) {
				// A native type's tp_new wrapper: keep the inherited native slot
				specific = *ptr;
			} else {
				useGeneric = true;
				generic = p->slotFunc;
			}
			p++;
		} while (p->name && p->offset == d->offset && p->table == d->table);
		if (ptr) {
			if (specific && !useGeneric)
				*ptr = specific;
			else if (generic)
				*ptr = generic;
			else
				*ptr = specific;
		}
		d = p;
	}
}

void typeUpdateSlot(Type *t, Str *name) {
	if (t->_flags & kTypeHeap)
		updateSlots(t);
}

// ---- type objects --------------------------------------------------------------

static void inheritSlots(Type *t, Type *base) {
	if (!base)
		return;
#define COPYSLOT(s) if (!t->s) t->s = base->s
	COPYSLOT(repr);
	COPYSLOT(str);
	COPYSLOT(call);
	COPYSLOT(iter);
	COPYSLOT(iterNext);
	COPYSLOT(descrGet);
	COPYSLOT(descrSet);
	COPYSLOT(init);
	COPYSLOT(newObj);
	if (!t->getAttr && !t->setAttr) {
		t->getAttr = base->getAttr;
		t->setAttr = base->setAttr;
	}
	if (!t->compare && !t->richCompare && !t->hash) {
		t->compare = base->compare;
		t->richCompare = base->richCompare;
		t->hash = base->hash;
	}
#undef COPYSLOT
	if (t->_flags & kTypeHeap) {
		// Heap types have their own tables, filled from the base
		if (base->asNumber) {
			if (!t->asNumber)
				t->asNumber = new NumberMethods(*base->asNumber);
		}
		if (base->asSequence) {
			if (!t->asSequence)
				t->asSequence = new SequenceMethods(*base->asSequence);
		}
		if (base->asMapping) {
			if (!t->asMapping)
				t->asMapping = new MappingMethods(*base->asMapping);
		}
	} else {
		if (!t->asNumber)
			t->asNumber = base->asNumber;
		if (!t->asSequence)
			t->asSequence = base->asSequence;
		if (!t->asMapping)
			t->asMapping = base->asMapping;
	}
	if (base->_flags & kTypeCheckTypes)
		t->_flags |= kTypeCheckTypes;
}

void typeInheritSlots(Type *t) {
	if (!t->_mro)
		return;
	for (uint32 i = 1; i < t->_mro->_size; i++) {
		Object *b = t->_mro->get(i);
		if (isInstanceOf(b, &g_typeType))
			inheritSlots(t, (Type *)b);
	}
}

static void classicMroAdd(Ref<List> &result, Object *cls) {
	if (containsItem(result.get(), cls) == 0)
		result->append(cls);
	Ref<Tuple> bases;
	if (isType(cls, &g_classType))
		bases = ((ClassObj *)cls)->_bases;
	for (uint32 i = 0; bases && i < bases->_size; i++)
		classicMroAdd(result, bases->get(i));
}

// CPython 2.2's conservative_merge
static void conservativeMerge(List *left, List *right) {
again:
	uint32 leftSize = left->size(), rightSize = right->size();
	for (uint32 i = 0; i < leftSize; i++) {
		for (uint32 j = 0; j < rightSize; j++) {
			if (left->get(i) == right->get(j)) {
				Ref<List> temp = newList();
				for (uint32 r = 0; r < j; r++) {
					Object *rr = right->get(r);
					if (containsItem(left, rr) == 0)
						temp->append(rr);
				}
				sequenceSetSlice(left, i, i, temp.get());
				sequenceSetSlice(right, 0, j + 1, nullptr);
				goto again;
			}
		}
	}
	sequenceSetSlice(left, leftSize, leftSize, right);
}

Ref<Tuple> computeMro(Type *t) {
	Ref<List> result = newList();
	result->append(t);
	Tuple *bases = t->_bases.get();
	for (uint32 i = 0; bases && i < bases->_size; i++) {
		Object *base = bases->get(i);
		Ref<List> parentMro = newList();
		if (isInstanceOf(base, &g_typeType)) {
			Tuple *m = ((Type *)base)->_mro.get();
			for (uint32 k = 0; m && k < m->_size; k++)
				parentMro->append(m->get(k));
		} else {
			classicMroAdd(parentMro, base);
		}
		conservativeMerge(result.get(), parentMro.get());
	}
	return listAsTuple(result.get()).as<Tuple>();
}

static ObjRef typeNewWrapper(Object *self, Tuple *args, Dict *kw) {
	Type *type = (Type *)self;
	if (args->_size < 1) {
		setErrorFormat(g_excTypeError, "%s.__new__(): not enough arguments", type->_name.c_str());
		return ObjRef();
	}
	Object *arg0 = args->get(0);
	if (!isInstanceOf(arg0, &g_typeType)) {
		setErrorFormat(g_excTypeError, "%s.__new__(X): X is not a type object (%s)", type->_name.c_str(), typeName(arg0));
		return ObjRef();
	}
	Type *subtype = (Type *)arg0;
	if (!isSubtype(subtype, type)) {
		setErrorFormat(g_excTypeError, "%s.__new__(%s): %s is not a subtype of %s",
		               type->_name.c_str(), subtype->_name.c_str(), subtype->_name.c_str(), type->_name.c_str());
		return ObjRef();
	}
	Ref<Tuple> rest = newTuple(args->_size - 1);
	for (uint32 i = 1; i < args->_size; i++)
		rest->set(i - 1, args->get(i));
	if (!type->newObj) {
		setErrorFormat(g_excTypeError, "cannot create '%.100s' instances", type->_name.c_str());
		return ObjRef();
	}
	return type->newObj(subtype, rest.get(), kw);
}

static const MethodDef g_newWrapperDef = { "__new__", typeNewWrapper, kMethKeywords, nullptr };

void typeReady(Type *t) {
	if (t->_flags & kTypeReady)
		return;
	t->_flags |= kTypeReady;
	if (!t->_base && t != &g_objectType)
		t->_base = &g_objectType;
	if (t->_base)
		typeReady(t->_base);
	if (!t->_bases) {
		if (t->_base)
			t->_bases = tuplePack(1, t->_base);
		else
			t->_bases = newTuple(0);
	}
	if (!t->_dict)
		t->_dict = newDict();
	Dict *dict = t->_dict.get();
	if (!(t->_flags & kTypeHeap)) {
		addOperators(t);
		for (const MethodDef *m = t->methods; m && m->name; m++) {
			if (dict->getItemString(m->name))
				continue;
			ObjRef d(new MethodDescr(m, t));
			dict->setItemString(m->name, d.get());
		}
		for (const GetSetDef *g = t->getSets; g && g->name; g++) {
			if (dict->getItemString(g->name))
				continue;
			ObjRef d(new GetSetDescr(g, t));
			dict->setItemString(g->name, d.get());
		}
		if (t->newObj && !dict->getItemString("__new__")) {
			ObjRef f(new BuiltinFunction(&g_newWrapperDef, t));
			dict->setItemString("__new__", f.get());
		}
	}
	if (!dict->getItemString("__doc__"))
		dict->setItemString("__doc__", g_none);
	t->_mro = computeMro(t);
	typeInheritSlots(t);
}

static ObjRef typeGetAttr(Object *o, Str *name);

ObjRef typeRepr(Object *o) {
	Type *t = (Type *)o;
	if (t->_flags & kTypeHeap) {
		Object *mod = t->_dict->getItemString("__module__");
		if (mod && isInstanceOf(mod, &g_strType) && !((Str *)mod)->equals("__builtin__"))
			return newStr(Common::String::format("<class '%s.%s'>", ((Str *)mod)->_data, t->_name.c_str()));
		return newStr(Common::String::format("<class '%s'>", t->_name.c_str()));
	}
	return newStr(Common::String::format("<type '%s'>", t->_name.c_str()));
}

static ObjRef typeGetAttr(Object *o, Str *name) {
	Type *type = (Type *)o;
	if (!(type->_flags & kTypeReady))
		typeReady(type);
	Type *metatype = o->_type;
	// Special attributes
	if (name->equals("__name__"))
		return newStr(type->_name);
	if (name->equals("__dict__"))
		return ObjRef(type->_dict.get()); // CPython wraps this in a read-only proxy
	if (name->equals("__bases__"))
		return ObjRef(type->_bases.get());
	if (name->equals("__base__"))
		return type->_base ? ObjRef(type->_base) : ObjRef(g_none);
	if (name->equals("__mro__"))
		return ObjRef(type->_mro.get());
	if (name->equals("__class__"))
		return ObjRef(metatype);
	if (name->equals("__module__")) {
		Object *mod = type->_dict->getItemString("__module__");
		if (mod)
			return ObjRef(mod);
		return newStr("__builtin__");
	}
	ObjRef metaAttribute = lookupType(metatype, name);
	DescrGetFunc metaGet = nullptr;
	if (metaAttribute) {
		metaGet = metaAttribute->_type->descrGet;
		if (metaGet && metaAttribute->_type->descrSet)
			return metaGet(metaAttribute.get(), o, metatype);
	}
	ObjRef attribute = lookupType(type, name);
	if (attribute) {
		DescrGetFunc localGet = attribute->_type->descrGet;
		if (localGet)
			return localGet(attribute.get(), nullptr, o);
		return attribute;
	}
	if (metaGet)
		return metaGet(metaAttribute.get(), o, metatype);
	if (metaAttribute)
		return metaAttribute;
	setErrorFormat(g_excAttributeError, "type object '%.50s' has no attribute '%.400s'", type->_name.c_str(), name->_data);
	return ObjRef();
}

static int typeSetAttr(Object *o, Str *name, Object *value) {
	Type *type = (Type *)o;
	if (!(type->_flags & kTypeHeap)) {
		setErrorFormat(g_excTypeError, "can't set attributes of built-in/extension type '%s'", type->_name.c_str());
		return -1;
	}
	if (name->equals("__name__")) {
		Str *s = value ? asStr(value) : nullptr;
		if (!s)
			return -1;
		type->_name = s->toString();
		return 0;
	}
	int r = genericSetAttr(o, name, value);
	if (r < 0)
		return r;
	if (name->_len > 4 && name->_data[0] == '_' && name->_data[1] == '_')
		updateSlots(type);
	else if (name->equals("next"))
		updateSlots(type);
	return 0;
}

ObjRef typeCall(Object *o, Tuple *args, Dict *kw) {
	Type *type = (Type *)o;
	if (!type->newObj) {
		setErrorFormat(g_excTypeError, "cannot create '%.100s' instances", type->_name.c_str());
		return ObjRef();
	}
	ObjRef obj = type->newObj(type, args, kw);
	if (!obj)
		return ObjRef();
	if (type == &g_typeType && args->_size == 1 && (!kw || kw->_used == 0))
		return obj;
	if (!isSubtype(obj->_type, type))
		return obj;
	type = obj->_type;
	if (type->init && type->init(obj.get(), args, kw) < 0)
		return ObjRef();
	return obj;
}

static int typeCompare(Object *a, Object *b) {
	uint32 x = objectAddress(a), y = objectAddress(b);
	return x < y ? -1 : x > y ? 1 : 0;
}

static bool typeHash(Object *o, int32 *out) {
	*out = hashPointer(o);
	return true;
}

static ObjRef typeMroMethod(Object *self, Tuple *, Dict *) {
	return sequenceList(((Type *)self)->_mro.get());
}

static const MethodDef g_typeMethods[] = {
	{ "mro", typeMroMethod, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// Instances of 'object' and of Python classes ------------------------------------

HeapObject::HeapObject(Type *t) : Object(t) {
}

HeapObject::~HeapObject() {
}

static ObjRef objectNew(Type *type, Tuple *, Dict *) {
	HeapObject *o = new HeapObject(type);
	return ObjRef(o);
}

static int objectInit(Object *, Tuple *, Dict *) {
	return 0;
}

static ObjRef objectRepr(Object *o) {
	Type *type = o->_type;
	if (type->_flags & kTypeHeap) {
		Object *mod = type->_dict->getItemString("__module__");
		if (mod && isInstanceOf(mod, &g_strType) && !((Str *)mod)->equals("__builtin__"))
			return newStr(Common::String::format("<%s.%s object at 0x%08X>", ((Str *)mod)->_data, type->_name.c_str(), objectAddress(o)));
	}
	return newStr(Common::String::format("<%s object at 0x%08X>", type->_name.c_str(), objectAddress(o)));
}

static ObjRef objectStr(Object *o) {
	ReprFunc f = o->_type->repr;
	if (!f)
		f = objectRepr;
	return f(o);
}

static bool objectHash(Object *o, int32 *out) {
	*out = hashPointer(o);
	return true;
}

static ObjRef objectGetClass(Object *o) {
	return ObjRef(o->_type);
}

static ObjRef objectReduce(Object *self, Tuple *, Dict *) {
	ObjRef copyReg = importModule("copy_reg");
	if (!copyReg)
		return ObjRef();
	return callMethod(copyReg.get(), "_reduce", 1, self);
}

static const MethodDef g_objectMethods[] = {
	{ "__reduce__", objectReduce, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_objectGetSets[] = {
	{ "__class__", objectGetClass, nullptr },
	{ nullptr, nullptr, nullptr }
};

static ObjRef subtypeGetDict(Object *o) {
	Dict *d = o->getDict();
	if (!d) {
		Ref<Dict> nd = newDict();
		o->setDict(nd.get());
		d = nd.get();
	}
	return ObjRef(d);
}

static int subtypeSetDict(Object *o, Object *value) {
	if (!value || !isInstanceOf(value, &g_dictType)) {
		setErrorString(g_excTypeError, "__dict__ must be set to a dictionary");
		return -1;
	}
	o->setDict((Dict *)value);
	return 0;
}

static const GetSetDef g_subtypeGetSets[] = {
	{ "__dict__", subtypeGetDict, subtypeSetDict },
	{ nullptr, nullptr, nullptr }
};

// class statements whose bases are types make new types
ObjRef typeNew(Type *metatype, Tuple *args, Dict *kw) {
	// type(x) returns x's type
	if (metatype == &g_typeType && args->_size == 1 && (!kw || kw->_used == 0)) {
		Object *x = args->get(0);
		if (isType(x, &g_instanceType))
			return ObjRef(x->_type); // 'instance' -- CPython returns the instance type too
		return ObjRef(x->_type);
	}
	if (args->_size != 3 || !isInstanceOf(args->get(0), &g_strType) || !isInstanceOf(args->get(1), &g_tupleType) ||
	    !isInstanceOf(args->get(2), &g_dictType)) {
		setErrorString(g_excTypeError, "type() takes 1 or 3 arguments");
		return ObjRef();
	}
	Str *name = (Str *)args->get(0);
	Tuple *bases = (Tuple *)args->get(1);
	Dict *dict = (Dict *)args->get(2);

	// Determine the proper metatype to deal with this
	Type *winner = metatype;
	for (uint32 i = 0; i < bases->_size; i++) {
		Object *tmp = bases->get(i);
		Type *tmptype = tmp->_type;
		if (tmptype == &g_classType)
			continue;
		if (isSubtype(winner, tmptype))
			continue;
		if (isSubtype(tmptype, winner)) {
			winner = tmptype;
			continue;
		}
		setErrorString(g_excTypeError, "metatype conflict among bases");
		return ObjRef();
	}
	if (winner != metatype && winner->newObj && winner->newObj != typeNew) {
		return winner->newObj(winner, args, kw);
	}
	metatype = winner;

	Ref<Tuple> realBases(bases);
	if (bases->_size == 0)
		realBases = tuplePack(1, &g_objectType);

	// The first base that is a type is the one whose layout is used
	Type *base = nullptr;
	for (uint32 i = 0; i < realBases->_size; i++) {
		Object *b = realBases->get(i);
		if (isInstanceOf(b, &g_typeType)) {
			Type *bt = (Type *)b;
			if (!(bt->_flags & kTypeBaseType)) {
				setErrorFormat(g_excTypeError, "type '%.100s' is not an acceptable base type", bt->_name.c_str());
				return ObjRef();
			}
			if (!base)
				base = bt;
		}
	}
	if (!base) {
		setErrorString(g_excTypeError, "a new-style class can't have only classic bases");
		return ObjRef();
	}

	Type *type = new Type(name->_data, base);
	type->_refs = 0;
	ObjRef rtype(type);
	if (metatype != &g_typeType) {
		type->_type = metatype;
		incRef(metatype);
	}
	type->_flags = kTypeHeap | kTypeBaseType | kTypeHasDict | kTypeCheckTypes;
	type->_bases = realBases;
	type->_heapName = name;
	type->_dict = dictCopy(dict).as<Dict>();
	Dict *tdict = type->_dict.get();

	// Set __module__ in the dict
	if (!tdict->getItemString("__module__")) {
		Dict *globals = getGlobals();
		if (globals) {
			Object *modname = globals->getItemString("__name__");
			if (modname)
				tdict->setItemString("__module__", modname);
		}
	}
	// Special-case __new__: if it's a plain function, make it a static function
	Object *newFunc = tdict->getItemString("__new__");
	if (newFunc && isType(newFunc, &g_functionType)) {
		ObjRef sm(new StaticMethod(newFunc));
		tdict->setItemString("__new__", sm.get());
	}
	// __dict__ for instances (unless a base already gives one)
	if (!tdict->getItemString("__dict__")) {
		ObjRef d(new GetSetDescr(&g_subtypeGetSets[0], type));
		tdict->setItemString("__dict__", d.get());
	}
	type->_flags |= kTypeReady;
	type->_mro = computeMro(type);
	typeInheritSlots(type);
	if (!type->getAttr)
		type->getAttr = genericGetAttr;
	if (!type->setAttr)
		type->setAttr = genericSetAttr;
	updateSlots(type);
	return rtype;
}

// ---- super ---------------------------------------------------------------------

static ObjRef superGetAttr(Object *self, Str *name) {
	Super *su = (Super *)self;
	if (su->_obj) {
		Type *starttype = su->_obj->_type;
		if (isInstanceOf(su->_obj.get(), &g_typeType) && isSubtype((Type *)su->_obj.get(), su->_type))
			starttype = (Type *)su->_obj.get();
		Tuple *mro = starttype->_mro.get();
		uint32 n = mro ? mro->_size : 0, i;
		for (i = 0; i < n; i++)
			if (mro->get(i) == su->_type)
				break;
		if (i >= n && isInstanceOf(su->_obj.get(), &g_typeType)) {
			starttype = (Type *)su->_obj.get();
			mro = starttype->_mro.get();
			n = mro ? mro->_size : 0;
			for (i = 0; i < n; i++)
				if (mro->get(i) == su->_type)
					break;
		}
		i++;
		for (; i < n; i++) {
			Object *tmp = mro->get(i);
			Dict *dict;
			if (isInstanceOf(tmp, &g_typeType))
				dict = ((Type *)tmp)->_dict.get();
			else if (isType(tmp, &g_classType))
				dict = ((ClassObj *)tmp)->_dict.get();
			else
				continue;
			Object *res = dict->getItem(name);
			if (res) {
				DescrGetFunc f = res->_type->descrGet;
				if (f)
					return f(res, su->_obj.get() == starttype ? nullptr : su->_obj.get(), starttype);
				return ObjRef(res);
			}
		}
	}
	return genericGetAttr(self, name);
}

static ObjRef superNew(Type *, Tuple *args, Dict *kw) {
	if (!noKeywords("super", kw) || !checkArgs("super", args, 1, 2))
		return ObjRef();
	if (!isInstanceOf(args->get(0), &g_typeType)) {
		setErrorString(g_excTypeError, "super() argument 1 must be type");
		return ObjRef();
	}
	Super *su = new Super();
	ObjRef r(su);
	su->_type = (Type *)args->get(0);
	if (args->_size > 1 && args->get(1) != g_none) {
		Object *obj = args->get(1);
		bool ok = isInstanceOf(obj, su->_type) ||
		          (isInstanceOf(obj, &g_typeType) && isSubtype((Type *)obj, su->_type));
		if (!ok) {
			setErrorString(g_excTypeError, "super(type, obj): obj must be an instance or subtype of type");
			return ObjRef();
		}
		su->_obj = obj;
	}
	return r;
}

static ObjRef superRepr(Object *o) {
	Super *su = (Super *)o;
	if (su->_obj)
		return newStr(Common::String::format("<super: <class '%s'>, <%s object>>", su->_type->_name.c_str(), typeName(su->_obj.get())));
	return newStr(Common::String::format("<super: <class '%s'>, NULL>", su->_type->_name.c_str()));
}

// ---- Initialisation -----------------------------------------------------------

void initIntType();
void initLongType();
void initFloatType();
void initStrType();
void initUnicodeType();
void initDictType();
void initSequenceTypes();
void initClassTypes();
void initFuncTypes();
void initFileType();
void initFrameType();

void typeInitAll() {
	g_typeType._flags |= kTypeBaseType;
	g_typeType.getAttr = typeGetAttr;
	g_typeType.setAttr = typeSetAttr;
	g_typeType.call = typeCall;
	g_typeType.repr = typeRepr;
	g_typeType.compare = typeCompare;
	g_typeType.hash = typeHash;

	g_typeType.newObj = typeNew;
	g_typeType.init = objectInit;
	g_typeType.methods = g_typeMethods;

	g_objectType._flags |= kTypeBaseType;
	g_objectType.getAttr = genericGetAttr;
	g_objectType.setAttr = genericSetAttr;
	g_objectType.repr = objectRepr;
	g_objectType.str = objectStr;
	g_objectType.hash = objectHash;
	g_objectType.newObj = objectNew;
	g_objectType.init = objectInit;
	g_objectType.getSets = g_objectGetSets;
	g_objectType.methods = g_objectMethods;

	g_superType.getAttr = superGetAttr;
	g_superType.newObj = superNew;
	g_superType.repr = superRepr;
	g_superType._flags |= kTypeBaseType;

	g_methodDescrType.descrGet = methodDescrGet;
	g_methodDescrType.call = methodDescrCall;
	g_methodDescrType.repr = methodDescrRepr;
	g_methodDescrType.getAttr = genericGetAttr;
	g_getSetDescrType.descrGet = getSetDescrGet;
	g_getSetDescrType.descrSet = getSetDescrSet;
	g_getSetDescrType.repr = getSetDescrRepr;
	g_getSetDescrType.getAttr = genericGetAttr;
	g_wrapperDescrType.descrGet = wrapperDescrGet;
	g_wrapperDescrType.call = wrapperDescrCall;
	g_wrapperDescrType.repr = wrapperDescrRepr;
	g_wrapperDescrType.getAttr = genericGetAttr;
	g_methodWrapperType.call = methodWrapperCall;
	g_methodWrapperType.repr = methodWrapperRepr;
	g_methodWrapperType.getAttr = genericGetAttr;

	initIntType();
	initLongType();
	initFloatType();
	initStrType();
	initUnicodeType();
	initDictType();
	initSequenceTypes();
	initClassTypes();
	initFuncTypes();
	initFileType();

	static Type *const allTypes[] = {
		&g_objectType, &g_typeType, &g_noneType, &g_notImplementedType, &g_ellipsisType,
		&g_intType, &g_longType, &g_floatType, &g_complexType, &g_strType, &g_unicodeType,
		&g_tupleType, &g_listType, &g_dictType, &g_sliceType, &g_xrangeType,
		&g_functionType, &g_codeType, &g_methodType, &g_builtinFunctionType, &g_methodDescrType,
		&g_classType, &g_instanceType, &g_moduleType, &g_frameType, &g_tracebackType,
		&g_propertyType, &g_staticMethodType, &g_classMethodType, &g_superType,
		&g_seqIterType, &g_callIterType, &g_dictIterType, &g_fileType, &g_getSetDescrType,
		&g_wrapperDescrType, &g_cellType, &g_methodWrapperType
	};
	for (uint i = 0; i < ARRAYSIZE(allTypes); i++)
		typeReady(allTypes[i]);
}

} // End of namespace Python
} // End of namespace Yaga
