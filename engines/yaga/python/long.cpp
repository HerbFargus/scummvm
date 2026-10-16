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

// Long integers, stored as CPython stores them: a sign and a magnitude of
// 15-bit digits, least significant first. The hash and the conversions depend
// on that digit size.

namespace Yaga {
namespace Python {

Type g_longType("long", &g_objectType);

enum {
	kShift = 15,
	kBase = 1 << kShift,
	kMask = kBase - 1
};

static Long *newLong(uint32 ndigits, bool negative = false) {
	Long *l = new Long();
	l->_digits.resize(ndigits);
	for (uint32 i = 0; i < ndigits; i++)
		l->_digits[i] = 0;
	l->_negative = negative;
	return l;
}

static void normalize(Long *l) {
	uint32 n = l->_digits.size();
	while (n > 0 && l->_digits[n - 1] == 0)
		n--;
	l->_digits.resize(n);
	if (n == 0)
		l->_negative = false;
}

ObjRef longNormalize(Long *l) {
	normalize(l);
	return ObjRef(l);
}

int longSign(Object *o) {
	Long *l = (Long *)o;
	if (l->_digits.empty())
		return 0;
	return l->_negative ? -1 : 1;
}

ObjRef longFromInt64(int64 v) {
	bool neg = v < 0;
	uint64 u = neg ? (uint64)(-(v + 1)) + 1 : (uint64)v;
	Long *l = newLong(0, neg);
	while (u) {
		l->_digits.push_back((uint16)(u & kMask));
		u >>= kShift;
	}
	if (l->_digits.empty())
		l->_negative = false;
	return ObjRef(l);
}

ObjRef longFromInt(Object *o) {
	if (isInstanceOf(o, &g_longType))
		return ObjRef(o);
	if (isInstanceOf(o, &g_intType))
		return longFromInt64(((Int *)o)->_value);
	setErrorString(g_excTypeError, "an integer is required");
	return ObjRef();
}

ObjRef longFromDouble(double d) {
	if (d != d || d - d != 0.0) {
		setErrorString(g_excOverflowError, "cannot convert float infinity to long");
		return ObjRef();
	}
	bool neg = d < 0;
	if (neg)
		d = -d;
	int expo;
	double frac = frexp(d, &expo);
	if (expo <= 0)
		return longFromInt64(0);
	uint32 ndig = (expo - 1) / kShift + 1;
	Long *l = newLong(ndig, neg);
	frac = ldexp(frac, (expo - 1) % kShift + 1);
	for (int i = ndig; --i >= 0;) {
		uint32 bits = (uint32)frac;
		l->_digits[i] = (uint16)bits;
		frac = frac - (double)bits;
		frac = ldexp(frac, kShift);
	}
	normalize(l);
	return ObjRef(l);
}

bool longAsInt64(Object *o, int64 *out) {
	if (isInstanceOf(o, &g_intType)) {
		*out = ((Int *)o)->_value;
		return true;
	}
	Long *l = (Long *)o;
	uint64 x = 0;
	for (int i = (int)l->_digits.size() - 1; i >= 0; i--) {
		uint64 prev = x;
		x = (x << kShift) | l->_digits[i];
		if ((x >> kShift) != prev || x > 0x8000000000000000ULL) {
			setErrorString(g_excOverflowError, "long int too large to convert");
			return false;
		}
	}
	if (l->_negative) {
		*out = x == 0x8000000000000000ULL ? (int64)(-0x7fffffffffffffffLL - 1) : -(int64)x;
	} else {
		if (x > 0x7fffffffffffffffULL) {
			setErrorString(g_excOverflowError, "long int too large to convert");
			return false;
		}
		*out = (int64)x;
	}
	return true;
}

bool longAsDouble(Object *o, double *out) {
	if (isInstanceOf(o, &g_intType)) {
		*out = ((Int *)o)->_value;
		return true;
	}
	Long *l = (Long *)o;
	double x = 0;
	for (int i = (int)l->_digits.size() - 1; i >= 0; i--)
		x = x * kBase + l->_digits[i];
	if (x - x != 0.0) {
		setErrorString(g_excOverflowError, "long int too large to convert to float");
		return false;
	}
	*out = l->_negative ? -x : x;
	return true;
}

// Magnitude arithmetic ---------------------------------------------------------

static Long *xAdd(Long *a, Long *b) {
	uint32 sa = a->_digits.size(), sb = b->_digits.size();
	if (sa < sb) {
		SWAP(a, b);
		SWAP(sa, sb);
	}
	Long *z = newLong(sa + 1);
	uint32 carry = 0, i;
	for (i = 0; i < sb; ++i) {
		carry += a->_digits[i] + b->_digits[i];
		z->_digits[i] = carry & kMask;
		carry >>= kShift;
	}
	for (; i < sa; ++i) {
		carry += a->_digits[i];
		z->_digits[i] = carry & kMask;
		carry >>= kShift;
	}
	z->_digits[i] = carry;
	normalize(z);
	return z;
}

static Long *xSub(Long *a, Long *b) {
	uint32 sa = a->_digits.size(), sb = b->_digits.size();
	bool sign = false;
	if (sa < sb) {
		sign = true;
		SWAP(a, b);
		SWAP(sa, sb);
	} else if (sa == sb) {
		int i = sa;
		while (--i >= 0 && a->_digits[i] == b->_digits[i])
			;
		if (i < 0)
			return newLong(0);
		if (a->_digits[i] < b->_digits[i]) {
			sign = true;
			SWAP(a, b);
		}
		sa = sb = i + 1;
	}
	Long *z = newLong(sa);
	uint32 borrow = 0, i;
	for (i = 0; i < sb; ++i) {
		borrow = a->_digits[i] - b->_digits[i] - borrow;
		z->_digits[i] = borrow & kMask;
		borrow >>= kShift;
		borrow &= 1;
	}
	for (; i < sa; ++i) {
		borrow = a->_digits[i] - borrow;
		z->_digits[i] = borrow & kMask;
		borrow >>= kShift;
		borrow &= 1;
	}
	z->_negative = sign;
	normalize(z);
	return z;
}

static Long *copyLong(Long *a) {
	Long *z = newLong(a->_digits.size(), a->_negative);
	for (uint32 i = 0; i < a->_digits.size(); i++)
		z->_digits[i] = a->_digits[i];
	return z;
}

static Long *lAdd(Long *a, Long *b) {
	Long *z;
	if (a->_negative) {
		if (b->_negative) {
			z = xAdd(a, b);
			if (!z->_digits.empty())
				z->_negative = true;
		} else {
			z = xSub(b, a);
		}
	} else {
		z = b->_negative ? xSub(a, b) : xAdd(a, b);
	}
	return z;
}

static Long *lSub(Long *a, Long *b) {
	Long *z;
	if (a->_negative) {
		if (b->_negative)
			z = xSub(a, b);
		else
			z = xAdd(a, b);
		if (!z->_digits.empty())
			z->_negative = !z->_negative;
	} else {
		z = b->_negative ? xAdd(a, b) : xSub(a, b);
	}
	return z;
}

static Long *lMul(Long *a, Long *b) {
	uint32 sa = a->_digits.size(), sb = b->_digits.size();
	Long *z = newLong(sa + sb);
	for (uint32 i = 0; i < sa; ++i) {
		uint32 carry = 0;
		uint32 f = a->_digits[i];
		for (uint32 j = 0; j < sb; j++) {
			carry += z->_digits[i + j] + b->_digits[j] * f;
			z->_digits[i + j] = carry & kMask;
			carry >>= kShift;
		}
		for (uint32 k = i + sb; carry != 0; k++) {
			carry += z->_digits[k];
			z->_digits[k] = carry & kMask;
			carry >>= kShift;
		}
	}
	z->_negative = (a->_negative != b->_negative);
	normalize(z);
	return z;
}

// Multiplies a magnitude by n and adds extra, in place
static Long *mulAdd1(Long *a, uint32 n, uint32 extra) {
	uint32 size = a->_digits.size();
	Long *z = newLong(size + 1);
	uint32 carry = extra, i;
	for (i = 0; i < size; ++i) {
		carry += (uint32)a->_digits[i] * n;
		z->_digits[i] = carry & kMask;
		carry >>= kShift;
	}
	z->_digits[i] = carry;
	normalize(z);
	return z;
}

// Divides a magnitude by a single digit; returns the remainder
static Long *divRem1(Long *a, uint32 n, uint32 *rem) {
	uint32 size = a->_digits.size();
	Long *z = newLong(size);
	uint32 r = 0;
	for (int i = size - 1; i >= 0; --i) {
		r = (r << kShift) | a->_digits[i];
		z->_digits[i] = (uint16)(r / n);
		r %= n;
	}
	*rem = r;
	normalize(z);
	return z;
}

// Knuth's algorithm D, exactly as CPython's x_divrem (with its 32-bit
// unsigned "twodigits" arithmetic)
static Long *xDivrem(Long *v1, Long *w1, Long **prem) {
	int32 sizeW = w1->_digits.size();
	uint32 d = (uint32)kBase / (w1->_digits[sizeW - 1] + 1);
	Long *v = mulAdd1(v1, d, 0);
	Long *w = mulAdd1(w1, d, 0);
	int32 sizeV = v->_digits.size();
	Long *a = newLong(sizeV - sizeW + 1);
	uint16 *vd = &v->_digits[0];
	const uint16 *wd = &w->_digits[0];
	for (int32 j = sizeV, k = a->_digits.size() - 1; k >= 0; --j, --k) {
		uint32 vj = (j >= sizeV) ? 0 : vd[j];
		uint32 q;
		int32 carry = 0;
		int32 i;
		if (vj == wd[sizeW - 1])
			q = kMask;
		else
			q = ((vj << kShift) + vd[j - 1]) / wd[sizeW - 1];
		while (wd[sizeW - 2] * q > ((((vj << kShift) + vd[j - 1] - q * wd[sizeW - 1]) << kShift) + vd[j - 2]))
			--q;
		for (i = 0; i < sizeW && i + k < sizeV; ++i) {
			uint32 z = wd[i] * q;
			uint32 zz = z >> kShift;
			carry += (int32)(vd[i + k] - z + (zz << kShift));
			vd[i + k] = carry & kMask;
			carry >>= kShift;
			carry -= zz;
		}
		if (i + k < sizeV) {
			carry += vd[i + k];
			vd[i + k] = 0;
		}
		if (carry == 0) {
			a->_digits[k] = (uint16)q;
		} else {
			a->_digits[k] = (uint16)(q - 1);
			carry = 0;
			for (i = 0; i < sizeW && i + k < sizeV; ++i) {
				carry += vd[i + k] + wd[i];
				vd[i + k] = carry & kMask;
				carry >>= kShift;
			}
		}
	}
	normalize(a);
	normalize(v);
	uint32 r;
	*prem = divRem1(v, d, &r);
	delete v;
	delete w;
	return a;
}

// Truncating division of magnitudes with signs applied, as CPython's long_divrem
static bool longDivrem(Long *a, Long *b, Long **pdiv, Long **prem) {
	uint32 sa = a->_digits.size(), sb = b->_digits.size();
	if (sb == 0) {
		setErrorString(g_excZeroDivisionError, "long division or modulo by zero");
		return false;
	}
	Long *z, *rem;
	if (sa < sb || (sa == sb && a->_digits[sa - 1] < b->_digits[sb - 1])) {
		*pdiv = newLong(0);
		*prem = copyLong(a);
		return true;
	}
	if (sb == 1) {
		uint32 r;
		z = divRem1(a, b->_digits[0], &r);
		Ref<Object> tmp = longFromInt64(r);
		rem = copyLong((Long *)tmp.get());
	} else {
		z = xDivrem(a, b, &rem);
	}
	if (a->_negative != b->_negative && !z->_digits.empty())
		z->_negative = true;
	if (a->_negative && !rem->_digits.empty())
		rem->_negative = true;
	*pdiv = z;
	*prem = rem;
	return true;
}

// Floor division, as CPython's l_divmod
static bool lDivmod(Long *v, Long *w, Long **pdiv, Long **pmod) {
	Long *div, *mod;
	if (!longDivrem(v, w, &div, &mod))
		return false;
	if ((mod->_negative && !w->_negative) || (!mod->_negative && w->_negative && !mod->_digits.empty())) {
		Long *temp = lAdd(mod, w);
		delete mod;
		mod = temp;
		Ref<Object> one = longFromInt64(1);
		temp = lSub(div, (Long *)one.get());
		delete div;
		div = temp;
	}
	*pdiv = div;
	*pmod = mod;
	return true;
}

// Slot functions ---------------------------------------------------------------

// Converts both operands to longs, or returns false for NotImplemented
static bool convertBinop(Object *v, Object *w, ObjRef *a, ObjRef *b) {
	if (isInstanceOf(v, &g_longType))
		*a = ObjRef(v);
	else if (isInstanceOf(v, &g_intType))
		*a = longFromInt64(((Int *)v)->_value);
	else
		return false;
	if (isInstanceOf(w, &g_longType))
		*b = ObjRef(w);
	else if (isInstanceOf(w, &g_intType))
		*b = longFromInt64(((Int *)w)->_value);
	else
		return false;
	return true;
}

#define CONVERT_BINOP(v, w) \
	ObjRef ra, rb; \
	if (!convertBinop(v, w, &ra, &rb)) \
		return ObjRef(g_notImplemented); \
	Long *a = (Long *)ra.get(), *b = (Long *)rb.get();

static ObjRef longAdd(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	return ObjRef(lAdd(a, b));
}

static ObjRef longSub(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	return ObjRef(lSub(a, b));
}

static ObjRef longMul(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	return ObjRef(lMul(a, b));
}

static ObjRef longDiv(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	Long *div, *mod;
	if (!lDivmod(a, b, &div, &mod))
		return ObjRef();
	delete mod;
	return ObjRef(div);
}

static ObjRef longMod(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	Long *div, *mod;
	if (!lDivmod(a, b, &div, &mod))
		return ObjRef();
	delete div;
	return ObjRef(mod);
}

static ObjRef longDivmodSlot(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	Long *div, *mod;
	if (!lDivmod(a, b, &div, &mod))
		return ObjRef();
	ObjRef d(div), m(mod);
	return tuplePack(2, d.get(), m.get());
}

static ObjRef longTrueDiv(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	double ad, bd;
	if (!longAsDouble(a, &ad) || !longAsDouble(b, &bd))
		return ObjRef();
	if (bd == 0.0) {
		setErrorString(g_excZeroDivisionError, "long division or modulo by zero");
		return ObjRef();
	}
	return newFloat(ad / bd);
}

static ObjRef longPow(Object *v, Object *w, Object *x) {
	CONVERT_BINOP(v, w)
	Long *c = nullptr;
	ObjRef rc;
	if (x != g_none) {
		if (isInstanceOf(x, &g_longType))
			rc = ObjRef(x);
		else if (isInstanceOf(x, &g_intType))
			rc = longFromInt64(((Int *)x)->_value);
		else
			return ObjRef(g_notImplemented);
		c = (Long *)rc.get();
	}
	if (b->_negative) {
		if (c) {
			setErrorString(g_excTypeError, "pow() 2nd argument cannot be negative when 3rd argument specified");
			return ObjRef();
		}
		// A float, as in 2.2
		double ad, bd;
		if (!longAsDouble(a, &ad) || !longAsDouble(b, &bd))
			return ObjRef();
		ObjRef fa = newFloat(ad), fb = newFloat(bd);
		return numberPower(fa.get(), fb.get(), g_none);
	}
	if (c && c->_digits.empty()) {
		setErrorString(g_excValueError, "pow() 3rd argument cannot be 0");
		return ObjRef();
	}
	ObjRef z = longFromInt64(1);
	ObjRef base(a);
	for (int i = 0; i < (int)b->_digits.size(); ++i) {
		uint32 bi = b->_digits[i];
		for (int j = 0; j < kShift; ++j) {
			if (bi & 1) {
				Long *t = lMul((Long *)z.get(), (Long *)base.get());
				z = ObjRef(t);
				if (c) {
					Long *div, *mod;
					if (!lDivmod((Long *)z.get(), c, &div, &mod))
						return ObjRef();
					delete div;
					z = ObjRef(mod);
				}
			}
			bi >>= 1;
			if (bi == 0 && i + 1 == (int)b->_digits.size())
				break;
			Long *t = lMul((Long *)base.get(), (Long *)base.get());
			base = ObjRef(t);
			if (c) {
				Long *div, *mod;
				if (!lDivmod((Long *)base.get(), c, &div, &mod))
					return ObjRef();
				delete div;
				base = ObjRef(mod);
			}
		}
	}
	if (c) {
		Long *div, *mod;
		if (!lDivmod((Long *)z.get(), c, &div, &mod))
			return ObjRef();
		delete div;
		z = ObjRef(mod);
	}
	return z;
}

static ObjRef longNeg(Object *v) {
	Long *z = copyLong((Long *)v);
	if (!z->_digits.empty())
		z->_negative = !z->_negative;
	return ObjRef(z);
}

static ObjRef longPos(Object *v) {
	if (isType(v, &g_longType))
		return ObjRef(v);
	return ObjRef(copyLong((Long *)v));
}

static ObjRef longAbs(Object *v) {
	if (((Long *)v)->_negative)
		return longNeg(v);
	return longPos(v);
}

static int longNonzero(Object *v) {
	return !((Long *)v)->_digits.empty();
}

static ObjRef longInvert(Object *v) {
	// ~x == -(x+1)
	ObjRef one = longFromInt64(1);
	Long *x = lAdd((Long *)v, (Long *)one.get());
	if (!x->_digits.empty())
		x->_negative = !x->_negative;
	return ObjRef(x);
}

static ObjRef longRshift(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	if (a->_negative) {
		// Right shift of a negative number: ~(~a >> b)
		ObjRef a1 = longInvert(a);
		ObjRef a2 = longRshift(a1.get(), b);
		if (!a2)
			return ObjRef();
		return longInvert(a2.get());
	}
	int64 shiftby;
	if (!longAsInt64(b, &shiftby))
		return ObjRef();
	if (shiftby < 0) {
		setErrorString(g_excValueError, "negative shift count");
		return ObjRef();
	}
	int32 wordshift = (int32)(shiftby / kShift);
	int32 newsize = (int32)a->_digits.size() - wordshift;
	if (newsize <= 0)
		return longFromInt64(0);
	int32 loshift = shiftby % kShift;
	int32 hishift = kShift - loshift;
	uint32 lomask = ((uint32)1 << hishift) - 1;
	uint32 himask = kMask ^ lomask;
	Long *z = newLong(newsize);
	for (int32 i = 0, j = wordshift; i < newsize; i++, j++) {
		z->_digits[i] = (a->_digits[j] >> loshift) & lomask;
		if (i + 1 < newsize)
			z->_digits[i] |= (a->_digits[j + 1] << hishift) & himask;
	}
	normalize(z);
	return ObjRef(z);
}

static ObjRef longLshift(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	int64 shiftby;
	if (!longAsInt64(b, &shiftby))
		return ObjRef();
	if (shiftby < 0) {
		setErrorString(g_excValueError, "negative shift count");
		return ObjRef();
	}
	if (shiftby > 0x7fffffff) {
		setErrorString(g_excValueError, "outrageous left shift count");
		return ObjRef();
	}
	int32 wordshift = (int32)shiftby / kShift;
	int32 remshift = (int32)shiftby - wordshift * kShift;
	int32 oldsize = a->_digits.size();
	int32 newsize = oldsize + wordshift;
	if (remshift)
		++newsize;
	Long *z = newLong(newsize, a->_negative);
	uint32 accum = 0;
	int32 i, j;
	for (i = wordshift, j = 0; j < oldsize; i++, j++) {
		accum |= (uint32)a->_digits[j] << remshift;
		z->_digits[i] = accum & kMask;
		accum >>= kShift;
	}
	if (remshift)
		z->_digits[newsize - 1] = (uint16)accum;
	normalize(z);
	return ObjRef(z);
}

// Bitwise operations on two's complement values, as CPython's long_bitwise
static ObjRef longBitwise(Long *a, int op, Long *b) {
	ObjRef ra(a), rb(b);
	uint32 maska = 0, maskb = 0;
	if (a->_negative) {
		ra = longInvert(a);
		maska = kMask;
	}
	if (b->_negative) {
		rb = longInvert(b);
		maskb = kMask;
	}
	a = (Long *)ra.get();
	b = (Long *)rb.get();
	bool negz = false;
	switch (op) {
	case '^':
		if (maska != maskb) {
			maska ^= kMask;
			negz = true;
		}
		break;
	case '&':
		if (maska && maskb) {
			op = '|';
			maska ^= kMask;
			maskb ^= kMask;
			negz = true;
		}
		break;
	case '|':
		if (maska || maskb) {
			op = '&';
			maska ^= kMask;
			maskb ^= kMask;
			negz = true;
		}
		break;
	default:
		break;
	}
	uint32 sizea = a->_digits.size(), sizeb = b->_digits.size();
	uint32 sizez;
	if (op == '&') {
		if (maska)
			sizez = sizeb;
		else if (maskb)
			sizez = sizea;
		else
			sizez = MIN(sizea, sizeb);
	} else {
		sizez = MAX(sizea, sizeb);
	}
	Long *z = newLong(sizez);
	for (uint32 i = 0; i < sizez; ++i) {
		uint32 diga = (i < sizea ? a->_digits[i] : 0) ^ maska;
		uint32 digb = (i < sizeb ? b->_digits[i] : 0) ^ maskb;
		switch (op) {
		case '&': z->_digits[i] = diga & digb; break;
		case '|': z->_digits[i] = diga | digb; break;
		case '^': z->_digits[i] = diga ^ digb; break;
		default: break;
		}
	}
	normalize(z);
	ObjRef rz(z);
	if (!negz)
		return rz;
	return longInvert(z);
}

static ObjRef longAnd(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	return longBitwise(a, '&', b);
}

static ObjRef longXor(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	return longBitwise(a, '^', b);
}

static ObjRef longOr(Object *v, Object *w) {
	CONVERT_BINOP(v, w)
	return longBitwise(a, '|', b);
}

static int longCoerce(ObjRef *pv, ObjRef *pw) {
	if (isInstanceOf(pw->get(), &g_intType)) {
		*pw = longFromInt64(((Int *)pw->get())->_value);
		return 0;
	}
	if (isInstanceOf(pw->get(), &g_longType))
		return 0;
	return 1;
}

static ObjRef longInt(Object *v) {
	int64 x;
	if (!longAsInt64(v, &x)) {
		clearError();
		setErrorString(g_excOverflowError, "long int too large to convert to int");
		return ObjRef();
	}
	if (x > 0x7fffffffLL || x < -0x7fffffffLL - 1) {
		setErrorString(g_excOverflowError, "long int too large to convert to int");
		return ObjRef();
	}
	return newInt((int32)x);
}

static ObjRef longLong(Object *v) {
	return longPos(v);
}

static ObjRef longFloat(Object *v) {
	double d;
	if (!longAsDouble(v, &d))
		return ObjRef();
	return newFloat(d);
}

ObjRef longFormat(Object *o, int base, bool addL) {
	Long *a = (Long *)o;
	Common::String digits;
	if (a->_digits.empty()) {
		digits = "0";
	} else {
		Long *scratch = copyLong(a);
		scratch->_negative = false;
		while (!scratch->_digits.empty()) {
			uint32 rem;
			Long *next = divRem1(scratch, base, &rem);
			delete scratch;
			scratch = next;
			digits += (char)(rem < 10 ? '0' + rem : 'A' + rem - 10);
		}
		delete scratch;
	}
	Common::String out;
	if (a->_negative)
		out += '-';
	if (base == 8) {
		if (!a->_digits.empty())
			out += '0';
	} else if (base == 16) {
		out += "0x";
	} else if (base != 10) {
		out += Common::String::format("%d#", base);
	}
	for (int i = digits.size() - 1; i >= 0; i--)
		out += digits[i];
	if (addL)
		out += 'L';
	return newStr(out);
}

static ObjRef longRepr(Object *v) { return longFormat(v, 10, true); }
static ObjRef longStr(Object *v) { return longFormat(v, 10, false); }
static ObjRef longOct(Object *v) { return longFormat(v, 8, true); }
static ObjRef longHex(Object *v) { return longFormat(v, 16, true); }

int longCompare(Long *a, Long *b) {
	int sa = longSign(a), sb = longSign(b);
	if (sa != sb)
		return sa < sb ? -1 : 1;
	int32 na = a->_digits.size(), nb = b->_digits.size();
	int sign;
	if (na != nb) {
		sign = na - nb;
	} else {
		int32 i = na;
		while (--i >= 0 && a->_digits[i] == b->_digits[i])
			;
		if (i < 0)
			sign = 0;
		else
			sign = (int)a->_digits[i] - (int)b->_digits[i];
	}
	if (sa < 0)
		sign = -sign;
	return sign < 0 ? -1 : sign > 0 ? 1 : 0;
}

static int longCompareSlot(Object *v, Object *w) {
	return longCompare((Long *)v, (Long *)w);
}

static bool longHash(Object *v, int32 *out) {
	Long *l = (Long *)v;
	uint32 x = 0;
	for (int i = (int)l->_digits.size() - 1; i >= 0; --i) {
		// A 32-bit circular shift, as CPython's long_hash
		x = ((x << kShift) & ~(uint32)kMask) | ((x >> (32 - kShift)) & kMask);
		x += l->_digits[i];
	}
	int32 r = (int32)x;
	if (l->_negative)
		r = -r;
	if (r == -1)
		r = -2;
	*out = r;
	return true;
}

ObjRef longFromString(const char *s, int base, bool allowWhitespace) {
	const char *start = s;
	if ((base != 0 && base < 2) || base > 36) {
		setErrorString(g_excValueError, "long() arg 2 must be >= 2 and <= 36");
		return ObjRef();
	}
	while (*s && Common::isSpace(*s))
		s++;
	bool neg = false;
	if (*s == '+' || *s == '-') {
		neg = *s == '-';
		s++;
	}
	while (*s && Common::isSpace(*s))
		s++;
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
	const char *digitsStart = s;
	ObjRef z = longFromInt64(0);
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
		z = ObjRef(mulAdd1((Long *)z.get(), base, d));
		s++;
	}
	if (s == digitsStart) {
		setErrorFormat(g_excValueError, "invalid literal for long(): %.200s", start);
		return ObjRef();
	}
	if (*s == 'L' || *s == 'l')
		s++;
	while (*s && Common::isSpace(*s))
		s++;
	if (*s) {
		setErrorFormat(g_excValueError, "invalid literal for long(): %.200s", start);
		return ObjRef();
	}
	if (neg && !((Long *)z.get())->_digits.empty())
		((Long *)z.get())->_negative = true;
	return z;
}

static ObjRef longNew(Type *type, Tuple *args, Dict *kw) {
	if (!noKeywords("long", kw) || !checkArgs("long", args, 0, 2))
		return ObjRef();
	ObjRef r;
	if (args->_size == 0) {
		r = longFromInt64(0);
	} else if (args->_size == 1) {
		r = numberLong(args->get(0));
	} else {
		int32 base;
		if (!asInt(args->get(1), &base))
			return ObjRef();
		if (!isInstanceOf(args->get(0), &g_strType)) {
			setErrorString(g_excTypeError, "long() can't convert non-string with explicit base");
			return ObjRef();
		}
		r = longFromString(((Str *)args->get(0))->_data, base, true);
	}
	if (!r || type == &g_longType)
		return r;
	Long *src = (Long *)r.get();
	Long *z = new Long(type);
	z->_digits = src->_digits;
	z->_negative = src->_negative;
	return ObjRef(z);
}

static NumberMethods g_longAsNumber;

void initLongType() {
	NumberMethods &m = g_longAsNumber;
	m.add = longAdd;
	m.subtract = longSub;
	m.multiply = longMul;
	m.divide = longDiv;
	m.remainder = longMod;
	m.divmod = longDivmodSlot;
	m.power = longPow;
	m.negative = longNeg;
	m.positive = longPos;
	m.absolute = longAbs;
	m.nonzero = longNonzero;
	m.invert = longInvert;
	m.lshift = longLshift;
	m.rshift = longRshift;
	m.and_ = longAnd;
	m.xor_ = longXor;
	m.or_ = longOr;
	m.coerce = longCoerce;
	m.toInt = longInt;
	m.toLong = longLong;
	m.toFloat = longFloat;
	m.toOct = longOct;
	m.toHex = longHex;
	m.floorDivide = longDiv;
	m.trueDivide = longTrueDiv;

	Type &t = g_longType;
	t._flags |= kTypeBaseType | kTypeCheckTypes;
	t.asNumber = &m;
	t.repr = longRepr;
	t.str = longStr;
	t.compare = longCompareSlot;
	t.hash = longHash;
	t.newObj = longNew;
	t.getAttr = genericGetAttr;
}

} // End of namespace Python
} // End of namespace Yaga
