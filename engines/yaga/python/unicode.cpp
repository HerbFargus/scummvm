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

// A small unicode type: the games' code never makes unicode strings, but the
// standard library they ship touches it. ASCII, Latin-1 and UTF-8 are known.

namespace Yaga {
namespace Python {

Type g_unicodeType("unicode", &g_objectType);

static Ref<Unicode> newUnicode() {
	return Ref<Unicode>(new Unicode());
}

static Common::String normalizeEncoding(const char *encoding) {
	Common::String e;
	for (const char *p = encoding; *p; p++) {
		char c = *p;
		if (c >= 'A' && c <= 'Z')
			c += 32;
		if (c == '_')
			c = '-';
		e += c;
	}
	return e;
}

ObjRef unicodeFromStr(Object *o, const char *encoding, const char *errors) {
	if (isInstanceOf(o, &g_unicodeType))
		return ObjRef(o);
	if (!isInstanceOf(o, &g_strType)) {
		ObjRef s = str(o);
		if (!s)
			return ObjRef();
		return unicodeFromStr(s.get(), encoding, errors);
	}
	Str *s = (Str *)o;
	Common::String enc = normalizeEncoding(encoding ? encoding : "ascii");
	bool strict = !errors || strcmp(errors, "strict") == 0;
	bool replace = errors && strcmp(errors, "replace") == 0;
	Ref<Unicode> u = newUnicode();
	if (enc == "ascii" || enc == "us-ascii") {
		for (uint32 i = 0; i < s->_len; i++) {
			uint8 c = (uint8)s->_data[i];
			if (c >= 128) {
				if (strict) {
					setErrorString(g_excUnicodeError, "ASCII decoding error: ordinal not in range(128)");
					return ObjRef();
				}
				if (replace)
					u->_chars.push_back(0xfffd);
				continue;
			}
			u->_chars.push_back(c);
		}
	} else if (enc == "latin-1" || enc == "latin1" || enc == "iso-8859-1" || enc == "iso8859-1" || enc == "mbcs" || enc == "cp1252") {
		for (uint32 i = 0; i < s->_len; i++)
			u->_chars.push_back((uint8)s->_data[i]);
	} else if (enc == "utf-8" || enc == "utf8") {
		const uint8 *p = (const uint8 *)s->_data, *end = p + s->_len;
		while (p < end) {
			uint32 c = *p++;
			int extra = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
			if (extra) {
				c &= (0x3f >> extra);
				for (int k = 0; k < extra; k++) {
					if (p >= end || (*p & 0xc0) != 0x80) {
						if (strict) {
							setErrorString(g_excUnicodeError, "UTF-8 decoding error: invalid data");
							return ObjRef();
						}
						c = 0xfffd;
						break;
					}
					c = (c << 6) | (*p++ & 0x3f);
				}
			}
			u->_chars.push_back(c);
		}
	} else {
		setErrorFormat(g_excLookupError, "unknown encoding: %s", encoding);
		return ObjRef();
	}
	return u;
}

static ObjRef encode(Unicode *u, const char *encoding, const char *errors) {
	Common::String enc = normalizeEncoding(encoding ? encoding : "ascii");
	bool strict = !errors || strcmp(errors, "strict") == 0;
	bool replace = errors && strcmp(errors, "replace") == 0;
	Common::String out;
	if (enc == "ascii" || enc == "us-ascii" || enc == "latin-1" || enc == "latin1" || enc == "iso-8859-1" || enc == "mbcs" || enc == "cp1252") {
		uint32 limit = (enc == "ascii" || enc == "us-ascii") ? 128 : 256;
		for (uint32 i = 0; i < u->_chars.size(); i++) {
			uint32 c = u->_chars[i];
			if (c >= limit) {
				if (strict) {
					setErrorString(g_excUnicodeError, limit == 128 ? "ASCII encoding error: ordinal not in range(128)"
					                                                : "Latin-1 encoding error: ordinal not in range(256)");
					return ObjRef();
				}
				if (replace)
					out += '?';
				continue;
			}
			out += (char)c;
		}
	} else if (enc == "utf-8" || enc == "utf8") {
		for (uint32 i = 0; i < u->_chars.size(); i++) {
			uint32 c = u->_chars[i];
			if (c < 0x80) {
				out += (char)c;
			} else if (c < 0x800) {
				out += (char)(0xc0 | (c >> 6));
				out += (char)(0x80 | (c & 0x3f));
			} else if (c < 0x10000) {
				out += (char)(0xe0 | (c >> 12));
				out += (char)(0x80 | ((c >> 6) & 0x3f));
				out += (char)(0x80 | (c & 0x3f));
			} else {
				out += (char)(0xf0 | (c >> 18));
				out += (char)(0x80 | ((c >> 12) & 0x3f));
				out += (char)(0x80 | ((c >> 6) & 0x3f));
				out += (char)(0x80 | (c & 0x3f));
			}
		}
	} else {
		setErrorFormat(g_excLookupError, "unknown encoding: %s", encoding);
		return ObjRef();
	}
	return newStr(out);
}

ObjRef unicodeToStr(Object *o) {
	return encode((Unicode *)o, "ascii", "strict");
}

int unicodeCompare(Object *a, Object *b) {
	ObjRef ua = unicodeFromStr(a, nullptr, nullptr);
	if (!ua)
		return -1;
	ObjRef ub = unicodeFromStr(b, nullptr, nullptr);
	if (!ub)
		return -1;
	const Common::Array<uint32> &x = ((Unicode *)ua.get())->_chars, &y = ((Unicode *)ub.get())->_chars;
	uint32 n = MIN(x.size(), y.size());
	for (uint32 i = 0; i < n; i++)
		if (x[i] != y[i])
			return x[i] < y[i] ? -1 : 1;
	return x.size() < y.size() ? -1 : x.size() > y.size() ? 1 : 0;
}

ObjRef unicodeFormat(Object *fmt, Object *args) {
	// Format as a byte string and widen the result; enough for ASCII text
	ObjRef sfmt = unicodeToStr(fmt);
	if (!sfmt)
		return ObjRef();
	// Unicode arguments are narrowed first, so that the byte formatter does not
	// hand the job straight back
	ObjRef sargs(args);
	if (isInstanceOf(args, &g_unicodeType)) {
		sargs = encode((Unicode *)args, "latin-1", "replace");
	} else if (isInstanceOf(args, &g_tupleType)) {
		Tuple *t = (Tuple *)args;
		Ref<Tuple> nt = newTuple(t->_size);
		for (uint32 i = 0; i < t->_size; i++) {
			Object *item = t->get(i);
			if (isInstanceOf(item, &g_unicodeType)) {
				ObjRef s = encode((Unicode *)item, "latin-1", "replace");
				nt->set(i, s.get());
			} else {
				nt->set(i, item);
			}
		}
		sargs = nt;
	}
	if (!sargs)
		return ObjRef();
	ObjRef r = strFormat(sfmt.get(), sargs.get());
	if (!r)
		return ObjRef();
	return unicodeFromStr(r.get(), "latin-1", nullptr);
}

static ObjRef unicodeRepr(Object *o) {
	Unicode *u = (Unicode *)o;
	Common::String out = "u'";
	for (uint32 i = 0; i < u->_chars.size(); i++) {
		uint32 c = u->_chars[i];
		if (c == '\'' || c == '\\') {
			out += '\\';
			out += (char)c;
		} else if (c == '\t') {
			out += "\\t";
		} else if (c == '\n') {
			out += "\\n";
		} else if (c == '\r') {
			out += "\\r";
		} else if (c < ' ' || c >= 0x7f) {
			if (c < 0x100)
				out += Common::String::format("\\x%02x", c);
			else
				out += Common::String::format("\\u%04x", c);
		} else {
			out += (char)c;
		}
	}
	out += "'";
	return newStr(out);
}

static ObjRef unicodeStr(Object *o) {
	return unicodeToStr(o);
}

static bool unicodeHash(Object *o, int32 *out) {
	Unicode *u = (Unicode *)o;
	uint32 len = u->_chars.size();
	uint32 x = len ? u->_chars[0] << 7 : 0;
	for (uint32 i = 0; i < len; i++)
		x = (1000003u * x) ^ u->_chars[i];
	x ^= len;
	int32 r = (int32)x;
	*out = r == -1 ? -2 : r;
	return true;
}

static int32 unicodeLength(Object *o) {
	return ((Unicode *)o)->_chars.size();
}

static ObjRef unicodeItem(Object *o, int32 i) {
	Unicode *u = (Unicode *)o;
	if (i < 0 || i >= (int32)u->_chars.size()) {
		setErrorString(g_excIndexError, "string index out of range");
		return ObjRef();
	}
	Ref<Unicode> r = newUnicode();
	r->_chars.push_back(u->_chars[i]);
	return r;
}

static ObjRef unicodeSlice(Object *o, int32 lo, int32 hi) {
	Unicode *u = (Unicode *)o;
	int32 n = u->_chars.size();
	if (lo < 0)
		lo = 0;
	if (hi > n)
		hi = n;
	if (hi < lo)
		hi = lo;
	Ref<Unicode> r = newUnicode();
	for (int32 i = lo; i < hi; i++)
		r->_chars.push_back(u->_chars[i]);
	return r;
}

static ObjRef unicodeConcat(Object *a, Object *b) {
	ObjRef ua = unicodeFromStr(a, nullptr, nullptr);
	ObjRef ub = ua ? unicodeFromStr(b, nullptr, nullptr) : ObjRef();
	if (!ub)
		return ObjRef();
	Ref<Unicode> r = newUnicode();
	r->_chars = ((Unicode *)ua.get())->_chars;
	const Common::Array<uint32> &y = ((Unicode *)ub.get())->_chars;
	for (uint32 i = 0; i < y.size(); i++)
		r->_chars.push_back(y[i]);
	return r;
}

static int unicodeContains(Object *o, Object *el) {
	ObjRef ue = unicodeFromStr(el, nullptr, nullptr);
	if (!ue)
		return -1;
	Unicode *u = (Unicode *)o, *e = (Unicode *)ue.get();
	if (e->_chars.size() != 1) {
		setErrorString(g_excTypeError, "'in <string>' requires character as left operand");
		return -1;
	}
	for (uint32 i = 0; i < u->_chars.size(); i++)
		if (u->_chars[i] == e->_chars[0])
			return 1;
	return 0;
}

static ObjRef unicodeRichCompare(Object *a, Object *b, int op) {
	if (!(isInstanceOf(a, &g_unicodeType) || isInstanceOf(a, &g_strType)) || !(isInstanceOf(b, &g_unicodeType) || isInstanceOf(b, &g_strType)))
		return ObjRef(g_notImplemented);
	int c = unicodeCompare(a, b);
	if (c == -1 && errorOccurred())
		return ObjRef();
	bool r;
	switch (op) {
	case kLT: r = c < 0; break;
	case kLE: r = c <= 0; break;
	case kEQ: r = c == 0; break;
	case kNE: r = c != 0; break;
	case kGT: r = c > 0; break;
	default: r = c >= 0; break;
	}
	return newBool(r);
}

static ObjRef unicodeEncodeMethod(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("encode", args, 0, 2))
		return ObjRef();
	const char *enc = nullptr, *errors = nullptr;
	if (args->_size > 0) {
		Str *s = asStr(args->get(0));
		if (!s)
			return ObjRef();
		enc = s->_data;
	}
	if (args->_size > 1) {
		Str *s = asStr(args->get(1));
		if (!s)
			return ObjRef();
		errors = s->_data;
	}
	return encode((Unicode *)self, enc, errors);
}

static ObjRef unicodeMapStr(Object *self, Tuple *args, const char *method) {
	ObjRef s = encode((Unicode *)self, "latin-1", "replace");
	if (!s)
		return ObjRef();
	ObjRef meth = getAttrString(s.get(), method);
	if (!meth)
		return ObjRef();
	ObjRef r = call(meth.get(), args, nullptr);
	if (!r)
		return ObjRef();
	if (isInstanceOf(r.get(), &g_strType))
		return unicodeFromStr(r.get(), "latin-1", nullptr);
	if (isInstanceOf(r.get(), &g_listType)) {
		List *l = (List *)r.get();
		for (uint32 i = 0; i < l->size(); i++) {
			ObjRef u = unicodeFromStr(l->get(i), "latin-1", nullptr);
			if (!u)
				return ObjRef();
			l->setItem(i, u.get());
		}
	}
	return r;
}

#define UMETHOD(fname, name) \
	static ObjRef fname(Object *self, Tuple *args, Dict *) { return unicodeMapStr(self, args, name); }
UMETHOD(unicodeLower, "lower")
UMETHOD(unicodeUpper, "upper")
UMETHOD(unicodeStrip, "strip")
UMETHOD(unicodeLstrip, "lstrip")
UMETHOD(unicodeRstrip, "rstrip")
UMETHOD(unicodeSplit, "split")
UMETHOD(unicodeReplace, "replace")
UMETHOD(unicodeFind, "find")
UMETHOD(unicodeStartswith, "startswith")
UMETHOD(unicodeEndswith, "endswith")

static ObjRef unicodeJoin(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("join", args, 1, 1))
		return ObjRef();
	ObjRef seq = sequenceTuple(args->get(0));
	if (!seq)
		return ObjRef();
	Tuple *t = (Tuple *)seq.get();
	Ref<Unicode> r = newUnicode();
	Unicode *sep = (Unicode *)self;
	for (uint32 i = 0; i < t->_size; i++) {
		if (i)
			for (uint32 k = 0; k < sep->_chars.size(); k++)
				r->_chars.push_back(sep->_chars[k]);
		ObjRef item = unicodeFromStr(t->get(i), nullptr, nullptr);
		if (!item)
			return ObjRef();
		const Common::Array<uint32> &c = ((Unicode *)item.get())->_chars;
		for (uint32 k = 0; k < c.size(); k++)
			r->_chars.push_back(c[k]);
	}
	return r;
}

static const MethodDef g_unicodeMethods[] = {
	{ "encode", unicodeEncodeMethod, kMethVarArgs, nullptr },
	{ "join", unicodeJoin, kMethVarArgs, nullptr },
	{ "lower", unicodeLower, kMethVarArgs, nullptr },
	{ "upper", unicodeUpper, kMethVarArgs, nullptr },
	{ "strip", unicodeStrip, kMethVarArgs, nullptr },
	{ "lstrip", unicodeLstrip, kMethVarArgs, nullptr },
	{ "rstrip", unicodeRstrip, kMethVarArgs, nullptr },
	{ "split", unicodeSplit, kMethVarArgs, nullptr },
	{ "replace", unicodeReplace, kMethVarArgs, nullptr },
	{ "find", unicodeFind, kMethVarArgs, nullptr },
	{ "startswith", unicodeStartswith, kMethVarArgs, nullptr },
	{ "endswith", unicodeEndswith, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef unicodeNew(Type *, Tuple *args, Dict *kw) {
	if (!checkArgs("unicode", args, 0, 3))
		return ObjRef();
	if (args->_size == 0)
		return newUnicode();
	const char *enc = nullptr, *errors = nullptr;
	if (args->_size > 1) {
		Str *s = asStr(args->get(1));
		if (!s)
			return ObjRef();
		enc = s->_data;
	}
	if (args->_size > 2) {
		Str *s = asStr(args->get(2));
		if (!s)
			return ObjRef();
		errors = s->_data;
	}
	return unicodeFromStr(args->get(0), enc, errors);
}

static SequenceMethods g_unicodeAsSequence;

void initUnicodeType() {
	g_unicodeAsSequence.length = unicodeLength;
	g_unicodeAsSequence.item = unicodeItem;
	g_unicodeAsSequence.slice = unicodeSlice;
	g_unicodeAsSequence.concat = unicodeConcat;
	g_unicodeAsSequence.contains = unicodeContains;
	g_unicodeType.asSequence = &g_unicodeAsSequence;
	g_unicodeType.repr = unicodeRepr;
	g_unicodeType.str = unicodeStr;
	g_unicodeType.hash = unicodeHash;
	g_unicodeType.richCompare = unicodeRichCompare;
	g_unicodeType.methods = g_unicodeMethods;
	g_unicodeType.newObj = unicodeNew;
	g_unicodeType.getAttr = genericGetAttr;
	g_unicodeType._flags |= kTypeBaseType;
}

} // End of namespace Python
} // End of namespace Yaga
