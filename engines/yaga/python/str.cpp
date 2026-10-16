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

Type g_strType("str", &g_objectType);

Str::Str(const char *data, uint32 len) : Object(&g_strType), _len(len), _hash(-1), _hashed(false), _interned(false) {
	_data = (char *)malloc(len + 1);
	if (len)
		memcpy(_data, data, len);
	_data[len] = 0;
}

Str::Str(Type *t, const char *data, uint32 len) : Object(t), _len(len), _hash(-1), _hashed(false), _interned(false) {
	_data = (char *)malloc(len + 1);
	if (len)
		memcpy(_data, data, len);
	_data[len] = 0;
}

Str::Str(uint32 len) : Object(&g_strType), _len(len), _hash(-1), _hashed(false), _interned(false) {
	_data = (char *)malloc(len + 1);
	_data[len] = 0;
}

Str::~Str() {
	free(_data);
}

bool Str::equals(const char *s) const {
	return strlen(s) == _len && memcmp(_data, s, _len) == 0;
}

static Str *g_emptyStr = nullptr;
static Str *g_charStrs[256];
static Dict *g_interned = nullptr;

Ref<Str> strFromCharacter(uint8 c) {
	Str *&s = g_charStrs[c];
	if (!s) {
		char ch = (char)c;
		s = new Str(&ch, 1);
		incRef(s);
	}
	return Ref<Str>(s);
}

Ref<Str> newStr(const char *s, uint32 len) {
	if (len == 0) {
		if (!g_emptyStr) {
			g_emptyStr = new Str("", 0);
			incRef(g_emptyStr);
		}
		return Ref<Str>(g_emptyStr);
	}
	if (len == 1)
		return strFromCharacter((uint8)s[0]);
	return Ref<Str>(new Str(s, len));
}

Ref<Str> newStr(const char *s) {
	return newStr(s, strlen(s));
}

Ref<Str> newStr(const Common::String &s) {
	return newStr(s.c_str(), s.size());
}

Ref<Str> intern(Str *s) {
	if (s->_interned || !isType(s, &g_strType))
		return Ref<Str>(s);
	if (!g_interned) {
		g_interned = new Dict();
		incRef(g_interned);
	}
	Object *existing = g_interned->getItem(s);
	if (existing)
		return Ref<Str>((Str *)existing);
	g_interned->setItem(s, s);
	s->_interned = true;
	return Ref<Str>(s);
}

Ref<Str> internStr(const char *s) {
	Ref<Str> str = newStr(s);
	return intern(str.get());
}

// CPython 2.2's string_hash, in 32-bit arithmetic
uint32 strHash(const char *data, uint32 len) {
	const uint8 *p = (const uint8 *)data;
	uint32 x = (uint32)(*p) << 7;
	uint32 n = len;
	while (n-- > 0)
		x = (1000003u * x) ^ *p++;
	x ^= len;
	if ((int32)x == -1)
		x = (uint32)-2;
	return x;
}

static bool strHashSlot(Object *o, int32 *out) {
	Str *s = (Str *)o;
	if (!s->_hashed) {
		s->_hash = (int32)strHash(s->_data, s->_len);
		s->_hashed = true;
	}
	*out = s->_hash;
	return true;
}

ObjRef strRepr(Str *s, bool smartQuotes) {
	char quote = '\'';
	if (smartQuotes && memchr(s->_data, '\'', s->_len) && !memchr(s->_data, '"', s->_len))
		quote = '"';
	Common::String out;
	out += quote;
	for (uint32 i = 0; i < s->_len; i++) {
		uint8 c = (uint8)s->_data[i];
		if (c == quote || c == '\\') {
			out += '\\';
			out += (char)c;
		} else if (c == '\t') {
			out += "\\t";
		} else if (c == '\n') {
			out += "\\n";
		} else if (c == '\r') {
			out += "\\r";
		} else if (c < ' ' || c >= 0x7f) {
			out += Common::String::format("\\x%02x", c);
		} else {
			out += (char)c;
		}
	}
	out += quote;
	return newStr(out);
}

static ObjRef strReprSlot(Object *o) {
	return strRepr((Str *)o);
}

static ObjRef strStrSlot(Object *o) {
	if (isType(o, &g_strType))
		return ObjRef(o);
	Str *s = (Str *)o;
	return newStr(s->_data, s->_len);
}

static int32 strLength(Object *o) {
	return ((Str *)o)->_len;
}

ObjRef strConcat(Str *a, Str *b) {
	if (b->_len == 0 && isType(a, &g_strType))
		return ObjRef(a);
	if (a->_len == 0 && isType(b, &g_strType))
		return ObjRef(b);
	Ref<Str> r(new Str(a->_len + b->_len));
	memcpy(r->_data, a->_data, a->_len);
	memcpy(r->_data + a->_len, b->_data, b->_len);
	return r;
}

static ObjRef strConcatSlot(Object *a, Object *b) {
	if (!isInstanceOf(b, &g_strType)) {
		if (isInstanceOf(b, &g_unicodeType)) {
			ObjRef ua = unicodeFromStr(a, nullptr, nullptr);
			if (!ua)
				return ObjRef();
			return sequenceConcat(ua.get(), b);
		}
		setErrorFormat(g_excTypeError, "cannot concatenate 'str' and '%.200s' objects", typeName(b));
		return ObjRef();
	}
	return strConcat((Str *)a, (Str *)b);
}

static ObjRef strRepeat(Object *o, int32 n) {
	Str *s = (Str *)o;
	if (n < 0)
		n = 0;
	uint64 size = (uint64)s->_len * n;
	if (size > 0x7fffffff) {
		setErrorString(g_excOverflowError, "repeated string is too long");
		return ObjRef();
	}
	if (size == s->_len && isType(o, &g_strType))
		return ObjRef(o);
	if (size == 0)
		return newStr("", 0);
	Ref<Str> r(new Str((uint32)size));
	for (int32 i = 0; i < n; i++)
		memcpy(r->_data + i * s->_len, s->_data, s->_len);
	return r;
}

static ObjRef strItem(Object *o, int32 i) {
	Str *s = (Str *)o;
	if (i < 0 || i >= (int32)s->_len) {
		setErrorString(g_excIndexError, "string index out of range");
		return ObjRef();
	}
	return strFromCharacter((uint8)s->_data[i]);
}

static ObjRef strSlice(Object *o, int32 lo, int32 hi) {
	Str *s = (Str *)o;
	if (lo < 0)
		lo = 0;
	if (hi > (int32)s->_len)
		hi = s->_len;
	if (lo == 0 && hi == (int32)s->_len && isType(o, &g_strType))
		return ObjRef(o);
	if (hi < lo)
		hi = lo;
	return newStr(s->_data + lo, hi - lo);
}

static int strContains(Object *o, Object *el) {
	if (isInstanceOf(el, &g_unicodeType)) {
		ObjRef u = unicodeFromStr(o, nullptr, nullptr);
		if (!u)
			return -1;
		return containsItem(u.get(), el);
	}
	if (!isInstanceOf(el, &g_strType) || ((Str *)el)->_len != 1) {
		setErrorString(g_excTypeError, "'in <string>' requires character as left operand");
		return -1;
	}
	Str *s = (Str *)o;
	return memchr(s->_data, ((Str *)el)->_data[0], s->_len) != nullptr;
}

static ObjRef strRichCompare(Object *a, Object *b, int op) {
	if (!isInstanceOf(a, &g_strType) || !isInstanceOf(b, &g_strType))
		return ObjRef(g_notImplemented);
	Str *x = (Str *)a, *y = (Str *)b;
	if (op == kEQ || op == kNE) {
		bool eq = x->_len == y->_len && memcmp(x->_data, y->_data, x->_len) == 0;
		return newBool(op == kEQ ? eq : !eq);
	}
	uint32 min = MIN(x->_len, y->_len);
	int c = min ? memcmp(x->_data, y->_data, min) : 0;
	if (c == 0)
		c = x->_len < y->_len ? -1 : x->_len > y->_len ? 1 : 0;
	bool r;
	switch (op) {
	case kLT: r = c < 0; break;
	case kLE: r = c <= 0; break;
	case kGT: r = c > 0; break;
	default: r = c >= 0; break;
	}
	return newBool(r);
}

static int strCompare(Object *a, Object *b) {
	Str *x = (Str *)a, *y = (Str *)b;
	uint32 min = MIN(x->_len, y->_len);
	int c = min ? memcmp(x->_data, y->_data, min) : 0;
	if (c)
		return c < 0 ? -1 : 1;
	return x->_len < y->_len ? -1 : x->_len > y->_len ? 1 : 0;
}

// Slicing helpers for find() and friends, as CPython's string_adjust_indices
static void adjustIndices(int32 *start, int32 *end, int32 len) {
	if (*end > len)
		*end = len;
	else if (*end < 0)
		*end += len;
	if (*end < 0)
		*end = 0;
	if (*start < 0)
		*start += len;
	if (*start < 0)
		*start = 0;
}

static bool getSliceArgs(Tuple *args, uint32 first, int32 *start, int32 *end) {
	*start = 0;
	*end = 0x7fffffff;
	if (args->_size > first && args->get(first) != g_none) {
		if (!asInt(args->get(first), start))
			return false;
	}
	if (args->_size > first + 1 && args->get(first + 1) != g_none) {
		if (!asInt(args->get(first + 1), end))
			return false;
	}
	return true;
}

static int32 findSub(Str *s, Str *sub, int32 i, int32 last, bool reverse) {
	int32 n = sub->_len;
	adjustIndices(&i, &last, s->_len);
	if (!reverse) {
		if (n == 0 && i <= last)
			return i;
		last -= n;
		for (; i <= last; ++i)
			if (s->_data[i] == sub->_data[0] && memcmp(&s->_data[i + 1], &sub->_data[1], n - 1) == 0)
				return i;
	} else {
		if (n == 0 && i <= last)
			return last;
		for (int32 j = last - n; j >= i; --j)
			if (s->_data[j] == sub->_data[0] && memcmp(&s->_data[j], sub->_data, n) == 0)
				return j;
	}
	return -1;
}

#define STR_SELF Str *self = (Str *)selfObj;

static Str *strArg(Tuple *args, uint32 i, const char *fname) {
	Object *o = args->get(i);
	if (!isInstanceOf(o, &g_strType)) {
		if (isInstanceOf(o, &g_unicodeType))
			setErrorFormat(g_excTypeError, "%s() unicode arguments are not supported", fname);
		else
			setErrorFormat(g_excTypeError, "expected a character buffer object");
		return nullptr;
	}
	return (Str *)o;
}

static ObjRef doFind(Object *selfObj, Tuple *args, bool reverse, bool raise, const char *name) {
	STR_SELF
	if (!checkArgs(name, args, 1, 3))
		return ObjRef();
	Str *sub = strArg(args, 0, name);
	if (!sub)
		return ObjRef();
	int32 start, end;
	if (!getSliceArgs(args, 1, &start, &end))
		return ObjRef();
	int32 r = findSub(self, sub, start, end, reverse);
	if (r < 0 && raise) {
		setErrorFormat(g_excValueError, "substring not found in string.%s", name);
		return ObjRef();
	}
	return newInt(r);
}

static ObjRef strFind(Object *s, Tuple *args, Dict *) { return doFind(s, args, false, false, "find"); }
static ObjRef strRfind(Object *s, Tuple *args, Dict *) { return doFind(s, args, true, false, "rfind"); }
static ObjRef strIndex(Object *s, Tuple *args, Dict *) { return doFind(s, args, false, true, "index"); }
static ObjRef strRindex(Object *s, Tuple *args, Dict *) { return doFind(s, args, true, true, "rindex"); }

static ObjRef strCount(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	if (!checkArgs("count", args, 1, 3))
		return ObjRef();
	Str *sub = strArg(args, 0, "count");
	if (!sub)
		return ObjRef();
	int32 i, last;
	if (!getSliceArgs(args, 1, &i, &last))
		return ObjRef();
	adjustIndices(&i, &last, self->_len);
	int32 n = sub->_len;
	int32 m = last + 1 - n;
	if (n == 0)
		return newInt(m >= i ? m - i : 0);
	int32 r = 0;
	while (i < m) {
		if (!memcmp(self->_data + i, sub->_data, n)) {
			r++;
			i += n;
		} else {
			i++;
		}
	}
	return newInt(r);
}

static bool isSpaceChar(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static ObjRef splitWhitespace(Str *s, int32 maxsplit) {
	Ref<List> list = newList();
	int32 len = s->_len, i = 0, j;
	const char *d = s->_data;
	for (i = j = 0; i < len;) {
		while (i < len && isSpaceChar(d[i]))
			i++;
		j = i;
		while (i < len && !isSpaceChar(d[i]))
			i++;
		if (j < i) {
			if (maxsplit-- <= 0)
				break;
			Ref<Str> item = newStr(d + j, i - j);
			list->append(item.get());
			while (i < len && isSpaceChar(d[i]))
				i++;
			j = i;
		}
	}
	if (j < len) {
		Ref<Str> item = newStr(d + j, len - j);
		list->append(item.get());
	}
	return list;
}

static ObjRef strSplit(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	if (!checkArgs("split", args, 0, 2))
		return ObjRef();
	int32 maxsplit = -1;
	if (args->_size > 1 && !asInt(args->get(1), &maxsplit))
		return ObjRef();
	if (maxsplit < 0)
		maxsplit = 0x7fffffff;
	if (args->_size == 0 || args->get(0) == g_none)
		return splitWhitespace(self, maxsplit);
	Str *sep = strArg(args, 0, "split");
	if (!sep)
		return ObjRef();
	int32 n = sep->_len;
	if (n == 0) {
		setErrorString(g_excValueError, "empty separator");
		return ObjRef();
	}
	Ref<List> list = newList();
	int32 len = self->_len, i = 0, j = 0;
	const char *d = self->_data;
	while (i + n <= len) {
		if (d[i] == sep->_data[0] && memcmp(d + i, sep->_data, n) == 0) {
			if (maxsplit-- <= 0)
				break;
			Ref<Str> item = newStr(d + j, i - j);
			list->append(item.get());
			i = j = i + n;
		} else {
			i++;
		}
	}
	Ref<Str> item = newStr(d + j, len - j);
	list->append(item.get());
	return list;
}

ObjRef strJoin(Str *sep, Object *orig) {
	ObjRef seq = sequenceTuple(orig);
	if (!seq) {
		if (errorMatches(g_excTypeError)) {
			clearError();
			setErrorFormat(g_excTypeError, "sequence expected, %.80s found", typeName(orig));
		}
		return ObjRef();
	}
	Tuple *t = (Tuple *)seq.get();
	if (t->_size == 0)
		return newStr("", 0);
	if (t->_size == 1 && isType(t->get(0), &g_strType))
		return ObjRef(t->get(0));
	uint32 total = 0;
	for (uint32 i = 0; i < t->_size; i++) {
		Object *item = t->get(i);
		if (!isInstanceOf(item, &g_strType)) {
			if (isInstanceOf(item, &g_unicodeType)) {
				// Defer to unicode.join
				ObjRef usep = unicodeFromStr(sep, nullptr, nullptr);
				if (!usep)
					return ObjRef();
				return callMethod(usep.get(), "join", 1, t);
			}
			setErrorFormat(g_excTypeError, "sequence item %u: expected string, %.80s found", i, typeName(item));
			return ObjRef();
		}
		total += ((Str *)item)->_len;
		if (i)
			total += sep->_len;
	}
	Ref<Str> r(new Str(total));
	char *p = r->_data;
	for (uint32 i = 0; i < t->_size; i++) {
		if (i) {
			memcpy(p, sep->_data, sep->_len);
			p += sep->_len;
		}
		Str *item = (Str *)t->get(i);
		memcpy(p, item->_data, item->_len);
		p += item->_len;
	}
	return r;
}

static ObjRef strJoinMethod(Object *selfObj, Tuple *args, Dict *) {
	if (!checkOneArg("join", args))
		return ObjRef();
	return strJoin((Str *)selfObj, args->get(0));
}

// 0 = both, 1 = left, 2 = right
static ObjRef doStrip(Object *selfObj, Tuple *args, int which, const char *name) {
	STR_SELF
	if (!checkArgs(name, args, 0, 1))
		return ObjRef();
	const char *chars = nullptr;
	uint32 nchars = 0;
	if (args->_size == 1 && args->get(0) != g_none) {
		Str *c = strArg(args, 0, name);
		if (!c)
			return ObjRef();
		chars = c->_data;
		nchars = c->_len;
	}
	const char *d = self->_data;
	int32 len = self->_len, i = 0, j = len;
	if (which != 2) {
		while (i < len && (chars ? memchr(chars, d[i], nchars) != nullptr : isSpaceChar(d[i])))
			i++;
	}
	if (which != 1) {
		while (j > i && (chars ? memchr(chars, d[j - 1], nchars) != nullptr : isSpaceChar(d[j - 1])))
			j--;
	}
	if (i == 0 && j == len && isType(selfObj, &g_strType))
		return ObjRef(selfObj);
	return newStr(d + i, j - i);
}

static ObjRef strStrip(Object *s, Tuple *a, Dict *) { return doStrip(s, a, 0, "strip"); }
static ObjRef strLstrip(Object *s, Tuple *a, Dict *) { return doStrip(s, a, 1, "lstrip"); }
static ObjRef strRstrip(Object *s, Tuple *a, Dict *) { return doStrip(s, a, 2, "rstrip"); }

static char toLowerChar(char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static char toUpperChar(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static bool isLowerChar(char c) { return c >= 'a' && c <= 'z'; }
static bool isUpperChar(char c) { return c >= 'A' && c <= 'Z'; }
static bool isAlphaChar(char c) { return isLowerChar(c) || isUpperChar(c); }
static bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

static ObjRef mapChars(Object *selfObj, Tuple *args, const char *name, int mode) {
	STR_SELF
	if (!checkArgs(name, args, 0, 0))
		return ObjRef();
	Ref<Str> r(new Str(self->_len));
	bool previousCased = false;
	for (uint32 i = 0; i < self->_len; i++) {
		char c = self->_data[i];
		switch (mode) {
		case 0: c = toLowerChar(c); break;
		case 1: c = toUpperChar(c); break;
		case 2: c = isLowerChar(c) ? toUpperChar(c) : isUpperChar(c) ? toLowerChar(c) : c; break;
		case 3: c = i == 0 ? toUpperChar(c) : toLowerChar(c); break;
		case 4:
			if (isLowerChar(c)) {
				if (!previousCased)
					c = toUpperChar(c);
				previousCased = true;
			} else if (isUpperChar(c)) {
				if (previousCased)
					c = toLowerChar(c);
				previousCased = true;
			} else {
				previousCased = false;
			}
			break;
		default: break;
		}
		r->_data[i] = c;
	}
	return r;
}

static ObjRef strLower(Object *s, Tuple *a, Dict *) { return mapChars(s, a, "lower", 0); }
static ObjRef strUpper(Object *s, Tuple *a, Dict *) { return mapChars(s, a, "upper", 1); }
static ObjRef strSwapcase(Object *s, Tuple *a, Dict *) { return mapChars(s, a, "swapcase", 2); }
static ObjRef strCapitalize(Object *s, Tuple *a, Dict *) { return mapChars(s, a, "capitalize", 3); }
static ObjRef strTitle(Object *s, Tuple *a, Dict *) { return mapChars(s, a, "title", 4); }

static ObjRef testChars(Object *selfObj, Tuple *args, const char *name, bool (*pred)(char)) {
	STR_SELF
	if (!checkArgs(name, args, 0, 0))
		return ObjRef();
	if (self->_len == 0)
		return newBool(false);
	for (uint32 i = 0; i < self->_len; i++)
		if (!pred(self->_data[i]))
			return newBool(false);
	return newBool(true);
}

static bool alnumPred(char c) { return isAlphaChar(c) || isDigitChar(c); }
static ObjRef strIsalpha(Object *s, Tuple *a, Dict *) { return testChars(s, a, "isalpha", isAlphaChar); }
static ObjRef strIsdigit(Object *s, Tuple *a, Dict *) { return testChars(s, a, "isdigit", isDigitChar); }
static ObjRef strIsalnum(Object *s, Tuple *a, Dict *) { return testChars(s, a, "isalnum", alnumPred); }
static ObjRef strIsspace(Object *s, Tuple *a, Dict *) { return testChars(s, a, "isspace", isSpaceChar); }

static ObjRef caseTest(Object *selfObj, Tuple *args, const char *name, bool lower) {
	STR_SELF
	if (!checkArgs(name, args, 0, 0))
		return ObjRef();
	bool cased = false;
	for (uint32 i = 0; i < self->_len; i++) {
		char c = self->_data[i];
		if (lower ? isUpperChar(c) : isLowerChar(c))
			return newBool(false);
		if (lower ? isLowerChar(c) : isUpperChar(c))
			cased = true;
	}
	return newBool(cased);
}

static ObjRef strIslower(Object *s, Tuple *a, Dict *) { return caseTest(s, a, "islower", true); }
static ObjRef strIsupper(Object *s, Tuple *a, Dict *) { return caseTest(s, a, "isupper", false); }

static ObjRef strIstitle(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	if (!checkArgs("istitle", args, 0, 0))
		return ObjRef();
	bool cased = false, previousCased = false;
	for (uint32 i = 0; i < self->_len; i++) {
		char c = self->_data[i];
		if (isUpperChar(c)) {
			if (previousCased)
				return newBool(false);
			previousCased = cased = true;
		} else if (isLowerChar(c)) {
			if (!previousCased)
				return newBool(false);
			previousCased = cased = true;
		} else {
			previousCased = false;
		}
	}
	return newBool(cased);
}

static ObjRef pad(Str *self, int32 left, int32 right) {
	if (left < 0)
		left = 0;
	if (right < 0)
		right = 0;
	if (left == 0 && right == 0 && isType(self, &g_strType))
		return ObjRef(self);
	Ref<Str> r(new Str(left + self->_len + right));
	memset(r->_data, ' ', left);
	memcpy(r->_data + left, self->_data, self->_len);
	memset(r->_data + left + self->_len, ' ', right);
	return r;
}

static ObjRef strLjust(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	int32 w;
	if (!checkArgs("ljust", args, 1, 1) || !asInt(args->get(0), &w))
		return ObjRef();
	return pad(self, 0, w - (int32)self->_len);
}

static ObjRef strRjust(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	int32 w;
	if (!checkArgs("rjust", args, 1, 1) || !asInt(args->get(0), &w))
		return ObjRef();
	return pad(self, w - (int32)self->_len, 0);
}

static ObjRef strCenter(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	int32 w;
	if (!checkArgs("center", args, 1, 1) || !asInt(args->get(0), &w))
		return ObjRef();
	int32 marg = w - (int32)self->_len;
	if (marg <= 0)
		return pad(self, 0, 0);
	int32 left = marg / 2 + (marg & w & 1);
	return pad(self, left, marg - left);
}

static ObjRef strZfill(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	int32 w;
	if (!checkArgs("zfill", args, 1, 1) || !asInt(args->get(0), &w))
		return ObjRef();
	if ((int32)self->_len >= w)
		return pad(self, 0, 0);
	int32 fill = w - self->_len;
	ObjRef padded = pad(self, fill, 0);
	Str *r = (Str *)padded.get();
	if (r == self) {
		Ref<Str> copy = newStr(self->_data, self->_len);
		padded = copy;
		r = copy.get();
	}
	memset(r->_data, '0', fill);
	if (r->_data[fill] == '+' || r->_data[fill] == '-') {
		r->_data[0] = r->_data[fill];
		r->_data[fill] = '0';
	}
	return padded;
}

static ObjRef strReplace(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	if (!checkArgs("replace", args, 2, 3))
		return ObjRef();
	Str *from = strArg(args, 0, "replace");
	Str *to = from ? strArg(args, 1, "replace") : nullptr;
	if (!to)
		return ObjRef();
	int32 count = -1;
	if (args->_size > 2 && !asInt(args->get(2), &count))
		return ObjRef();
	if (from->_len == 0) {
		setErrorString(g_excValueError, "empty pattern string");
		return ObjRef();
	}
	if (count < 0)
		count = 0x7fffffff;
	Common::String out;
	uint32 i = 0, n = from->_len;
	bool changed = false;
	while (i < self->_len) {
		if (count > 0 && i + n <= self->_len && memcmp(self->_data + i, from->_data, n) == 0) {
			out += Common::String(to->_data, to->_len);
			i += n;
			count--;
			changed = true;
		} else {
			out += self->_data[i++];
		}
	}
	if (!changed && isType(selfObj, &g_strType))
		return ObjRef(selfObj);
	return newStr(out);
}

static ObjRef doStartsEnds(Object *selfObj, Tuple *args, bool ends, const char *name) {
	STR_SELF
	if (!checkArgs(name, args, 1, 3))
		return ObjRef();
	Str *fix = strArg(args, 0, name);
	if (!fix)
		return ObjRef();
	int32 start, end;
	if (!getSliceArgs(args, 1, &start, &end))
		return ObjRef();
	int32 len = self->_len, plen = fix->_len;
	adjustIndices(&start, &end, len);
	if (!ends) {
		if (start + plen > len)
			return newBool(false);
		if (end - start >= plen)
			return newBool(memcmp(self->_data + start, fix->_data, plen) == 0);
		return newBool(end - start == 0 && plen == 0);
	}
	if (end - start < plen || start > len)
		return newBool(false);
	if (end - plen >= start)
		return newBool(memcmp(self->_data + end - plen, fix->_data, plen) == 0);
	return newBool(false);
}

static ObjRef strStartswith(Object *s, Tuple *a, Dict *) { return doStartsEnds(s, a, false, "startswith"); }
static ObjRef strEndswith(Object *s, Tuple *a, Dict *) { return doStartsEnds(s, a, true, "endswith"); }

static ObjRef strExpandtabs(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	int32 tabsize = 8;
	if (!checkArgs("expandtabs", args, 0, 1))
		return ObjRef();
	if (args->_size && !asInt(args->get(0), &tabsize))
		return ObjRef();
	Common::String out;
	int32 col = 0;
	for (uint32 i = 0; i < self->_len; i++) {
		char c = self->_data[i];
		if (c == '\t') {
			if (tabsize > 0) {
				int32 n = tabsize - (col % tabsize);
				col += n;
				while (n--)
					out += ' ';
			}
		} else {
			out += c;
			col++;
			if (c == '\n' || c == '\r')
				col = 0;
		}
	}
	return newStr(out);
}

static ObjRef strSplitlines(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	int32 keepends = 0;
	if (!checkArgs("splitlines", args, 0, 1))
		return ObjRef();
	if (args->_size && !asInt(args->get(0), &keepends))
		return ObjRef();
	Ref<List> list = newList();
	const char *d = self->_data;
	int32 len = self->_len, i, j;
	for (i = j = 0; i < len;) {
		while (i < len && d[i] != '\n' && d[i] != '\r')
			i++;
		int32 eol = i;
		if (i < len) {
			if (d[i] == '\r' && i + 1 < len && d[i + 1] == '\n')
				i += 2;
			else
				i++;
			if (keepends)
				eol = i;
		}
		Ref<Str> line = newStr(d + j, eol - j);
		list->append(line.get());
		j = i;
	}
	if (j < len) {
		Ref<Str> line = newStr(d + j, len - j);
		list->append(line.get());
	}
	return list;
}

static ObjRef strTranslate(Object *selfObj, Tuple *args, Dict *) {
	STR_SELF
	if (!checkArgs("translate", args, 1, 2))
		return ObjRef();
	Str *table = strArg(args, 0, "translate");
	if (!table)
		return ObjRef();
	if (table->_len != 256) {
		setErrorString(g_excValueError, "translation table must be 256 characters long");
		return ObjRef();
	}
	Str *del = nullptr;
	if (args->_size > 1) {
		del = strArg(args, 1, "translate");
		if (!del)
			return ObjRef();
	}
	Common::String out;
	for (uint32 i = 0; i < self->_len; i++) {
		uint8 c = (uint8)self->_data[i];
		if (del && memchr(del->_data, c, del->_len))
			continue;
		out += table->_data[c];
	}
	return newStr(out);
}

static ObjRef strDecode(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("decode", args, 0, 2))
		return ObjRef();
	const char *enc = nullptr, *errors = nullptr;
	if (args->_size > 0) {
		Str *e = strArg(args, 0, "decode");
		if (!e)
			return ObjRef();
		enc = e->_data;
	}
	if (args->_size > 1) {
		Str *e = strArg(args, 1, "decode");
		if (!e)
			return ObjRef();
		errors = e->_data;
	}
	return unicodeFromStr(selfObj, enc, errors);
}

static ObjRef strEncode(Object *selfObj, Tuple *args, Dict *) {
	if (!checkArgs("encode", args, 0, 2))
		return ObjRef();
	ObjRef u = unicodeFromStr(selfObj, nullptr, nullptr);
	if (!u)
		return ObjRef();
	ObjRef meth = getAttrString(u.get(), "encode");
	if (!meth)
		return ObjRef();
	return call(meth.get(), args, nullptr);
}

static const MethodDef g_strMethods[] = {
	{ "join", strJoinMethod, kMethVarArgs, nullptr },
	{ "split", strSplit, kMethVarArgs, nullptr },
	{ "lower", strLower, kMethVarArgs, nullptr },
	{ "upper", strUpper, kMethVarArgs, nullptr },
	{ "islower", strIslower, kMethVarArgs, nullptr },
	{ "isupper", strIsupper, kMethVarArgs, nullptr },
	{ "isspace", strIsspace, kMethVarArgs, nullptr },
	{ "isdigit", strIsdigit, kMethVarArgs, nullptr },
	{ "istitle", strIstitle, kMethVarArgs, nullptr },
	{ "isalpha", strIsalpha, kMethVarArgs, nullptr },
	{ "isalnum", strIsalnum, kMethVarArgs, nullptr },
	{ "capitalize", strCapitalize, kMethVarArgs, nullptr },
	{ "count", strCount, kMethVarArgs, nullptr },
	{ "endswith", strEndswith, kMethVarArgs, nullptr },
	{ "find", strFind, kMethVarArgs, nullptr },
	{ "index", strIndex, kMethVarArgs, nullptr },
	{ "lstrip", strLstrip, kMethVarArgs, nullptr },
	{ "replace", strReplace, kMethVarArgs, nullptr },
	{ "rfind", strRfind, kMethVarArgs, nullptr },
	{ "rindex", strRindex, kMethVarArgs, nullptr },
	{ "rstrip", strRstrip, kMethVarArgs, nullptr },
	{ "startswith", strStartswith, kMethVarArgs, nullptr },
	{ "strip", strStrip, kMethVarArgs, nullptr },
	{ "swapcase", strSwapcase, kMethVarArgs, nullptr },
	{ "translate", strTranslate, kMethVarArgs, nullptr },
	{ "title", strTitle, kMethVarArgs, nullptr },
	{ "ljust", strLjust, kMethVarArgs, nullptr },
	{ "rjust", strRjust, kMethVarArgs, nullptr },
	{ "center", strCenter, kMethVarArgs, nullptr },
	{ "zfill", strZfill, kMethVarArgs, nullptr },
	{ "encode", strEncode, kMethVarArgs, nullptr },
	{ "decode", strDecode, kMethVarArgs, nullptr },
	{ "expandtabs", strExpandtabs, kMethVarArgs, nullptr },
	{ "splitlines", strSplitlines, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// ---- The % operator (CPython 2.2's PyString_Format) ------------------------

enum {
	kFlagLeft = 1, kFlagSign = 2, kFlagBlank = 4, kFlagAlt = 8, kFlagZero = 16
};

// Pads a formatted number as C's printf would, given its sign and digits
static Common::String padNumber(const Common::String &body, int flags, int width, const char *prefix = "") {
	Common::String sign;
	Common::String digits = body;
	if (!digits.empty() && digits[0] == '-') {
		sign = "-";
		digits = Common::String(digits.c_str() + 1);
	} else if (flags & kFlagSign) {
		sign = "+";
	} else if (flags & kFlagBlank) {
		sign = " ";
	}
	Common::String head = sign + prefix;
	int len = head.size() + digits.size();
	if (width <= len)
		return head + digits;
	int fill = width - len;
	Common::String padding;
	if (flags & kFlagLeft) {
		for (int i = 0; i < fill; i++)
			padding += ' ';
		return head + digits + padding;
	}
	if (flags & kFlagZero) {
		for (int i = 0; i < fill; i++)
			padding += '0';
		return head + padding + digits;
	}
	for (int i = 0; i < fill; i++)
		padding += ' ';
	return padding + head + digits;
}

static ObjRef getNextArg(Object *args, int32 arglen, int32 *argidx) {
	int32 idx = *argidx;
	if (idx < arglen) {
		(*argidx)++;
		if (arglen < 0)
			return ObjRef(args);
		return ObjRef(((Tuple *)args)->get(idx));
	}
	setErrorString(g_excTypeError, "not enough arguments for format string");
	return ObjRef();
}

static bool formatFloatArg(Common::String &out, int flags, int prec, char type, Object *v, int width) {
	double x;
	if (!asDouble(v, &x))
		return false;

	if (prec < 0)
		prec = 6;
	if (prec > 50)
		prec = 50;
	if ((type == 'f' || type == 'F') && fabs(x) / 1e25 >= 1e25)
		type = 'g';
	out = padNumber(formatDouble(x, prec, type, (flags & kFlagAlt) != 0), flags, width);
	return true;
}

static bool formatIntArg(Common::String &out, int flags, int prec, char type, Object *v, int width) {
	int32 x;
	if (!asInt(v, &x))
		return false;
	Common::String digits;
	const char *prefix = "";
	switch (type) {
	case 'd':
	case 'i':
		digits = Common::String::format("%d", x);
		break;
	case 'u':
		digits = Common::String::format("%u", (uint32)x);
		break;
	case 'o':
		digits = Common::String::format("%o", (uint32)x);
		if ((flags & kFlagAlt) && x != 0)
			digits = "0" + digits;
		break;
	case 'x':
		digits = Common::String::format("%x", (uint32)x);
		if (flags & kFlagAlt)
			prefix = "0x";
		break;
	case 'X':
		digits = Common::String::format("%X", (uint32)x);
		if (flags & kFlagAlt)
			prefix = "0X";
		break;
	default:
		break;
	}
	if (prec >= 0) {
		bool neg = !digits.empty() && digits[0] == '-';
		Common::String mag = neg ? Common::String(digits.c_str() + 1) : digits;
		while ((int)mag.size() < prec)
			mag = "0" + mag;
		digits = neg ? "-" + mag : mag;
		flags &= ~kFlagZero;
	}
	out = padNumber(digits, flags, width, prefix);
	return true;
}

static bool formatLongArg(Common::String &out, int flags, int prec, char type, Object *v, int width) {
	int base = (type == 'o') ? 8 : (type == 'x' || type == 'X') ? 16 : 10;
	ObjRef s = longFormat(v, base, false);
	if (!s)
		return false;
	Common::String digits = ((Str *)s.get())->toString();
	bool neg = !digits.empty() && digits[0] == '-';
	if (neg)
		digits = Common::String(digits.c_str() + 1);
	// Drop the base prefix longFormat adds; it is added back below for '#'
	if (base == 16 && digits.hasPrefix("0x"))
		digits = Common::String(digits.c_str() + 2);
	if (base == 8 && digits.size() > 1 && digits[0] == '0')
		digits = Common::String(digits.c_str() + 1);
	if (type == 'x') {
		for (uint i = 0; i < digits.size(); i++)
			digits.setChar(toLowerChar(digits[i]), i);
	}
	if (prec >= 0) {
		while ((int)digits.size() < prec)
			digits = "0" + digits;
	}
	const char *prefix = "";
	if (flags & kFlagAlt) {
		if (type == 'x')
			prefix = "0x";
		else if (type == 'X')
			prefix = "0X";
		else if (type == 'o' && digits[0] != '0')
			digits = "0" + digits;
	}
	out = padNumber(neg ? "-" + digits : digits, flags, width, prefix);
	return true;
}

ObjRef strFormat(Object *formatObj, Object *args) {
	Str *format = (Str *)formatObj;
	const char *fmt = format->_data;
	int32 fmtcnt = format->_len;
	int32 arglen, argidx;
	ObjRef dict;
	if (isType(args, &g_tupleType) || isSubtype(args->_type, &g_tupleType)) {
		arglen = ((Tuple *)args)->_size;
		argidx = 0;
	} else {
		arglen = -1;
		argidx = -2;
	}
	if (args->_type->asMapping && !isInstanceOf(args, &g_tupleType) && !isInstanceOf(args, &g_strType))
		dict = ObjRef(args);
	Common::String result;
	const char *start = fmt;
	while (--fmtcnt >= 0) {
		if (*fmt != '%') {
			result += *fmt++;
			continue;
		}
		// Got a format specifier
		int flags = 0;
		int width = -1, prec = -1;
		char c = 0;
		ObjRef v;
		ObjRef keyArg; // the argument found by %(key)
		fmt++;
		if (*fmt == '(') {
			if (!dict) {
				setErrorString(g_excTypeError, "format requires a mapping");
				return ObjRef();
			}
			++fmt;
			--fmtcnt;
			const char *keystart = fmt;
			int pcount = 1;
			while (pcount > 0 && --fmtcnt >= 0) {
				if (*fmt == ')')
					--pcount;
				else if (*fmt == '(')
					++pcount;
				fmt++;
			}
			int32 keylen = fmt - keystart - 1;
			if (fmtcnt < 0 || pcount > 0) {
				setErrorString(g_excValueError, "incomplete format key");
				return ObjRef();
			}
			Ref<Str> key = newStr(keystart, keylen);
			keyArg = getItem(dict.get(), key.get());
			if (!keyArg)
				return ObjRef();
			arglen = -1;
			argidx = -2;
		}
		Object *argsForThis = keyArg ? keyArg.get() : args;
		while (--fmtcnt >= 0) {
			switch (c = *fmt++) {
			case '-': flags |= kFlagLeft; continue;
			case '+': flags |= kFlagSign; continue;
			case ' ': flags |= kFlagBlank; continue;
			case '#': flags |= kFlagAlt; continue;
			case '0': flags |= kFlagZero; continue;
			default: break;
			}
			break;
		}
		if (c == '*') {
			ObjRef wv = getNextArg(argsForThis, arglen, &argidx);
			if (!wv)
				return ObjRef();
			if (!isInstanceOf(wv.get(), &g_intType)) {
				setErrorString(g_excTypeError, "* wants int");
				return ObjRef();
			}
			width = ((Int *)wv.get())->_value;
			if (width < 0) {
				flags |= kFlagLeft;
				width = -width;
			}
			if (--fmtcnt >= 0)
				c = *fmt++;
		} else if (c >= 0 && Common::isDigit(c)) {
			width = c - '0';
			while (--fmtcnt >= 0) {
				c = *fmt++;
				if (!Common::isDigit(c))
					break;
				width = width * 10 + (c - '0');
			}
		}
		if (c == '.') {
			prec = 0;
			if (--fmtcnt >= 0)
				c = *fmt++;
			if (c == '*') {
				ObjRef pv = getNextArg(argsForThis, arglen, &argidx);
				if (!pv)
					return ObjRef();
				if (!isInstanceOf(pv.get(), &g_intType)) {
					setErrorString(g_excTypeError, "* wants int");
					return ObjRef();
				}
				prec = ((Int *)pv.get())->_value;
				if (prec < 0)
					prec = 0;
				if (--fmtcnt >= 0)
					c = *fmt++;
			} else if (c >= 0 && Common::isDigit(c)) {
				prec = c - '0';
				while (--fmtcnt >= 0) {
					c = *fmt++;
					if (!Common::isDigit(c))
						break;
					prec = prec * 10 + (c - '0');
				}
			}
		}
		if (fmtcnt >= 0) {
			if (c == 'h' || c == 'l' || c == 'L') {
				if (--fmtcnt >= 0)
					c = *fmt++;
			}
		}
		if (fmtcnt < 0) {
			setErrorString(g_excValueError, "incomplete format");
			return ObjRef();
		}
		if (c != '%') {
			v = getNextArg(argsForThis, arglen, &argidx);
			if (!v)
				return ObjRef();
		}
		Common::String piece;
		switch (c) {
		case '%':
			piece = "%";
			break;
		case 's':
		case 'r': {
			if (c == 's' && isInstanceOf(v.get(), &g_unicodeType)) {
				// The result becomes unicode, as in CPython
				ObjRef ufmt = unicodeFromStr(formatObj, nullptr, nullptr);
				if (!ufmt)
					return ObjRef();
				return unicodeFormat(ufmt.get(), args);
			}
			ObjRef temp = c == 's' ? str(v.get()) : repr(v.get());
			if (!temp)
				return ObjRef();
			if (!isInstanceOf(temp.get(), &g_strType)) {
				setErrorString(g_excTypeError, "%s argument has non-string str()");
				return ObjRef();
			}
			Str *ts = (Str *)temp.get();
			int32 len = ts->_len;
			if (prec >= 0 && len > prec)
				len = prec;
			piece = Common::String(ts->_data, len);
			if (width > len) {
				Common::String padding;
				for (int i = len; i < width; i++)
					padding += ' ';
				piece = (flags & kFlagLeft) ? piece + padding : padding + piece;
			}
			break;
		}
		case 'i':
		case 'd':
		case 'u':
		case 'o':
		case 'x':
		case 'X':
			if (isInstanceOf(v.get(), &g_longType)) {
				if (!formatLongArg(piece, flags, prec, c, v.get(), width))
					return ObjRef();
			} else {
				if (!formatIntArg(piece, flags, prec, c, v.get(), width))
					return ObjRef();
			}
			break;
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G':
			if (!formatFloatArg(piece, flags, prec, c, v.get(), width))
				return ObjRef();
			break;
		case 'c': {
			char ch;
			if (isInstanceOf(v.get(), &g_strType)) {
				if (((Str *)v.get())->_len != 1) {
					setErrorString(g_excTypeError, "%c requires int or char");
					return ObjRef();
				}
				ch = ((Str *)v.get())->_data[0];
			} else {
				int32 x;
				if (!asInt(v.get(), &x)) {
					clearError();
					setErrorString(g_excTypeError, "%c requires int or char");
					return ObjRef();
				}
				if (x < 0) {
					setErrorString(g_excOverflowError, "unsigned byte integer is less than minimum");
					return ObjRef();
				}
				if (x > 255) {
					setErrorString(g_excOverflowError, "unsigned byte integer is greater than maximum");
					return ObjRef();
				}
				ch = (char)x;

			}
			piece = Common::String(&ch, 1);
			if (width > 1) {
				Common::String padding;
				for (int i = 1; i < width; i++)
					padding += ' ';
				piece = (flags & kFlagLeft) ? piece + padding : padding + piece;
			}
			break;
		}
		default:
			setErrorFormat(g_excValueError, "unsupported format character '%c' (0x%x) at index %d",
			               c, (uint8)c, (int)(fmt - 1 - start));
			return ObjRef();
		}
		result += piece;
		if (dict && argidx < arglen && c != '%') {
			setErrorString(g_excTypeError, "not all arguments converted");
			return ObjRef();
		}
	}
	if (argidx < arglen && !dict) {
		setErrorString(g_excTypeError, "not all arguments converted");
		return ObjRef();
	}
	return newStr(result);
}

static ObjRef strRemainder(Object *a, Object *b) {
	if (!isInstanceOf(a, &g_strType))
		return ObjRef(g_notImplemented);
	return strFormat(a, b);
}

static ObjRef strNew(Type *type, Tuple *args, Dict *kw) {
	if (!noKeywords("str", kw) || !checkArgs("str", args, 0, 1))
		return ObjRef();
	ObjRef r = args->_size ? str(args->get(0)) : ObjRef(newStr("", 0));
	if (!r || type == &g_strType)
		return r;
	Str *s = (Str *)r.get();
	return ObjRef(new Str(type, s->_data, s->_len));
}

static SequenceMethods g_strAsSequence;
static NumberMethods g_strAsNumber;

void initStrType() {
	SequenceMethods &s = g_strAsSequence;
	s.length = strLength;
	s.concat = strConcatSlot;
	s.repeat = strRepeat;
	s.item = strItem;
	s.slice = strSlice;
	s.contains = strContains;

	Type &t = g_strType;
	t._flags |= kTypeBaseType;
	t.asSequence = &s;
	t.repr = strReprSlot;
	t.str = strStrSlot;
	t.hash = strHashSlot;
	t.richCompare = strRichCompare;
	(void)strCompare;
	t.methods = g_strMethods;
	t.newObj = strNew;
	t.getAttr = genericGetAttr;
	(void)strRemainder;
	(void)g_strAsNumber;
}

} // End of namespace Python
} // End of namespace Yaga
