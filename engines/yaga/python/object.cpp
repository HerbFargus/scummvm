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

namespace Yaga {
namespace Python {

static uint32 g_nextSerial = 1;

Object::Object(Type *type) : _refs(0), _type(type), _serial(g_nextSerial++) {
	if (type && (type->_flags & kTypeHeap))
		incRef(type);
}

Object::~Object() {
	if (_type && (_type->_flags & kTypeHeap))
		decRef(_type);
}

void incRef(Object *o) {
	o->_refs++;
}

// Deleting can cascade through long chains (a linked list of frames, say), so
// deletions are queued and run iteratively rather than recursing.
static Common::Array<Object *> *g_pendingDeletes = nullptr;
static bool g_deleting = false;

void decRef(Object *o) {
	if (--o->_refs > 0)
		return;
	if (o->_refs < 0) {
		// Static objects never go away
		o->_refs = 0x40000000;
		return;
	}
	if (!g_pendingDeletes)
		g_pendingDeletes = new Common::Array<Object *>();
	if (g_deleting) {
		g_pendingDeletes->push_back(o);
		return;
	}
	g_deleting = true;
	delete o;
	while (!g_pendingDeletes->empty()) {
		Object *next = g_pendingDeletes->back();
		g_pendingDeletes->pop_back();
		delete next;
	}
	g_deleting = false;
}

bool isSubtype(Type *a, Type *b) {
	if (a == b)
		return true;
	if (a->_mro) {
		for (uint32 i = 0; i < a->_mro->_size; i++)
			if (a->_mro->get(i) == b)
				return true;
		return false;
	}
	for (Type *t = a; t; t = t->_base)
		if (t == b)
			return true;
	return b == &g_objectType;
}

const char *typeName(Object *o) {
	return o->_type->_name.c_str();
}

// ---- Attributes ------------------------------------------------------------

ObjRef lookupType(Type *t, Str *name) {
	if (t->_mro) {
		for (uint32 i = 0; i < t->_mro->_size; i++) {
			Object *base = t->_mro->get(i);
			Dict *d = nullptr;
			if (isType(base, &g_classType))
				d = ((ClassObj *)base)->_dict.get();
			else
				d = ((Type *)base)->_dict.get();
			if (!d)
				continue;
			Object *v = d->getItem(name);
			if (v)
				return ObjRef(v);
		}
		return ObjRef();
	}
	for (Type *b = t; b; b = b->_base) {
		if (!b->_dict)
			continue;
		Object *v = b->_dict->getItem(name);
		if (v)
			return ObjRef(v);
	}
	return ObjRef();
}

ObjRef genericGetAttr(Object *o, Str *name) {
	Type *tp = o->_type;
	ObjRef descr = lookupType(tp, name);
	DescrGetFunc f = nullptr;
	if (descr) {
		f = descr->_type->descrGet;
		if (f && descr->_type->descrSet) {
			// A data descriptor wins over the instance dict
			return f(descr.get(), o, tp);
		}
	}
	Dict *dict = o->getDict();
	if (dict) {
		Object *v = dict->getItem(name);
		if (v)
			return ObjRef(v);
	}
	if (f)
		return f(descr.get(), o, tp);
	if (descr)
		return descr;
	setErrorFormat(g_excAttributeError, "'%.50s' object has no attribute '%.400s'", tp->_name.c_str(), name->_data);
	return ObjRef();
}

int genericSetAttr(Object *o, Str *name, Object *value) {
	Type *tp = o->_type;
	ObjRef descr = lookupType(tp, name);
	if (descr && descr->_type->descrSet)
		return descr->_type->descrSet(descr.get(), o, value);
	Dict *dict = o->getDict();
	if (!dict) {
		if (descr || !(tp->_flags & kTypeHasDict)) {
			if (descr)
				setErrorFormat(g_excAttributeError, "'%.50s' object attribute '%.400s' is read-only", tp->_name.c_str(), name->_data);
			else
				setErrorFormat(g_excAttributeError, "'%.50s' object has no attribute '%.400s'", tp->_name.c_str(), name->_data);
			return -1;
		}
		Ref<Dict> d = newDict();
		o->setDict(d.get());
		dict = d.get();
	}
	if (value)
		return dict->setItem(name, value);
	if (dict->delItem(name) < 0) {
		if (errorMatches(g_excKeyError)) {
			clearError();
			setErrorFormat(g_excAttributeError, "'%.50s' object has no attribute '%.400s'", tp->_name.c_str(), name->_data);
		}
		return -1;
	}
	return 0;
}

ObjRef getAttr(Object *o, Str *name) {
	if (!isInstanceOf(name, &g_strType)) {
		setErrorString(g_excTypeError, "attribute name must be string");
		return ObjRef();
	}
	if (o->_type->getAttr)
		return o->_type->getAttr(o, name);
	setErrorFormat(g_excAttributeError, "'%.50s' object has no attribute '%.400s'", typeName(o), name->_data);
	return ObjRef();
}

ObjRef getAttrString(Object *o, const char *name) {
	Ref<Str> n = internStr(name);
	return getAttr(o, n.get());
}

int setAttr(Object *o, Str *name, Object *value) {
	if (!isInstanceOf(name, &g_strType)) {
		setErrorString(g_excTypeError, "attribute name must be string");
		return -1;
	}
	Ref<Str> n = intern(name);
	if (o->_type->setAttr)
		return o->_type->setAttr(o, n.get(), value);
	if (o->_type->getAttr)
		setErrorFormat(g_excTypeError, "'%.100s' object has only read-only attributes (%s .%.100s)",
		               typeName(o), value ? "assign to" : "del", name->_data);
	else
		setErrorFormat(g_excTypeError, "'%.100s' object has no attributes (%s .%.100s)",
		               typeName(o), value ? "assign to" : "del", name->_data);
	return -1;
}

int setAttrString(Object *o, const char *name, Object *value) {
	Ref<Str> n = internStr(name);
	return setAttr(o, n.get(), value);
}

int delAttr(Object *o, Str *name) {
	return setAttr(o, name, nullptr);
}

bool hasAttrString(Object *o, const char *name) {
	ObjRef r = getAttrString(o, name);
	if (r)
		return true;
	clearError();
	return false;
}

// ---- repr, str, hash -------------------------------------------------------

ObjRef repr(Object *o) {
	if (!o->_type->repr)
		return newStr(Common::String::format("<%s object at %p>", typeName(o), (void *)(uintptr)objectAddress(o)));
	ObjRef r = o->_type->repr(o);
	if (r && !isInstanceOf(r.get(), &g_strType)) {
		setErrorFormat(g_excTypeError, "__repr__ returned non-string (type %.200s)", typeName(r.get()));
		return ObjRef();
	}
	return r;
}

ObjRef str(Object *o) {
	if (isType(o, &g_strType))
		return ObjRef(o);
	if (!o->_type->str)
		return repr(o);
	ObjRef r = o->_type->str(o);
	if (r && !isInstanceOf(r.get(), &g_strType)) {
		if (isInstanceOf(r.get(), &g_unicodeType))
			return unicodeToStr(r.get());
		setErrorFormat(g_excTypeError, "__str__ returned non-string (type %.200s)", typeName(r.get()));
		return ObjRef();
	}
	return r;
}

int32 hashPointer(Object *o) {
	int32 x = (int32)objectAddress(o);
	if (x == -1)
		x = -2;
	return x;
}

bool hash(Object *o, int32 *out) {
	Type *tp = o->_type;
	if (tp->hash)
		return tp->hash(o, out);
	if (!tp->compare && !tp->richCompare) {
		*out = hashPointer(o);
		return true;
	}
	setErrorString(g_excTypeError, "unhashable type");
	return false;
}

// ---- Comparison (CPython 2.2's object.c) -------------------------------------

// The order CPython's types happen to have in python22.dll, which decides how
// unlike numeric-ish types sort (see defaultCompare)
static int typeAddressRank(Type *t) {
	if (t == &g_instanceType) return 1;
	if (t == &g_intType) return 2;
	if (t == &g_floatType) return 3;
	if (t == &g_longType) return 4;
	if (t == &g_complexType) return 5;
	return 100 + (int)t->_serial;
}

static int adjustCompare(int c) {
	if (errorOccurred())
		return -2;
	return c < -1 ? -1 : c > 1 ? 1 : c;
}

static int defaultCompare(Object *v, Object *w) {
	if (v->_type == w->_type) {
		uint32 vv = objectAddress(v), ww = objectAddress(w);
		return vv < ww ? -1 : vv > ww ? 1 : 0;
	}
	if (isInstanceOf(v, &g_unicodeType) || isInstanceOf(w, &g_unicodeType)) {
		int c = unicodeCompare(v, w);
		if (c == -1 && errorOccurred()) {
			// Like CPython, fall back to the default order if they cannot be compared
			if (!errorMatches(g_excUnicodeError))
				return -2;
			clearError();
		} else {
			return c;
		}
	}
	if (v == g_none)
		return -1;
	if (w == g_none)
		return 1;
	const char *vname = v->_type->asNumber ? "" : v->_type->_name.c_str();
	const char *wname = w->_type->asNumber ? "" : w->_type->_name.c_str();
	int c = strcmp(vname, wname);
	if (c < 0)
		return -1;
	if (c > 0)
		return 1;
	return typeAddressRank(v->_type) < typeAddressRank(w->_type) ? -1 : 1;
}

// Returns -1, 0, 1, 2 (not implemented) or -2 (error)
static int try3WayCompare(Object *v, Object *w) {
	CmpFunc f = v->_type->compare;
	if (isType(v, &g_instanceType))
		return f(v, w);
	if (isType(w, &g_instanceType))
		return w->_type->compare(v, w);
	if (f && f == w->_type->compare)
		return adjustCompare(f(v, w));
	if (f == slotCompare || w->_type->compare == slotCompare)
		return slotCompare(v, w);
	ObjRef cv(v), cw(w);
	int c = numberCoerce(&cv, &cw);
	if (c < 0)
		return -2;
	if (c > 0)
		return 2;
	f = cv->_type->compare;
	if (f)
		return adjustCompare(f(cv.get(), cw.get()));
	return 2;
}

static ObjRef convert3WayToObject(int op, int c) {
	bool ok = false;
	switch (op) {
	case kLT: ok = c < 0; break;
	case kLE: ok = c <= 0; break;
	case kEQ: ok = c == 0; break;
	case kNE: ok = c != 0; break;
	case kGT: ok = c > 0; break;
	case kGE: ok = c >= 0; break;
	default: break;
	}
	return newBool(ok);
}

static const int g_swappedOp[] = { kGT, kGE, kEQ, kNE, kLT, kLE };

static ObjRef tryRichCompare(Object *v, Object *w, int op) {
	RichCmpFunc f;
	if (v->_type != w->_type && isSubtype(w->_type, v->_type) && (f = w->_type->richCompare)) {
		ObjRef res = f(w, v, g_swappedOp[op]);
		if (!res || res.get() != g_notImplemented)
			return res;
	}
	if ((f = v->_type->richCompare)) {
		ObjRef res = f(v, w, op);
		if (!res || res.get() != g_notImplemented)
			return res;
	}
	if ((f = w->_type->richCompare))
		return f(w, v, g_swappedOp[op]);
	return ObjRef(g_notImplemented);
}

// Returns -1, 0, 1, or 2 if no rich comparison decided it; -2 on error
static int tryRichTo3WayCompare(Object *v, Object *w) {
	if (!v->_type->richCompare && !w->_type->richCompare)
		return 2;
	static const struct { int op; int outcome; } tries[3] = { { kEQ, 0 }, { kLT, -1 }, { kGT, 1 } };
	for (int i = 0; i < 3; i++) {
		ObjRef res = tryRichCompare(v, w, tries[i].op);
		if (!res)
			return -2;
		if (res.get() == g_notImplemented)
			continue;
		int t = isTrue(res.get());
		if (t < 0)
			return -2;
		if (t)
			return tries[i].outcome;
	}
	return 2;
}

// CPython's do_cmp
static int doCompare(Object *v, Object *w) {
	CmpFunc f;
	if (v->_type == w->_type && (f = v->_type->compare)) {
		int c = f(v, w);
		if (isType(v, &g_instanceType)) {
			// Instance comparison returns 2 when __cmp__ is undefined
			if (c != 2)
				return c;
		} else {
			return adjustCompare(c);
		}
	}
	int c = tryRichTo3WayCompare(v, w);
	if (c < 2)
		return c;
	c = try3WayCompare(v, w);
	if (c < 2)
		return c;
	return defaultCompare(v, w);
}

int compare(Object *v, Object *w) {
	if (v == w)
		return 0;
	if (!enterRecursiveCall())
		return -1;
	int c = doCompare(v, w);
	leaveRecursiveCall();
	return c < -1 ? -1 : c;
}

ObjRef richCompare(Object *v, Object *w, int op) {
	if (v->_type == w->_type && !isType(v, &g_instanceType)) {
		RichCmpFunc frich = v->_type->richCompare;
		if (frich) {
			ObjRef res = frich(v, w, op);
			if (!res || res.get() != g_notImplemented)
				return res;
		}
		CmpFunc fcmp = v->_type->compare;
		if (fcmp) {
			int c = fcmp(v, w);
			if (c < 0 && errorOccurred())
				return ObjRef();
			return convert3WayToObject(op, c);
		}
	}
	ObjRef res = tryRichCompare(v, w, op);
	if (!res || res.get() != g_notImplemented)
		return res;
	int c = try3WayCompare(v, w);
	if (c >= 2)
		c = defaultCompare(v, w);
	else if (c <= -2)
		return ObjRef();
	return convert3WayToObject(op, c);
}

int richCompareBool(Object *v, Object *w, int op) {
	ObjRef res = richCompare(v, w, op);
	if (!res)
		return -1;
	return isTrue(res.get());
}

int isTrue(Object *o) {
	if (o == g_none)
		return 0;
	Type *tp = o->_type;
	if (tp->asNumber && tp->asNumber->nonzero)
		return tp->asNumber->nonzero(o) ? (errorOccurred() ? -1 : 1) : 0;
	if (tp->asMapping && tp->asMapping->length) {
		int32 n = tp->asMapping->length(o);
		return n < 0 ? -1 : n > 0;
	}
	if (tp->asSequence && tp->asSequence->length) {
		int32 n = tp->asSequence->length(o);
		return n < 0 ? -1 : n > 0;
	}
	return 1;
}

int notTrue(Object *o) {
	int t = isTrue(o);
	return t < 0 ? -1 : !t;
}

bool callable(Object *o) {
	if (isType(o, &g_instanceType)) {
		ObjRef c = getAttrString(o, "__call__");
		if (!c) {
			clearError();
			return false;
		}
		return true;
	}
	return o->_type->call != nullptr;
}

// ---- Calls -------------------------------------------------------------------

ObjRef call(Object *func, Tuple *args, Dict *kw) {
	CallFunc f = func->_type->call;
	if (!f) {
		setErrorFormat(g_excTypeError, "object of type '%.200s' is not callable", typeName(func));
		return ObjRef();
	}
	ObjRef r = f(func, args, kw && kw->_used ? kw : nullptr);
	if (!r && !errorOccurred())
		setErrorString(g_excSystemError, "NULL result without error in call");
	return r;
}

ObjRef callObject(Object *func, Tuple *args) {
	if (args)
		return call(func, args, nullptr);
	Ref<Tuple> empty = newTuple(0);
	return call(func, empty.get(), nullptr);
}

ObjRef callFunction(Object *func, uint32 n, ...) {
	Ref<Tuple> args = newTuple(n);
	va_list va;
	va_start(va, n);
	for (uint32 i = 0; i < n; i++)
		args->set(i, va_arg(va, Object *));
	va_end(va);
	return call(func, args.get(), nullptr);
}

ObjRef callMethod(Object *o, const char *name, uint32 n, ...) {
	ObjRef meth = getAttrString(o, name);
	if (!meth)
		return ObjRef();
	Ref<Tuple> args = newTuple(n);
	va_list va;
	va_start(va, n);
	for (uint32 i = 0; i < n; i++)
		args->set(i, va_arg(va, Object *));
	va_end(va);
	return call(meth.get(), args.get(), nullptr);
}

// ---- Iteration ---------------------------------------------------------------

ObjRef getIter(Object *o) {
	UnaryFunc f = o->_type->iter;
	if (f) {
		ObjRef it = f(o);
		if (it && !it->_type->iterNext) {
			setErrorFormat(g_excTypeError, "iter() returned non-iterator of type '%.100s'", typeName(it.get()));
			return ObjRef();
		}
		return it;
	}
	if (o->_type->asSequence && o->_type->asSequence->item)
		return ObjRef(new SeqIter(o));
	setErrorString(g_excTypeError, "iteration over non-sequence");
	return ObjRef();
}

ObjRef iterNext(Object *it) {
	ObjRef r = it->_type->iterNext(it);
	if (!r && errorMatches(g_excStopIteration))
		clearError();
	return r;
}

// ---- Items -------------------------------------------------------------------

static bool indexFromObject(Object *key, int32 *out) {
	if (isInstanceOf(key, &g_intType)) {
		*out = ((Int *)key)->_value;
		return true;
	}
	if (isInstanceOf(key, &g_longType)) {
		int64 v;
		if (!longAsInt64(key, &v))
			return false;
		if (v > 0x7fffffff || v < -0x7fffffffLL - 1) {
			setErrorString(g_excIndexError, "cannot fit 'long' into an index-sized integer");
			return false;
		}
		*out = (int32)v;
		return true;
	}
	return false;
}

ObjRef getItem(Object *o, Object *key) {
	Type *tp = o->_type;
	if (tp->asMapping && tp->asMapping->subscript)
		return tp->asMapping->subscript(o, key);
	if (tp->asSequence && tp->asSequence->item) {
		int32 i;
		if (indexFromObject(key, &i))
			return sequenceGetItem(o, i);
		if (errorOccurred())
			return ObjRef();
		setErrorString(g_excTypeError, "sequence index must be integer");
		return ObjRef();
	}
	setErrorString(g_excTypeError, "unsubscriptable object");
	return ObjRef();
}

int setItem(Object *o, Object *key, Object *value) {
	Type *tp = o->_type;
	if (tp->asMapping && tp->asMapping->assSubscript)
		return tp->asMapping->assSubscript(o, key, value);
	if (tp->asSequence && tp->asSequence->assItem) {
		int32 i;
		if (indexFromObject(key, &i)) {
			if (i < 0 && tp->asSequence->length) {
				int32 n = tp->asSequence->length(o);
				if (n < 0)
					return -1;
				i += n;
			}
			return tp->asSequence->assItem(o, i, value);
		}
		if (errorOccurred())
			return -1;
		setErrorString(g_excTypeError, "sequence index must be integer");
		return -1;
	}
	setErrorString(g_excTypeError, value ? "object does not support item assignment" : "object doesn't support item deletion");
	return -1;
}

int delItem(Object *o, Object *key) {
	return setItem(o, key, nullptr);
}

int32 length(Object *o) {
	Type *tp = o->_type;
	if (tp->asSequence && tp->asSequence->length)
		return tp->asSequence->length(o);
	if (tp->asMapping && tp->asMapping->length)
		return tp->asMapping->length(o);
	setErrorString(g_excTypeError, "len() of unsized object");
	return -1;
}

int containsItem(Object *seq, Object *item) {
	Type *tp = seq->_type;
	if (tp->asSequence && tp->asSequence->contains)
		return tp->asSequence->contains(seq, item);
	// Iterate, as CPython's _PySequence_IterSearch does
	ObjRef it = getIter(seq);
	if (!it) {
		if (errorMatches(g_excTypeError)) {
			clearError();
			setErrorString(g_excTypeError, "'in' or 'not in' needs sequence right argument");
		}
		return -1;
	}
	for (;;) {
		ObjRef v = iterNext(it.get());
		if (!v)
			return errorOccurred() ? -1 : 0;
		int c = richCompareBool(item, v.get(), kEQ);
		if (c != 0)
			return c;
	}
}

ObjRef sequenceGetItem(Object *o, int32 i) {
	SequenceMethods *m = o->_type->asSequence;
	if (!m || !m->item) {
		setErrorString(g_excTypeError, "unindexable object");
		return ObjRef();
	}
	if (i < 0 && m->length) {
		int32 n = m->length(o);
		if (n < 0)
			return ObjRef();
		i += n;
	}
	return m->item(o, i);
}

ObjRef sequenceGetSlice(Object *o, int32 lo, int32 hi) {
	SequenceMethods *m = o->_type->asSequence;
	if (m && m->slice) {
		if ((lo < 0 || hi < 0) && m->length) {
			int32 n = m->length(o);
			if (n < 0)
				return ObjRef();
			if (lo < 0)
				lo += n;
			if (hi < 0)
				hi += n;
		}
		return m->slice(o, lo, hi);
	}
	if (o->_type->asMapping && o->_type->asMapping->subscript) {
		ObjRef a(newInt(lo)), b(newInt(hi));
		ObjRef sl(new Slice(a.get(), b.get(), g_none));
		return o->_type->asMapping->subscript(o, sl.get());
	}
	setErrorString(g_excTypeError, "unsliceable object");
	return ObjRef();
}

int sequenceSetSlice(Object *o, int32 lo, int32 hi, Object *v) {
	SequenceMethods *m = o->_type->asSequence;
	if (m && m->assSlice) {
		if ((lo < 0 || hi < 0) && m->length) {
			int32 n = m->length(o);
			if (n < 0)
				return -1;
			if (lo < 0)
				lo += n;
			if (hi < 0)
				hi += n;
		}
		return m->assSlice(o, lo, hi, v);
	}
	if (o->_type->asMapping && o->_type->asMapping->assSubscript) {
		ObjRef a(newInt(lo)), b(newInt(hi));
		ObjRef sl(new Slice(a.get(), b.get(), g_none));
		return o->_type->asMapping->assSubscript(o, sl.get(), v);
	}
	setErrorString(g_excTypeError, "object doesn't support slice assignment");
	return -1;
}

ObjRef sequenceList(Object *o) {
	Ref<List> l = newList();
	ObjRef it = getIter(o);
	if (!it)
		return ObjRef();
	for (;;) {
		ObjRef v = iterNext(it.get());
		if (!v) {
			if (errorOccurred())
				return ObjRef();
			break;
		}
		l->append(v.get());
	}
	return l;
}

ObjRef sequenceTuple(Object *o) {
	if (isType(o, &g_tupleType))
		return ObjRef(o);
	if (isInstanceOf(o, &g_listType)) {
		List *l = (List *)o;
		Ref<Tuple> t = newTuple(l->size());
		for (uint32 i = 0; i < l->size(); i++)
			t->set(i, l->get(i));
		return t;
	}
	ObjRef l = sequenceList(o);
	if (!l)
		return ObjRef();
	return sequenceTuple(l.get());
}

ObjRef sequenceConcat(Object *a, Object *b) {
	SequenceMethods *m = a->_type->asSequence;
	if (m && m->concat)
		return m->concat(a, b);
	setErrorString(g_excTypeError, "object can't be concatenated");
	return ObjRef();
}

ObjRef sequenceRepeat(Object *a, int32 n) {
	SequenceMethods *m = a->_type->asSequence;
	if (m && m->repeat)
		return m->repeat(a, n);
	setErrorString(g_excTypeError, "object can't be repeated");
	return ObjRef();
}

int32 sequenceIndex(Object *seq, Object *item) {
	ObjRef it = getIter(seq);
	if (!it)
		return -1;
	for (int32 i = 0;; i++) {
		ObjRef v = iterNext(it.get());
		if (!v) {
			if (!errorOccurred())
				setErrorString(g_excValueError, "sequence.index(x): x not in sequence");
			return -1;
		}
		int c = richCompareBool(item, v.get(), kEQ);
		if (c < 0)
			return -1;
		if (c)
			return i;
	}
}

int32 sequenceCount(Object *seq, Object *item) {
	ObjRef it = getIter(seq);
	if (!it)
		return -1;
	int32 n = 0;
	for (;;) {
		ObjRef v = iterNext(it.get());
		if (!v)
			return errorOccurred() ? -1 : n;
		int c = richCompareBool(item, v.get(), kEQ);
		if (c < 0)
			return -1;
		n += c;
	}
}

// ---- Numbers (CPython 2.2's abstract.c) --------------------------------------

#define NB_SLOT(m, off) (*(BinaryFunc *)((char *)(m) + (off)))
#define SLOT_OFFSET(name) ((size_t)&((NumberMethods *)nullptr)->name)

static bool newStyleNumber(Object *o) {
	return (o->_type->_flags & kTypeCheckTypes) != 0;
}

bool isNumber(Object *o) {
	return o->_type->asNumber && (o->_type->asNumber->toInt || o->_type->asNumber->toFloat);
}

int numberCoerce(ObjRef *pv, ObjRef *pw) {
	Object *v = pv->get(), *w = pw->get();
	if (v->_type == w->_type && !isType(v, &g_instanceType))
		return 0;
	if (v->_type->asNumber && v->_type->asNumber->coerce) {
		int r = v->_type->asNumber->coerce(pv, pw);
		if (r <= 0)
			return r;
	}
	if (w->_type->asNumber && w->_type->asNumber->coerce) {
		int r = w->_type->asNumber->coerce(pw, pv);
		if (r <= 0)
			return r;
	}
	return 1;
}

static ObjRef binaryOp1(Object *v, Object *w, size_t slot) {
	BinaryFunc slotv = nullptr, slotw = nullptr;
	if (v->_type->asNumber && newStyleNumber(v))
		slotv = NB_SLOT(v->_type->asNumber, slot);
	if (w->_type != v->_type && w->_type->asNumber && newStyleNumber(w)) {
		slotw = NB_SLOT(w->_type->asNumber, slot);
		if (slotw == slotv)
			slotw = nullptr;
	}
	if (slotv) {
		if (slotw && isSubtype(w->_type, v->_type)) {
			ObjRef x = slotw(v, w);
			if (!x || x.get() != g_notImplemented)
				return x;
			slotw = nullptr;
		}
		ObjRef x = slotv(v, w);
		if (!x || x.get() != g_notImplemented)
			return x;
	}
	if (slotw) {
		ObjRef x = slotw(v, w);
		if (!x || x.get() != g_notImplemented)
			return x;
	}
	if (!newStyleNumber(v) || !newStyleNumber(w)) {
		ObjRef cv(v), cw(w);
		int err = numberCoerce(&cv, &cw);
		if (err < 0)
			return ObjRef();
		if (err == 0) {
			NumberMethods *mv = cv->_type->asNumber;
			if (mv) {
				BinaryFunc s = NB_SLOT(mv, slot);
				if (s)
					return s(cv.get(), cw.get());
			}
		}
	}
	return ObjRef(g_notImplemented);
}

static ObjRef binopTypeError(Object *v, Object *w, const char *opName) {
	setErrorFormat(g_excTypeError, "unsupported operand types for %s: '%s' and '%s'", opName, typeName(v), typeName(w));
	return ObjRef();
}

static ObjRef binaryOp(Object *v, Object *w, size_t slot, const char *opName) {
	ObjRef r = binaryOp1(v, w, slot);
	if (r && r.get() == g_notImplemented)
		return binopTypeError(v, w, opName);
	return r;
}

#define BINOP(func, slot, name) \
	ObjRef func(Object *a, Object *b) { return binaryOp(a, b, SLOT_OFFSET(slot), name); }

BINOP(numberOr, or_, "|")
BINOP(numberXor, xor_, "^")
BINOP(numberAnd, and_, "&")
BINOP(numberLshift, lshift, "<<")
BINOP(numberRshift, rshift, ">>")
BINOP(numberSubtract, subtract, "-")
BINOP(numberDivide, divide, "/")
BINOP(numberDivmod, divmod, "divmod()")
BINOP(numberFloorDivide, floorDivide, "//")
BINOP(numberTrueDivide, trueDivide, "/")

ObjRef numberAdd(Object *v, Object *w) {
	ObjRef r = binaryOp1(v, w, SLOT_OFFSET(add));
	if (r && r.get() == g_notImplemented) {
		SequenceMethods *m = v->_type->asSequence;
		if (m && m->concat)
			return m->concat(v, w);
		return binopTypeError(v, w, "+");
	}
	return r;
}

static ObjRef sequenceRepeatOp(IntArgFunc repeatFunc, Object *seq, Object *n) {
	int32 count;
	if (isInstanceOf(n, &g_intType)) {
		count = ((Int *)n)->_value;
	} else if (isInstanceOf(n, &g_longType)) {
		int64 v;
		if (!longAsInt64(n, &v))
			return ObjRef();
		count = (int32)v;
	} else {
		setErrorString(g_excTypeError, "can't multiply sequence to non-int");
		return ObjRef();
	}
	return repeatFunc(seq, count);
}

ObjRef numberMultiply(Object *v, Object *w) {
	ObjRef r = binaryOp1(v, w, SLOT_OFFSET(multiply));
	if (r && r.get() == g_notImplemented) {
		SequenceMethods *mv = v->_type->asSequence, *mw = w->_type->asSequence;
		if (mv && mv->repeat)
			return sequenceRepeatOp(mv->repeat, v, w);
		if (mw && mw->repeat)
			return sequenceRepeatOp(mw->repeat, w, v);
		return binopTypeError(v, w, "*");
	}
	return r;
}

ObjRef numberRemainder(Object *v, Object *w) {
	if (isInstanceOf(v, &g_strType))
		return strFormat(v, w);
	if (isInstanceOf(v, &g_unicodeType))
		return unicodeFormat(v, w);
	return binaryOp(v, w, SLOT_OFFSET(remainder), "%");
}

static ObjRef ternaryOp(Object *v, Object *w, Object *z, size_t slot, const char *opName) {
	NumberMethods *mv = v->_type->asNumber, *mw = w->_type->asNumber, *mz;
	TernaryFunc slotv = nullptr, slotw = nullptr, slotz = nullptr;
#define TSLOT(m) (*(TernaryFunc *)((char *)(m) + slot))
	if (mv && newStyleNumber(v))
		slotv = TSLOT(mv);
	if (w->_type != v->_type && mw && newStyleNumber(w)) {
		slotw = TSLOT(mw);
		if (slotw == slotv)
			slotw = nullptr;
	}
	if (slotv) {
		if (slotw && isSubtype(w->_type, v->_type)) {
			ObjRef x = slotw(v, w, z);
			if (!x || x.get() != g_notImplemented)
				return x;
			slotw = nullptr;
		}
		ObjRef x = slotv(v, w, z);
		if (!x || x.get() != g_notImplemented)
			return x;
	}
	if (slotw) {
		ObjRef x = slotw(v, w, z);
		if (!x || x.get() != g_notImplemented)
			return x;
	}
	mz = z->_type->asNumber;
	if (mz && newStyleNumber(z)) {
		slotz = TSLOT(mz);
		if (slotz == slotv || slotz == slotw)
			slotz = nullptr;
		if (slotz) {
			ObjRef x = slotz(v, w, z);
			if (!x || x.get() != g_notImplemented)
				return x;
		}
	}
	if (!newStyleNumber(v) || !newStyleNumber(w) || (z != g_none && !newStyleNumber(z))) {
		ObjRef cv(v), cw(w);
		int c = numberCoerce(&cv, &cw);
		if (c < 0)
			return ObjRef();
		if (c == 0) {
			if (z == g_none) {
				if (cv->_type->asNumber && TSLOT(cv->_type->asNumber))
					return TSLOT(cv->_type->asNumber)(cv.get(), cw.get(), z);
			} else {
				ObjRef cv2(cv), cz(z);
				c = numberCoerce(&cv2, &cz);
				if (c < 0)
					return ObjRef();
				if (c == 0 && cv2->_type->asNumber && TSLOT(cv2->_type->asNumber))
					return TSLOT(cv2->_type->asNumber)(cv2.get(), cw.get(), cz.get());
			}
		}
	}
#undef TSLOT
	if (z == g_none)
		setErrorFormat(g_excTypeError, "unsupported operand type(s) for %s: '%s' and '%s'", opName, typeName(v), typeName(w));
	else
		setErrorFormat(g_excTypeError, "unsupported operand type(s) for %s: '%s', '%s', '%s'", opName, typeName(v), typeName(w), typeName(z));
	return ObjRef();
}

ObjRef numberPower(Object *a, Object *b, Object *c) {
	return ternaryOp(a, b, c, SLOT_OFFSET(power), "** or pow()");
}

// In-place operators fall back to the plain ones
static ObjRef binaryIop(Object *v, Object *w, size_t islot, size_t slot, const char *opName) {
	NumberMethods *mv = v->_type->asNumber;
	if (mv) {
		BinaryFunc f = NB_SLOT(mv, islot);
		if (f) {
			ObjRef x = f(v, w);
			if (!x || x.get() != g_notImplemented)
				return x;
		}
	}
	return binaryOp(v, w, slot, opName);
}

#define IBINOP(func, islot, slot, name) \
	ObjRef func(Object *a, Object *b) { return binaryIop(a, b, SLOT_OFFSET(islot), SLOT_OFFSET(slot), name); }

IBINOP(numberInplaceOr, inplaceOr, or_, "|=")
IBINOP(numberInplaceXor, inplaceXor, xor_, "^=")
IBINOP(numberInplaceAnd, inplaceAnd, and_, "&=")
IBINOP(numberInplaceLshift, inplaceLshift, lshift, "<<=")
IBINOP(numberInplaceRshift, inplaceRshift, rshift, ">>=")
IBINOP(numberInplaceSubtract, inplaceSubtract, subtract, "-=")
IBINOP(numberInplaceDivide, inplaceDivide, divide, "/=")
IBINOP(numberInplaceFloorDivide, inplaceFloorDivide, floorDivide, "//=")
IBINOP(numberInplaceTrueDivide, inplaceTrueDivide, trueDivide, "/=")

ObjRef numberInplaceAdd(Object *v, Object *w) {
	NumberMethods *mv = v->_type->asNumber;
	if (mv && mv->inplaceAdd) {
		ObjRef x = mv->inplaceAdd(v, w);
		if (!x || x.get() != g_notImplemented)
			return x;
	}
	SequenceMethods *ms = v->_type->asSequence;
	if (ms && ms->inplaceConcat)
		return ms->inplaceConcat(v, w);
	return numberAdd(v, w);
}

ObjRef numberInplaceMultiply(Object *v, Object *w) {
	NumberMethods *mv = v->_type->asNumber;
	if (mv && mv->inplaceMultiply) {
		ObjRef x = mv->inplaceMultiply(v, w);
		if (!x || x.get() != g_notImplemented)
			return x;
	}
	SequenceMethods *ms = v->_type->asSequence;
	if (ms && ms->inplaceRepeat && (isInstanceOf(w, &g_intType) || isInstanceOf(w, &g_longType)))
		return sequenceRepeatOp(ms->inplaceRepeat, v, w);
	return numberMultiply(v, w);
}

ObjRef numberInplaceRemainder(Object *v, Object *w) {
	NumberMethods *mv = v->_type->asNumber;
	if (mv && mv->inplaceRemainder) {
		ObjRef x = mv->inplaceRemainder(v, w);
		if (!x || x.get() != g_notImplemented)
			return x;
	}
	return numberRemainder(v, w);
}

ObjRef numberInplacePower(Object *v, Object *w, Object *z) {
	NumberMethods *mv = v->_type->asNumber;
	if (mv && mv->inplacePower) {
		ObjRef x = mv->inplacePower(v, w, z);
		if (!x || x.get() != g_notImplemented)
			return x;
	}
	return numberPower(v, w, z);
}

static ObjRef unaryOp(Object *o, size_t slot, const char *msg) {
	NumberMethods *m = o->_type->asNumber;
	if (m) {
		UnaryFunc f = *(UnaryFunc *)((char *)m + slot);
		if (f)
			return f(o);
	}
	setErrorString(g_excTypeError, msg);
	return ObjRef();
}

ObjRef numberNegative(Object *o) { return unaryOp(o, SLOT_OFFSET(negative), "bad operand type for unary -"); }
ObjRef numberPositive(Object *o) { return unaryOp(o, SLOT_OFFSET(positive), "bad operand type for unary +"); }
ObjRef numberInvert(Object *o) { return unaryOp(o, SLOT_OFFSET(invert), "bad operand type for unary ~"); }
ObjRef numberAbsolute(Object *o) { return unaryOp(o, SLOT_OFFSET(absolute), "bad operand type for abs()"); }

ObjRef numberInt(Object *o) {
	if (isType(o, &g_intType))
		return ObjRef(o);
	if (isInstanceOf(o, &g_strType))
		return intFromString(((Str *)o)->_data, 10, true);
	if (isInstanceOf(o, &g_unicodeType)) {
		ObjRef s = unicodeToStr(o);
		if (!s)
			return ObjRef();
		return intFromString(((Str *)s.get())->_data, 10, true);
	}
	NumberMethods *m = o->_type->asNumber;
	if (m && m->toInt)
		return m->toInt(o);
	setErrorString(g_excTypeError, "int() argument must be a string or a number");
	return ObjRef();
}

ObjRef numberLong(Object *o) {
	if (isType(o, &g_longType))
		return ObjRef(o);
	if (isInstanceOf(o, &g_strType))
		return longFromString(((Str *)o)->_data, 10, true);
	NumberMethods *m = o->_type->asNumber;
	if (m && m->toLong)
		return m->toLong(o);
	setErrorString(g_excTypeError, "long() argument must be a string or a number");
	return ObjRef();
}

ObjRef numberFloat(Object *o) {
	if (isType(o, &g_floatType))
		return ObjRef(o);
	if (isInstanceOf(o, &g_strType))
		return floatFromString((Str *)o);
	NumberMethods *m = o->_type->asNumber;
	if (m && m->toFloat)
		return m->toFloat(o);
	setErrorString(g_excTypeError, "float() needs a string or a number argument");
	return ObjRef();
}

bool asInt(Object *o, int32 *out) {
	if (isInstanceOf(o, &g_intType)) {
		*out = ((Int *)o)->_value;
		return true;
	}
	if (!o->_type->asNumber || !o->_type->asNumber->toInt || isInstanceOf(o, &g_strType)) {
		setErrorString(g_excTypeError, "an integer is required");
		return false;
	}
	ObjRef r = o->_type->asNumber->toInt(o);
	if (!r)
		return false;
	if (isInstanceOf(r.get(), &g_intType)) {
		*out = ((Int *)r.get())->_value;
		return true;
	}
	if (isInstanceOf(r.get(), &g_longType)) {
		int64 v;
		if (!longAsInt64(r.get(), &v))
			return false;
		if (v > 0x7fffffff || v < -0x7fffffffLL - 1) {
			setErrorString(g_excOverflowError, "long int too large to convert to int");
			return false;
		}
		*out = (int32)v;
		return true;
	}
	setErrorString(g_excTypeError, "nb_int should return int object");
	return false;
}

bool asDouble(Object *o, double *out) {
	if (isInstanceOf(o, &g_floatType)) {
		*out = ((Float *)o)->_value;
		return true;
	}
	if (isInstanceOf(o, &g_intType)) {
		*out = ((Int *)o)->_value;
		return true;
	}
	if (isInstanceOf(o, &g_longType))
		return longAsDouble(o, out);
	if (!o->_type->asNumber || !o->_type->asNumber->toFloat || isInstanceOf(o, &g_strType)) {
		setErrorString(g_excTypeError, "a float is required");
		return false;
	}
	ObjRef r = o->_type->asNumber->toFloat(o);
	if (!r)
		return false;
	if (!isInstanceOf(r.get(), &g_floatType)) {
		setErrorString(g_excTypeError, "nb_float should return float object");
		return false;
	}
	*out = ((Float *)r.get())->_value;
	return true;
}

Str *asStr(Object *o) {
	if (isInstanceOf(o, &g_strType))
		return (Str *)o;
	setErrorFormat(g_excTypeError, "expected string, %.200s found", typeName(o));
	return nullptr;
}

bool checkArgs(const char *fname, Tuple *args, uint32 min, uint32 max) {
	uint32 n = args->_size;
	if (n >= min && n <= max)
		return true;
	if (min == max)
		setErrorFormat(g_excTypeError, "%s() takes exactly %u argument%s (%u given)", fname, min, min == 1 ? "" : "s", n);
	else if (n < min)
		setErrorFormat(g_excTypeError, "%s() takes at least %u argument%s (%u given)", fname, min, min == 1 ? "" : "s", n);
	else
		setErrorFormat(g_excTypeError, "%s() takes at most %u argument%s (%u given)", fname, max, max == 1 ? "" : "s", n);
	return false;
}

bool noKeywords(const char *fname, Dict *kw) {
	if (!kw || kw->_used == 0)
		return true;
	setErrorFormat(g_excTypeError, "%s() takes no keyword arguments", fname);
	return false;
}

} // End of namespace Python
} // End of namespace Yaga
