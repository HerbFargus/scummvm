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

namespace Yaga {

using namespace Python;

EngineObject::EngineObject(Type *t) : Object(t) {
	_dict = newDict();
}

void readyEngineType(Type *t, const MethodDef *methods, const GetSetDef *getSets, bool traced) {
	if (t->_flags & kTypeReady)
		return;
	t->_flags |= kTypeHasDict;
	t->methods = methods;
	t->getSets = getSets;
	t->getAttr = traced ? stubFallbackGetAttr : genericGetAttr;
	t->setAttr = genericSetAttr;
	typeReady(t);
}

void setEngineAttr(Object *o, const char *name, Object *value) {
	o->getDict()->setItemString(name, value);
}

void addConstants(Module *m, const char *className, const ConstDef *defs) {
	Ref<Dict> dict = newDict();
	Object *modName = m->_dict->getItemString("__name__");
	dict->setItemString("__module__", modName ? modName : g_none);
	for (const ConstDef *d = defs; d->name; d++)
		dict->setItemString(d->name, newInt(d->value).get());
	Ref<Tuple> bases = tuplePack(1, &g_objectType);
	Ref<Tuple> args = tuplePack(3, newStr(className).get(), bases.get(), dict.get());
	ObjRef cls = typeNew(&g_typeType, args.get(), nullptr);
	if (!cls) {
		warning("yaga: cannot make %s: %s", className, formatError().c_str());
		clearError();
		return;
	}
	moduleAddObject(m, className, cls.get());
}

void addEnum(Module *m, const char *className, const ConstDef *defs) {
	Ref<Dict> dict = newDict();
	Object *modName = m->_dict->getItemString("__name__");
	dict->setItemString("__module__", modName ? modName : g_none);
	Ref<Tuple> bases = tuplePack(1, &g_intType);
	Ref<Tuple> args = tuplePack(3, newStr(className).get(), bases.get(), dict.get());
	ObjRef cls = typeNew(&g_typeType, args.get(), nullptr);
	if (!cls) {
		warning("yaga: cannot make %s: %s", className, formatError().c_str());
		clearError();
		return;
	}
	for (const ConstDef *d = defs; d->name; d++) {
		ObjRef value = callFunction(cls.get(), 1, newInt(d->value).get());
		if (!value || setAttrString(cls.get(), d->name, value.get()) < 0) {
			warning("yaga: cannot make %s.%s: %s", className, d->name, formatError().c_str());
			clearError();
		}
	}
	moduleAddObject(m, className, cls.get());
}

} // End of namespace Yaga
