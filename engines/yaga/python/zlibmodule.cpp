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

// zlib.h comes before ScummVM's headers, which forbid some of what it uses
#include "config.h"
#ifdef USE_ZLIB
#include <zlib.h>
#endif

#include "common/scummsys.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/vm.h"

// The zlib module (zlib.dll beside the games' executables). The games' own
// importer reads their bytecode archive with zlib.decompress.

namespace Yaga {
namespace Python {

static Object *g_zlibError = nullptr;

#ifdef USE_ZLIB

static ObjRef zlibFail(int err, const char *what, z_stream *zst) {
	if (err == Z_VERSION_ERROR)
		setErrorString(g_zlibError, "library version mismatch");
	else if (zst && zst->msg)
		setErrorFormat(g_zlibError, "Error %d %s: %.200s", err, what, zst->msg);
	else
		setErrorFormat(g_zlibError, "Error %d %s", err, what);
	return ObjRef();
}

static ObjRef zlibCompress(Object *, Tuple *args, Dict *) {
	if (!checkArgs("compress", args, 1, 2))
		return ObjRef();
	Str *s = asStr(args->get(0));
	int32 level = Z_DEFAULT_COMPRESSION;
	if (!s || (args->_size > 1 && !asInt(args->get(1), &level)))
		return ObjRef();
	uLong bound = s->_len + s->_len / 1000 + 12 + 1;
	Common::Array<byte> out;
	out.resize(bound);
	z_stream zst;
	memset(&zst, 0, sizeof(zst));
	zst.next_in = (Bytef *)s->_data;
	zst.avail_in = s->_len;
	zst.next_out = out.data();
	zst.avail_out = bound;
	int err = deflateInit(&zst, level);
	if (err == Z_MEM_ERROR) {
		setErrorString(g_excMemoryError, "Out of memory while compressing data");
		return ObjRef();
	}
	if (err != Z_OK) {
		deflateEnd(&zst);
		return zlibFail(err, "while compressing data", &zst);
	}
	err = deflate(&zst, Z_FINISH);
	if (err != Z_STREAM_END) {
		deflateEnd(&zst);
		return zlibFail(err, "while compressing data", &zst);
	}
	uLong total = zst.total_out;
	deflateEnd(&zst);
	return newStr((const char *)out.data(), total);
}

static ObjRef inflateAll(const char *data, uint32 len, int32 wbits, uint32 bufsize, uint32 *consumed, Common::String &out, z_stream *existing) {
	z_stream local;
	z_stream &zst = existing ? *existing : local;
	if (!existing) {
		memset(&zst, 0, sizeof(zst));
		int err = inflateInit2(&zst, wbits);
		if (err == Z_MEM_ERROR) {
			setErrorString(g_excMemoryError, "Out of memory while decompressing data");
			return ObjRef();
		}
		if (err != Z_OK) {
			inflateEnd(&zst);
			return zlibFail(err, "while preparing to decompress data", &zst);
		}
	}
	zst.next_in = (Bytef *)const_cast<char *>(data);

	zst.avail_in = len;
	Common::Array<byte> buf;
	buf.resize(bufsize ? bufsize : 16384);
	int err = Z_OK;
	for (;;) {
		zst.next_out = buf.data();
		zst.avail_out = buf.size();
		err = inflate(&zst, Z_SYNC_FLUSH);
		out += Common::String((const char *)buf.data(), buf.size() - zst.avail_out);
		if (err == Z_STREAM_END)
			break;
		if (err == Z_BUF_ERROR && zst.avail_out > 0) {
			// Out of input: decompress() calls this an incomplete stream
			if (!existing) {
				inflateEnd(&zst);
				setErrorString(g_zlibError, "Error -5 while decompressing data");
				return ObjRef();
			}
			break;
		}
		if (err != Z_OK) {
			ObjRef r = zlibFail(err, "while decompressing data", &zst);
			if (!existing)
				inflateEnd(&zst);
			return r;
		}
		if (zst.avail_in == 0 && zst.avail_out > 0) {
			if (!existing) {
				inflateEnd(&zst);
				setErrorString(g_zlibError, "Error -5 while decompressing data");
				return ObjRef();
			}
			break;
		}
	}
	if (consumed)
		*consumed = len - zst.avail_in;
	if (!existing)
		inflateEnd(&zst);
	return ObjRef(g_none);
}

static ObjRef zlibDecompress(Object *, Tuple *args, Dict *) {
	if (!checkArgs("decompress", args, 1, 3))
		return ObjRef();
	Str *s = asStr(args->get(0));
	int32 wbits = MAX_WBITS, bufsize = 16384;
	if (!s || (args->_size > 1 && !asInt(args->get(1), &wbits)) || (args->_size > 2 && !asInt(args->get(2), &bufsize)))
		return ObjRef();
	Common::String out;
	ObjRef r = inflateAll(s->_data, s->_len, wbits, bufsize, nullptr, out, nullptr);
	if (!r)
		return ObjRef();
	return newStr(out);
}

static ObjRef zlibCrc32(Object *, Tuple *args, Dict *) {
	if (!checkArgs("crc32", args, 1, 2))
		return ObjRef();
	Str *s = asStr(args->get(0));
	int32 start = 0;
	if (!s || (args->_size > 1 && !asInt(args->get(1), &start)))
		return ObjRef();
	uLong c = crc32((uLong)(uint32)start, (const Bytef *)s->_data, s->_len);
	return newInt((int32)(uint32)c);
}

static ObjRef zlibAdler32(Object *, Tuple *args, Dict *) {
	if (!checkArgs("adler32", args, 1, 2))
		return ObjRef();
	Str *s = asStr(args->get(0));
	int32 start = 1;
	if (!s || (args->_size > 1 && !asInt(args->get(1), &start)))
		return ObjRef();
	uLong a = adler32((uLong)(uint32)start, (const Bytef *)s->_data, s->_len);
	return newInt((int32)(uint32)a);
}

// Compression and decompression objects

struct ZStreamObject : public Object {
	z_stream _zst;
	bool _deflate;
	bool _open;
	Ref<Str> _unusedData, _unconsumedTail;
	ZStreamObject(Type *t, bool deflating) : Object(t), _deflate(deflating), _open(false) {
		memset(&_zst, 0, sizeof(_zst));
		_unusedData = newStr("");
		_unconsumedTail = newStr("");
	}
	~ZStreamObject() override {
		if (_open) {
			if (_deflate)
				deflateEnd(&_zst);
			else
				inflateEnd(&_zst);
		}
	}
};

static Type g_compressType("Compress", &g_objectType);
static Type g_decompressType("Decompress", &g_objectType);

static ObjRef compCompress(Object *self, Tuple *args, Dict *) {
	ZStreamObject *z = (ZStreamObject *)self;
	if (!checkArgs("compress", args, 1, 1))
		return ObjRef();
	Str *s = asStr(args->get(0));
	if (!s)
		return ObjRef();
	z->_zst.next_in = (Bytef *)s->_data;
	z->_zst.avail_in = s->_len;
	Common::String out;
	byte buf[16384];
	do {
		z->_zst.next_out = buf;
		z->_zst.avail_out = sizeof(buf);
		int err = deflate(&z->_zst, Z_NO_FLUSH);
		if (err != Z_OK && err != Z_BUF_ERROR)
			return zlibFail(err, "while compressing", &z->_zst);
		out += Common::String((const char *)buf, sizeof(buf) - z->_zst.avail_out);
	} while (z->_zst.avail_out == 0);
	return newStr(out);
}

static ObjRef compFlush(Object *self, Tuple *args, Dict *) {
	ZStreamObject *z = (ZStreamObject *)self;
	int32 mode = Z_FINISH;
	if (args->_size && !asInt(args->get(0), &mode))
		return ObjRef();
	if (mode == Z_NO_FLUSH)
		return newStr("");
	z->_zst.avail_in = 0;
	Common::String out;
	byte buf[16384];
	int err;
	do {
		z->_zst.next_out = buf;
		z->_zst.avail_out = sizeof(buf);
		err = deflate(&z->_zst, mode);
		if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR)
			return zlibFail(err, "while flushing", &z->_zst);
		out += Common::String((const char *)buf, sizeof(buf) - z->_zst.avail_out);
	} while (z->_zst.avail_out == 0 && err != Z_STREAM_END);
	if (err == Z_STREAM_END && mode == Z_FINISH) {
		deflateEnd(&z->_zst);
		z->_open = false;
	}
	return newStr(out);
}

static ObjRef decompDecompress(Object *self, Tuple *args, Dict *) {
	ZStreamObject *z = (ZStreamObject *)self;
	if (!checkArgs("decompress", args, 1, 2))
		return ObjRef();
	Str *s = asStr(args->get(0));
	if (!s)
		return ObjRef();
	Common::String out;
	uint32 used = 0;
	ObjRef r = inflateAll(s->_data, s->_len, 0, 16384, &used, out, &z->_zst);
	if (!r)
		return ObjRef();
	if (used < s->_len)
		z->_unusedData = newStr(s->_data + used, s->_len - used);
	return newStr(out);
}

static ObjRef decompFlush(Object *, Tuple *, Dict *) {
	return newStr("");
}

static ObjRef zGetUnused(Object *o) { return ObjRef(((ZStreamObject *)o)->_unusedData.get()); }
static ObjRef zGetTail(Object *o) { return ObjRef(((ZStreamObject *)o)->_unconsumedTail.get()); }

static const MethodDef g_compressMethods[] = {
	{ "compress", compCompress, kMethVarArgs, nullptr },
	{ "flush", compFlush, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const MethodDef g_decompressMethods[] = {
	{ "decompress", decompDecompress, kMethVarArgs, nullptr },
	{ "flush", decompFlush, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_decompressGetSets[] = {
	{ "unused_data", zGetUnused, nullptr },
	{ "unconsumed_tail", zGetTail, nullptr },
	{ nullptr, nullptr, nullptr }
};

static ObjRef zlibCompressobj(Object *, Tuple *args, Dict *) {
	int32 level = Z_DEFAULT_COMPRESSION, method = Z_DEFLATED, wbits = MAX_WBITS, memLevel = 8, strategy = 0;
	int32 *vals[5] = { &level, &method, &wbits, &memLevel, &strategy };
	for (uint32 i = 0; i < args->_size && i < 5; i++)
		if (!asInt(args->get(i), vals[i]))
			return ObjRef();
	ZStreamObject *z = new ZStreamObject(&g_compressType, true);
	ObjRef rz(z);
	int err = deflateInit2(&z->_zst, level, method, wbits, memLevel, strategy);
	if (err != Z_OK)
		return zlibFail(err, "while creating compression object", &z->_zst);
	z->_open = true;
	return rz;
}

static ObjRef zlibDecompressobj(Object *, Tuple *args, Dict *) {
	int32 wbits = MAX_WBITS;
	if (args->_size && !asInt(args->get(0), &wbits))
		return ObjRef();
	ZStreamObject *z = new ZStreamObject(&g_decompressType, false);
	ObjRef rz(z);
	int err = inflateInit2(&z->_zst, wbits);
	if (err != Z_OK)
		return zlibFail(err, "while creating decompression object", &z->_zst);
	z->_open = true;
	return rz;
}

static const MethodDef g_zlibMethods[] = {
	{ "compress", zlibCompress, kMethVarArgs, nullptr },
	{ "decompress", zlibDecompress, kMethVarArgs, nullptr },
	{ "compressobj", zlibCompressobj, kMethVarArgs, nullptr },
	{ "decompressobj", zlibDecompressobj, kMethVarArgs, nullptr },
	{ "crc32", zlibCrc32, kMethVarArgs, nullptr },
	{ "adler32", zlibAdler32, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

#else

static ObjRef zlibUnavailable(Object *, Tuple *, Dict *) {
	setErrorString(g_zlibError, "zlib is not available in this build");
	return ObjRef();
}

static const MethodDef g_zlibMethods[] = {
	{ "compress", zlibUnavailable, kMethVarArgs, nullptr },
	{ "decompress", zlibUnavailable, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

#endif

static void initZlib(Module *m) {
	ObjRef error = newExceptionClass("zlib.error", nullptr);
	if (g_zlibError)
		decRef(g_zlibError);
	g_zlibError = error.get();
	incRef(g_zlibError);
	moduleAddObject(m, "error", error.get());
	for (const MethodDef *d = g_zlibMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
#ifdef USE_ZLIB
	g_compressType.methods = g_compressMethods;
	g_compressType.getAttr = genericGetAttr;
	typeReady(&g_compressType);
	g_decompressType.methods = g_decompressMethods;
	g_decompressType.getSets = g_decompressGetSets;
	g_decompressType.getAttr = genericGetAttr;
	typeReady(&g_decompressType);
	moduleAddInt(m, "MAX_WBITS", MAX_WBITS);
	moduleAddInt(m, "DEFLATED", Z_DEFLATED);
	moduleAddInt(m, "DEF_MEM_LEVEL", 8);
	moduleAddInt(m, "Z_BEST_SPEED", Z_BEST_SPEED);
	moduleAddInt(m, "Z_BEST_COMPRESSION", Z_BEST_COMPRESSION);
	moduleAddInt(m, "Z_DEFAULT_COMPRESSION", Z_DEFAULT_COMPRESSION);
	moduleAddInt(m, "Z_FILTERED", Z_FILTERED);
	moduleAddInt(m, "Z_HUFFMAN_ONLY", Z_HUFFMAN_ONLY);
	moduleAddInt(m, "Z_DEFAULT_STRATEGY", Z_DEFAULT_STRATEGY);
	moduleAddInt(m, "Z_FINISH", Z_FINISH);
	moduleAddInt(m, "Z_NO_FLUSH", Z_NO_FLUSH);
	moduleAddInt(m, "Z_SYNC_FLUSH", Z_SYNC_FLUSH);
	moduleAddInt(m, "Z_FULL_FLUSH", Z_FULL_FLUSH);
	moduleAddString(m, "ZLIB_VERSION", ZLIB_VERSION);
#endif
}

void registerZlibModule(Interpreter *vm) {
	vm->addNativeModule("zlib", initZlib, true);
}

} // End of namespace Python
} // End of namespace Yaga
