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
#include "common/system.h"

#include "yaga/detection.h"
#include "yaga/display.h"
#include "yaga/modules.h"
#include "yaga/pyhost.h"
#include "yaga/python/internal.h"

// yagagraphics: the graphics system, its one video device and the render
// target the game draws into. Images and animations are still stand-ins.

namespace Yaga {

using namespace Python;

enum {
	kPixelR5G6B5 = 1
};

enum {
	kTargetWindowed = 1,
	kClearBack = 1
};

static const ConstDef g_hardware[] = {
	{ "SYSTEM_SOFTWARE", 0 }, { "SYSTEM_HARDWARE", 1 },
	{ nullptr, 0 }
};

static const ConstDef g_pixelFormats[] = {
	{ "PXL_UNKNOWN", 0 }, { "PXL_R5G6B5", kPixelR5G6B5 }, { "PXL_X1R5G5B5", 2 }, { "PXL_A1R5G5B5", 3 },
	{ "PXL_R8G8B8", 4 }, { "PXL_X8R8G8B8", 5 }, { "PXL_A8R8G8B8", 6 }, { "PXL_PAL8", 7 },
	{ "PXL_APAL8", 8 }, { "PXL_A8PAL8", 9 }, { "PXL_LUM8", 10 }, { "PXL_A8LUM8", 11 }, { "PXL_A8", 12 },
	{ nullptr, 0 }
};

static const ConstDef g_targetTypes[] = {
	{ "TARGET_DEFAULT", 0 }, { "TARGET_WINDOWED", kTargetWindowed }, { "TARGET_FULL_SCREEN", 2 },
	{ nullptr, 0 }
};

static const ConstDef g_clearFlags[] = {
	{ "CLEAR_BACK", kClearBack }, { "CLEAR_ZBUFFER", 2 }, { "CLEAR_STENCIL", 4 },
	{ nullptr, 0 }
};

static const ConstDef g_renderHints[] = {
	{ "RENDER_DEFAULT", 0 },
	{ nullptr, 0 }
};

static const ConstDef g_compressionTypes[] = {
	{ "COMPRESS_NONE", 0 }, { "COMPRESS_YRLE", 1 },
	{ nullptr, 0 }
};

static const ConstDef g_geomFlags[] = {
	{ "GEOMFLAG_XYZ", 1 }, { "GEOMFLAG_DIFFUSE", 2 },
	{ nullptr, 0 }
};

static const ConstDef g_geomTypes[] = {
	{ "GEOMTYPE_LINE_LIST", 1 }, { "GEOMTYPE_LINE_STRIP", 2 },
	{ nullptr, 0 }
};

static Type g_videoModeType("yagagraphics.VideoMode", &g_objectType);
static Type g_rectType("yagagraphics.Rect", &g_objectType);
static Type g_colorType("yagagraphics.Color", &g_objectType);
static Type g_systemType("yagagraphics.GraphicsSystem", &g_objectType);
static Type g_deviceType("yagagraphics.VideoDevice", &g_objectType);
static Type g_targetType("yagagraphics.RenderTarget", &g_objectType);

// Fills attributes from positional or keyword arguments, with defaults
static bool fillAttrs(Object *o, const char *fname, const char *const *names, const int32 *defaults, uint32 count, Tuple *args, Dict *kw) {
	if (!checkArgs(fname, args, 0, count))
		return false;
	for (uint32 i = 0; i < count; i++) {
		Object *v = i < args->_size ? args->get(i) : nullptr;
		if (!v && kw)
			v = kw->getItemString(names[i]);
		if (v)
			setEngineAttr(o, names[i], v);
		else
			setEngineAttr(o, names[i], newInt(defaults[i]).get());
	}
	return true;
}

static ObjRef attrsRepr(Object *o, const char *prefix, const char *const *names, uint32 count) {
	Common::String s = Common::String(prefix) + "(";
	for (uint32 i = 0; i < count; i++) {
		Object *v = o->getDict()->getItemString(names[i]);
		ObjRef r = v ? repr(v) : ObjRef();
		if (!r) {
			clearError();
			r = newStr("?");
		}
		s += Common::String::format("%s%s", i ? ", " : "", ((Str *)r.get())->_data);
	}
	return newStr(s + ")");
}

static const char *const g_modeNames[] = { "width", "height", "format" };
static const int32 g_modeDefaults[] = { 640, 480, kPixelR5G6B5 };

static ObjRef newVideoMode(int32 w, int32 h, int32 format) {
	ObjRef m(new EngineObject(&g_videoModeType));
	setEngineAttr(m.get(), "width", newInt(w).get());
	setEngineAttr(m.get(), "height", newInt(h).get());
	setEngineAttr(m.get(), "format", newInt(format).get());
	return m;
}

static ObjRef videoModeNew(Type *t, Tuple *args, Dict *kw) {
	ObjRef m(new EngineObject(t));
	if (!fillAttrs(m.get(), "VideoMode", g_modeNames, g_modeDefaults, 3, args, kw))
		return ObjRef();
	return m;
}

static ObjRef videoModeRepr(Object *o) {
	return attrsRepr(o, "VideoMode", g_modeNames, 3);
}

static const char *const g_rectNames[] = { "x", "y", "width", "height" };
static const int32 g_rectDefaults[] = { 0, 0, 0, 0 };

static ObjRef rectNew(Type *t, Tuple *args, Dict *kw) {
	ObjRef r(new EngineObject(t));
	if (!fillAttrs(r.get(), "Rect", g_rectNames, g_rectDefaults, 4, args, kw))
		return ObjRef();
	return r;
}

static ObjRef rectRepr(Object *o) {
	return attrsRepr(o, "Rect", g_rectNames, 4);
}

static const char *const g_colorNames[] = { "r", "g", "b", "a" };
static const int32 g_colorDefaults[] = { 0, 0, 0, 255 };

static ObjRef colorNew(Type *t, Tuple *args, Dict *kw) {
	ObjRef c(new EngineObject(t));
	if (!fillAttrs(c.get(), "Color", g_colorNames, g_colorDefaults, 4, args, kw))
		return ObjRef();
	return c;
}

static ObjRef colorRepr(Object *o) {
	return attrsRepr(o, "Color", g_colorNames, 4);
}

// ---- The render target ---------------------------------------------------------------

static int32 attrInt(Object *o, const char *name, int32 fallback) {
	Object *v = o->getDict()->getItemString(name);
	int32 i;
	if (!v || !asInt(v, &i)) {
		clearError();
		return fallback;
	}
	return i;
}

static ObjRef targetRenderBegin(Object *, Tuple *args, Dict *) {
	if (!checkArgs("RenderBegin", args, 0, 1))
		return ObjRef();
	int32 flags = 0;
	if (args->_size && !asInt(args->get(0), &flags))
		return ObjRef();
	if (flags & kClearBack)
		g_display->clear();
	return ObjRef(g_none);
}

static ObjRef targetRenderEnd(Object *, Tuple *, Dict *) {
	g_display->present();
	return ObjRef(g_none);
}

static const MethodDef g_targetMethods[] = {
	{ "RenderBegin", targetRenderBegin, kMethVarArgs, nullptr },
	{ "RenderEnd", targetRenderEnd, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// CreateRenderTarget(mode, buffers, targetType) sets the screen mode
static ObjRef deviceCreateTarget(Object *, Tuple *args, Dict *) {
	if (!checkArgs("CreateRenderTarget", args, 1, 3))
		return ObjRef();
	Object *mode = args->get(0);
	if (!isInstanceOf(mode, &g_videoModeType)) {
		setErrorFormat(g_excTypeError, "CreateRenderTarget() expects a VideoMode, got %.100s", typeName(mode));
		return ObjRef();
	}
	int32 w = attrInt(mode, "width", 640), h = attrInt(mode, "height", 480);
	g_display->open(w, h);
	ObjRef t(new EngineObject(&g_targetType));
	setEngineAttr(t.get(), "width", newInt(w).get());
	setEngineAttr(t.get(), "height", newInt(h).get());
	setEngineAttr(t.get(), "format", newInt(attrInt(mode, "format", kPixelR5G6B5)).get());
	setEngineAttr(t.get(), "mode", args->_size > 2 ? args->get(2) : newInt(kTargetWindowed).get());
	// Render target events carry this as their deviceID
	setEngineAttr(t.get(), "identity", newInt(1).get());
	return t;
}

static const MethodDef g_deviceMethods[] = {
	{ "CreateRenderTarget", deviceCreateTarget, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// GraphicsSystem() is a singleton with one video device
static Object *g_graphicsSystem = nullptr;

static ObjRef systemNew(Type *, Tuple *, Dict *) {
	if (g_graphicsSystem)
		return ObjRef(g_graphicsSystem);
	g_graphicsSystem = new EngineObject(&g_systemType);
	incRef(g_graphicsSystem);
	ObjRef device(new EngineObject(&g_deviceType));
	Ref<List> modes = newList();
	modes->append(newVideoMode(640, 480, kPixelR5G6B5).get());
	setEngineAttr(device.get(), "modes", modes.get());
	setEngineAttr(device.get(), "currentMode", newVideoMode(640, 480, kPixelR5G6B5).get());
	setEngineAttr(device.get(), "name", newStr("primary").get());
	Ref<List> devices = newList();
	devices->append(device.get());
	setEngineAttr(g_graphicsSystem, "systemType", newInt(0).get());
	setEngineAttr(g_graphicsSystem, "primaryVideoDev", device.get());
	setEngineAttr(g_graphicsSystem, "videoDevices", devices.get());
	return ObjRef(g_graphicsSystem);
}

static void fillYagagraphics(Module *m) {
	if (!(g_systemType._flags & kTypeReady)) {
		g_videoModeType.newObj = videoModeNew;
		g_videoModeType.repr = videoModeRepr;
		readyEngineType(&g_videoModeType, nullptr, nullptr, false);
		g_rectType.newObj = rectNew;
		g_rectType.repr = rectRepr;
		readyEngineType(&g_rectType, nullptr, nullptr, false);
		g_colorType.newObj = colorNew;
		g_colorType.repr = colorRepr;
		readyEngineType(&g_colorType, nullptr, nullptr, false);
		g_systemType.newObj = systemNew;
		readyEngineType(&g_systemType, nullptr, nullptr, true);
		readyEngineType(&g_deviceType, g_deviceMethods, nullptr, true);
		readyEngineType(&g_targetType, g_targetMethods, nullptr, true);
	}
	addConstants(m, "GraphicsHardware", g_hardware);
	addConstants(m, "PixelFormat", g_pixelFormats);
	addConstants(m, "TargetType", g_targetTypes);
	addConstants(m, "ClearFlags", g_clearFlags);
	addConstants(m, "RenderHint", g_renderHints);
	addConstants(m, "CompressionType", g_compressionTypes);
	addConstants(m, "GeomFlags", g_geomFlags);
	addConstants(m, "GeomType", g_geomTypes);
	moduleAddObject(m, "VideoMode", &g_videoModeType);
	moduleAddObject(m, "Rect", &g_rectType);
	moduleAddObject(m, "Color", &g_colorType);
	moduleAddObject(m, "GraphicsSystem", &g_systemType);
	moduleAddObject(m, "VideoDevice", &g_deviceType);
	moduleAddObject(m, "RenderTarget", &g_targetType);
}

void registerYagagraphics() {
	g_graphicsSystem = nullptr;
	registerEngineModule("yagagraphics", fillYagagraphics);
}

} // End of namespace Yaga
