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

// CPython 2.2's dictobject.c: the same table sizes, probe sequence, resize
// rule and dummy entries, so that keys come out in the order the games saw.

namespace Yaga {
namespace Python {

Type g_dictType("dict", &g_objectType);
Type g_dictIterType("dictionary-iterator", &g_objectType);

static NoneObject g_dummyObj(&g_noneType);
Object *g_dictDummy = &g_dummyObj;

enum {
	kMinSize = 8,
	kPerturbShift = 5
};

Dict::Dict() : Object(&g_dictType), _fill(0), _used(0), _mask(kMinSize - 1), _table(_smallTable) {
	memset(_smallTable, 0, sizeof(_smallTable));
}

Dict::Dict(Type *t) : Object(t), _fill(0), _used(0), _mask(kMinSize - 1), _table(_smallTable) {
	memset(_smallTable, 0, sizeof(_smallTable));
}

Dict::~Dict() {
	int32 fill = _fill;
	for (DictEntry *ep = _table; fill > 0; ep++) {
		if (ep->key) {
			--fill;
			if (ep->key != g_dictDummy)
				decRef(ep->key);
			if (ep->value)
				decRef(ep->value);
		}
	}
	if (_table != _smallTable)
		free(_table);
}

Ref<Dict> newDict() {
	return Ref<Dict>(new Dict());
}

DictEntry *Dict::lookup(Object *key, int32 hashValue) {
	uint32 mask = _mask;
	DictEntry *ep0 = _table;
	uint32 i = (uint32)hashValue & mask;
	DictEntry *ep = &ep0[i];
	DictEntry *freeslot;
	bool mustRestore = false, checkedError = false;
	ObjRef errType, errValue, errTb;
	if (!ep->key || ep->key == key)
		return ep;
	if (ep->key == g_dictDummy) {
		freeslot = ep;
	} else {
		if (ep->hash == hashValue) {
			checkedError = true;
			if (errorOccurred()) {
				mustRestore = true;
				fetchError(&errType, &errValue, &errTb);
			}
			Object *startkey = ep->key;
			incRef(startkey);
			int cmp = richCompareBool(startkey, key, kEQ);
			if (cmp < 0)
				clearError();
			bool unchanged = ep0 == _table && ep->key == startkey;
			decRef(startkey);
			if (unchanged) {
				if (cmp > 0)
					goto done;
			} else {
				// The comparison changed the dict: start over
				ep = lookup(key, hashValue);
				goto done;
			}
		}
		freeslot = nullptr;
	}
	for (uint32 perturb = (uint32)hashValue;; perturb >>= kPerturbShift) {
		i = (i << 2) + i + perturb + 1;
		ep = &ep0[i & mask];
		if (!ep->key) {
			if (freeslot)
				ep = freeslot;
			break;
		}
		if (ep->key == key)
			break;
		if (ep->hash == hashValue && ep->key != g_dictDummy) {
			if (!checkedError) {
				checkedError = true;
				if (errorOccurred()) {
					mustRestore = true;
					fetchError(&errType, &errValue, &errTb);
				}
			}
			Object *startkey = ep->key;
			incRef(startkey);
			int cmp = richCompareBool(startkey, key, kEQ);
			if (cmp < 0)
				clearError();
			bool unchanged = ep0 == _table && ep->key == startkey;
			decRef(startkey);
			if (unchanged) {
				if (cmp > 0)
					break;
			} else {
				ep = lookup(key, hashValue);
				break;
			}
		} else if (ep->key == g_dictDummy && !freeslot) {
			freeslot = ep;
		}
	}
done:
	if (mustRestore)
		restoreError(errType.get(), errValue.get(), errTb.get());
	return ep;
}

// Takes over a reference to key and to value
void Dict::insert(Object *key, int32 hashValue, Object *value) {
	DictEntry *ep = lookup(key, hashValue);
	if (ep->value) {
		Object *old = ep->value;
		ep->value = value;
		decRef(old);
		decRef(key);
	} else {
		if (!ep->key)
			_fill++;
		else if (ep->key != g_dictDummy)
			decRef(ep->key);
		ep->key = key;
		ep->hash = hashValue;
		ep->value = value;
		_used++;
	}
}

int Dict::resize(int32 minUsed) {
	int32 newSize;
	for (newSize = kMinSize; newSize <= minUsed && newSize > 0; newSize <<= 1)
		;
	DictEntry *oldTable = _table;
	bool isOldSmall = oldTable == _smallTable;
	DictEntry smallCopy[kMinSize];
	if (isOldSmall) {
		memcpy(smallCopy, _smallTable, sizeof(smallCopy));
		oldTable = smallCopy;
	}
	DictEntry *newTable;
	if (newSize == kMinSize) {
		newTable = _smallTable;
	} else {
		newTable = (DictEntry *)malloc(sizeof(DictEntry) * newSize);
	}
	memset(newTable, 0, sizeof(DictEntry) * newSize);
	_table = newTable;
	_mask = newSize - 1;
	_used = 0;
	int32 i = _fill;
	_fill = 0;
	for (DictEntry *ep = oldTable; i > 0; ep++) {
		if (ep->value) {
			--i;
			insert(ep->key, ep->hash, ep->value);
		} else if (ep->key) {
			--i; // a dummy
		}
	}
	if (!isOldSmall)
		free(oldTable);
	return 0;
}

Object *Dict::getItem(Object *key) {
	int32 h;
	if (isType(key, &g_strType) && ((Str *)key)->_hashed) {
		h = ((Str *)key)->_hash;
	} else if (!hash(key, &h)) {
		clearError();
		return nullptr;
	}
	return lookup(key, h)->value;
}

Object *Dict::getItemString(const char *key) {
	Ref<Str> k = newStr(key);
	return getItem(k.get());
}

int Dict::setItem(Object *key, Object *value) {
	int32 h;
	if (!hash(key, &h))
		return -1;
	int32 nUsed = _used;
	incRef(value);
	incRef(key);
	insert(key, h, value);
	if (!(_used > nUsed && _fill * 3 >= (_mask + 1) * 2))
		return 0;
	return resize(_used * 2);
}

int Dict::setItemString(const char *key, Object *value) {
	Ref<Str> k = internStr(key);
	return setItem(k.get(), value);
}

int Dict::delItem(Object *key) {
	int32 h;
	if (!hash(key, &h))
		return -1;
	DictEntry *ep = lookup(key, h);
	if (!ep->value) {
		setError(g_excKeyError, key);
		return -1;
	}
	Object *oldKey = ep->key;
	Object *oldValue = ep->value;
	ep->key = g_dictDummy;
	ep->value = nullptr;
	_used--;
	decRef(oldValue);
	decRef(oldKey);
	return 0;
}

void Dict::clear() {
	DictEntry *oldTable = _table;
	int32 fill = _fill;
	bool wasSmall = oldTable == _smallTable;
	DictEntry smallCopy[kMinSize];
	if (wasSmall) {
		memcpy(smallCopy, _smallTable, sizeof(smallCopy));
		oldTable = smallCopy;
	}
	memset(_smallTable, 0, sizeof(_smallTable));
	_table = _smallTable;
	_mask = kMinSize - 1;
	_used = _fill = 0;
	for (DictEntry *ep = oldTable; fill > 0; ep++) {
		if (ep->key) {
			--fill;
			if (ep->key != g_dictDummy)
				decRef(ep->key);
			if (ep->value)
				decRef(ep->value);
		}
	}
	if (!wasSmall)
		free(oldTable);
}

bool Dict::next(int32 *pos, Object **key, Object **value) const {
	int32 i = *pos;
	if (i < 0)
		return false;
	while (i <= _mask && !_table[i].value)
		i++;
	*pos = i + 1;
	if (i > _mask)
		return false;
	if (key)
		*key = _table[i].key;
	if (value)
		*value = _table[i].value;
	return true;
}

// ---- Methods ---------------------------------------------------------------

ObjRef dictKeys(Dict *d) {
	Ref<List> l = newList();
	int32 pos = 0;
	Object *k;
	while (d->next(&pos, &k, nullptr))
		l->append(k);
	return l;
}

ObjRef dictValues(Dict *d) {
	Ref<List> l = newList();
	int32 pos = 0;
	Object *v;
	while (d->next(&pos, nullptr, &v))
		l->append(v);
	return l;
}

ObjRef dictItems(Dict *d) {
	Ref<List> l = newList();
	int32 pos = 0;
	Object *k, *v;
	while (d->next(&pos, &k, &v)) {
		Ref<Tuple> t = tuplePack(2, k, v);
		l->append(t.get());
	}
	return l;
}

int dictMerge(Dict *a, Object *b, bool override) {
	if (isInstanceOf(b, &g_dictType)) {
		Dict *other = (Dict *)b;
		if (other == a || other->_used == 0)
			return 0;
		if ((a->_fill + other->_used) * 3 >= (a->_mask + 1) * 2) {
			if (a->resize((a->_used + other->_used) * 3 / 2) != 0)
				return -1;
		}
		for (int32 i = 0; i <= other->_mask; i++) {
			DictEntry *entry = &other->_table[i];
			if (entry->value && (override || !a->getItem(entry->key))) {
				incRef(entry->key);
				incRef(entry->value);
				a->insert(entry->key, entry->hash, entry->value);
			}
		}
		return 0;
	}
	ObjRef keys = callMethod(b, "keys", 0);
	if (!keys)
		return -1;
	ObjRef it = getIter(keys.get());
	if (!it)
		return -1;
	for (;;) {
		ObjRef key = iterNext(it.get());
		if (!key)
			return errorOccurred() ? -1 : 0;
		if (!override && a->getItem(key.get()))
			continue;
		ObjRef value = getItem(b, key.get());
		if (!value)
			return -1;
		if (a->setItem(key.get(), value.get()) < 0)
			return -1;
	}
}

ObjRef dictCopy(Dict *d) {
	Ref<Dict> copy = newDict();
	if (dictMerge(copy.get(), d, true) < 0)
		return ObjRef();
	return copy;
}

#define DICT_SELF Dict *self = (Dict *)selfObj;

static ObjRef dictKeysMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("keys", args, 0, 0))
		return ObjRef();
	return dictKeys((Dict *)selfObj);
}

static ObjRef dictValuesMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("values", args, 0, 0))
		return ObjRef();
	return dictValues((Dict *)selfObj);
}

static ObjRef dictItemsMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("items", args, 0, 0))
		return ObjRef();
	return dictItems((Dict *)selfObj);
}

static ObjRef dictHasKey(Object *selfObj, Tuple *args, Dict *) {
	DICT_SELF
	if (!checkOneArg("has_key", args))
		return ObjRef();
	int32 h;
	if (!hash(args->get(0), &h))
		return ObjRef();
	return newBool(self->lookup(args->get(0), h)->value != nullptr);
}

static ObjRef dictGet(Object *selfObj, Tuple *args, Dict *) {
	DICT_SELF
	if (!checkArgs("get", args, 1, 2))
		return ObjRef();
	int32 h;
	if (!hash(args->get(0), &h))
		return ObjRef();
	Object *v = self->lookup(args->get(0), h)->value;
	if (!v)
		v = args->_size > 1 ? args->get(1) : g_none;
	return ObjRef(v);
}

static ObjRef dictSetdefault(Object *selfObj, Tuple *args, Dict *) {
	DICT_SELF
	if (!checkArgs("setdefault", args, 1, 2))
		return ObjRef();
	int32 h;
	if (!hash(args->get(0), &h))
		return ObjRef();
	Object *v = self->lookup(args->get(0), h)->value;
	if (!v) {
		v = args->_size > 1 ? args->get(1) : g_none;
		if (self->setItem(args->get(0), v) < 0)
			return ObjRef();
	}
	return ObjRef(v);
}

static ObjRef dictClearMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("clear", args, 0, 0))
		return ObjRef();
	((Dict *)selfObj)->clear();
	return ObjRef(g_none);
}

static ObjRef dictCopyMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("copy", args, 0, 0))
		return ObjRef();
	return dictCopy((Dict *)selfObj);
}

static ObjRef dictUpdate(Object *selfObj, Tuple *args, Dict *) {
	if (!checkOneArg("update", args))
		return ObjRef();
	if (dictMerge((Dict *)selfObj, args->get(0), true) < 0)
		return ObjRef();
	return ObjRef(g_none);
}

static ObjRef dictPopitem(Object *selfObj, Tuple *args, Dict *) {
	DICT_SELF
	if (!checkArgs("popitem", args, 0, 0))
		return ObjRef();
	if (self->_used == 0) {
		setErrorString(g_excKeyError, "popitem(): dictionary is empty");
		return ObjRef();
	}
	int32 i = 0;
	DictEntry *ep = &self->_table[0];
	if (!ep->value) {
		i = ep->hash;
		if (i > self->_mask || i < 1)
			i = 1;
		while (!(ep = &self->_table[i])->value) {
			i++;
			if (i > self->_mask)
				i = 1;
		}
	}
	Ref<Tuple> res = tuplePack(2, ep->key, ep->value);
	decRef(ep->key);
	decRef(ep->value);
	ep->key = g_dictDummy;
	ep->value = nullptr;
	self->_used--;
	self->_table[0].hash = i + 1;
	return res;
}

static ObjRef dictIterkeys(Object *selfObj, Tuple *, Dict *) { return ObjRef(new DictIter((Dict *)selfObj, 0)); }
static ObjRef dictItervalues(Object *selfObj, Tuple *, Dict *) { return ObjRef(new DictIter((Dict *)selfObj, 1)); }
static ObjRef dictIteritems(Object *selfObj, Tuple *, Dict *) { return ObjRef(new DictIter((Dict *)selfObj, 2)); }

static const MethodDef g_dictMethods[] = {
	{ "has_key", dictHasKey, kMethVarArgs, nullptr },
	{ "get", dictGet, kMethVarArgs, nullptr },
	{ "setdefault", dictSetdefault, kMethVarArgs, nullptr },
	{ "popitem", dictPopitem, kMethVarArgs, nullptr },
	{ "keys", dictKeysMethod, kMethVarArgs, nullptr },
	{ "items", dictItemsMethod, kMethVarArgs, nullptr },
	{ "values", dictValuesMethod, kMethVarArgs, nullptr },
	{ "update", dictUpdate, kMethVarArgs, nullptr },
	{ "clear", dictClearMethod, kMethVarArgs, nullptr },
	{ "copy", dictCopyMethod, kMethVarArgs, nullptr },
	{ "iterkeys", dictIterkeys, kMethVarArgs, nullptr },
	{ "itervalues", dictItervalues, kMethVarArgs, nullptr },
	{ "iteritems", dictIteritems, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// ---- Slots -----------------------------------------------------------------

static int32 dictLength(Object *o) {
	return ((Dict *)o)->_used;
}

static ObjRef dictSubscript(Object *o, Object *key) {
	Dict *d = (Dict *)o;
	int32 h;
	if (!hash(key, &h))
		return ObjRef();
	Object *v = d->lookup(key, h)->value;
	if (!v) {
		setError(g_excKeyError, key);
		return ObjRef();
	}
	return ObjRef(v);
}

static int dictAssSubscript(Object *o, Object *key, Object *value) {
	if (value)
		return ((Dict *)o)->setItem(key, value);
	return ((Dict *)o)->delItem(key);
}

static int dictContains(Object *o, Object *key) {
	int32 h;
	if (!hash(key, &h))
		return -1;
	return ((Dict *)o)->lookup(key, h)->value != nullptr;
}

static ObjRef dictRepr(Object *o) {
	Dict *d = (Dict *)o;
	if (d->_used == 0)
		return newStr("{}");
	if (!enterRecursiveCall())
		return ObjRef();
	Common::String out = "{";
	int32 pos = 0;
	Object *k, *v;
	bool first = true;
	while (d->next(&pos, &k, &v)) {
		ObjRef kk(k), vv(v);
		ObjRef kr = repr(k);
		if (!kr) {
			leaveRecursiveCall();
			return ObjRef();
		}
		ObjRef vr = repr(v);
		if (!vr) {
			leaveRecursiveCall();
			return ObjRef();
		}
		if (!first)
			out += ", ";
		first = false;
		out += ((Str *)kr.get())->toString();
		out += ": ";
		out += ((Str *)vr.get())->toString();
	}
	leaveRecursiveCall();
	out += "}";
	return newStr(out);
}

// Equality of two dicts, as CPython's dict_equal
static int dictEqual(Dict *a, Dict *b) {
	if (a->_used != b->_used)
		return 0;
	for (int32 i = 0; i <= a->_mask; i++) {
		Object *aval = a->_table[i].value;
		if (!aval)
			continue;
		Object *key = a->_table[i].key;
		ObjRef kk(key), av(aval);
		Object *bval = b->getItem(key);
		if (!bval)
			return 0;
		int cmp = richCompareBool(aval, bval, kEQ);
		if (cmp <= 0)
			return cmp;
	}
	return 1;
}

// The smallest key in a for which b's value differs or is missing, as
// CPython's characterize; returns false on error
static bool characterize(Dict *a, Dict *b, ObjRef *pkey, ObjRef *pval) {
	ObjRef akey, aval;
	for (int32 i = 0; i <= a->_mask; i++) {
		Object *thiskey = a->_table[i].key;
		Object *thisaval = a->_table[i].value;
		if (!thisaval)
			continue;
		ObjRef tk(thiskey), tv(thisaval);
		if (akey) {
			int cmp = richCompareBool(akey.get(), thiskey, kLT);
			if (cmp < 0)
				return false;
			if (cmp > 0)
				continue;
		}
		Object *thisbval = b->getItem(thiskey);
		int cmp;
		if (!thisbval) {
			cmp = 0;
		} else {
			cmp = richCompareBool(thisaval, thisbval, kEQ);
			if (cmp < 0)
				return false;
		}
		if (cmp == 0) {
			akey = tk;
			aval = tv;
		}
	}
	*pkey = akey;
	*pval = aval;
	return true;
}

static int dictCompareSlot(Object *ao, Object *bo) {
	Dict *a = (Dict *)ao, *b = (Dict *)bo;
	if (a->_used < b->_used)
		return -1;
	if (a->_used > b->_used)
		return 1;
	ObjRef adiff, aval, bdiff, bval;
	if (!characterize(a, b, &adiff, &aval))
		return -1;
	if (!adiff)
		return 0;
	if (!characterize(b, a, &bdiff, &bval))
		return -1;
	int res = 0;
	if (bdiff) {
		res = compare(adiff.get(), bdiff.get());
		if (res == 0 && !errorOccurred())
			res = compare(aval.get(), bval.get());
	}
	return res;
}

static ObjRef dictRichCompare(Object *v, Object *w, int op) {
	if (!isInstanceOf(v, &g_dictType) || !isInstanceOf(w, &g_dictType) || (op != kEQ && op != kNE))
		return ObjRef(g_notImplemented);
	int cmp = dictEqual((Dict *)v, (Dict *)w);
	if (cmp < 0)
		return ObjRef();
	return newBool((cmp != 0) == (op == kEQ));
}

static ObjRef dictIterSlot(Object *o) {
	return ObjRef(new DictIter((Dict *)o, 0));
}

// dict.__new__ makes an empty dict; dict.__init__ fills it, as in CPython 2.2
static ObjRef dictNew(Type *type, Tuple *, Dict *) {
	return ObjRef(type == &g_dictType ? new Dict() : new Dict(type));
}

static int dictInit(Object *self, Tuple *args, Dict *kw) {
	if (!noKeywords("dict", kw) || !checkArgs("dict", args, 0, 1))
		return -1;
	Dict *d = (Dict *)self;
	if (args->_size == 1) {
		Object *arg = args->get(0);
		if (hasAttrString(arg, "keys")) {
			if (dictMerge(d, arg, true) < 0)
				return -1;
		} else {
			// A sequence of pairs
			ObjRef it = getIter(arg);
			if (!it)
				return -1;
			for (int32 i = 0;; i++) {
				ObjRef item = iterNext(it.get());
				if (!item) {
					if (errorOccurred())
						return -1;
					break;
				}
				ObjRef pair = sequenceTuple(item.get());
				if (!pair) {
					clearError();
					setErrorFormat(g_excTypeError, "cannot convert dictionary update sequence element #%d to a sequence", i);
					return -1;
				}
				Tuple *t = (Tuple *)pair.get();
				if (t->_size != 2) {
					setErrorFormat(g_excValueError, "dictionary update sequence element #%d has length %u; 2 is required", i, t->_size);
					return -1;
				}
				if (d->setItem(t->get(0), t->get(1)) < 0)
					return -1;
			}
		}
	}
	return 0;
}

static bool dictHashSlot(Object *, int32 *) {
	setErrorString(g_excTypeError, "dict objects are unhashable");
	return false;
}

// ---- Iterator ----------------------------------------------------------------

DictIter::DictIter(Dict *d, int kind) : Object(&g_dictIterType), _dict(d), _pos(0), _used(d->_used), _kind(kind) {
}

static ObjRef dictIterNext(Object *o) {
	DictIter *it = (DictIter *)o;
	if (it->_used != it->_dict->_used) {
		setErrorString(g_excRuntimeError, "dictionary changed size during iteration");
		return ObjRef();
	}
	Object *k, *v;
	if (!it->_dict->next(&it->_pos, &k, &v))
		return ObjRef();
	if (it->_kind == 0)
		return ObjRef(k);
	if (it->_kind == 1)
		return ObjRef(v);
	return tuplePack(2, k, v);
}

static ObjRef iterSelf(Object *o) {
	return ObjRef(o);
}

static ObjRef iterNextMethod(Object *selfObj, Tuple *, Dict *) {
	ObjRef r = selfObj->_type->iterNext(selfObj);
	if (!r && !errorOccurred())
		setError(g_excStopIteration, g_none);
	return r;
}

static const MethodDef g_iterMethods[] = {
	{ "next", iterNextMethod, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static MappingMethods g_dictAsMapping;
static SequenceMethods g_dictAsSequence;

void initDictType() {
	g_dictAsMapping.length = dictLength;
	g_dictAsMapping.subscript = dictSubscript;
	g_dictAsMapping.assSubscript = dictAssSubscript;
	g_dictAsSequence.contains = dictContains;

	Type &t = g_dictType;
	t._flags |= kTypeBaseType;
	t.asMapping = &g_dictAsMapping;
	t.asSequence = &g_dictAsSequence;
	t.repr = dictRepr;
	t.compare = dictCompareSlot;
	t.richCompare = dictRichCompare;
	t.hash = dictHashSlot;
	t.iter = dictIterSlot;
	t.methods = g_dictMethods;
	t.newObj = dictNew;
	t.init = dictInit;

	t.getAttr = genericGetAttr;

	g_dictIterType.iter = iterSelf;
	g_dictIterType.iterNext = dictIterNext;
	g_dictIterType.methods = g_iterMethods;
	g_dictIterType.getAttr = genericGetAttr;
}

const MethodDef *iteratorMethods() {
	return g_iterMethods;
}

} // End of namespace Python
} // End of namespace Yaga
