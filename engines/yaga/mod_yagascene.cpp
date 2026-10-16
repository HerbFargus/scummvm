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

#include "yaga/modules.h"
#include "yaga/pyhost.h"
#include "yaga/python/internal.h"

// yagascene: positions, the scene manager and the scenes sprites run in.
// A scene's own methods are still stand-ins.

namespace Yaga {

using namespace Python;

static const ConstDef g_sceneEvents[] = {
	{ "SCENE_RUN", 0 }, { "SCENE_STOP", 1 }, { "SCENE_ADVANCE", 2 },
	{ nullptr, 0 }
};

static Type g_pointType("yagascene.Point", &g_objectType);
static Type g_sinkType("yagascene.ISceneEventSink", &g_objectType);
static Type g_sceneManagerType("yagascene.SceneManager", &g_objectType);
static Type g_sceneType("yagascene.Scene", &g_objectType);

// ---- Point ------------------------------------------------------------------------------

struct PointObject : public Object {
	ObjRef _coords[3];
	explicit PointObject(Type *t) : Object(t) {
		for (int i = 0; i < 3; i++)
			_coords[i] = newInt(0);
	}
};

// Point(), Point(x, y), Point(x, y, z), or Point(pair, ...) as some scripts write it
static ObjRef pointNew(Type *t, Tuple *args, Dict *kw) {
	if (!checkArgs("Point", args, 0, 3))
		return ObjRef();
	Ref<PointObject> p(new PointObject(t));
	static const char *const names[] = { "x", "y", "z" };
	for (uint32 i = 0; i < 3; i++) {
		Object *v = i < args->_size ? args->get(i) : (kw ? kw->getItemString(names[i]) : nullptr);
		if (v)
			p->_coords[i] = v;
	}
	Object *first = p->_coords[0].get();
	if (!isNumber(first) && !isInstanceOf(first, &g_strType) && length(first) >= 0) {
		ObjRef seq(first);
		int32 n = length(seq.get());
		p->_coords[0] = newInt(0);
		for (int32 i = 0; i < n && i < 2; i++) {
			ObjRef item = sequenceGetItem(seq.get(), i);
			if (!item)
				return ObjRef();
			if (i == 0 || args->_size < 2)
				p->_coords[i] = item;
		}
	}
	if (errorOccurred())
		clearError(); // length() of a plain object
	return ObjRef(p.get());
}

static ObjRef pointRepr(Object *o) {
	PointObject *p = (PointObject *)o;
	Common::String s = "Point(";
	for (int i = 0; i < 3; i++) {
		ObjRef r = repr(p->_coords[i].get());
		if (!r)
			return ObjRef();
		s += Common::String::format("%s%s", i ? ", " : "", ((Str *)r.get())->_data);
	}
	return newStr(s + ")");
}

#define POINT_COORD(name, index) \
	static ObjRef pointGet##name(Object *o) { return ((PointObject *)o)->_coords[index]; } \
	static int pointSet##name(Object *o, Object *v) { \
		if (!v) { \
			setErrorString(g_excTypeError, "cannot delete a coordinate"); \
			return -1; \
		} \
		((PointObject *)o)->_coords[index] = v; \
		return 0; \
	}

POINT_COORD(X, 0)
POINT_COORD(Y, 1)
POINT_COORD(Z, 2)

static const GetSetDef g_pointGetSets[] = {
	{ "x", pointGetX, pointSetX },
	{ "y", pointGetY, pointSetY },
	{ "z", pointGetZ, pointSetZ },
	{ nullptr, nullptr, nullptr }
};

// ---- ISceneEventSink: the base of the game's sprite callbacks ------------------------

static ObjRef sinkNoop(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static const MethodDef g_sinkMethods[] = {
	{ "__init__", sinkNoop, kMethVarArgs | kMethKeywords, nullptr },
	{ "Event", sinkNoop, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef sinkNew(Type *type, Tuple *, Dict *) {
	return ObjRef(new HeapObject(type));
}

// ---- The scene manager ----------------------------------------------------------------

static ObjRef managerCreateScene(Object *, Tuple *, Dict *) {
	return ObjRef(new EngineObject(&g_sceneType));
}

static const MethodDef g_managerMethods[] = {
	{ "CreateScene", managerCreateScene, kMethVarArgs | kMethKeywords, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static Object *g_sceneManager = nullptr;

static ObjRef managerNew(Type *, Tuple *, Dict *) {
	if (!g_sceneManager) {
		g_sceneManager = new EngineObject(&g_sceneManagerType);
		incRef(g_sceneManager);
	}
	return ObjRef(g_sceneManager);
}

static void fillYagascene(Module *m) {
	if (!(g_pointType._flags & kTypeReady)) {
		g_pointType._flags |= kTypeBaseType; // utility.CPointWrapper derives from it
		g_pointType.newObj = pointNew;
		g_pointType.repr = pointRepr;
		g_pointType.getSets = g_pointGetSets;
		g_pointType.getAttr = genericGetAttr;
		g_pointType.setAttr = genericSetAttr;
		typeReady(&g_pointType);
		g_sinkType._flags |= kTypeBaseType | kTypeHasDict;
		g_sinkType.methods = g_sinkMethods;
		g_sinkType.newObj = sinkNew;
		g_sinkType.getAttr = genericGetAttr;
		g_sinkType.setAttr = genericSetAttr;
		typeReady(&g_sinkType);
		g_sceneManagerType.newObj = managerNew;
		readyEngineType(&g_sceneManagerType, g_managerMethods, nullptr, true);
		readyEngineType(&g_sceneType, nullptr, nullptr, true);
	}
	addConstants(m, "SceneEvents", g_sceneEvents);
	moduleAddObject(m, "Point", &g_pointType);
	moduleAddObject(m, "ISceneEventSink", &g_sinkType);
	moduleAddObject(m, "SceneManager", &g_sceneManagerType);
	moduleAddObject(m, "Scene", &g_sceneType);
}

void registerYagascene() {
	g_sceneManager = nullptr;
	registerEngineModule("yagascene", fillYagascene);
}

} // End of namespace Yaga
