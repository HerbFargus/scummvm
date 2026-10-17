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

Type g_floatType("float", &g_objectType);
Type g_complexType("complex", &g_objectType);

Ref<Float> newFloat(double v) {
	return Ref<Float>(new Float(v));
}

static bool isNan(double v) { return v != v; }
static bool isInf(double v) { return !isNan(v) && v - v != 0.0; }

// Exact decimal digits of a finite, non-negative double: the value is
// 0.<digits> * 10^pointPos, with no leading zeros in digits
static void exactDigits(double v, Common::String &digits, int &pointPos) {
	digits.clear();
	pointPos = 0;
	if (v == 0.0)
		return;
	int e2;
	double m = frexp(v, &e2); // v = m * 2^e2, 0.5 <= m < 1
	uint64 mant = (uint64)ldexp(m, 53);
	e2 -= 53;
	// Little-endian base 10^9 limbs
	Common::Array<uint32> limbs;
	limbs.push_back((uint32)(mant % 1000000000ULL));
	if (mant >= 1000000000ULL) {
		limbs.push_back((uint32)((mant / 1000000000ULL) % 1000000000ULL));
		if (mant >= 1000000000000000000ULL)
			limbs.push_back((uint32)(mant / 1000000000000000000ULL));
	}
	int exp10 = 0;
	uint32 mult;
	int count, step;
	if (e2 >= 0) {
		count = e2;
		step = 29; // multiply by 2^29 at a time
		mult = 2;
	} else {
		count = -e2;
		step = 13; // multiply by 5^13 at a time
		mult = 5;
		exp10 = e2;
	}
	while (count > 0) {
		int n = MIN(count, step);
		count -= n;
		uint64 f = 1;
		for (int i = 0; i < n; i++)
			f *= mult;
		uint64 carry = 0;
		for (uint i = 0; i < limbs.size(); i++) {
			uint64 x = (uint64)limbs[i] * f + carry;
			limbs[i] = (uint32)(x % 1000000000ULL);
			carry = x / 1000000000ULL;
		}
		while (carry) {
			limbs.push_back((uint32)(carry % 1000000000ULL));
			carry /= 1000000000ULL;
		}
	}
	digits = Common::String::format("%u", limbs.back());
	for (int i = (int)limbs.size() - 2; i >= 0; i--)
		digits += Common::String::format("%09u", limbs[i]);
	pointPos = (int)digits.size() + exp10;
	// Trailing zeros carry no information
	uint len = digits.size();
	while (len > 0 && digits[len - 1] == '0')
		len--;
	digits = Common::String(digits.c_str(), len);
}

// Rounds a digit string half-up to n digits; returns true if it carried into
// a new leading digit (the caller then adds one to the point position)
static bool roundDigits(Common::String &digits, int n) {
	if (n < 0) {
		digits.clear();
		return false;
	}
	if ((int)digits.size() <= n)
		return false;
	bool up = digits[n] >= '5';
	digits = Common::String(digits.c_str(), n);
	if (!up)
		return false;
	int i = n - 1;
	for (; i >= 0; i--) {
		if (digits[i] != '9') {
			digits.setChar(digits[i] + 1, i);
			break;
		}
		digits.setChar('0', i);
	}
	if (i < 0) {
		digits = "1" + digits;
		digits = Common::String(digits.c_str(), MAX(n, 1));
		return true;
	}
	return false;
}

// The digits Microsoft's C runtime works from: the exact value rounded to 17
// significant digits. Anything printed beyond them is zeros.
static void msvcDigits(double v, Common::String &digits, int &pointPos) {
	exactDigits(v, digits, pointPos);
	if (roundDigits(digits, 17))
		pointPos++;
	uint len = digits.size();
	while (len > 0 && digits[len - 1] == '0')
		len--;
	digits = Common::String(digits.c_str(), len);
}

static char digitAt(const Common::String &d, int i) {
	return (i >= 0 && i < (int)d.size()) ? d[i] : '0';
}

static Common::String formatExponent(int e, bool upper) {
	return Common::String::format("%c%c%03d", upper ? 'E' : 'e', e < 0 ? '-' : '+', e < 0 ? -e : e);
}

// printf's %e, %f and %g as Microsoft's (pre-2015) C runtime prints them:
// half-up rounding of 17-digit values, three-digit exponents, and 1.#INF /
// -1.#IND for infinities and NaNs. The result has a '-' for negative values
// but no other padding or sign.
Common::String formatDouble(double v, int precision, char type, bool alt) {
	if (isNan(v))
		return "-1.#IND";
	if (isInf(v))
		return v < 0 ? "-1.#INF" : "1.#INF";
	bool neg = v < 0 || (v == 0.0 && 1.0 / v < 0);
	double a = fabs(v);
	Common::String digits;
	int pointPos;
	msvcDigits(a, digits, pointPos);
	bool upper = type == 'E' || type == 'G';
	char t = type == 'E' ? 'e' : type == 'G' ? 'g' : type == 'F' ? 'f' : type;
	if (precision < 0)
		precision = 6;
	Common::String out = neg ? "-" : "";
	if (t == 'g') {
		if (precision == 0)
			precision = 1;
		// The exponent after rounding to precision significant digits
		Common::String r = digits;
		int pp = pointPos;
		if (roundDigits(r, precision))
			pp++;
		int x = r.empty() ? 0 : pp - 1;
		if (x < -4 || x >= precision) {
			t = 'e';
			precision -= 1;
		} else {
			t = 'f';
			precision = precision - 1 - x;
		}
		Common::String body = formatDouble(a, precision, upper ? (t == 'e' ? 'E' : 'F') : t, alt);
		if (!alt) {
			// Strip trailing zeros (and a trailing point) from the fraction
			int epos = -1;
			for (uint i = 0; i < body.size(); i++)
				if (body[i] == 'e' || body[i] == 'E')
					epos = i;
			Common::String mant = epos >= 0 ? Common::String(body.c_str(), epos) : body;
			Common::String expo = epos >= 0 ? Common::String(body.c_str() + epos) : Common::String();
			bool hasPoint = false;
			for (uint i = 0; i < mant.size(); i++)
				if (mant[i] == '.')
					hasPoint = true;
			if (hasPoint) {
				uint len = mant.size();
				while (len > 0 && mant[len - 1] == '0')
					len--;
				if (len > 0 && mant[len - 1] == '.')
					len--;
				mant = Common::String(mant.c_str(), len);
			}
			body = mant + expo;
		}
		return out + body;
	}
	if (t == 'e') {
		Common::String r = digits;
		int pp = pointPos;
		if (roundDigits(r, precision + 1))
			pp++;
		int x = r.empty() ? 0 : pp - 1;
		out += digitAt(r, 0);
		if (precision > 0 || alt)
			out += '.';
		for (int i = 1; i <= precision; i++)
			out += digitAt(r, i);
		return out + formatExponent(x, upper);
	}
	// 'f'
	Common::String r = digits;
	int pp = pointPos;
	int keep = pp + precision;
	if (keep < 0) {
		r.clear();
	} else if (roundDigits(r, keep)) {
		pp++;
	}
	if (r.empty())
		pp = 0;
	if (pp <= 0) {
		out += '0';
	} else {
		for (int i = 0; i < pp; i++)
			out += digitAt(r, i);
	}
	if (precision > 0 || alt)
		out += '.';
	for (int i = 0; i < precision; i++)
		out += digitAt(r, pp + i);
	return out;
}

// CPython's format_float: %.<precision>g, with ".0" added to whole numbers
static Common::String formatFloat(double v, int precision) {
	Common::String s = formatDouble(v, precision, 'g');
	for (uint i = 0; i < s.size(); i++) {
		char c = s[i];
		if (c != '-' && !Common::isDigit(c))
			return s;
	}
	return s + ".0";
}

Common::String floatRepr(double v) { return formatFloat(v, 17); }
Common::String floatStr(double v) { return formatFloat(v, 12); }

int32 hashDouble(double v) {
	double intpart;
	double fractpart = modf(v, &intpart);
	if (fractpart == 0.0) {
		if (intpart > 2147483647.0 || -intpart > 2147483647.0) {
			if (isInf(intpart))
				v = v < 0 ? -271828.0 : 314159.0;
			ObjRef l = longFromDouble(v);
			int32 x = 0;
			if (l)
				hash(l.get(), &x);
			return x;
		}
		int32 x = (int32)intpart;
		return x == -1 ? -2 : x;
	}
	int expo;
	v = frexp(v, &expo);
	v *= 2147483648.0;
	int32 hipart = (int32)v;
	v = (v - (double)hipart) * 2147483648.0;
	int32 x = (int32)((uint32)hipart + (uint32)(int32)v + ((uint32)expo << 15));
	return x == -1 ? -2 : x;
}

// Gets an operand as a double, or returns NotImplemented
#define CONVERT(obj, var) \
	double var; \
	if (isInstanceOf(obj, &g_floatType)) \
		var = ((Float *)(obj))->_value; \
	else if (isInstanceOf(obj, &g_intType)) \
		var = ((Int *)(obj))->_value; \
	else if (isInstanceOf(obj, &g_longType)) { \
		if (!longAsDouble(obj, &var)) \
			return ObjRef(); \
	} else \
		return ObjRef(g_notImplemented);

static ObjRef floatAdd(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	return newFloat(a + b);
}

static ObjRef floatSub(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	return newFloat(a - b);
}

static ObjRef floatMul(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	return newFloat(a * b);
}

static ObjRef floatDiv(Object *v, Object *w) {
	CONVERT(v, a) CONVERT(w, b)
	if (b == 0.0) {
		setErrorString(g_excZeroDivisionError, "float division");
		return ObjRef();
	}
	return newFloat(a / b);
}

static ObjRef floatRem(Object *v, Object *w) {
	CONVERT(v, vx) CONVERT(w, wx)
	if (wx == 0.0) {
		setErrorString(g_excZeroDivisionError, "float modulo");
		return ObjRef();
	}
	double mod = fmod(vx, wx);
	if (mod && ((wx < 0) != (mod < 0)))
		mod += wx;
	return newFloat(mod);
}

static ObjRef floatDivmod(Object *v, Object *w) {
	CONVERT(v, vx) CONVERT(w, wx)
	if (wx == 0.0) {
		setErrorString(g_excZeroDivisionError, "float divmod()");
		return ObjRef();
	}
	double mod = fmod(vx, wx);
	double div = (vx - mod) / wx;
	if (mod && ((wx < 0) != (mod < 0))) {
		mod += wx;
		div -= 1.0;
	}
	// Snap the quotient to the nearest integral value
	double floordiv = floor(div);
	if (div - floordiv > 0.5)
		floordiv += 1.0;
	ObjRef d = newFloat(floordiv), m = newFloat(mod);
	return tuplePack(2, d.get(), m.get());
}

static ObjRef floatFloorDiv(Object *v, Object *w) {
	ObjRef t = floatDivmod(v, w);
	if (!t || t.get() == g_notImplemented)
		return t;
	return ObjRef(((Tuple *)t.get())->get(0));
}

static ObjRef floatPow(Object *v, Object *w, Object *z) {
	if (z != g_none) {
		setErrorString(g_excTypeError, "pow() 3rd argument not allowed unless all arguments are integers");
		return ObjRef();
	}
	CONVERT(v, iv) CONVERT(w, iw)
	if (iw == 0)
		return newFloat(1.0);
	if (iv == 0.0) {
		if (iw < 0.0) {
			setErrorString(g_excZeroDivisionError, "0.0 cannot be raised to a negative power");
			return ObjRef();
		}
		return newFloat(0.0);
	}
	if (iv < 0.0 && iw != floor(iw)) {
		setErrorString(g_excValueError, "negative number cannot be raised to a fractional power");
		return ObjRef();
	}
	double ix = pow(iv, iw);
	if (isInf(ix) && !isInf(iv) && !isInf(iw)) {
		setErrorString(g_excOverflowError, "(34, 'Result too large')");
		return ObjRef();
	}
	return newFloat(ix);
}

static ObjRef floatNeg(Object *v) { return newFloat(-((Float *)v)->_value); }
static ObjRef floatPos(Object *v) {
	if (isType(v, &g_floatType))
		return ObjRef(v);
	return newFloat(((Float *)v)->_value);
}
static ObjRef floatAbs(Object *v) { return newFloat(fabs(((Float *)v)->_value)); }
static int floatNonzero(Object *v) { return ((Float *)v)->_value != 0.0; }

static int floatCoerce(ObjRef *pv, ObjRef *pw) {
	Object *w = pw->get();
	if (isInstanceOf(w, &g_intType)) {
		*pw = newFloat(((Int *)w)->_value);
		return 0;
	}
	if (isInstanceOf(w, &g_longType)) {
		double d;
		if (!longAsDouble(w, &d))
			return -1;
		*pw = newFloat(d);
		return 0;
	}
	if (isInstanceOf(w, &g_floatType))
		return 0;
	return 1;
}

static ObjRef floatInt(Object *v) {
	double x = ((Float *)v)->_value;
	double wholepart;
	modf(x, &wholepart);
	if (wholepart > -2147483649.0 && wholepart < 2147483648.0)
		return newInt((int32)wholepart);
	setErrorString(g_excOverflowError, "float too large to convert");
	return ObjRef();
}

static ObjRef floatLong(Object *v) {
	return longFromDouble(((Float *)v)->_value);
}

static ObjRef floatFloat(Object *v) {
	return floatPos(v);
}

static ObjRef floatReprSlot(Object *v) { return newStr(floatRepr(((Float *)v)->_value)); }
static ObjRef floatStrSlot(Object *v) { return newStr(floatStr(((Float *)v)->_value)); }

static int floatCompare(Object *v, Object *w) {
	double a = ((Float *)v)->_value, b = ((Float *)w)->_value;
	return a < b ? -1 : a > b ? 1 : 0;
}

static bool floatHash(Object *v, int32 *out) {
	*out = hashDouble(((Float *)v)->_value);
	return true;
}

ObjRef floatFromString(Str *s) {
	const char *p = s->_data;
	const char *end = p + s->_len;
	while (p < end && Common::isSpace(*p))
		p++;
	if (p == end) {
		setErrorString(g_excValueError, "empty string for float()");
		return ObjRef();
	}
	if (strlen(s->_data) != s->_len) {
		setErrorString(g_excValueError, "null byte in argument for float()");
		return ObjRef();
	}
	char *last;
	double x = strtod(p, &last);
	const char *q = last;
	while (q < end && Common::isSpace(*q))
		q++;
	if (last == p || q != end) {
		setErrorFormat(g_excValueError, "invalid literal for float(): %.200s", s->_data);
		return ObjRef();
	}
	return newFloat(x);
}

static ObjRef floatNew(Type *type, Tuple *args, Dict *kw) {
	if (!noKeywords("float", kw) || !checkArgs("float", args, 0, 1))
		return ObjRef();
	ObjRef r;
	if (args->_size == 0)
		r = newFloat(0.0);
	else
		r = numberFloat(args->get(0));
	if (!r || type == &g_floatType)
		return r;
	return ObjRef(new Float(type, ((Float *)r.get())->_value));
}

static NumberMethods g_floatAsNumber;

static ObjRef complexRepr(Object *v) {
	Complex *c = (Complex *)v;
	if (c->_real == 0.0)
		return newStr(formatDouble(c->_imag, 17, 'g') + "j");
	return newStr("(" + formatDouble(c->_real, 17, 'g') + (c->_imag >= 0 ? "+" : "") + formatDouble(c->_imag, 17, 'g') + "j)");
}

void initFloatType() {
	NumberMethods &m = g_floatAsNumber;
	m.add = floatAdd;
	m.subtract = floatSub;
	m.multiply = floatMul;
	m.divide = floatDiv;
	m.remainder = floatRem;
	m.divmod = floatDivmod;
	m.power = floatPow;
	m.negative = floatNeg;
	m.positive = floatPos;
	m.absolute = floatAbs;
	m.nonzero = floatNonzero;
	m.coerce = floatCoerce;
	m.toInt = floatInt;
	m.toLong = floatLong;
	m.toFloat = floatFloat;
	m.floorDivide = floatFloorDiv;
	m.trueDivide = floatDiv;

	Type &t = g_floatType;
	t._flags |= kTypeBaseType | kTypeCheckTypes;
	t.asNumber = &m;
	t.repr = floatReprSlot;
	t.str = floatStrSlot;
	t.compare = floatCompare;
	t.hash = floatHash;
	t.newObj = floatNew;
	t.getAttr = genericGetAttr;

	static NumberMethods complexAsNumber;
	g_complexType.asNumber = &complexAsNumber; // makes complex sort as a number
	g_complexType.repr = complexRepr;
	g_complexType.getAttr = genericGetAttr;
}

} // End of namespace Python
} // End of namespace Yaga
