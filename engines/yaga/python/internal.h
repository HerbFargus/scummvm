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

#ifndef YAGA_PYTHON_INTERNAL_H
#define YAGA_PYTHON_INTERNAL_H

#include "yaga/python/object.h"

// Helpers shared between the VM's source files

namespace Yaga {
namespace Python {

// The address CPython would have printed and hashed: a stable stand-in made
// from the object's creation order
inline uint32 objectAddress(Object *o) { return 0x00a00000u + o->_serial * 16u; }
int32 hashPointer(Object *o);

bool enterRecursiveCall();
void leaveRecursiveCall();

// Numbers
ObjRef intFromString(const char *s, int base, bool allowWhitespace);
ObjRef longFromString(const char *s, int base, bool allowWhitespace);
ObjRef floatFromString(Str *s);
ObjRef longFromInt64(int64 v);
ObjRef longFromDouble(double d);
bool longAsInt64(Object *o, int64 *out); // OverflowError if it does not fit
bool longAsDouble(Object *o, double *out);
ObjRef longFromInt(Object *o);
ObjRef longFormat(Object *o, int base, bool addL);
int longSign(Object *o);
ObjRef longNormalize(Long *l); // returns the same object
int32 hashDouble(double v);
Common::String formatDouble(double v, int precision, char type, bool alt = false); // MSVC printf style
Common::String floatRepr(double v);
Common::String floatStr(double v);
ObjRef intOrLongBinary(Object *v, Object *w, const char *op); // used by the long slots
int longCompare(Long *a, Long *b);

// Strings
ObjRef strFormat(Object *fmt, Object *args);
ObjRef strConcat(Str *a, Str *b);
ObjRef strJoin(Str *sep, Object *seq);
ObjRef strRepr(Str *s, bool smartQuotes = true);
Ref<Str> strFromCharacter(uint8 c);
uint32 strHash(const char *data, uint32 len);

// Unicode
ObjRef unicodeToStr(Object *u);
ObjRef unicodeFromStr(Object *s, const char *encoding, const char *errors);
int unicodeCompare(Object *a, Object *b);
ObjRef unicodeFormat(Object *fmt, Object *args);

// Classes
int slotCompare(Object *a, Object *b);
ObjRef classLookup(ClassObj *cls, Str *name, ClassObj **found);
ObjRef instanceGetAttr(Object *o, Str *name);
ObjRef newClassObj(Tuple *bases, Dict *dict, Str *name);
ObjRef newInstance(Object *cls, Object *arg, Object *kw);
bool classIsSubclass(Object *cls, Object *base);
bool isInstance(Object *inst, Object *cls); // isinstance(); -1 errors are flagged via errorOccurred
bool isSubclass(Object *derived, Object *cls);
ObjRef newMethod(Object *func, Object *self, Object *cls);

// Types
ObjRef typeNew(Type *metatype, Tuple *args, Dict *kw);
void typeAddMethods(Type *t);
void typeInitAll();
ObjRef typeRepr(Object *o);
Ref<Tuple> computeMro(Type *t);
ObjRef callWithSelf(Object *func, Object *self, Tuple *args, Dict *kw);

// Functions and frames
ObjRef evalCode(Code *code, Dict *globals, Dict *locals);
ObjRef evalCodeEx(Code *co, Dict *globals, Object *locals,
                  Object **args, uint32 argCount, Object **kws, uint32 kwCount,
                  Object **defs, uint32 defCount, Tuple *closure);
ObjRef functionCall(Object *func, Tuple *args, Dict *kw);
Frame *currentFrame();
ObjRef newGenerator(Frame *f);
void initGeneratorType();

void addTraceback(Frame *f);
Dict *getBuiltins();
Dict *getGlobals();
ObjRef getLocals();
int32 codeAddrToLine(Code *co, int32 addr);

// Modules
Module *addModule(const char *name); // borrowed, from sys.modules
ObjRef importModule(const char *name);
ObjRef importModuleEx(const char *name, Dict *globals, Dict *locals, Object *fromlist);
ObjRef execCodeModule(const char *name, Code *co);
Dict *getModuleDict(); // sys.modules
Ref<Module> newModule(const char *name);
void moduleAddObject(Module *m, const char *name, Object *value);
void moduleAddInt(Module *m, const char *name, int32 value);
void moduleAddString(Module *m, const char *name, const char *value);
Ref<Module> initModule(const char *name, const MethodDef *methods, const char *doc = nullptr);

// sys
ObjRef sysGetObject(const char *name); // borrowed-like: may be null
int sysSetObject(const char *name, Object *value);
void writeToStdout(const char *s);
void writeToStderr(const char *s);
int fileWriteString(const char *s, Object *f);
int fileWriteObject(Object *v, Object *f, bool raw);
int fileSoftSpace(Object *f, int newflag);

// Marshal
ObjRef marshalLoads(const char *data, uint32 len, uint32 *consumed = nullptr);
ObjRef marshalDumps(Object *o);

// Compiler
ObjRef compileString(const char *source, const char *filename, int mode); // mode: 0 exec, 1 eval, 2 single
enum { kCompileExec = 0, kCompileEval = 1, kCompileSingle = 2 };

// Exceptions
void initExceptions();
ObjRef newException(Object *type, const char *msg);

// Slot wrappers and helpers for new-style types
void typeInheritSlots(Type *t);
void typeFixupSlots(Type *t); // after __dict__ changes on heap types
void typeUpdateSlot(Type *t, Str *name);

// Lists
int listSort(List *l, Object *cmpFunc);
void listReverse(List *l);
ObjRef listAsTuple(List *l);
int listInsert(List *l, int32 where, Object *v);

// Dicts
ObjRef dictKeys(Dict *d);
ObjRef dictValues(Dict *d);
ObjRef dictItems(Dict *d);
int dictMerge(Dict *a, Object *b, bool override);
ObjRef dictCopy(Dict *d);

// Files
ObjRef newFileObject(const char *name, const char *mode);

} // End of namespace Python
} // End of namespace Yaga

#endif // YAGA_PYTHON_INTERNAL_H
