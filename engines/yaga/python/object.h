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

#ifndef YAGA_PYTHON_OBJECT_H
#define YAGA_PYTHON_OBJECT_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/str.h"

// A Python 2.2 virtual machine. The object model follows CPython 2.2 closely
// (type slots, classic classes, the dict layout and comparison rules), since
// the Yaga game scripts rely on its exact behaviour, including dict order.
//
// Errors are reported as in CPython: a function returns a null reference (or
// -1) and the error is held by the VM until it is caught or reported.

namespace Yaga {
namespace Python {

struct Object;
struct Type;
struct Str;
struct Tuple;
struct List;
struct Dict;
struct Int;
struct Code;
struct Frame;
struct Module;

void incRef(Object *o);
void decRef(Object *o);

// An owning reference. Constructing from a raw pointer adds a reference, so a
// new object is wrapped as soon as it is made: Ref<Int> i(new Int(5)).
template<class T>
class Ref {
public:
	Ref() : _p(nullptr) {}
	Ref(T *p) : _p(p) { if (_p) incRef((Object *)_p); }
	Ref(const Ref &r) : _p(r._p) { if (_p) incRef((Object *)_p); }
	template<class U>
	Ref(const Ref<U> &r) : _p(r.get()) { if (_p) incRef((Object *)_p); }
	~Ref() { if (_p) decRef((Object *)_p); }

	Ref &operator=(const Ref &r) {
		T *old = _p;
		_p = r._p;
		if (_p) incRef((Object *)_p);
		if (old) decRef((Object *)old);
		return *this;
	}
	Ref &operator=(T *p) {
		T *old = _p;
		_p = p;
		if (_p) incRef((Object *)_p);
		if (old) decRef((Object *)old);
		return *this;
	}

	T *get() const { return _p; }
	T *operator->() const { return _p; }
	T &operator*() const { return *_p; }
	operator bool() const { return _p != nullptr; }
	bool operator!() const { return _p == nullptr; }
	bool operator==(const Ref &r) const { return _p == r._p; }
	bool operator!=(const Ref &r) const { return _p != r._p; }

	// Gives up ownership; the caller must decRef the result.
	T *release() { T *p = _p; _p = nullptr; return p; }
	void reset() { T *old = _p; _p = nullptr; if (old) decRef((Object *)old); }

	template<class U>
	Ref<U> as() const { return Ref<U>((U *)_p); }

private:
	T *_p;
};

typedef Ref<Object> ObjRef;

typedef ObjRef (*UnaryFunc)(Object *);
typedef ObjRef (*BinaryFunc)(Object *, Object *);
typedef ObjRef (*TernaryFunc)(Object *, Object *, Object *);
typedef int (*InquiryFunc)(Object *);
typedef int32 (*LenFunc)(Object *);
typedef int (*CoerceFunc)(ObjRef *, ObjRef *);
typedef ObjRef (*IntArgFunc)(Object *, int32);
typedef ObjRef (*IntIntArgFunc)(Object *, int32, int32);
typedef int (*IntObjArgProc)(Object *, int32, Object *);
typedef int (*IntIntObjArgProc)(Object *, int32, int32, Object *);
typedef int (*ObjObjProc)(Object *, Object *);
typedef int (*ObjObjArgProc)(Object *, Object *, Object *);
typedef ObjRef (*GetAttrFunc)(Object *, Str *);
typedef int (*SetAttrFunc)(Object *, Str *, Object *);
typedef int (*CmpFunc)(Object *, Object *);
typedef ObjRef (*ReprFunc)(Object *);
typedef bool (*HashFunc)(Object *, int32 *);
typedef ObjRef (*RichCmpFunc)(Object *, Object *, int);
typedef ObjRef (*CallFunc)(Object *, Tuple *, Dict *);
typedef ObjRef (*DescrGetFunc)(Object *, Object *, Object *);
typedef int (*DescrSetFunc)(Object *, Object *, Object *);
typedef int (*InitFunc)(Object *, Tuple *, Dict *);
typedef ObjRef (*NewFunc)(Type *, Tuple *, Dict *);

enum {
	kLT = 0, kLE = 1, kEQ = 2, kNE = 3, kGT = 4, kGE = 5
};

struct NumberMethods {
	BinaryFunc add, subtract, multiply, divide, remainder, divmod;
	TernaryFunc power;
	UnaryFunc negative, positive, absolute;
	InquiryFunc nonzero;
	UnaryFunc invert;
	BinaryFunc lshift, rshift, and_, xor_, or_;
	CoerceFunc coerce;
	UnaryFunc toInt, toLong, toFloat, toOct, toHex;
	BinaryFunc inplaceAdd, inplaceSubtract, inplaceMultiply, inplaceDivide, inplaceRemainder;
	TernaryFunc inplacePower;
	BinaryFunc inplaceLshift, inplaceRshift, inplaceAnd, inplaceXor, inplaceOr;
	BinaryFunc floorDivide, trueDivide, inplaceFloorDivide, inplaceTrueDivide;
};

struct SequenceMethods {
	LenFunc length;
	BinaryFunc concat;
	IntArgFunc repeat;
	IntArgFunc item;
	IntIntArgFunc slice;
	IntObjArgProc assItem;
	IntIntObjArgProc assSlice;
	ObjObjProc contains;
	BinaryFunc inplaceConcat;
	IntArgFunc inplaceRepeat;
};

struct MappingMethods {
	LenFunc length;
	BinaryFunc subscript;
	ObjObjArgProc assSubscript;
};

// Type flags, as in CPython's Py_TPFLAGS_*
enum {
	kTypeHeap = 1 << 0,      // created by a class statement
	kTypeBaseType = 1 << 1,  // can be subclassed
	kTypeCheckTypes = 1 << 2, // number slots take mixed operand types
	kTypeReady = 1 << 3,
	kTypeHasDict = 1 << 4    // instances carry a __dict__
};

struct Object {
	int32 _refs;
	Type *_type;
	uint32 _serial; // stands in for the object's address (id, default order and hash)
	Dict *_extraDict; // the __dict__ of instances of Python subclasses of native types

	explicit Object(Type *type);
	virtual ~Object();

	// The instance dict, for objects that have one
	virtual Dict *getDict() { return _extraDict; }
	virtual void setDict(Dict *d);

};

// A method table entry for native types
struct MethodDef {
	const char *name;
	CallFunc func;	// called with the bound object as the first argument
	int flags;
	const char *doc;
};

enum {
	kMethVarArgs = 0,
	kMethKeywords = 1,
	kMethClass = 2,  // classmethod: receives the type
	kMethStatic = 4  // no self
};

// A member descriptor for native types (read-only unless a setter is given)
typedef ObjRef (*GetterFunc)(Object *);
typedef int (*SetterFunc)(Object *, Object *);

struct GetSetDef {
	const char *name;
	GetterFunc get;
	SetterFunc set;
};

struct Type : public Object {
	Common::String _name;
	int _flags;
	Type *_base;
	Ref<Tuple> _bases;
	Ref<Tuple> _mro;
	Ref<Dict> _dict;
	Ref<Str> _heapName;

	ReprFunc repr, str;
	HashFunc hash;
	CmpFunc compare;
	RichCmpFunc richCompare;
	GetAttrFunc getAttr;
	SetAttrFunc setAttr;
	CallFunc call;
	UnaryFunc iter;
	UnaryFunc iterNext;
	DescrGetFunc descrGet;
	DescrSetFunc descrSet;
	InitFunc init;
	NewFunc newObj;

	NumberMethods *asNumber;
	SequenceMethods *asSequence;
	MappingMethods *asMapping;

	const MethodDef *methods;
	const GetSetDef *getSets;

	Type(const char *name, Type *base = nullptr);
	~Type() override;
	Dict *getDict() override { return _dict.get(); }
};

// Every built-in type
extern Type g_typeType, g_objectType, g_noneType, g_notImplementedType, g_ellipsisType;
extern Type g_intType, g_longType, g_floatType, g_complexType, g_strType, g_unicodeType;
extern Type g_tupleType, g_listType, g_dictType, g_sliceType, g_xrangeType;
extern Type g_functionType, g_codeType, g_methodType, g_builtinFunctionType, g_methodDescrType;
extern Type g_classType, g_instanceType, g_moduleType, g_frameType, g_tracebackType;
extern Type g_propertyType, g_staticMethodType, g_classMethodType, g_superType;
extern Type g_seqIterType, g_callIterType, g_dictIterType, g_fileType, g_getSetDescrType;
extern Type g_wrapperDescrType, g_cellType, g_bufferType;

// Singletons
extern Object *g_none;
extern Object *g_notImplemented;
extern Object *g_ellipsis;

inline bool isType(Object *o, Type *t) { return o->_type == t; }
bool isSubtype(Type *a, Type *b);
inline bool isInstanceOf(Object *o, Type *t) { return o->_type == t || isSubtype(o->_type, t); }

struct NoneObject : public Object {
	NoneObject(Type *t) : Object(t) { _refs = 0x40000000; } // static, never freed
};

struct Int : public Object {
	int32 _value;
	explicit Int(int32 v) : Object(&g_intType), _value(v) {}
	Int(Type *t, int32 v) : Object(t), _value(v) {}
};

// Arbitrary-precision integer: magnitude in 15-bit digits, least significant
// first, as CPython stores it (hash and str depend on the digit size).
struct Long : public Object {
	Common::Array<uint16> _digits;
	bool _negative;
	Long() : Object(&g_longType), _negative(false) {}
	explicit Long(Type *t) : Object(t), _negative(false) {}
};

struct Float : public Object {
	double _value;
	explicit Float(double v) : Object(&g_floatType), _value(v) {}
	Float(Type *t, double v) : Object(t), _value(v) {}
};

struct Complex : public Object {
	double _real, _imag;
	Complex(double r, double i) : Object(&g_complexType), _real(r), _imag(i) {}
};

struct Str : public Object {
	uint32 _len;
	char *_data; // always NUL-terminated
	int32 _hash;
	bool _hashed;
	bool _interned;

	Str(const char *data, uint32 len);
	Str(Type *t, const char *data, uint32 len);
	explicit Str(uint32 len); // uninitialised contents
	~Str() override;
	Common::String toString() const { return Common::String(_data, _len); }
	bool equals(const char *s) const;
};

struct Unicode : public Object {
	Common::Array<uint32> _chars;
	Unicode() : Object(&g_unicodeType) {}
};

struct Tuple : public Object {
	uint32 _size;
	Object **_items;

	explicit Tuple(uint32 size);
	~Tuple() override;
	Object *get(uint32 i) const { return _items[i]; }
	void set(uint32 i, Object *o); // takes a new reference to o
};

struct List : public Object {
	Common::Array<Object *> _items; // each item holds a reference

	List() : Object(&g_listType) {}
	explicit List(Type *t) : Object(t) {}
	~List() override;
	uint32 size() const { return _items.size(); }
	Object *get(uint32 i) const { return _items[i]; }
	void append(Object *o);
	void setItem(uint32 i, Object *o);
	void clear();
};

struct DictEntry {
	int32 hash;
	Object *key;   // null = never used, g_dictDummy = deleted
	Object *value;
};

struct Dict : public Object {
	int32 _fill;  // active + dummy slots
	int32 _used;  // active slots
	int32 _mask;  // table size - 1
	DictEntry *_table;
	DictEntry _smallTable[8];

	Dict();
	explicit Dict(Type *t);
	~Dict() override;

	// These return -1 on error
	int setItem(Object *key, Object *value);
	int delItem(Object *key);
	// Returns a borrowed reference, or null when missing (with no error set,
	// unless comparing the keys raised one)
	Object *getItem(Object *key);
	Object *getItemString(const char *key);
	int setItemString(const char *key, Object *value);
	void clear();
	// Iterates in table order: pos starts at 0
	bool next(int32 *pos, Object **key, Object **value) const;
	int resize(int32 minUsed);
	DictEntry *lookup(Object *key, int32 hash);
	void insert(Object *key, int32 hash, Object *value);
};

extern Object *g_dictDummy;

struct Slice : public Object {
	ObjRef _start, _stop, _step;
	Slice(Object *start, Object *stop, Object *step);
};

struct XRange : public Object {
	int32 _start, _step, _len, _reps;
	XRange() : Object(&g_xrangeType), _start(0), _step(1), _len(0), _reps(1) {}
};

struct Code : public Object {
	int32 _argCount, _nLocals, _stackSize, _flags;
	Ref<Str> _code;
	Ref<Tuple> _consts, _names, _varNames, _freeVars, _cellVars;
	Ref<Str> _filename, _name;
	int32 _firstLineNo;
	Ref<Str> _lnotab;
	Code() : Object(&g_codeType), _argCount(0), _nLocals(0), _stackSize(0), _flags(0), _firstLineNo(0) {}
};

enum {
	kCoOptimized = 0x1,
	kCoNewLocals = 0x2,
	kCoVarArgs = 0x4,
	kCoVarKeywords = 0x8,
	kCoNested = 0x10,
	kCoGenerator = 0x20,
	kCoNoFree = 0x40
};

struct Function : public Object {
	Ref<Code> _code;
	Ref<Dict> _globals;
	Ref<Tuple> _defaults;
	Ref<Tuple> _closure;
	ObjRef _doc;
	Ref<Str> _name;
	Ref<Dict> _dict;
	Function(Code *code, Dict *globals);
	Dict *getDict() override { return _dict.get(); }
	void setDict(Dict *d) override { _dict = d; }
};

// A bound or unbound method (self is null when unbound)
struct Method : public Object {
	ObjRef _func, _self, _class;
	Method(Object *func, Object *self, Object *cls);
};

// A native function; self is the bound object (null for module functions)
struct BuiltinFunction : public Object {
	const MethodDef *_def;
	ObjRef _self;
	Ref<Str> _module;
	BuiltinFunction(const MethodDef *def, Object *self);
};

// A native method of a type, before binding
struct MethodDescr : public Object {
	const MethodDef *_def;
	Type *_owner;
	MethodDescr(const MethodDef *def, Type *owner);
};

struct GetSetDescr : public Object {
	const GetSetDef *_def;
	Type *_owner;
	GetSetDescr(const GetSetDef *def, Type *owner);
};

// A slot wrapper such as int.__add__ or object.__init__
typedef ObjRef (*WrapperFunc)(Object *self, Tuple *args, void *wrapped);
struct WrapperDescr : public Object {
	const char *_name;
	WrapperFunc _wrapper;
	void *_wrapped;
	Type *_owner;
	WrapperDescr(const char *name, WrapperFunc wrapper, void *wrapped, Type *owner);
};

struct ClassObj : public Object {
	Ref<Tuple> _bases;
	Ref<Dict> _dict;
	Ref<Str> _name;
	// Cached special methods (null when absent)
	ObjRef _getattr, _setattr, _delattr;
	ClassObj() : Object(&g_classType) {}
	Dict *getDict() override { return _dict.get(); }
	void setDict(Dict *d) override { _dict = d; }
};

struct Instance : public Object {
	Ref<ClassObj> _class;
	Ref<Dict> _dict;
	Instance(ClassObj *cls, Dict *dict);
	~Instance() override;
	Dict *getDict() override { return _dict.get(); }
	void setDict(Dict *d) override { _dict = d; }
};

// An instance of a class statement's new-style type
struct HeapObject : public Object {
	Ref<Dict> _dict;
	explicit HeapObject(Type *t);
	~HeapObject() override;
	Dict *getDict() override { return _dict.get(); }
	void setDict(Dict *d) override { _dict = d; }
};

struct Module : public Object {
	Ref<Dict> _dict;
	explicit Module(const char *name);
	Dict *getDict() override { return _dict.get(); }
	void setDict(Dict *d) override { _dict = d; }
};

struct Property : public Object {
	ObjRef _get, _set, _del, _doc;
	Property() : Object(&g_propertyType) {}
};

struct StaticMethod : public Object {
	ObjRef _callable;
	explicit StaticMethod(Object *c) : Object(&g_staticMethodType), _callable(c) {}
};

struct ClassMethod : public Object {
	ObjRef _callable;
	explicit ClassMethod(Object *c) : Object(&g_classMethodType), _callable(c) {}
};

struct Super : public Object {
	Type *_type;
	ObjRef _obj;
	Super() : Object(&g_superType), _type(nullptr) {}
};

struct Cell : public Object {
	ObjRef _ref;
	explicit Cell(Object *o) : Object(&g_cellType), _ref(o) {}
};

struct SeqIter : public Object {
	ObjRef _seq;
	int32 _index;
	explicit SeqIter(Object *seq) : Object(&g_seqIterType), _seq(seq), _index(0) {}
};

struct CallIter : public Object {
	ObjRef _callable, _sentinel;
	CallIter(Object *c, Object *s) : Object(&g_callIterType), _callable(c), _sentinel(s) {}
};

struct DictIter : public Object {
	Ref<Dict> _dict;
	int32 _pos;
	int32 _used;
	int _kind; // 0 keys, 1 values, 2 items
	DictIter(Dict *d, int kind);
};

struct Traceback : public Object {
	Ref<Traceback> _next;
	Ref<Frame> _frame;
	int32 _lasti, _lineno;
	Traceback() : Object(&g_tracebackType), _lasti(0), _lineno(0) {}
};

// ---- Error state --------------------------------------------------------

void setError(Object *type, Object *value);
void setErrorString(Object *type, const char *msg);
void setErrorFormat(Object *type, const char *fmt, ...) GCC_PRINTF(2, 3);
bool errorOccurred();
Object *errorType(); // borrowed
bool errorMatches(Object *exc);
bool givenExceptionMatches(Object *given, Object *exc);
void fetchError(ObjRef *type, ObjRef *value, ObjRef *tb);
void restoreError(Object *type, Object *value, Object *tb);
void clearError();
void normalizeException(ObjRef *type, ObjRef *value, ObjRef *tb);
void setNoMemory();
void badArgument();
void badInternalCall();
// Formats the pending error and its traceback (for logs); clears it
Common::String formatError();

// The exception classes (classic classes, made by the exceptions module)
extern Object *g_excException, *g_excStandardError, *g_excArithmeticError;
extern Object *g_excLookupError, *g_excAssertionError, *g_excAttributeError;
extern Object *g_excEOFError, *g_excFloatingPointError, *g_excEnvironmentError;
extern Object *g_excIOError, *g_excOSError, *g_excWindowsError, *g_excImportError;
extern Object *g_excIndexError, *g_excKeyError, *g_excKeyboardInterrupt;
extern Object *g_excMemoryError, *g_excNameError, *g_excOverflowError;
extern Object *g_excRuntimeError, *g_excNotImplementedError, *g_excSyntaxError;
extern Object *g_excIndentationError, *g_excTabError, *g_excReferenceError;
extern Object *g_excSystemError, *g_excSystemExit, *g_excTypeError;
extern Object *g_excUnboundLocalError, *g_excUnicodeError, *g_excValueError;
extern Object *g_excZeroDivisionError, *g_excStopIteration;
extern Object *g_excWarning, *g_excUserWarning, *g_excDeprecationWarning;
extern Object *g_excSyntaxWarning, *g_excOverflowWarning, *g_excRuntimeWarning;

// ---- Constructors ---------------------------------------------------------

Ref<Int> newInt(int32 v);
ObjRef newIntOrLong(int64 v);
Ref<Float> newFloat(double v);
Ref<Str> newStr(const char *s);
Ref<Str> newStr(const char *s, uint32 len);
Ref<Str> newStr(const Common::String &s);
Ref<Str> internStr(const char *s);
Ref<Str> intern(Str *s);
Ref<Tuple> newTuple(uint32 size);
Ref<Tuple> tuplePack(uint32 n, ...);
Ref<List> newList(uint32 size = 0);
Ref<Dict> newDict();
ObjRef newBool(bool b); // an int, as in Python 2.2

// ---- Generic object protocol ---------------------------------------------

ObjRef getAttr(Object *o, Str *name);
ObjRef getAttrString(Object *o, const char *name);
int setAttr(Object *o, Str *name, Object *value);
int setAttrString(Object *o, const char *name, Object *value);
int delAttr(Object *o, Str *name);
bool hasAttrString(Object *o, const char *name);
ObjRef genericGetAttr(Object *o, Str *name);
int genericSetAttr(Object *o, Str *name, Object *value);
ObjRef lookupType(Type *t, Str *name); // MRO lookup, no error set when missing

ObjRef repr(Object *o);
ObjRef str(Object *o);
bool hash(Object *o, int32 *out);
int compare(Object *a, Object *b); // cmp(); check errorOccurred()
ObjRef richCompare(Object *a, Object *b, int op);
int richCompareBool(Object *a, Object *b, int op); // -1 on error
int isTrue(Object *o); // -1 on error
int notTrue(Object *o);
bool callable(Object *o);

ObjRef call(Object *callable, Tuple *args, Dict *kwargs = nullptr);
ObjRef callObject(Object *callable, Tuple *args);
ObjRef callFunction(Object *callable, uint32 n, ...);
ObjRef callMethod(Object *o, const char *name, uint32 n, ...);

ObjRef getIter(Object *o);
ObjRef iterNext(Object *it); // null with no error = exhausted

ObjRef getItem(Object *o, Object *key);
int setItem(Object *o, Object *key, Object *value);
int delItem(Object *o, Object *key);
int32 length(Object *o); // -1 on error
int containsItem(Object *seq, Object *item); // -1 on error
ObjRef sequenceGetItem(Object *o, int32 i);
ObjRef sequenceGetSlice(Object *o, int32 lo, int32 hi);
int sequenceSetSlice(Object *o, int32 lo, int32 hi, Object *v);
ObjRef sequenceTuple(Object *o);
ObjRef sequenceList(Object *o);
ObjRef sequenceConcat(Object *a, Object *b);
ObjRef sequenceRepeat(Object *a, int32 n);
int32 sequenceIndex(Object *seq, Object *item);
int32 sequenceCount(Object *seq, Object *item);

ObjRef numberAdd(Object *a, Object *b);
ObjRef numberSubtract(Object *a, Object *b);
ObjRef numberMultiply(Object *a, Object *b);
ObjRef numberDivide(Object *a, Object *b);
ObjRef numberFloorDivide(Object *a, Object *b);
ObjRef numberTrueDivide(Object *a, Object *b);
ObjRef numberRemainder(Object *a, Object *b);
ObjRef numberDivmod(Object *a, Object *b);
ObjRef numberPower(Object *a, Object *b, Object *c);
ObjRef numberLshift(Object *a, Object *b);
ObjRef numberRshift(Object *a, Object *b);
ObjRef numberAnd(Object *a, Object *b);
ObjRef numberXor(Object *a, Object *b);
ObjRef numberOr(Object *a, Object *b);
ObjRef numberInplaceAdd(Object *a, Object *b);
ObjRef numberInplaceSubtract(Object *a, Object *b);
ObjRef numberInplaceMultiply(Object *a, Object *b);
ObjRef numberInplaceDivide(Object *a, Object *b);
ObjRef numberInplaceFloorDivide(Object *a, Object *b);
ObjRef numberInplaceTrueDivide(Object *a, Object *b);
ObjRef numberInplaceRemainder(Object *a, Object *b);
ObjRef numberInplacePower(Object *a, Object *b, Object *c);
ObjRef numberInplaceLshift(Object *a, Object *b);
ObjRef numberInplaceRshift(Object *a, Object *b);
ObjRef numberInplaceAnd(Object *a, Object *b);
ObjRef numberInplaceXor(Object *a, Object *b);
ObjRef numberInplaceOr(Object *a, Object *b);
ObjRef numberNegative(Object *a);
ObjRef numberPositive(Object *a);
ObjRef numberInvert(Object *a);
ObjRef numberAbsolute(Object *a);
ObjRef numberInt(Object *a);
ObjRef numberLong(Object *a);
ObjRef numberFloat(Object *a);
int numberCoerce(ObjRef *a, ObjRef *b); // 0 ok, 1 not possible, -1 error
bool isNumber(Object *o);

// Conversions; these set TypeError on failure
bool asInt(Object *o, int32 *out);
bool asDouble(Object *o, double *out);
Str *asStr(Object *o); // null + TypeError if not a str

// Type creation for native types
void typeReady(Type *t);
ObjRef typeCall(Object *type, Tuple *args, Dict *kw);
const char *typeName(Object *o);

// Argument parsing helpers for native functions
bool checkArgs(const char *fname, Tuple *args, uint32 min, uint32 max);
bool checkOneArg(const char *fname, Tuple *args);

bool noKeywords(const char *fname, Dict *kw);

} // End of namespace Python
} // End of namespace Yaga

#endif // YAGA_PYTHON_OBJECT_H
