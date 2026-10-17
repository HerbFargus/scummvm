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

// Python 2.2 ints are C longs: 32 bits on Windows, where the games ran.
// Arithmetic that overflows returns a long, but shifts silently truncate.

namespace Yaga {
namespace Python {

Type g_intType("int", &g_objectType);

static const int kSmallNeg = 1, kSmallPos = 100;
static Int *g_smallInts[kSmallNeg + kSmallPos];

Ref<Int> newInt(int32 v) {
	if (v >= -kSmallNeg && v < kSmallPos) {
		Int *&slot = g_smallInts[v + kSmallNeg];
		if (!slot) {
			slot = new Int(v);
			incRef(slot); // kept for good
		}
		return Ref<Int>(slot);
	}
	return Ref<Int>(new Int(v));
}

ObjRef newIntOrLong(int64 v) {
	if (v >= -0x7fffffffLL - 1 && v <= 0x7fffffffLL)
		return newInt((int32)v);
	return longFromInt64(v);
}

// True and False are ints, but not the cached small ints (as Py_True and Py_False)
static Int g_falseInt(0), g_trueInt(1);

ObjRef newBool(bool b) {
	if (g_trueInt._refs < 0x40000000) {
		g_trueInt._refs = 0x40000000;
		g_falseInt._refs = 0x40000000;
	}
	return ObjRef(b ? &g_trueInt : &g_falseInt);
}

// Gets the operand as a C long, or returns NotImplemented, like CPython's CONVERT_TO_LONG
#define CONVERT(obj, var) \
	int32 var; \
	if (isInstanceOf(obj, &g_intType)) \
		var = ((Int *)(obj))->_value; \
	else \
		return ObjRef(g_notImplemented);

// Falls back to long arithmetic when the int result overflows
static ObjRef viaLong(Object *v, Object *w, BinaryFunc f) {
	ObjRef lv = longFromInt(v), lw = longFromInt(w);
	if (!lv || !lw)
		return ObjRef();
	return f(lv.get(), lw.get());
}

static ObjRef intAdd(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	int64 x = (int64)a + b;
	if (x >= -0x7fffffffLL - 1 && x <= 0x7fffffffLL)
		return newInt((int32)x);
	return longFromInt64(x);
}

static ObjRef intSub(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	int64 x = (int64)a - b;
	if (x >= -0x7fffffffLL - 1 && x <= 0x7fffffffLL)
		return newInt((int32)x);
	return longFromInt64(x);
}

static ObjRef intMul(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	int64 x = (int64)a * b;
	if (x >= -0x7fffffffLL - 1 && x <= 0x7fffffffLL)
		return newInt((int32)x);
	return longFromInt64(x);
}

// Floor division and modulo; returns 0 ok, -1 zero division, 1 overflow
static int intDivmod(int32 x, int32 y, int32 *pdiv, int32 *pmod) {
	if (y == 0) {
		setErrorString(g_excZeroDivisionError, "integer division or modulo by zero");
		return -1;
	}
	if (y == -1 && x == (int32)0x80000000)
		return 1;
	int32 xdivy = x / y;
	int32 xmody = x - xdivy * y;
	if (xmody && ((y ^ xmody) < 0)) {
		xmody += y;
		--xdivy;
	}
	*pdiv = xdivy;
	*pmod = xmody;
	return 0;
}

static ObjRef longDivideHelper(Object *a, Object *b);
static ObjRef longRemHelper(Object *a, Object *b);
static ObjRef longDivmodHelper(Object *a, Object *b);

static ObjRef intDiv(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	int32 d, m;
	switch (intDivmod(a, b, &d, &m)) {
	case 0: return newInt(d);
	case 1: return viaLong(v, w, longDivideHelper);
	default: return ObjRef();
	}
}

static ObjRef intMod(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	int32 d, m;
	switch (intDivmod(a, b, &d, &m)) {
	case 0: return newInt(m);
	case 1: return viaLong(v, w, longRemHelper);
	default: return ObjRef();
	}
}

static ObjRef intDivmodSlot(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	int32 d, m;
	switch (intDivmod(a, b, &d, &m)) {
	case 0: {
		Ref<Int> di = newInt(d), mi = newInt(m);
		return tuplePack(2, di.get(), mi.get());
	}
	case 1: return viaLong(v, w, longDivmodHelper);
	default: return ObjRef();
	}
}

static ObjRef intTrueDiv(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	if (b == 0) {
		setErrorString(g_excZeroDivisionError, "integer division or modulo by zero");
		return ObjRef();
	}
	return newFloat((double)a / (double)b);
}

static ObjRef longPowHelper(Object *a, Object *b, Object *c);

static ObjRef intPow(Object *v, Object *w, Object *z) {
	CONVERT(v, iv) CONVERT(w, iw)
	if (iw < 0) {
		if (z != g_none) {
			setErrorString(g_excTypeError, "pow() 2nd argument cannot be negative when 3rd argument specified");
			return ObjRef();
		}
		// Python 2.2 returns a float for a negative exponent
		ObjRef fv = newFloat(iv), fw = newFloat(iw);
		return numberPower(fv.get(), fw.get(), g_none);
	}
	int32 iz = 0;
	if (z != g_none) {
		if (!isInstanceOf(z, &g_intType))
			return ObjRef(g_notImplemented);
		iz = ((Int *)z)->_value;
		if (iz == 0) {
			setErrorString(g_excValueError, "pow() 3rd argument cannot be 0");
			return ObjRef();
		}
	}
	// Repeated squaring, as CPython's int_pow; overflow switches to longs
	int64 temp = iv, ix = 1;
	int32 e = iw;
	while (e > 0) {
		if (e & 1) {
			ix = ix * temp;
			if (!iz && (ix > 0x7fffffffLL || ix < -0x7fffffffLL - 1)) {
				ObjRef lv = longFromInt(v), lw = longFromInt(w);
				return longPowHelper(lv.get(), lw.get(), g_none);
			}
		}
		e >>= 1;
		if (e == 0)
			break;
		temp = temp * temp;
		if (!iz && (temp > 0x7fffffffLL || temp < -0x7fffffffLL - 1)) {
			ObjRef lv = longFromInt(v), lw = longFromInt(w);
			return longPowHelper(lv.get(), lw.get(), g_none);
		}
		if (iz) {
			ix = ix % iz;
			temp = temp % iz;
		}
	}
	if (iz) {
		int32 d, m;
		if (intDivmod((int32)(ix % iz), iz, &d, &m) < 0)
			return ObjRef();
		ix = m;
	}
	return newInt((int32)ix);
}

static ObjRef intNeg(Object *v) {
	int32 a = ((Int *)v)->_value;
	if (a == (int32)0x80000000)
		return longFromInt64(-(int64)a);
	return newInt(-a);
}

static ObjRef intPos(Object *v) {
	if (isType(v, &g_intType))
		return ObjRef(v);
	return newInt(((Int *)v)->_value);
}

static ObjRef intAbs(Object *v) {
	int32 a = ((Int *)v)->_value;
	if (a >= 0)
		return intPos(v);
	return intNeg(v);
}

static int intNonzero(Object *v) {
	return ((Int *)v)->_value != 0;
}

static ObjRef intInvert(Object *v) {
	return newInt(~((Int *)v)->_value);
}

static ObjRef intLshift(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	if (b < 0) {
		setErrorString(g_excValueError, "negative shift count");
		return ObjRef();
	}
	if (a == 0 || b == 0)
		return intPos(v);
	if (b >= 32)
		return newInt(0);
	return newInt((int32)((uint32)a << b));
}

static ObjRef intRshift(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	if (b < 0) {
		setErrorString(g_excValueError, "negative shift count");
		return ObjRef();
	}
	if (a == 0 || b == 0)
		return intPos(v);
	if (b >= 32)
		return newInt(a < 0 ? -1 : 0);
	return newInt(a >> b);
}

static ObjRef intAnd(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	return newInt(a & b);
}

static ObjRef intXor(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	return newInt(a ^ b);
}

static ObjRef intOr(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	return newInt(a | b);
}

static int intCoerce(ObjRef *pv, ObjRef *pw) {
	if (isInstanceOf(pw->get(), &g_intType))
		return 0;
	return 1;
}

static ObjRef intInt(Object *v) {
	return intPos(v);
}

static ObjRef intLong(Object *v) {
	return longFromInt(v);
}

static ObjRef intFloat(Object *v) {
	return newFloat(((Int *)v)->_value);
}

static ObjRef intOct(Object *v) {
	int32 x = ((Int *)v)->_value;
	if (x == 0)
		return newStr("0");
	return newStr(Common::String::format("0%o", (uint32)x));
}

static ObjRef intHex(Object *v) {
	return newStr(Common::String::format("0x%x", (uint32)((Int *)v)->_value));
}

static ObjRef intRepr(Object *v) {
	return newStr(Common::String::format("%d", ((Int *)v)->_value));
}

static int intCompare(Object *v, Object *w) {
	int32 a = ((Int *)v)->_value, b = ((Int *)w)->_value;
	return a < b ? -1 : a > b ? 1 : 0;
}

static bool intHash(Object *v, int32 *out) {
	int32 x = ((Int *)v)->_value;
	*out = x == -1 ? -2 : x;
	return true;
}

ObjRef intFromString(const char *s, int base, bool allowWhitespace) {
	const char *start = s;
	if ((base != 0 && base < 2) || base > 36) {
		setErrorString(g_excValueError, "int() base must be >= 2 and <= 36");
		return ObjRef();
	}
	while (*s && Common::isSpace(*s))
		s++;
	bool neg = false;
	if (*s == '-' || *s == '+') {
		neg = *s == '-';
		s++;
	}
	if (base == 0) {
		if (s[0] != '0')
			base = 10;
		else if (s[1] == 'x' || s[1] == 'X')
			base = 16;
		else
			base = 8;
	}
	if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	const char *digits = s;
	uint64 x = 0;
	bool overflow = false;
	while (*s) {
		int d;
		char c = *s;
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'Z')
			d = c - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		x = x * base + d;
		if (x > 0xffffffffULL)
			overflow = true;
		s++;
	}
	bool empty = s == digits;
	while (*s && Common::isSpace(*s))
		s++;
	if (empty || *s) {
		setErrorFormat(g_excValueError, "invalid literal for int(): %.200s", start);
		return ObjRef();
	}
	// Like strtol, a value that fits unsigned but not signed is an overflow
	// unless the base was given implicitly (hex/octal literals wrap)
	if (overflow || (!neg && x > 0x7fffffffULL) || (neg && x > 0x80000000ULL)) {
		setErrorFormat(g_excValueError, "int() literal too large: %.200s", start);
		return ObjRef();
	}
	int32 v = neg ? (int32)(-(int64)x) : (int32)x;
	return newInt(v);
}

static ObjRef intNew(Type *type, Tuple *args, Dict *kw) {
	if (!noKeywords("int", kw) || !checkArgs("int", args, 0, 2))
		return ObjRef();
	ObjRef result;
	if (args->_size == 0) {
		result = newInt(0);
	} else if (args->_size == 1) {
		result = numberInt(args->get(0));
	} else {
		int32 base;
		if (!asInt(args->get(1), &base))
			return ObjRef();
		Object *x = args->get(0);
		if (isInstanceOf(x, &g_strType)) {
			result = intFromString(((Str *)x)->_data, base, true);
		} else {
			setErrorString(g_excTypeError, "int() can't convert non-string with explicit base");
			return ObjRef();
		}
	}
	if (!result || type == &g_intType)
		return result;
	// A subclass of int
	if (!isInstanceOf(result.get(), &g_intType)) {
		setErrorString(g_excOverflowError, "long int too large to convert to int");
		return ObjRef();
	}
	return ObjRef(new Int(type, ((Int *)result.get())->_value));
}

static NumberMethods g_intAsNumber;

void initIntType() {
	NumberMethods &m = g_intAsNumber;
	m.add = intAdd;
	m.subtract = intSub;
	m.multiply = intMul;
	m.divide = intDiv;
	m.remainder = intMod;
	m.divmod = intDivmodSlot;
	m.power = intPow;
	m.negative = intNeg;
	m.positive = intPos;
	m.absolute = intAbs;
	m.nonzero = intNonzero;
	m.invert = intInvert;
	m.lshift = intLshift;
	m.rshift = intRshift;
	m.and_ = intAnd;
	m.xor_ = intXor;
	m.or_ = intOr;
	m.coerce = intCoerce;
	m.toInt = intInt;
	m.toLong = intLong;
	m.toFloat = intFloat;
	m.toOct = intOct;
	m.toHex = intHex;
	m.floorDivide = intDiv;
	m.trueDivide = intTrueDiv;

	Type &t = g_intType;
	t._flags |= kTypeBaseType | kTypeCheckTypes;
	t.asNumber = &m;
	t.repr = intRepr;
	t.str = intRepr;
	t.compare = intCompare;
	t.hash = intHash;
	t.newObj = intNew;
	t.getAttr = genericGetAttr;
}

// Long helpers live in long.cpp; these forwarders keep the int slots simple
static ObjRef longDivideHelper(Object *a, Object *b) { return numberDivide(a, b); }
static ObjRef longRemHelper(Object *a, Object *b) { return numberRemainder(a, b); }
static ObjRef longDivmodHelper(Object *a, Object *b) { return numberDivmod(a, b); }
static ObjRef longPowHelper(Object *a, Object *b, Object *c) { return numberPower(a, b, c); }

} // End of namespace Python
} // End of namespace Yaga
