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

// The struct module, for a 32-bit little-endian Windows build: native sizes
// and alignment are those of MSVC on x86.

namespace Yaga {
namespace Python {

struct FormatCode {
	char code;
	int size;
	int align;
};

static const FormatCode g_nativeCodes[] = {
	{ 'x', 1, 1 }, { 'c', 1, 1 }, { 'b', 1, 1 }, { 'B', 1, 1 }, { 'h', 2, 2 }, { 'H', 2, 2 },
	{ 'i', 4, 4 }, { 'I', 4, 4 }, { 'l', 4, 4 }, { 'L', 4, 4 }, { 'q', 8, 8 }, { 'Q', 8, 8 },
	{ 'f', 4, 4 }, { 'd', 8, 8 }, { 's', 1, 1 }, { 'p', 1, 1 }, { 'P', 4, 4 }, { 0, 0, 0 }
};

static const FormatCode g_standardCodes[] = {
	{ 'x', 1, 1 }, { 'c', 1, 1 }, { 'b', 1, 1 }, { 'B', 1, 1 }, { 'h', 2, 1 }, { 'H', 2, 1 },
	{ 'i', 4, 1 }, { 'I', 4, 1 }, { 'l', 4, 1 }, { 'L', 4, 1 }, { 'q', 8, 1 }, { 'Q', 8, 1 },
	{ 'f', 4, 1 }, { 'd', 8, 1 }, { 's', 1, 1 }, { 'p', 1, 1 }, { 0, 0, 0 }
};

struct Format {
	bool bigEndian;
	bool native;
	const FormatCode *table;
};

static Format parsePrefix(const char *&fmt) {
	Format f;
	f.bigEndian = false;
	f.native = true;
	f.table = g_nativeCodes;
	switch (*fmt) {
	case '@':
		fmt++;
		break;
	case '=':
	case '<':
		f.native = false;
		f.table = g_standardCodes;
		fmt++;
		break;
	case '>':
	case '!':
		f.native = false;
		f.bigEndian = true;
		f.table = g_standardCodes;
		fmt++;
		break;
	default:
		break;
	}
	return f;
}

static const FormatCode *findCode(const Format &f, char c) {
	for (const FormatCode *e = f.table; e->code; e++)
		if (e->code == c)
			return e;
	setErrorString(g_excValueError, "bad char in struct format");
	return nullptr;
}

static int32 calcSize(const char *fmt) {
	Format f = parsePrefix(fmt);
	int32 size = 0;
	while (*fmt) {
		char c = *fmt++;
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			continue;
		int32 num = 1;
		if (Common::isDigit(c)) {
			num = c - '0';
			while (Common::isDigit(*fmt))
				num = num * 10 + (*fmt++ - '0');
			if (!*fmt) {
				setErrorString(g_excValueError, "repeat count given without format specifier");
				return -1;
			}
			c = *fmt++;
		}
		const FormatCode *e = findCode(f, c);
		if (!e)
			return -1;
		if (f.native && e->align > 1)
			size = (size + e->align - 1) / e->align * e->align;
		if (c == 's' || c == 'p')
			size += num;
		else
			size += num * e->size;
	}
	return size;
}

static void putInt(Common::String &out, uint64 v, int size, bool big) {
	for (int i = 0; i < size; i++) {
		int shift = big ? (size - 1 - i) * 8 : i * 8;
		out += (char)((v >> shift) & 0xff);
	}
}

static uint64 getInt(const uint8 *p, int size, bool big) {
	uint64 v = 0;
	for (int i = 0; i < size; i++) {
		int shift = big ? (size - 1 - i) * 8 : i * 8;
		v |= (uint64)p[i] << shift;
	}
	return v;
}

static bool argAsInt64(Object *o, int64 *out) {
	if (isInstanceOf(o, &g_intType)) {
		*out = ((Int *)o)->_value;
		return true;
	}
	if (isInstanceOf(o, &g_longType))
		return longAsInt64(o, out);
	setErrorString(g_excTypeError, "required argument is not an integer");
	return false;
}

static ObjRef structPack(Object *, Tuple *args, Dict *) {
	if (args->_size < 1) {
		setErrorString(g_excTypeError, "struct.pack requires at least one argument");
		return ObjRef();
	}
	Str *fmtStr = asStr(args->get(0));
	if (!fmtStr)
		return ObjRef();
	const char *fmt = fmtStr->_data;
	Format f = parsePrefix(fmt);
	Common::String out;
	uint32 argi = 1;
	while (*fmt) {
		char c = *fmt++;
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			continue;
		int32 num = 1;
		if (Common::isDigit(c)) {
			num = c - '0';
			while (Common::isDigit(*fmt))
				num = num * 10 + (*fmt++ - '0');
			c = *fmt++;
		}
		const FormatCode *e = findCode(f, c);
		if (!e)
			return ObjRef();
		if (f.native && e->align > 1)
			while (out.size() % e->align)
				out += '\0';
		if (c == 's' || c == 'p') {
			if (argi >= args->_size) {
				setErrorString(g_excTypeError, "insufficient arguments to pack");
				return ObjRef();
			}
			Str *s = asStr(args->get(argi++));
			if (!s) {
				clearError();
				setErrorString(g_excTypeError, "argument for 's' must be a string");
				return ObjRef();
			}
			int32 n = c == 'p' ? MIN<int32>(s->_len, num - 1) : MIN<int32>(s->_len, num);
			if (c == 'p')
				out += (char)MIN(n, 255);
			out += Common::String(s->_data, n);
			for (int32 k = n + (c == 'p' ? 1 : 0); k < num; k++)
				out += '\0';
			continue;
		}
		for (int32 k = 0; k < num; k++) {
			if (c == 'x') {
				out += '\0';
				continue;
			}
			if (argi >= args->_size) {
				setErrorString(g_excTypeError, "insufficient arguments to pack");
				return ObjRef();
			}
			Object *v = args->get(argi++);
			if (c == 'c') {
				Str *s = asStr(v);
				if (!s || s->_len != 1) {
					clearError();
					setErrorString(g_excTypeError, "char format require string of length 1");
					return ObjRef();
				}
				out += s->_data[0];
			} else if (c == 'f' || c == 'd') {
				double d;
				if (!asDouble(v, &d))
					return ObjRef();
				if (c == 'f') {
					float fl = (float)d;
					uint32 bits;
					memcpy(&bits, &fl, 4);
					putInt(out, bits, 4, f.bigEndian);
				} else {
					uint64 bits;
					memcpy(&bits, &d, 8);
					putInt(out, bits, 8, f.bigEndian);
				}
			} else {
				int64 x;
				if (!argAsInt64(v, &x))
					return ObjRef();
				putInt(out, (uint64)x, e->size, f.bigEndian);
			}
		}
	}
	if (argi != args->_size) {
		setErrorString(g_excTypeError, "too many arguments for pack format");
		return ObjRef();
	}
	return newStr(out);
}

static ObjRef structUnpack(Object *, Tuple *args, Dict *) {
	if (!checkArgs("unpack", args, 2, 2))
		return ObjRef();
	Str *fmtStr = asStr(args->get(0));
	Str *data = fmtStr ? asStr(args->get(1)) : nullptr;
	if (!data)
		return ObjRef();
	int32 size = calcSize(fmtStr->_data);
	if (size < 0)
		return ObjRef();
	if (size != (int32)data->_len) {
		setErrorString(g_excValueError, "unpack str size does not match format");
		return ObjRef();
	}
	const char *fmt = fmtStr->_data;
	Format f = parsePrefix(fmt);
	const uint8 *p = (const uint8 *)data->_data;
	uint32 pos = 0;
	Ref<List> result = newList();
	while (*fmt) {
		char c = *fmt++;
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			continue;
		int32 num = 1;
		if (Common::isDigit(c)) {
			num = c - '0';
			while (Common::isDigit(*fmt))
				num = num * 10 + (*fmt++ - '0');
			c = *fmt++;
		}
		const FormatCode *e = findCode(f, c);
		if (!e)
			return ObjRef();
		if (f.native && e->align > 1)
			pos = (pos + e->align - 1) / e->align * e->align;
		if (c == 's') {
			result->append(newStr((const char *)p + pos, num).get());
			pos += num;
			continue;
		}
		if (c == 'p') {
			int32 n = MIN<int32>(p[pos], num - 1);
			result->append(newStr((const char *)p + pos + 1, n).get());
			pos += num;
			continue;
		}
		for (int32 k = 0; k < num; k++) {
			if (c == 'x') {
				pos++;
				continue;
			}
			uint64 raw = getInt(p + pos, e->size, f.bigEndian);
			pos += e->size;
			ObjRef v;
			switch (c) {
			case 'c': v = strFromCharacter((uint8)raw); break;
			case 'b': v = newInt((int8)raw); break;
			case 'B': v = newInt((uint8)raw); break;
			case 'h': v = newInt((int16)raw); break;
			case 'H': v = newInt((uint16)raw); break;
			case 'i':
			case 'l': v = newInt((int32)raw); break;
			case 'I':
			case 'L':
			case 'P': v = newIntOrLong((uint32)raw); break;
			case 'q': v = longFromInt64((int64)raw); break;
			case 'Q': v = longFromInt64((int64)raw); break;
			case 'f': {
				uint32 bits = (uint32)raw;
				float fl;
				memcpy(&fl, &bits, 4);
				v = newFloat(fl);
				break;
			}
			case 'd': {
				double d;
				memcpy(&d, &raw, 8);
				v = newFloat(d);
				break;
			}
			default:
				break;
			}
			result->append(v.get());
		}
	}
	return listAsTuple(result.get());
}

static ObjRef structCalcsize(Object *, Tuple *args, Dict *) {
	if (!checkArgs("calcsize", args, 1, 1))
		return ObjRef();
	Str *fmt = asStr(args->get(0));
	if (!fmt)
		return ObjRef();
	int32 n = calcSize(fmt->_data);
	if (n < 0)
		return ObjRef();
	return newInt(n);
}

static const MethodDef g_structMethods[] = {
	{ "pack", structPack, kMethVarArgs, nullptr },
	{ "unpack", structUnpack, kMethVarArgs, nullptr },
	{ "calcsize", structCalcsize, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

void initStructModule(Module *m) {
	for (const MethodDef *d = g_structMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	// struct.error is a string exception in Python 2.2
	moduleAddObject(m, "error", newStr("struct.error").get());
}

} // End of namespace Python
} // End of namespace Yaga
