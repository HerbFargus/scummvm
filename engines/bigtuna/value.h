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

#ifndef BIGTUNA_VALUE_H
#define BIGTUNA_VALUE_H

#include "common/array.h"
#include "common/ptr.h"
#include "common/str.h"

namespace BigTuna {

// Object types as the player numbers them
enum ObjectType {
	kObjList = 0,
	kObjPropList = 1,
	kObjString = 3,
	kObjXtra = 4
};

struct Object;
typedef Common::SharedPtr<Object> ObjectPtr;

/**
 * A script value: void, an integer, or a reference to an object. Lists and
 * proplists are shared by reference, as in the original player.
 */
struct Value {
	enum Kind { kVoid, kInt, kObject };

	Value() : kind(kVoid), i(0) {}
	Value(int32 n) : kind(kInt), i(n) {}
	Value(const ObjectPtr &o) : kind(o ? kObject : kVoid), i(0), obj(o) {}

	static Value string(const Common::String &s);
	static Value list();
	static Value propList();
	static Value point(int32 h, int32 v);
	static Value rect(int32 l, int32 t, int32 r, int32 b);

	bool isVoid() const { return kind == kVoid; }
	bool isInt() const { return kind == kInt; }
	bool isObject() const { return kind == kObject; }
	bool isType(ObjectType t) const;
	bool isString() const { return isType(kObjString); }
	bool isList() const { return isType(kObjList); }
	bool isPropList() const { return isType(kObjPropList); }
	// The player's object type, or 5 for anything that is not an object
	int objectType() const;

	// popInt semantics: void is 0, strings go through atoi
	int32 toInt() const;
	// String conversion as the player does it (ints in decimal, lists bracketed)
	Common::String toString() const;
	// Raw word identity, as the player compares stack values
	bool identical(const Value &v) const;

	int32 prop(const char *key) const;
	void setProp(const char *key, const Value &v);
	Value getProp(const char *key) const;

	Kind kind;
	int32 i;
	ObjectPtr obj;
};

struct PropEntry {
	Common::String key;
	Value value;
};

struct Object {
	Object(ObjectType t) : type(t) {}

	ObjectType type;
	Common::String str;
	Common::Array<Value> items;
	Common::Array<PropEntry> props;

	PropEntry *findProp(const Common::String &key);
};

// stringToArray: "[1,"two",[3]]"
Value parseList(const char *&p);
// Three-way compare of two stack values (FUN_00408490): <0, 0, >0
int compareValues(const Value &a, const Value &b);

} // End of namespace BigTuna

#endif // BIGTUNA_VALUE_H
