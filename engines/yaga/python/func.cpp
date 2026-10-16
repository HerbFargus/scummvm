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

#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/eval.h"

// Functions, code objects, modules, frames and the small built-in types

namespace Yaga {
namespace Python {

Type g_noneType("NoneType", &g_objectType);
Type g_notImplementedType("NotImplementedType", &g_objectType);
Type g_ellipsisType("ellipsis", &g_objectType);
Type g_functionType("function", &g_objectType);
Type g_codeType("code", &g_objectType);
Type g_builtinFunctionType("builtin_function_or_method", &g_objectType);
Type g_moduleType("module", &g_objectType);
Type g_frameType("frame", &g_objectType);
Type g_tracebackType("traceback", &g_objectType);
Type g_propertyType("property", &g_objectType);
Type g_staticMethodType("staticmethod", &g_objectType);
Type g_classMethodType("classmethod", &g_objectType);
Type g_cellType("cell", &g_objectType);

static NoneObject g_noneObj(&g_noneType);
static NoneObject g_notImplementedObj(&g_notImplementedType);
static NoneObject g_ellipsisObj(&g_ellipsisType);
Object *g_none = &g_noneObj;
Object *g_notImplemented = &g_notImplementedObj;
Object *g_ellipsis = &g_ellipsisObj;

static ObjRef noneRepr(Object *) { return newStr("None"); }
static ObjRef notImplementedRepr(Object *) { return newStr("NotImplemented"); }
static ObjRef ellipsisRepr(Object *) { return newStr("Ellipsis"); }

// ---- Functions ---------------------------------------------------------------

Function::Function(Code *code, Dict *globals) : Object(&g_functionType), _code(code), _globals(globals), _doc(g_none) {
	_name = code->_name;
	Tuple *consts = code->_consts.get();
	if (consts && consts->_size > 0 && isInstanceOf(consts->get(0), &g_strType))
		_doc = consts->get(0);
}

ObjRef functionCall(Object *o, Tuple *args, Dict *kw) {
	Function *f = (Function *)o;
	Common::Array<Object *> kws;
	if (kw) {
		int32 pos = 0;
		Object *k, *v;
		while (kw->next(&pos, &k, &v)) {
			kws.push_back(k);
			kws.push_back(v);
		}
	}
	Tuple *defs = f->_defaults.get();
	return evalCodeEx(f->_code.get(), f->_globals.get(), nullptr,
	                  args->_size ? args->_items : nullptr, args->_size,
	                  kws.empty() ? nullptr : &kws[0], kws.size() / 2,
	                  defs && defs->_size ? defs->_items : nullptr, defs ? defs->_size : 0,
	                  f->_closure.get());
}

static ObjRef functionDescrGet(Object *func, Object *obj, Object *type) {
	if (obj == g_none)
		obj = nullptr;
	return newMethod(func, obj, type);
}

static ObjRef functionRepr(Object *o) {
	Function *f = (Function *)o;
	if (f->_name.get() == nullptr || !isInstanceOf(f->_name.get(), &g_strType))
		return newStr(Common::String::format("<anonymous function at 0x%08X>", objectAddress(o)));
	return newStr(Common::String::format("<function %s at 0x%08X>", f->_name->_data, objectAddress(o)));
}

static ObjRef functionGetAttr(Object *o, Str *name) {
	Function *f = (Function *)o;
	const char *s = name->_data;
	if (name->equals("func_code"))
		return ObjRef(f->_code.get());
	if (name->equals("func_globals"))
		return ObjRef(f->_globals.get());
	if (name->equals("func_name") || name->equals("__name__"))
		return ObjRef(f->_name.get());
	if (name->equals("func_doc") || name->equals("__doc__"))
		return f->_doc;
	if (name->equals("func_defaults"))
		return f->_defaults ? ObjRef(f->_defaults.get()) : ObjRef(g_none);
	if (name->equals("func_closure"))
		return f->_closure ? ObjRef(f->_closure.get()) : ObjRef(g_none);
	if (name->equals("func_dict") || name->equals("__dict__")) {
		if (!f->_dict)
			f->_dict = newDict();
		return ObjRef(f->_dict.get());
	}
	if (name->equals("__class__"))
		return ObjRef(&g_functionType);
	if (f->_dict) {
		Object *v = f->_dict->getItem(name);
		if (v)
			return ObjRef(v);
	}
	ObjRef r = genericGetAttr(o, name);
	(void)s;
	return r;
}

static int functionSetAttr(Object *o, Str *name, Object *v) {
	Function *f = (Function *)o;
	if (name->equals("func_code")) {
		if (!v || !isType(v, &g_codeType)) {
			setErrorString(g_excTypeError, "func_code must be set to a code object");
			return -1;
		}
		f->_code = (Code *)v;
		return 0;
	}
	if (name->equals("func_defaults")) {
		if (v && v != g_none && !isInstanceOf(v, &g_tupleType)) {
			setErrorString(g_excTypeError, "func_defaults must be set to a tuple object");
			return -1;
		}
		f->_defaults = (v && v != g_none) ? (Tuple *)v : nullptr;
		return 0;
	}
	if (name->equals("func_doc") || name->equals("__doc__")) {
		f->_doc = v ? v : g_none;
		return 0;
	}
	if (name->equals("func_name") || name->equals("__name__") || name->equals("func_globals") || name->equals("func_closure")) {
		setErrorString(g_excTypeError, "readonly attribute");
		return -1;
	}
	if (name->equals("func_dict") || name->equals("__dict__")) {
		if (!v || !isInstanceOf(v, &g_dictType)) {
			setErrorString(g_excTypeError, "func_dict must be set to a dict object");
			return -1;
		}
		f->_dict = (Dict *)v;
		return 0;
	}
	if (!f->_dict) {
		if (!v) {
			setErrorFormat(g_excAttributeError, "'function' object has no attribute '%.400s'", name->_data);
			return -1;
		}
		f->_dict = newDict();
	}
	if (v)
		return f->_dict->setItem(name, v);
	int r = f->_dict->delItem(name);
	if (r < 0) {
		clearError();
		setErrorFormat(g_excAttributeError, "'function' object has no attribute '%.400s'", name->_data);
	}
	return r;
}

// ---- Code objects ------------------------------------------------------------

static ObjRef codeRepr(Object *o) {
	Code *c = (Code *)o;
	const char *filename = (c->_filename && isInstanceOf(c->_filename.get(), &g_strType)) ? c->_filename->_data : "???";
	const char *name = (c->_name && isInstanceOf(c->_name.get(), &g_strType)) ? c->_name->_data : "?";
	return newStr(Common::String::format("<code object %.100s at 0x%08X, file \"%.300s\", line %d>", name, objectAddress(o), filename, c->_firstLineNo));
}

static ObjRef codeGetAttr(Object *o, Str *name) {
	Code *c = (Code *)o;
	if (name->equals("co_argcount")) return newInt(c->_argCount);
	if (name->equals("co_nlocals")) return newInt(c->_nLocals);
	if (name->equals("co_stacksize")) return newInt(c->_stackSize);
	if (name->equals("co_flags")) return newInt(c->_flags);
	if (name->equals("co_code")) return ObjRef(c->_code.get());
	if (name->equals("co_consts")) return ObjRef(c->_consts.get());
	if (name->equals("co_names")) return ObjRef(c->_names.get());
	if (name->equals("co_varnames")) return ObjRef(c->_varNames.get());
	if (name->equals("co_freevars")) return ObjRef(c->_freeVars.get());
	if (name->equals("co_cellvars")) return ObjRef(c->_cellVars.get());
	if (name->equals("co_filename")) return ObjRef(c->_filename.get());
	if (name->equals("co_name")) return ObjRef(c->_name.get());
	if (name->equals("co_firstlineno")) return newInt(c->_firstLineNo);
	if (name->equals("co_lnotab")) return ObjRef(c->_lnotab.get());
	return genericGetAttr(o, name);
}

static int codeCompare(Object *a, Object *b) {
	Code *x = (Code *)a, *y = (Code *)b;
	int c = compare(x->_name.get(), y->_name.get());
	if (c) return c;
	c = x->_argCount - y->_argCount;
	if (c) return c < 0 ? -1 : 1;
	c = x->_nLocals - y->_nLocals;
	if (c) return c < 0 ? -1 : 1;
	c = x->_flags - y->_flags;
	if (c) return c < 0 ? -1 : 1;
	c = compare(x->_code.get(), y->_code.get());
	if (c) return c;
	c = compare(x->_consts.get(), y->_consts.get());
	if (c) return c;
	c = compare(x->_names.get(), y->_names.get());
	if (c) return c;
	return compare(x->_varNames.get(), y->_varNames.get());
}

static bool codeHash(Object *o, int32 *out) {
	Code *c = (Code *)o;
	int32 h0, h1, h2, h3;
	if (!hash(c->_name.get(), &h0) || !hash(c->_code.get(), &h1) || !hash(c->_consts.get(), &h2) || !hash(c->_names.get(), &h3))
		return false;
	int32 h = h0 ^ h1 ^ h2 ^ h3 ^ c->_argCount ^ c->_nLocals ^ c->_flags;
	*out = h == -1 ? -2 : h;
	return true;
}

int32 codeAddrToLine(Code *co, int32 addr) {
	int32 line = co->_firstLineNo;
	Str *tab = co->_lnotab.get();
	if (!tab)
		return line;
	int32 size = tab->_len / 2;
	const uint8 *p = (const uint8 *)tab->_data;
	int32 a = 0;
	while (--size >= 0) {
		a += *p++;
		if (a > addr)
			break;
		line += *p++;
	}
	return line;
}

// ---- Built-in functions ------------------------------------------------------

BuiltinFunction::BuiltinFunction(const MethodDef *def, Object *self) : Object(&g_builtinFunctionType), _def(def), _self(self) {
}

static ObjRef builtinCall(Object *o, Tuple *args, Dict *kw) {
	BuiltinFunction *f = (BuiltinFunction *)o;
	const MethodDef *def = f->_def;
	if (!(def->flags & kMethKeywords) && kw && kw->_used) {
		setErrorFormat(g_excTypeError, "%.200s() takes no keyword arguments", def->name);
		return ObjRef();
	}
	return def->func(f->_self.get(), args, kw);
}

static ObjRef builtinRepr(Object *o) {
	BuiltinFunction *f = (BuiltinFunction *)o;
	if (!f->_self || isType(f->_self.get(), &g_moduleType))
		return newStr(Common::String::format("<built-in function %s>", f->_def->name));
	return newStr(Common::String::format("<built-in method %s of %s object at 0x%08X>",
	                                     f->_def->name, typeName(f->_self.get()), objectAddress(f->_self.get())));
}

static ObjRef builtinGetAttr(Object *o, Str *name) {
	BuiltinFunction *f = (BuiltinFunction *)o;
	if (name->equals("__name__"))
		return newStr(f->_def->name);
	if (name->equals("__doc__"))
		return f->_def->doc ? ObjRef(newStr(f->_def->doc)) : ObjRef(g_none);
	if (name->equals("__self__"))
		return f->_self ? f->_self : ObjRef(g_none);
	return genericGetAttr(o, name);
}

static int builtinCompare(Object *a, Object *b) {
	BuiltinFunction *x = (BuiltinFunction *)a, *y = (BuiltinFunction *)b;
	if (x->_self != y->_self)
		return objectAddress(x->_self ? x->_self.get() : g_none) < objectAddress(y->_self ? y->_self.get() : g_none) ? -1 : 1;
	if (x->_def->func == y->_def->func)
		return 0;
	return strcmp(x->_def->name, y->_def->name) < 0 ? -1 : 1;
}

static bool builtinHash(Object *o, int32 *out) {
	BuiltinFunction *f = (BuiltinFunction *)o;
	int32 x = 0;
	if (f->_self && !hash(f->_self.get(), &x))
		return false;
	*out = x ^ (int32)(uintptr)f->_def->func;
	if (*out == -1)
		*out = -2;
	return true;
}

// ---- Modules -------------------------------------------------------------------

Module::Module(const char *name) : Object(&g_moduleType) {
	_dict = newDict();
	Ref<Str> n = newStr(name);
	_dict->setItemString("__name__", n.get());
	_dict->setItemString("__doc__", g_none);
}

Ref<Module> newModule(const char *name) {
	return Ref<Module>(new Module(name));
}

static ObjRef moduleRepr(Object *o) {
	Module *m = (Module *)o;
	Object *name = m->_dict->getItemString("__name__");
	const char *n = (name && isInstanceOf(name, &g_strType)) ? ((Str *)name)->_data : "?";
	Object *file = m->_dict->getItemString("__file__");
	if (!file || !isInstanceOf(file, &g_strType))
		return newStr(Common::String::format("<module '%s' (built-in)>", n));
	return newStr(Common::String::format("<module '%s' from '%s'>", n, ((Str *)file)->_data));
}

static ObjRef moduleGetAttr(Object *o, Str *name) {
	Module *m = (Module *)o;
	if (name->equals("__dict__"))
		return ObjRef(m->_dict.get());
	Object *v = m->_dict->getItem(name);
	if (v)
		return ObjRef(v);
	if (name->equals("__class__"))
		return ObjRef(&g_moduleType);
	// Methods inherited from object, such as __reduce__
	ObjRef descr = lookupType(o->_type, name);
	if (descr) {
		DescrGetFunc f = descr->_type->descrGet;
		return f ? f(descr.get(), o, o->_type) : descr;
	}
	setErrorFormat(g_excAttributeError, "'module' object has no attribute '%.400s'", name->_data);
	return ObjRef();
}

static int moduleSetAttr(Object *o, Str *name, Object *v) {
	Module *m = (Module *)o;
	if (name->equals("__dict__")) {
		setErrorString(g_excTypeError, "readonly attribute");
		return -1;
	}
	if (v)
		return m->_dict->setItem(name, v);
	int r = m->_dict->delItem(name);
	if (r < 0) {
		clearError();
		setErrorFormat(g_excAttributeError, "'module' object has no attribute '%.400s'", name->_data);
	}
	return r;
}

static ObjRef moduleNew(Type *, Tuple *args, Dict *kw) {
	if (!checkArgs("module", args, 1, 2))
		return ObjRef();
	Str *name = asStr(args->get(0));
	if (!name)
		return ObjRef();
	Ref<Module> m = newModule(name->_data);
	if (args->_size > 1)
		m->_dict->setItemString("__doc__", args->get(1));
	return m;
}

void moduleAddObject(Module *m, const char *name, Object *value) {
	m->_dict->setItemString(name, value);
}

void moduleAddInt(Module *m, const char *name, int32 value) {
	Ref<Int> v = newInt(value);
	m->_dict->setItemString(name, v.get());
}

void moduleAddString(Module *m, const char *name, const char *value) {
	Ref<Str> v = newStr(value);
	m->_dict->setItemString(name, v.get());
}

Ref<Module> initModule(const char *name, const MethodDef *methods, const char *doc) {
	Module *m = addModule(name);
	Ref<Module> rm(m);
	Ref<Str> modName = newStr(name);
	for (const MethodDef *d = methods; d && d->name; d++) {
		BuiltinFunction *f = new BuiltinFunction(d, nullptr);
		ObjRef rf(f);
		f->_module = modName;
		m->_dict->setItemString(d->name, f);
	}
	if (doc) {
		Ref<Str> d = newStr(doc);
		m->_dict->setItemString("__doc__", d.get());
	}
	return rm;
}

// ---- Property, staticmethod, classmethod ---------------------------------------

static ObjRef propertyGet(Object *self, Object *obj, Object *type) {
	Property *p = (Property *)self;
	if (!obj || obj == g_none)
		return ObjRef(self);
	if (!p->_get || p->_get.get() == g_none) {
		setErrorString(g_excAttributeError, "unreadable attribute");
		return ObjRef();
	}
	return callFunction(p->_get.get(), 1, obj);
}

static int propertySet(Object *self, Object *obj, Object *value) {
	Property *p = (Property *)self;
	Object *func = value ? p->_set.get() : p->_del.get();
	if (!func || func == g_none) {
		setErrorString(g_excAttributeError, value ? "can't set attribute" : "can't delete attribute");
		return -1;
	}
	ObjRef res = value ? callFunction(func, 2, obj, value) : callFunction(func, 1, obj);
	return res ? 0 : -1;
}

static int propertyInit(Object *self, Tuple *args, Dict *kw) {
	Property *p = (Property *)self;
	static const char *const kwlist[] = { "fget", "fset", "fdel", "doc" };
	ObjRef vals[4] = { ObjRef(g_none), ObjRef(g_none), ObjRef(g_none), ObjRef(g_none) };
	if (args->_size > 4) {
		setErrorString(g_excTypeError, "property() takes at most 4 arguments");
		return -1;
	}
	for (uint32 i = 0; i < args->_size; i++)
		vals[i] = args->get(i);
	if (kw) {
		int32 pos = 0;
		Object *k, *v;
		while (kw->next(&pos, &k, &v)) {
			bool found = false;
			for (int i = 0; i < 4; i++) {
				if (isInstanceOf(k, &g_strType) && ((Str *)k)->equals(kwlist[i])) {
					vals[i] = v;
					found = true;
				}
			}
			if (!found) {
				setErrorFormat(g_excTypeError, "'%s' is an invalid keyword argument for this function", isInstanceOf(k, &g_strType) ? ((Str *)k)->_data : "?");
				return -1;
			}
		}
	}
	p->_get = vals[0];
	p->_set = vals[1];
	p->_del = vals[2];
	p->_doc = vals[3];
	return 0;
}

static ObjRef propertyNew(Type *type, Tuple *, Dict *) {
	Property *p = new Property();
	if (type != &g_propertyType) {
		p->_type = type;
		incRef(type);
	}
	return ObjRef(p);
}

static ObjRef propertyGetAttr(Object *o, Str *name) {
	Property *p = (Property *)o;
	if (name->equals("fget")) return p->_get ? p->_get : ObjRef(g_none);
	if (name->equals("fset")) return p->_set ? p->_set : ObjRef(g_none);
	if (name->equals("fdel")) return p->_del ? p->_del : ObjRef(g_none);
	if (name->equals("__doc__")) return p->_doc ? p->_doc : ObjRef(g_none);
	return genericGetAttr(o, name);
}

static ObjRef staticMethodGet(Object *self, Object *, Object *) {
	return ((StaticMethod *)self)->_callable;
}

static ObjRef staticMethodNew(Type *, Tuple *args, Dict *kw) {
	if (!noKeywords("staticmethod", kw) || !checkArgs("staticmethod", args, 1, 1))
		return ObjRef();
	return ObjRef(new StaticMethod(args->get(0)));
}

static ObjRef classMethodGet(Object *self, Object *obj, Object *type) {
	if (!type)
		type = obj->_type;
	return newMethod(((ClassMethod *)self)->_callable.get(), type, type->_type);
}

static ObjRef classMethodNew(Type *, Tuple *args, Dict *kw) {
	if (!noKeywords("classmethod", kw) || !checkArgs("classmethod", args, 1, 1))
		return ObjRef();
	return ObjRef(new ClassMethod(args->get(0)));
}

// ---- Frames and tracebacks -------------------------------------------------------

static ObjRef frameGetAttr(Object *o, Str *name) {
	Frame *f = (Frame *)o;
	if (name->equals("f_back")) return f->_back ? ObjRef(f->_back.get()) : ObjRef(g_none);
	if (name->equals("f_code")) return ObjRef(f->_code.get());
	if (name->equals("f_globals")) return ObjRef(f->_globals.get());
	if (name->equals("f_builtins")) return ObjRef(f->_builtins.get());
	if (name->equals("f_lineno")) return newInt(f->_lineno);
	if (name->equals("f_lasti")) return newInt(f->_lasti);
	if (name->equals("f_restricted")) return newInt(0);
	if (name->equals("f_trace")) return ObjRef(g_none);
	if (name->equals("f_locals")) {
		fastToLocals(f);
		return f->_locals;
	}
	if (name->equals("f_exc_type")) return f->_excType ? f->_excType : ObjRef(g_none);
	if (name->equals("f_exc_value")) return f->_excValue ? f->_excValue : ObjRef(g_none);
	if (name->equals("f_exc_traceback")) return f->_excTraceback ? f->_excTraceback : ObjRef(g_none);
	return genericGetAttr(o, name);
}

static ObjRef tracebackGetAttr(Object *o, Str *name) {
	Traceback *tb = (Traceback *)o;
	if (name->equals("tb_next")) return tb->_next ? ObjRef(tb->_next.get()) : ObjRef(g_none);
	if (name->equals("tb_frame")) return ObjRef(tb->_frame.get());
	if (name->equals("tb_lasti")) return newInt(tb->_lasti);
	if (name->equals("tb_lineno")) return newInt(tb->_lineno);
	return genericGetAttr(o, name);
}

static ObjRef cellGetAttr(Object *o, Str *name) {
	return genericGetAttr(o, name);
}

void initFuncTypes() {
	g_noneType.repr = noneRepr;
	g_notImplementedType.repr = notImplementedRepr;
	g_ellipsisType.repr = ellipsisRepr;
	g_noneType.getAttr = genericGetAttr;
	g_notImplementedType.getAttr = genericGetAttr;
	g_ellipsisType.getAttr = genericGetAttr;

	g_functionType.call = functionCall;
	g_functionType.descrGet = functionDescrGet;
	g_functionType.repr = functionRepr;
	g_functionType.getAttr = functionGetAttr;
	g_functionType.setAttr = functionSetAttr;

	g_codeType.repr = codeRepr;
	g_codeType.getAttr = codeGetAttr;
	g_codeType.compare = codeCompare;
	g_codeType.hash = codeHash;

	g_builtinFunctionType.call = builtinCall;
	g_builtinFunctionType.repr = builtinRepr;
	g_builtinFunctionType.getAttr = builtinGetAttr;
	g_builtinFunctionType.compare = builtinCompare;
	g_builtinFunctionType.hash = builtinHash;

	g_moduleType.repr = moduleRepr;
	g_moduleType.getAttr = moduleGetAttr;
	g_moduleType.setAttr = moduleSetAttr;
	g_moduleType.newObj = moduleNew;
	g_moduleType._flags |= kTypeBaseType;

	g_propertyType.descrGet = propertyGet;
	g_propertyType.descrSet = propertySet;
	g_propertyType.init = propertyInit;
	g_propertyType.newObj = propertyNew;
	g_propertyType.getAttr = propertyGetAttr;
	g_propertyType._flags |= kTypeBaseType;

	g_staticMethodType.descrGet = staticMethodGet;
	g_staticMethodType.newObj = staticMethodNew;
	g_staticMethodType.getAttr = genericGetAttr;
	g_classMethodType.descrGet = classMethodGet;
	g_classMethodType.newObj = classMethodNew;
	g_classMethodType.getAttr = genericGetAttr;

	g_frameType.getAttr = frameGetAttr;
	g_tracebackType.getAttr = tracebackGetAttr;
	g_cellType.getAttr = cellGetAttr;
}

} // End of namespace Python
} // End of namespace Yaga
