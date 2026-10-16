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

#ifndef YAGA_MODULES_H
#define YAGA_MODULES_H

#include "common/ptr.h"

#include "yaga/imageanim.h"
#include "yaga/python/object.h"
#include "yaga/python/vm.h"

// The yaga* engine modules the game's scripts import (yaga.dll,
// yagares.dll, ...). Those not written yet are tracing stand-ins (pyhost.h).

namespace Yaga {

class Resources;

// A loaded asset, as yagares.ResourceManager.Load returns it
struct ResourceObject : public Python::Object {
	Common::String _path;
	Python::Ref<Python::Str> _data;
	Common::SharedPtr<AnimData> _anim; // decoded once, for IImageAnim
	ResourceObject(Python::Type *t, const Common::String &path, Python::Str *data);
};

extern Python::Type g_resourceType;

// The bytes of a resource argument (a Resource object), or null with a TypeError
Python::Str *resourceData(Python::Object *o);

// A native engine object whose attributes live in its own dict
struct EngineObject : public Python::Object {
	Python::Ref<Python::Dict> _dict;
	explicit EngineObject(Python::Type *t);
	Python::Dict *getDict() override { return _dict.get(); }
	void setDict(Python::Dict *d) override { _dict = d; }
};

// Readies an engine type. With traced set, names it lacks are traced
// stand-ins (for objects not fully written yet) rather than AttributeErrors
void readyEngineType(Python::Type *t, const Python::MethodDef *methods, const Python::GetSetDef *getSets, bool traced);

// Sets an attribute in an engine object's dict
void setEngineAttr(Python::Object *o, const char *name, Python::Object *value);

// A class of named constants, such as EEventClass.CLASS_TIMER
struct ConstDef {
	const char *name;
	int32 value;
};
void addConstants(Python::Module *m, const char *className, const ConstDef *defs);

// yagascene's Point: a value, so sprites copy the ones they are given
Python::ObjRef newPoint(int32 x, int32 y, int32 z);
Python::ObjRef copyPoint(Python::Object *o); // anything else is returned as it is
bool pointXY(Python::Object *o, int32 *x, int32 *y); // false if not a point (no error set)

// yagagraphics
Python::ObjRef newRect(int32 x, int32 y, int32 w, int32 h);
// The decoded frames behind an IImageAnim, or null
AnimData *imageAnimData(Python::Object *o);
double imageAnimFps(Python::Object *o);
Common::String imageAnimLocator(Python::Object *o);

// yagasprite: advances every playing sprite, once per loop
void advanceSprites();
// Ends any movie that is playing
void skipVideos();

void registerYagares(Python::Interpreter *vm, Resources *resources);
void registerYagaxml(Python::Interpreter *vm, Resources *resources);
void registerYagaevents();
void registerYagagraphics(Resources *resources);
void registerYagascene();
void registerYagasprite();

} // End of namespace Yaga

#endif // YAGA_MODULES_H
