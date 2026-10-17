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

#include "common/textconsole.h"
#include "common/util.h"

#include "bigtuna/value.h"

namespace BigTuna {

Value Value::string(const Common::String &s) {
	ObjectPtr o(new Object(kObjString));
	o->str = s;
	return Value(o);
}

Value Value::list() {
	return Value(ObjectPtr(new Object(kObjList)));
}

Value Value::propList() {
	return Value(ObjectPtr(new Object(kObjPropList)));
}

Value Value::point(int32 h, int32 v) {
	Value p = propList();
	p.setProp("loch", h);
	p.setProp("locv", v);
	return p;
}

Value Value::rect(int32 l, int32 t, int32 r, int32 b) {
	Value p = propList();
	p.setProp("left", l);
	p.setProp("top", t);
	p.setProp("right", r);
	p.setProp("bottom", b);
	return p;
}

bool Value::isType(ObjectType t) const {
	return kind == kObject && obj->type == t;
}

int Value::objectType() const {
	return kind == kObject ? (int)obj->type : 5;
}

int32 Value::toInt() const {
	if (kind == kInt)
		return i;
	if (isString())
		return atoi(obj->str.c_str());
	if (kind == kObject)
		warning("popInt: not an integer");
	return 0;
}

Common::String Value::toString() const {
	if (kind == kVoid)
		return "void";
	if (kind == kInt)
		return Common::String::format("%d", i);
	if (obj->type == kObjString)
		return obj->str;
	if (obj->type == kObjList) {
		Common::String s = "[";
		for (uint n = 0; n < obj->items.size(); n++) {
			if (n)
				s += ",";
			const Value &v = obj->items[n];
			if (v.isString())
				s += "\"" + v.obj->str + "\"";
			else
				s += v.toString();
		}
		return s + "]";
	}
	warning("arrayToString unimplemented type");
	return "";
}

bool Value::identical(const Value &v) const {
	if (kind != v.kind)
		return false;
	if (kind == kInt)
		return i == v.i;
	return obj == v.obj;
}

PropEntry *Object::findProp(const Common::String &key) {
	for (auto &p : props) {
		if (p.key.equalsIgnoreCase(key))
			return &p;
	}
	return nullptr;
}

Value Value::getProp(const char *key) const {
	if (kind != kObject)
		return Value();
	PropEntry *p = obj->findProp(key);
	return p ? p->value : Value();
}

int32 Value::prop(const char *key) const {
	return getProp(key).toInt();
}

void Value::setProp(const char *key, const Value &v) {
	if (kind != kObject)
		return;
	PropEntry *p = obj->findProp(key);
	if (p) {
		p->value = v;
		return;
	}
	obj->props.push_back(PropEntry{key, v});
}

Value parseList(const char *&p) {
	Value list = Value::list();
	for (;;) {
		char c = *p++;
		if (c == ']' || c == '\0') {
			if (c == '\0')
				p--;
			return list;
		}
		if (c == '[') {
			list.obj->items.push_back(parseList(p));
		} else if (c == '"') {
			const char *start = p;
			while (*p != '"' && *p)
				p++;
			list.obj->items.push_back(Value::string(Common::String(start, p - start)));
			if (*p == '"')
				p++;
		} else if (c >= '0' && c <= '9') {
			int32 n = c - '0';
			while (*p >= '0' && *p <= '9')
				n = n * 10 + (*p++ - '0');
			list.obj->items.push_back(n);
		} else if (c != ',' && c != ' ' && c != '\n' && c != '\r') {
			warning("stringToArray unknown char type");
		}
	}
}

static int strcmpiMS(const char *a, const char *b) {
	for (;; a++, b++) {
		int ca = tolower((byte)*a), cb = tolower((byte)*b);
		if (ca != cb || !ca)
			return ca - cb;
	}
}

int compareValues(const Value &a, const Value &b) {
	if (a.identical(b))
		return 0;
	if (a.isVoid())
		return (b.isInt() && b.i == 0) ? 0 : -1;
	if (b.isVoid())
		return (a.isInt() && a.i == 0) ? 0 : 1;
	if (a.isInt()) {
		if (b.isInt())
			return a.i < b.i ? -1 : 1;
	} else if (a.isString()) {
		if (b.isString())
			return strcmpiMS(a.obj->str.c_str(), b.obj->str.c_str());
		if (b.isInt()) {
			char c = a.obj->str.empty() ? 0 : a.obj->str[0];
			if (c >= '0' && c <= '9')
				return atoi(a.obj->str.c_str()) - b.i;
			return -1;
		}
	} else if (a.isPropList() && b.isPropList()) {
		int d = a.prop("loch") - b.prop("loch");
		if (!d)
			d = a.prop("locv") - b.prop("locv");
		return d;
	} else {
		return 1;
	}
	warning("unimplemented compare");
	return -1;
}

} // End of namespace BigTuna
