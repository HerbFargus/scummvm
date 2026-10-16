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

namespace Yaga {
namespace Python {

Type g_tupleType("tuple", &g_objectType);
Type g_listType("list", &g_objectType);
Type g_sliceType("slice", &g_objectType);
Type g_xrangeType("xrange", &g_objectType);
Type g_seqIterType("iterator", &g_objectType);
Type g_callIterType("callable-iterator", &g_objectType);

const MethodDef *iteratorMethods();

// ---- Tuples ----------------------------------------------------------------

Tuple::Tuple(uint32 size) : Object(&g_tupleType), _size(size) {
	_items = size ? (Object **)malloc(sizeof(Object *) * size) : nullptr;
	for (uint32 i = 0; i < size; i++)
		_items[i] = nullptr;
}

Tuple::~Tuple() {
	for (uint32 i = 0; i < _size; i++)
		if (_items[i])
			decRef(_items[i]);
	free(_items);
}

void Tuple::set(uint32 i, Object *o) {
	if (o)
		incRef(o);
	if (_items[i])
		decRef(_items[i]);
	_items[i] = o;
}

static Tuple *g_emptyTuple = nullptr;

Ref<Tuple> newTuple(uint32 size) {
	if (size == 0) {
		if (!g_emptyTuple) {
			g_emptyTuple = new Tuple(0);
			incRef(g_emptyTuple);
		}
		return Ref<Tuple>(g_emptyTuple);
	}
	return Ref<Tuple>(new Tuple(size));
}

Ref<Tuple> tuplePack(uint32 n, ...) {
	Ref<Tuple> t = newTuple(n);
	va_list va;
	va_start(va, n);
	for (uint32 i = 0; i < n; i++)
		t->set(i, va_arg(va, Object *));
	va_end(va);
	return t;
}

static int32 tupleLength(Object *o) {
	return ((Tuple *)o)->_size;
}

static ObjRef tupleItem(Object *o, int32 i) {
	Tuple *t = (Tuple *)o;
	if (i < 0 || i >= (int32)t->_size) {
		setErrorString(g_excIndexError, "tuple index out of range");
		return ObjRef();
	}
	return ObjRef(t->get(i));
}

static ObjRef tupleSlice(Object *o, int32 lo, int32 hi) {
	Tuple *t = (Tuple *)o;
	if (lo < 0)
		lo = 0;
	if (hi > (int32)t->_size)
		hi = t->_size;
	if (hi < lo)
		hi = lo;
	if (lo == 0 && hi == (int32)t->_size && isType(o, &g_tupleType))
		return ObjRef(o);
	if (hi == lo)
		return newTuple(0);
	Ref<Tuple> r = newTuple(hi - lo);

	for (int32 i = lo; i < hi; i++)
		r->set(i - lo, t->get(i));
	return r;
}

static ObjRef tupleConcat(Object *a, Object *b) {
	if (!isInstanceOf(b, &g_tupleType)) {
		setErrorFormat(g_excTypeError, "can only concatenate tuple (not \"%.200s\") to tuple", typeName(b));
		return ObjRef();
	}
	Tuple *x = (Tuple *)a, *y = (Tuple *)b;
	Ref<Tuple> r = newTuple(x->_size + y->_size);
	for (uint32 i = 0; i < x->_size; i++)
		r->set(i, x->get(i));
	for (uint32 i = 0; i < y->_size; i++)
		r->set(x->_size + i, y->get(i));
	return r;
}

static ObjRef tupleRepeat(Object *o, int32 n) {
	Tuple *t = (Tuple *)o;
	if (n < 0)
		n = 0;
	if (t->_size == 0 || n == 1) {
		if (isType(o, &g_tupleType))
			return ObjRef(o);
	}
	Ref<Tuple> r = newTuple(t->_size * n);
	for (int32 j = 0; j < n; j++)
		for (uint32 i = 0; i < t->_size; i++)
			r->set(j * t->_size + i, t->get(i));
	return r;
}

static int tupleContains(Object *o, Object *el) {
	Tuple *t = (Tuple *)o;
	for (uint32 i = 0; i < t->_size; i++) {
		int c = richCompareBool(el, t->get(i), kEQ);
		if (c != 0)
			return c;
	}
	return 0;
}

static ObjRef seqRepr(Object *const *items, uint32 n, const char *open, const char *close, bool oneComma) {
	if (!enterRecursiveCall())
		return ObjRef();
	Common::String out = open;
	for (uint32 i = 0; i < n; i++) {
		if (i)
			out += ", ";
		ObjRef r = repr(items[i]);
		if (!r) {
			leaveRecursiveCall();
			return ObjRef();
		}
		out += ((Str *)r.get())->toString();
	}
	if (oneComma && n == 1)
		out += ",";
	out += close;
	leaveRecursiveCall();
	return newStr(out);
}

static ObjRef tupleRepr(Object *o) {
	Tuple *t = (Tuple *)o;
	return seqRepr(t->_items, t->_size, "(", ")", true);
}

static bool tupleHash(Object *o, int32 *out) {
	Tuple *t = (Tuple *)o;
	uint32 x = 0x345678;
	for (uint32 i = 0; i < t->_size; i++) {
		int32 y;
		if (!hash(t->get(i), &y))
			return false;
		x = (1000003u * x) ^ (uint32)y;
	}
	x ^= t->_size;
	int32 r = (int32)x;
	*out = r == -1 ? -2 : r;
	return true;
}

// Rich comparison of two item arrays, as CPython's tuplerichcompare and list_richcompare
static ObjRef seqRichCompare(Object *const *v, uint32 vlen, Object *const *w, uint32 wlen, int op) {
	uint32 i;
	for (i = 0; i < vlen && i < wlen; i++) {
		int k = richCompareBool(v[i], w[i], kEQ);
		if (k < 0)
			return ObjRef();
		if (!k)
			break;
	}
	if (i >= vlen || i >= wlen) {
		bool cmp;
		switch (op) {
		case kLT: cmp = vlen < wlen; break;
		case kLE: cmp = vlen <= wlen; break;
		case kEQ: cmp = vlen == wlen; break;
		case kNE: cmp = vlen != wlen; break;
		case kGT: cmp = vlen > wlen; break;
		default: cmp = vlen >= wlen; break;
		}
		return newBool(cmp);
	}
	if (op == kEQ)
		return newBool(false);
	if (op == kNE)
		return newBool(true);
	return richCompare(v[i], w[i], op);
}

static ObjRef tupleRichCompare(Object *v, Object *w, int op) {
	if (!isInstanceOf(v, &g_tupleType) || !isInstanceOf(w, &g_tupleType))
		return ObjRef(g_notImplemented);
	Tuple *a = (Tuple *)v, *b = (Tuple *)w;
	return seqRichCompare(a->_items, a->_size, b->_items, b->_size, op);
}

static ObjRef tupleNew(Type *type, Tuple *args, Dict *kw) {
	if (!noKeywords("tuple", kw) || !checkArgs("tuple", args, 0, 1))
		return ObjRef();
	ObjRef r = args->_size ? sequenceTuple(args->get(0)) : ObjRef(newTuple(0));
	if (!r || type == &g_tupleType)
		return r;
	Tuple *src = (Tuple *)r.get();
	Tuple *t = new Tuple(src->_size);
	t->_type = type;
	incRef(type);
	for (uint32 i = 0; i < src->_size; i++)
		t->set(i, src->get(i));
	return ObjRef(t);
}

// ---- Lists -----------------------------------------------------------------

List::~List() {
	for (uint32 i = 0; i < _items.size(); i++)
		decRef(_items[i]);
}

void List::append(Object *o) {
	incRef(o);
	_items.push_back(o);
}

void List::setItem(uint32 i, Object *o) {
	incRef(o);
	Object *old = _items[i];
	_items[i] = o;
	decRef(old);
}

void List::clear() {
	Common::Array<Object *> old;
	old.swap(_items);
	for (uint32 i = 0; i < old.size(); i++)
		decRef(old[i]);
}

Ref<List> newList(uint32 size) {
	Ref<List> l(new List());
	for (uint32 i = 0; i < size; i++)
		l->append(g_none);
	return l;
}

int listInsert(List *l, int32 where, Object *v) {
	int32 n = l->_items.size();
	// Python 2.2 treats any negative index as the start
	if (where < 0)
		where = 0;
	if (where > n)
		where = n;
	incRef(v);
	l->_items.insert_at(where, v);
	return 0;
}

static int32 listLength(Object *o) {
	return ((List *)o)->size();
}

static ObjRef listItem(Object *o, int32 i) {
	List *l = (List *)o;
	if (i < 0 || i >= (int32)l->size()) {
		setErrorString(g_excIndexError, "list index out of range");
		return ObjRef();
	}
	return ObjRef(l->get(i));
}

static ObjRef listSliceObj(Object *o, int32 lo, int32 hi) {
	List *l = (List *)o;
	if (lo < 0)
		lo = 0;
	else if (lo > (int32)l->size())
		lo = l->size();
	if (hi < lo)
		hi = lo;
	else if (hi > (int32)l->size())
		hi = l->size();
	Ref<List> r = newList();
	for (int32 i = lo; i < hi; i++)
		r->append(l->get(i));
	return r;
}

static ObjRef listConcat(Object *a, Object *b) {
	if (!isInstanceOf(b, &g_listType)) {
		setErrorFormat(g_excTypeError, "can only concatenate list (not \"%.200s\") to list", typeName(b));
		return ObjRef();
	}
	List *x = (List *)a, *y = (List *)b;
	Ref<List> r = newList();
	for (uint32 i = 0; i < x->size(); i++)
		r->append(x->get(i));
	for (uint32 i = 0; i < y->size(); i++)
		r->append(y->get(i));
	return r;
}

static ObjRef listRepeat(Object *o, int32 n) {
	List *l = (List *)o;
	Ref<List> r = newList();
	for (int32 j = 0; j < n; j++)
		for (uint32 i = 0; i < l->size(); i++)
			r->append(l->get(i));
	return r;
}

static int listAssSlice(Object *o, int32 ilow, int32 ihigh, Object *v) {
	List *a = (List *)o;
	Common::Array<Object *> recycle;
	ObjRef keep;
	Common::Array<Object *> items;
	if (v) {
		if (v == o) {
			// Special case "a[i:j] = a": copy first
			keep = listSliceObj(o, 0, a->size());
			v = keep.get();
		}
		if (isInstanceOf(v, &g_listType)) {
			List *b = (List *)v;
			for (uint32 i = 0; i < b->size(); i++)
				items.push_back(b->get(i));
		} else {
			setErrorFormat(g_excTypeError, "must assign list (not \"%.200s\") to slice", typeName(v));
			return -1;
		}
	}
	int32 n = a->size();
	if (ilow < 0)
		ilow = 0;
	else if (ilow > n)
		ilow = n;
	if (ihigh < ilow)
		ihigh = ilow;
	else if (ihigh > n)
		ihigh = n;
	for (int32 i = ilow; i < ihigh; i++)
		recycle.push_back(a->_items[i]);
	Common::Array<Object *> result;
	for (int32 i = 0; i < ilow; i++)
		result.push_back(a->_items[i]);
	for (uint32 i = 0; i < items.size(); i++) {
		incRef(items[i]);
		result.push_back(items[i]);
	}
	for (int32 i = ihigh; i < n; i++)
		result.push_back(a->_items[i]);
	a->_items.swap(result);
	for (uint32 i = 0; i < recycle.size(); i++)
		decRef(recycle[i]);
	return 0;
}

static int listAssItem(Object *o, int32 i, Object *v) {
	List *l = (List *)o;
	if (i < 0 || i >= (int32)l->size()) {
		setErrorString(g_excIndexError, "list assignment index out of range");
		return -1;
	}
	if (!v)
		return listAssSlice(o, i, i + 1, nullptr);
	l->setItem(i, v);
	return 0;
}

static int listContains(Object *o, Object *el) {
	List *l = (List *)o;
	for (uint32 i = 0; i < l->size(); i++) {
		ObjRef item(l->get(i));
		int c = richCompareBool(el, item.get(), kEQ);
		if (c != 0)
			return c;
	}
	return 0;
}

static int listExtendInternal(List *l, Object *b) {
	ObjRef seq = sequenceTuple(b);
	if (!seq) {
		if (errorMatches(g_excTypeError)) {
			clearError();
			setErrorString(g_excTypeError, "list.extend() argument must be a sequence");
		}
		return -1;
	}
	Tuple *t = (Tuple *)seq.get();
	for (uint32 i = 0; i < t->_size; i++)
		l->append(t->get(i));
	return 0;
}

static ObjRef listInplaceConcat(Object *o, Object *b) {
	if (listExtendInternal((List *)o, b) < 0)
		return ObjRef();
	return ObjRef(o);
}

static ObjRef listInplaceRepeat(Object *o, int32 n) {
	List *l = (List *)o;
	if (n < 1) {
		l->clear();
		return ObjRef(o);
	}
	uint32 size = l->size();
	for (int32 j = 1; j < n; j++)
		for (uint32 i = 0; i < size; i++)
			l->append(l->get(i));
	return ObjRef(o);
}

static ObjRef listRepr(Object *o) {
	List *l = (List *)o;
	if (l->size() == 0)
		return newStr("[]");
	Common::Array<Object *> items = l->_items;
	for (uint32 i = 0; i < items.size(); i++)
		incRef(items[i]);
	ObjRef r = seqRepr(items.size() ? &items[0] : nullptr, items.size(), "[", "]", false);
	for (uint32 i = 0; i < items.size(); i++)
		decRef(items[i]);
	return r;
}

static ObjRef listRichCompare(Object *v, Object *w, int op) {
	if (!isInstanceOf(v, &g_listType) || !isInstanceOf(w, &g_listType))
		return ObjRef(g_notImplemented);
	List *a = (List *)v, *b = (List *)w;
	if (a->size() != b->size() && (op == kEQ || op == kNE))
		return newBool(op == kNE);
	Common::Array<Object *> av = a->_items, bv = b->_items;
	for (uint32 i = 0; i < av.size(); i++)
		incRef(av[i]);
	for (uint32 i = 0; i < bv.size(); i++)
		incRef(bv[i]);
	ObjRef r = seqRichCompare(av.size() ? &av[0] : nullptr, av.size(), bv.size() ? &bv[0] : nullptr, bv.size(), op);
	for (uint32 i = 0; i < av.size(); i++)
		decRef(av[i]);
	for (uint32 i = 0; i < bv.size(); i++)
		decRef(bv[i]);
	return r;
}

static bool listHash(Object *, int32 *) {
	setErrorString(g_excTypeError, "list objects are unhashable");
	return false;
}

#define LIST_SELF List *self = (List *)selfObj;

static ObjRef listAppend(Object *selfObj, Tuple *args, Dict *) {
	if (!checkOneArg("append", args))
		return ObjRef();
	((List *)selfObj)->append(args->get(0));
	return ObjRef(g_none);
}

static ObjRef listInsertMethod(Object *selfObj, Tuple *args, Dict *) {
	int32 i;
	if (!checkArgs("insert", args, 2, 2) || !asInt(args->get(0), &i))
		return ObjRef();
	listInsert((List *)selfObj, i, args->get(1));
	return ObjRef(g_none);
}

static ObjRef listExtend(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("extend", args, 1, 1))
		return ObjRef();
	if (listExtendInternal((List *)selfObj, args->get(0)) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef listPop(Object *selfObj, Tuple *args, Dict *) {
	LIST_SELF
	if (!checkArgs("pop", args, 0, 1))
		return ObjRef();
	int32 i = -1;
	if (args->_size && !asInt(args->get(0), &i))
		return ObjRef();
	if (self->size() == 0) {
		setErrorString(g_excIndexError, "pop from empty list");
		return ObjRef();
	}
	if (i < 0)
		i += self->size();
	if (i < 0 || i >= (int32)self->size()) {
		setErrorString(g_excIndexError, "pop index out of range");
		return ObjRef();
	}
	ObjRef v(self->get(i));
	if (listAssSlice(self, i, i + 1, nullptr) < 0)
		return ObjRef();
	return v;
}

static ObjRef listRemove(Object *selfObj, Tuple *args, Dict *) {
	LIST_SELF
	if (!checkOneArg("remove", args))
		return ObjRef();
	for (uint32 i = 0; i < self->size(); i++) {
		ObjRef item(self->get(i));
		int c = richCompareBool(item.get(), args->get(0), kEQ);
		if (c > 0) {
			if (listAssSlice(self, i, i + 1, nullptr) < 0)
				return ObjRef();
			return ObjRef(g_none);
		}
		if (c < 0)
			return ObjRef();
	}
	setErrorString(g_excValueError, "list.remove(x): x not in list");
	return ObjRef();
}

static ObjRef listIndex(Object *selfObj, Tuple *args, Dict *) {
	LIST_SELF
	if (!checkOneArg("index", args))
		return ObjRef();
	for (uint32 i = 0; i < self->size(); i++) {
		ObjRef item(self->get(i));
		int c = richCompareBool(item.get(), args->get(0), kEQ);
		if (c > 0)
			return newInt(i);
		if (c < 0)
			return ObjRef();
	}
	setErrorString(g_excValueError, "list.index(x): x not in list");
	return ObjRef();
}

static ObjRef listCount(Object *selfObj, Tuple *args, Dict *) {
	LIST_SELF
	if (!checkOneArg("count", args))
		return ObjRef();
	int32 n = 0;
	for (uint32 i = 0; i < self->size(); i++) {
		ObjRef item(self->get(i));
		int c = richCompareBool(item.get(), args->get(0), kEQ);
		if (c < 0)
			return ObjRef();
		n += c;
	}
	return newInt(n);
}

void listReverse(List *l) {
	uint32 n = l->size();
	for (uint32 i = 0; i < n / 2; i++)
		SWAP(l->_items[i], l->_items[n - 1 - i]);
}

static ObjRef listReverseMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("reverse", args, 0, 0))
		return ObjRef();
	listReverse((List *)selfObj);
	return ObjRef(g_none);
}

// Sorting, as CPython 2.2's samplesort: binary insertion below 100 items
// (the case the games hit) with the same sequence of comparisons; larger lists
// use a merge sort, which only differs for items that compare equal.

// Returns -1 if x < y, else 0; 2 (CMPERROR) on error
static int docompare(Object *x, Object *y, Object *cmpFunc) {
	if (!cmpFunc) {
		int i = richCompareBool(x, y, kLT);
		if (i < 0)
			return 2;
		return -i;
	}
	ObjRef res = callFunction(cmpFunc, 2, x, y);
	if (!res)
		return 2;
	if (!isInstanceOf(res.get(), &g_intType)) {
		setErrorString(g_excTypeError, "comparison function must return int");
		return 2;
	}
	int32 i = ((Int *)res.get())->_value;
	return i < 0 ? -1 : i > 0 ? 1 : 0;
}

static bool binarySort(Object **lo, Object **hi, Object **start, Object *cmpFunc) {
	if (lo == start)
		++start;
	for (; start < hi; ++start) {
		Object **l = lo, **r = start, **p;
		Object *pivot = *r;
		do {
			p = l + ((r - l) >> 1);
			int k = docompare(pivot, *p, cmpFunc);
			if (k == 2)
				return false;
			if (k < 0)
				r = p;
			else
				l = p + 1;
		} while (l < r);
		for (p = start; p > l; --p)
			*p = *(p - 1);
		*l = pivot;
	}
	return true;
}

static bool mergeSort(Object **a, uint32 n, Object **tmp, Object *cmpFunc) {
	if (n < 100)
		return binarySort(a, a + n, a, cmpFunc);
	uint32 half = n / 2;
	if (!mergeSort(a, half, tmp, cmpFunc) || !mergeSort(a + half, n - half, tmp, cmpFunc))
		return false;
	uint32 i = 0, j = half, k = 0;
	while (i < half && j < n) {
		int c = docompare(a[j], a[i], cmpFunc);
		if (c == 2)
			return false;
		if (c < 0)
			tmp[k++] = a[j++];
		else
			tmp[k++] = a[i++];
	}
	while (i < half)
		tmp[k++] = a[i++];
	while (j < n)
		tmp[k++] = a[j++];
	for (uint32 m = 0; m < n; m++)
		a[m] = tmp[m];
	return true;
}

int listSort(List *l, Object *cmpFunc) {
	// Sort a private copy so that a comparison changing the list cannot break the sort
	Common::Array<Object *> items;
	items.swap(l->_items);
	uint32 n = items.size();
	bool ok = true;
	if (n > 1) {
		Common::Array<Object *> tmp;
		tmp.resize(n);
		ok = mergeSort(&items[0], n, &tmp[0], cmpFunc);
	}
	if (!l->_items.empty()) {
		// The list was changed during the sort
		Common::Array<Object *> junk;
		junk.swap(l->_items);
		for (uint32 i = 0; i < junk.size(); i++)
			decRef(junk[i]);
		if (ok) {
			setErrorString(g_excTypeError, "a list cannot be modified while it is being sorted");
			ok = false;
		}
	}
	l->_items.swap(items);
	return ok ? 0 : -1;
}

static ObjRef listSortMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("sort", args, 0, 1))
		return ObjRef();
	Object *cmpFunc = args->_size ? args->get(0) : nullptr;
	if (cmpFunc == g_none)
		cmpFunc = nullptr;
	if (listSort((List *)selfObj, cmpFunc) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

ObjRef listAsTuple(List *l) {
	Ref<Tuple> t = newTuple(l->size());
	for (uint32 i = 0; i < l->size(); i++)
		t->set(i, l->get(i));
	return t;
}

static const MethodDef g_listMethods[] = {
	{ "append", listAppend, kMethVarArgs, nullptr },
	{ "insert", listInsertMethod, kMethVarArgs, nullptr },
	{ "extend", listExtend, kMethVarArgs, nullptr },
	{ "pop", listPop, kMethVarArgs, nullptr },
	{ "remove", listRemove, kMethVarArgs, nullptr },
	{ "index", listIndex, kMethVarArgs, nullptr },
	{ "count", listCount, kMethVarArgs, nullptr },
	{ "reverse", listReverseMethod, kMethVarArgs, nullptr },
	{ "sort", listSortMethod, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// list.__new__ makes an empty list; list.__init__ fills it, as in CPython 2.2
static ObjRef listNew(Type *type, Tuple *, Dict *) {
	return ObjRef(type == &g_listType ? new List() : new List(type));
}

static int listInit(Object *self, Tuple *args, Dict *kw) {
	if (!noKeywords("list", kw) || !checkArgs("list", args, 0, 1))
		return -1;
	List *l = (List *)self;
	l->clear();
	if (args->_size) {
		ObjRef items = sequenceList(args->get(0));
		if (!items)
			return -1;
		List *src = (List *)items.get();
		for (uint32 i = 0; i < src->size(); i++)
			l->append(src->get(i));
	}
	return 0;
}

// ---- Extended slices and subscripts ----------------------------------------

Slice::Slice(Object *start, Object *stop, Object *step) : Object(&g_sliceType),
	_start(start ? start : g_none), _stop(stop ? stop : g_none), _step(step ? step : g_none) {
}

static ObjRef sliceRepr(Object *o) {
	Slice *s = (Slice *)o;
	ObjRef a = repr(s->_start.get()), b = repr(s->_stop.get()), c = repr(s->_step.get());
	if (!a || !b || !c)
		return ObjRef();
	return newStr("slice(" + ((Str *)a.get())->toString() + ", " + ((Str *)b.get())->toString() + ", " + ((Str *)c.get())->toString() + ")");
}

static ObjRef sliceNew(Type *, Tuple *args, Dict *kw) {
	if (!noKeywords("slice", kw) || !checkArgs("slice", args, 1, 3))
		return ObjRef();
	if (args->_size == 1)
		return ObjRef(new Slice(g_none, args->get(0), g_none));
	return ObjRef(new Slice(args->get(0), args->get(1), args->_size > 2 ? args->get(2) : g_none));
}

static ObjRef sliceStart(Object *o) { return ((Slice *)o)->_start; }
static ObjRef sliceStop(Object *o) { return ((Slice *)o)->_stop; }
static ObjRef sliceStep(Object *o) { return ((Slice *)o)->_step; }

static const GetSetDef g_sliceGetSets[] = {
	{ "start", sliceStart, nullptr },
	{ "stop", sliceStop, nullptr },
	{ "step", sliceStep, nullptr },
	{ nullptr, nullptr, nullptr }
};

// ---- xrange ------------------------------------------------------------------

static ObjRef xrangeItem(Object *o, int32 i) {
	XRange *r = (XRange *)o;
	if (i < 0 || i >= r->_len * r->_reps) {
		setErrorString(g_excIndexError, "xrange object index out of range");
		return ObjRef();
	}
	return newInt(r->_start + (i % r->_len) * r->_step);
}

static int32 xrangeLength(Object *o) {
	XRange *r = (XRange *)o;
	return r->_len * r->_reps;
}

static ObjRef xrangeRepr(Object *o) {
	XRange *r = (XRange *)o;
	if (r->_start == 0 && r->_step == 1)
		return newStr(Common::String::format("xrange(%d)", r->_start + r->_len * r->_step));
	if (r->_step == 1)
		return newStr(Common::String::format("xrange(%d, %d)", r->_start, r->_start + r->_len * r->_step));
	return newStr(Common::String::format("xrange(%d, %d, %d)", r->_start, r->_start + r->_len * r->_step, r->_step));
}

static ObjRef xrangeNew(Type *, Tuple *args, Dict *kw) {
	if (!noKeywords("xrange", kw) || !checkArgs("xrange", args, 1, 3))
		return ObjRef();
	int32 ilow = 0, ihigh, istep = 1;
	if (args->_size == 1) {
		if (!asInt(args->get(0), &ihigh))
			return ObjRef();
	} else {
		if (!asInt(args->get(0), &ilow) || !asInt(args->get(1), &ihigh))
			return ObjRef();
		if (args->_size > 2 && !asInt(args->get(2), &istep))
			return ObjRef();
	}
	if (istep == 0) {
		setErrorString(g_excValueError, "xrange() arg 3 must not be zero");
		return ObjRef();
	}
	int32 n;
	if (istep > 0)
		n = ilow < ihigh ? (int32)(((int64)ihigh - ilow - 1) / istep + 1) : 0;
	else
		n = ilow > ihigh ? (int32)(((int64)ilow - ihigh - 1) / -(int64)istep + 1) : 0;
	XRange *r = new XRange();
	r->_start = ilow;
	r->_len = n;
	r->_step = istep;
	return ObjRef(r);
}

// ---- Iterators -----------------------------------------------------------------

static ObjRef seqIterNext(Object *o) {
	SeqIter *it = (SeqIter *)o;
	if (!it->_seq)
		return ObjRef();
	Object *seq = it->_seq.get();
	if (isType(seq, &g_listType)) {
		List *l = (List *)seq;
		if (it->_index < (int32)l->size())
			return ObjRef(l->get(it->_index++));
		it->_seq.reset();
		return ObjRef();
	}
	if (isType(seq, &g_tupleType)) {
		Tuple *t = (Tuple *)seq;
		if (it->_index < (int32)t->_size)
			return ObjRef(t->get(it->_index++));
		it->_seq.reset();
		return ObjRef();
	}
	ObjRef r = sequenceGetItem(seq, it->_index);
	if (r) {
		it->_index++;
		return r;
	}
	if (errorMatches(g_excIndexError) || errorMatches(g_excStopIteration)) {
		clearError();
		it->_seq.reset();
	}
	return ObjRef();
}

static ObjRef callIterNext(Object *o) {
	CallIter *it = (CallIter *)o;
	if (!it->_callable)
		return ObjRef();
	ObjRef r = callObject(it->_callable.get(), nullptr);
	if (!r) {
		if (errorMatches(g_excStopIteration)) {
			clearError();
			it->_callable.reset();
		}
		return ObjRef();
	}
	int ok = richCompareBool(r.get(), it->_sentinel.get(), kEQ);
	if (ok == 0)
		return r;
	if (ok > 0)
		it->_callable.reset();
	return ObjRef();
}

static ObjRef iterSelfSeq(Object *o) {
	return ObjRef(o);
}

static SequenceMethods g_tupleAsSequence, g_listAsSequence, g_xrangeAsSequence;

void initSequenceTypes() {
	SequenceMethods &ts = g_tupleAsSequence;
	ts.length = tupleLength;
	ts.concat = tupleConcat;
	ts.repeat = tupleRepeat;
	ts.item = tupleItem;
	ts.slice = tupleSlice;
	ts.contains = tupleContains;
	Type &tt = g_tupleType;
	tt._flags |= kTypeBaseType;
	tt.asSequence = &ts;
	tt.repr = tupleRepr;
	tt.hash = tupleHash;
	tt.richCompare = tupleRichCompare;
	tt.newObj = tupleNew;
	tt.getAttr = genericGetAttr;

	SequenceMethods &ls = g_listAsSequence;
	ls.length = listLength;
	ls.concat = listConcat;
	ls.repeat = listRepeat;
	ls.item = listItem;
	ls.slice = listSliceObj;
	ls.assItem = listAssItem;
	ls.assSlice = listAssSlice;
	ls.contains = listContains;
	ls.inplaceConcat = listInplaceConcat;
	ls.inplaceRepeat = listInplaceRepeat;
	Type &lt = g_listType;
	lt._flags |= kTypeBaseType;
	lt.asSequence = &ls;
	lt.repr = listRepr;
	lt.hash = listHash;
	lt.richCompare = listRichCompare;
	lt.methods = g_listMethods;
	lt.newObj = listNew;
	lt.init = listInit;
	lt.getAttr = genericGetAttr;

	g_sliceType.repr = sliceRepr;
	g_sliceType.newObj = sliceNew;
	g_sliceType.getSets = g_sliceGetSets;
	g_sliceType.getAttr = genericGetAttr;

	g_xrangeAsSequence.length = xrangeLength;
	g_xrangeAsSequence.item = xrangeItem;
	g_xrangeType.asSequence = &g_xrangeAsSequence;
	g_xrangeType.repr = xrangeRepr;
	g_xrangeType.newObj = xrangeNew;
	g_xrangeType.getAttr = genericGetAttr;

	g_seqIterType.iter = iterSelfSeq;
	g_seqIterType.iterNext = seqIterNext;
	g_seqIterType.methods = iteratorMethods();
	g_seqIterType.getAttr = genericGetAttr;
	g_callIterType.iter = iterSelfSeq;
	g_callIterType.iterNext = callIterNext;
	g_callIterType.methods = iteratorMethods();
	g_callIterType.getAttr = genericGetAttr;
}

} // End of namespace Python
} // End of namespace Yaga
