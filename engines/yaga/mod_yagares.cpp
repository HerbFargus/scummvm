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

#include "common/debug.h"

#include "yaga/detection.h"
#include "yaga/modules.h"
#include "yaga/resources.h"
#include "yaga/python/internal.h"

// yagares: the engine's resource manager. Loading is synchronous, so a
// preload is just a load; the format handlers the game registers are kept
// but not consulted (each asset type's module decodes its own data).

namespace Yaga {

using namespace Python;

static Resources *g_res = nullptr;

Type g_resourceType("yagares.Resource", &g_objectType);
static Type g_resourceManagerType("yagares.ResourceManager", &g_objectType);

ResourceObject::ResourceObject(Type *t, const Common::String &path, Str *data) : Object(t), _path(path), _data(data) {
}

Str *resourceData(Object *o) {
	if (!isInstanceOf(o, &g_resourceType)) {
		setErrorFormat(g_excTypeError, "expected a resource, got %.100s", typeName(o));
		return nullptr;
	}
	return ((ResourceObject *)o)->_data.get();
}

static ObjRef resourceRepr(Object *o) {
	ResourceObject *r = (ResourceObject *)o;
	return newStr(Common::String::format("<Resource %s, %u bytes>", r->_path.c_str(), r->_data->_len));
}

static ObjRef resourceGetPath(Object *o) { return newStr(((ResourceObject *)o)->_path); }
static ObjRef resourceGetSize(Object *o) { return newInt(((ResourceObject *)o)->_data->_len); }
static ObjRef resourceGetLoaded(Object *) { return newInt(1); }

static const GetSetDef g_resourceGetSets[] = {
	{ "path", resourceGetPath, nullptr },
	{ "size", resourceGetSize, nullptr },
	{ "isLoaded", resourceGetLoaded, nullptr },
	{ nullptr, nullptr, nullptr }
};

struct ResourceManagerObject : public Object {
	Ref<Dict> _cache;    // lower-case path -> Resource, or 0 if missing
	Ref<List> _handlers;
	uint32 _cacheBytes;
	explicit ResourceManagerObject(Type *t) : Object(t), _cacheBytes(0) {
		_cache = newDict();
		_handlers = newList();
	}
};

static ObjRef managerNew(Type *type, Tuple *, Dict *) {
	return ObjRef(new ResourceManagerObject(type));
}

static ObjRef managerLoad(Object *self, Tuple *args, Dict *) {
	ResourceManagerObject *m = (ResourceManagerObject *)self;
	if (!checkArgs("Load", args, 1, 1))
		return ObjRef();
	ObjRef pathObj = str(args->get(0));
	if (!pathObj)
		return ObjRef();
	Common::String path = ((Str *)pathObj.get())->toString();
	Common::String key = path;
	key.toLowercase();
	Ref<Str> keyStr = newStr(key);
	Object *cached = m->_cache->getItem(keyStr.get());
	if (cached)
		return ObjRef(cached);
	Common::String bytes;
	if (!g_res->read(path, bytes)) {
		// Missing assets are 0, which the scripts test for (an .rle is tried before each .mng)
		debugC(2, kDebugScript, "yagares: no %s", path.c_str());
		Ref<Int> zero = newInt(0);
		m->_cache->setItem(keyStr.get(), zero.get());
		return zero;
	}
	Ref<Str> data = newStr(bytes);
	ObjRef r(new ResourceObject(&g_resourceType, path, data.get()));
	m->_cache->setItem(keyStr.get(), r.get());
	m->_cacheBytes += bytes.size();
	debugC(2, kDebugScript, "yagares: loaded %s, %u bytes", path.c_str(), bytes.size());
	return r;
}

static ObjRef managerRegister(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("RegisterFormatHandler", args, 1, 1))
		return ObjRef();
	((ResourceManagerObject *)self)->_handlers->append(args->get(0));
	return ObjRef(g_none);
}

static ObjRef managerUnregister(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("UnregisterFormatHandler", args, 1, 1))
		return ObjRef();
	List *l = ((ResourceManagerObject *)self)->_handlers.get();
	for (uint32 i = 0; i < l->size(); i++) {
		if (l->get(i) == args->get(0)) {
			l->_items.remove_at(i);
			decRef(args->get(0));
			break;
		}
	}
	return ObjRef(g_none);
}

static ObjRef managerFlush(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static ObjRef managerSave(Object *, Tuple *args, Dict *) {
	// Screen captures, which nothing here can make yet
	debugC(1, kDebugScript, "yagares: ResourceManager.Save ignored");
	return ObjRef(g_none);
}

static ObjRef managerGetCacheBytes(Object *o) {
	return newInt(((ResourceManagerObject *)o)->_cacheBytes);
}

static const MethodDef g_managerMethods[] = {
	{ "Load", managerLoad, kMethVarArgs, nullptr },
	{ "LoadStream", managerLoad, kMethVarArgs, nullptr },
	{ "Preload", managerLoad, kMethVarArgs, nullptr },
	{ "RegisterFormatHandler", managerRegister, kMethVarArgs, nullptr },
	{ "UnregisterFormatHandler", managerUnregister, kMethVarArgs, nullptr },
	{ "FlushPreloadQueue", managerFlush, kMethVarArgs, nullptr },
	{ "Save", managerSave, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_managerGetSets[] = {
	{ "cacheBytes", managerGetCacheBytes, nullptr },
	{ nullptr, nullptr, nullptr }
};

static void initYagares(Module *m) {
	if (!(g_resourceType._flags & kTypeReady)) {
		g_resourceType.repr = resourceRepr;
		g_resourceType.getSets = g_resourceGetSets;
		g_resourceType.getAttr = genericGetAttr;
		typeReady(&g_resourceType);
		g_resourceManagerType._flags |= kTypeBaseType;
		g_resourceManagerType.methods = g_managerMethods;
		g_resourceManagerType.getSets = g_managerGetSets;
		g_resourceManagerType.newObj = managerNew;
		g_resourceManagerType.getAttr = genericGetAttr;
		typeReady(&g_resourceManagerType);
	}
	moduleAddObject(m, "Resource", &g_resourceType);
	moduleAddObject(m, "ResourceManager", &g_resourceManagerType);
}

void registerYagares(Interpreter *vm, Resources *resources) {
	g_res = resources;
	vm->addNativeModule("yagares", initYagares, true);
}

} // End of namespace Yaga
