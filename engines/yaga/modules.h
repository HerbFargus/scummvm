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
	ResourceObject(Python::Type *t, const Common::String &path, Python::Str *data);
};

extern Python::Type g_resourceType;

// The bytes of a resource argument (a Resource object), or null with a TypeError
Python::Str *resourceData(Python::Object *o);

void registerYagares(Python::Interpreter *vm, Resources *resources);
void registerYagaxml(Python::Interpreter *vm, Resources *resources);

} // End of namespace Yaga

#endif // YAGA_MODULES_H
