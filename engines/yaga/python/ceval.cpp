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
#include "common/util.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/eval.h"
#include "yaga/python/opcode.h"

// The bytecode interpreter, following CPython 2.2's ceval.c

namespace Yaga {
namespace Python {

ThreadState *g_ts = nullptr;

ThreadState::ThreadState() : frame(nullptr), recursionDepth(0), recursionLimit(1000) {
}

bool enterRecursiveCall() {
	if (++g_ts->recursionDepth > g_ts->recursionLimit) {
		--g_ts->recursionDepth;
		setErrorString(g_excRuntimeError, "maximum recursion depth exceeded");
		return false;
	}
	return true;
}

void leaveRecursiveCall() {
	--g_ts->recursionDepth;
}

Frame *currentFrame() {
	return g_ts ? g_ts->frame : nullptr;
}

Dict *getGlobals() {
	Frame *f = currentFrame();
	return f ? f->_globals.get() : nullptr;
}

Dict *getBuiltins() {
	Frame *f = currentFrame();
	if (f)
		return f->_builtins.get();
	return g_ts->builtins.get();
}

ObjRef getLocals() {
	Frame *f = currentFrame();
	if (!f)
		return ObjRef();
	fastToLocals(f);
	return f->_locals;
}

// ---- Frames --------------------------------------------------------------------

Frame::Frame(Code *code, Dict *globals, Object *locals, Frame *back) : Object(&g_frameType),
	_back(back), _code(code), _globals(globals), _stack(nullptr), _stackTop(0), _blockCount(0),
	_lasti(-1), _lineno(code->_firstLineNo), _savedExc(false), _resumeAt(-1), _finished(false) {
	Ref<Str> builtinsName = internStr("__builtins__");
	if (!back || back->_globals.get() != globals) {
		Object *b = globals->getItem(builtinsName.get());
		if (b) {
			if (isType(b, &g_moduleType))
				b = ((Module *)b)->_dict.get();
			if (isInstanceOf(b, &g_dictType))
				_builtins = (Dict *)b;
		}
		if (!_builtins) {
			// No builtins: make up a minimal one
			_builtins = newDict();
			_builtins->setItemString("None", g_none);
		}
	} else {
		_builtins = back->_builtins;
	}
	if ((code->_flags & (kCoNewLocals | kCoOptimized)) == (kCoNewLocals | kCoOptimized)) {
		// Made on demand by fastToLocals
	} else if (code->_flags & kCoNewLocals) {
		_locals = newDict();
	} else {
		_locals = locals ? locals : (Object *)globals;
	}
	uint32 nfree = (code->_cellVars ? code->_cellVars->_size : 0) + (code->_freeVars ? code->_freeVars->_size : 0);
	_fast.resize(code->_nLocals + nfree);
	for (uint32 i = 0; i < _fast.size(); i++)
		_fast[i] = nullptr;
	_stack = (Object **)malloc(sizeof(Object *) * (code->_stackSize + 1));
}

Frame::~Frame() {
	clearLocals();
	for (int i = 0; i < _stackTop; i++)
		if (_stack[i])
			decRef(_stack[i]);
	free(_stack);
}

void Frame::clearLocals() {
	for (uint32 i = 0; i < _fast.size(); i++) {
		Object *o = _fast[i];
		_fast[i] = nullptr;
		if (o)
			decRef(o);
	}
}

void fastToLocals(Frame *f) {
	if (!f->_locals)
		f->_locals = newDict();
	if (!isInstanceOf(f->_locals.get(), &g_dictType))
		return;
	Dict *locals = (Dict *)f->_locals.get();
	Code *co = f->_code.get();
	if (!(co->_flags & kCoOptimized))
		return;
	ObjRef errType, errValue, errTb;
	fetchError(&errType, &errValue, &errTb);
	Tuple *names = co->_varNames.get();
	for (int32 j = 0; j < co->_nLocals && j < (int32)names->_size; j++) {
		Object *key = names->get(j);
		Object *value = f->_fast[j];
		if (!value) {
			if (locals->delItem(key) != 0)
				clearError();
		} else {
			locals->setItem(key, value);
		}
	}
	restoreError(errType.get(), errValue.get(), errTb.get());
}

void localsToFast(Frame *f, bool clear) {
	if (!f->_locals || !isInstanceOf(f->_locals.get(), &g_dictType))
		return;
	Dict *locals = (Dict *)f->_locals.get();
	Code *co = f->_code.get();
	if (!(co->_flags & kCoOptimized))
		return;
	ObjRef errType, errValue, errTb;
	fetchError(&errType, &errValue, &errTb);
	Tuple *names = co->_varNames.get();
	for (int32 j = 0; j < co->_nLocals && j < (int32)names->_size; j++) {
		Object *value = locals->getItem(names->get(j));
		if (!value) {
			clearError();
			if (!clear)
				continue;
		}
		if (value)
			incRef(value);
		Object *old = f->_fast[j];
		f->_fast[j] = value;
		if (old)
			decRef(old);
	}
	restoreError(errType.get(), errValue.get(), errTb.get());
}

void addTraceback(Frame *f) {
	ObjRef type, value, tb;
	fetchError(&type, &value, &tb);
	Traceback *t = new Traceback();
	ObjRef rt(t);
	if (tb && isType(tb.get(), &g_tracebackType))
		t->_next = (Traceback *)tb.get();
	t->_frame = f;
	t->_lasti = f->_lasti;
	t->_lineno = f->_lineno;
	restoreError(type.get(), value.get(), t);
}

// ---- Exception state for sys.exc_info ------------------------------------------

static void setExcInfo(Object *type, Object *value, Object *tb) {
	Frame *frame = g_ts->frame;
	if (!frame->_savedExc) {
		// Save the thread's previous exception in this frame
		if (!g_ts->excType)
			g_ts->excType = g_none;
		frame->_excType = g_ts->excType;
		frame->_excValue = g_ts->excValue;
		frame->_excTraceback = g_ts->excTraceback;
		frame->_savedExc = true;
	}
	g_ts->excType = type;
	g_ts->excValue = value;
	g_ts->excTraceback = tb;
	sysSetObject("exc_type", type);
	sysSetObject("exc_value", value);
	sysSetObject("exc_traceback", tb);
}

static void resetExcInfo() {
	Frame *frame = g_ts->frame;
	if (frame->_savedExc) {
		g_ts->excType = frame->_excType;
		g_ts->excValue = frame->_excValue;
		g_ts->excTraceback = frame->_excTraceback;
		sysSetObject("exc_type", frame->_excType ? frame->_excType.get() : g_none);
		sysSetObject("exc_value", frame->_excValue ? frame->_excValue.get() : g_none);
		sysSetObject("exc_traceback", frame->_excTraceback ? frame->_excTraceback.get() : g_none);
		frame->_excType.reset();
		frame->_excValue.reset();
		frame->_excTraceback.reset();
		frame->_savedExc = false;
	}
}

// ---- Helpers for the opcodes --------------------------------------------------------

enum Why {
	kWhyNot = 1, kWhyException = 2, kWhyReraise = 4, kWhyReturn = 8,
	kWhyBreak = 16, kWhyContinue = 32, kWhyYield = 64
};

static Why doRaise(Object *typeIn, Object *valueIn, Object *tbIn) {
	ObjRef type(typeIn), value(valueIn), tb(tbIn);
	if (!typeIn) {
		// Reraise
		type = g_ts->excType ? g_ts->excType : ObjRef(g_none);
		value = g_ts->excValue;
		tb = g_ts->excTraceback;
	}
	if (tb && tb.get() == g_none)
		tb.reset();
	else if (tb && !isType(tb.get(), &g_tracebackType)) {
		setErrorString(g_excTypeError, "raise: arg 3 must be a traceback or None");
		return kWhyException;
	}
	if (!value)
		value = g_none;
	while (isInstanceOf(type.get(), &g_tupleType) && ((Tuple *)type.get())->_size > 0)
		type = ((Tuple *)type.get())->get(0);
	if (isInstanceOf(type.get(), &g_strType)) {
		// string exception
	} else if (isType(type.get(), &g_classType)) {
		normalizeException(&type, &value, &tb);
		if (!type)
			return kWhyException;
	} else if (isType(type.get(), &g_instanceType)) {
		if (value.get() != g_none) {
			setErrorString(g_excTypeError, "instance exception may not have a separate value");
			return kWhyException;
		}
		value = type;
		type = ((Instance *)type.get())->_class.get();
	} else {
		setErrorFormat(g_excTypeError, "exceptions must be strings, classes, or instances, not %s", typeName(type.get()));
		return kWhyException;
	}
	restoreError(type.get(), value.get(), tb.get());
	return tb ? kWhyReraise : kWhyException;
}

static bool sliceIndex(Object *v, int32 *pi) {
	if (!v)
		return true;
	if (isInstanceOf(v, &g_intType)) {
		*pi = ((Int *)v)->_value;
		return true;
	}
	if (isInstanceOf(v, &g_longType)) {
		int64 x;
		if (!longAsInt64(v, &x)) {
			clearError();
			x = longSign(v) > 0 ? 0x7fffffff : -0x7fffffff;
		}
		if (x > 0x7fffffff)
			x = 0x7fffffff;
		else if (x < -0x7fffffff)
			x = 0;
		*pi = (int32)x;
		return true;
	}
	setErrorString(g_excTypeError, "slice indices must be integers");
	return false;
}

#define ISINT(x) (!(x) || isInstanceOf((x), &g_intType) || isInstanceOf((x), &g_longType))

static ObjRef applySlice(Object *u, Object *v, Object *w) {
	SequenceMethods *sq = u->_type->asSequence;
	if (sq && sq->slice && ISINT(v) && ISINT(w)) {
		int32 ilow = 0, ihigh = 0x7fffffff;
		if (!sliceIndex(v, &ilow) || !sliceIndex(w, &ihigh))
			return ObjRef();
		return sequenceGetSlice(u, ilow, ihigh);
	}
	ObjRef slice(new Slice(v ? v : g_none, w ? w : g_none, g_none));
	return getItem(u, slice.get());
}

static int assignSlice(Object *u, Object *v, Object *w, Object *x) {
	SequenceMethods *sq = u->_type->asSequence;
	if (sq && sq->assSlice && ISINT(v) && ISINT(w)) {
		int32 ilow = 0, ihigh = 0x7fffffff;
		if (!sliceIndex(v, &ilow) || !sliceIndex(w, &ihigh))
			return -1;
		return sequenceSetSlice(u, ilow, ihigh, x);
	}
	ObjRef slice(new Slice(v ? v : g_none, w ? w : g_none, g_none));
	if (x)
		return setItem(u, slice.get(), x);
	return delItem(u, slice.get());
}

static ObjRef cmpOutcome(int op, Object *v, Object *w) {
	bool res;
	switch (op) {
	case kCmpIs:
		res = v == w;
		break;
	case kCmpIsNot:
		res = v != w;
		break;
	case kCmpIn:
	case kCmpNotIn: {
		int r = containsItem(w, v);
		if (r < 0)
			return ObjRef();
		res = op == kCmpIn ? r != 0 : r == 0;
		break;
	}
	case kCmpExcMatch:
		res = givenExceptionMatches(v, w);
		break;
	default:
		return richCompare(v, w, op);
	}
	return newBool(res);
}

static ObjRef importFrom(Object *v, Str *name) {
	ObjRef x = getAttr(v, name);
	if (!x && errorMatches(g_excAttributeError)) {
		clearError();
		setErrorFormat(g_excImportError, "cannot import name %.230s", name->_data);
	}
	return x;
}

static int importAllFrom(Object *locals, Object *v) {
	ObjRef all = getAttrString(v, "__all__");
	bool skipLeadingUnderscores = false;
	if (!all) {
		if (!errorMatches(g_excAttributeError))
			return -1;
		clearError();
		ObjRef dict = getAttrString(v, "__dict__");
		if (!dict) {
			if (!errorMatches(g_excAttributeError))
				return -1;
			clearError();
			setErrorString(g_excImportError, "from-import-* object has no __dict__ and no __all__");
			return -1;
		}
		all = callMethod(dict.get(), "keys", 0);
		if (!all)
			return -1;
		skipLeadingUnderscores = true;
	}
	for (int32 pos = 0;; pos++) {
		ObjRef name = sequenceGetItem(all.get(), pos);
		if (!name) {
			if (!errorMatches(g_excIndexError))
				return -1;
			clearError();
			return 0;
		}
		if (skipLeadingUnderscores && isInstanceOf(name.get(), &g_strType) && ((Str *)name.get())->_data[0] == '_')
			continue;
		ObjRef value = getAttr(v, (Str *)name.get());
		if (!value)
			return -1;
		if (setItem(locals, name.get(), value.get()) < 0)
			return -1;
	}
}

static ObjRef buildClass(Object *methods, Object *bases, Object *name) {
	ObjRef metaclass;
	if (isInstanceOf(methods, &g_dictType)) {
		Object *m = ((Dict *)methods)->getItemString("__metaclass__");
		if (m)
			metaclass = m;
	}
	if (!metaclass) {
		if (isInstanceOf(bases, &g_tupleType) && ((Tuple *)bases)->_size > 0) {
			Object *base = ((Tuple *)bases)->get(0);
			metaclass = getAttrString(base, "__class__");
			if (!metaclass) {
				clearError();
				metaclass = base->_type;
			}
		} else {
			Dict *g = getGlobals();
			Object *m = g ? g->getItemString("__metaclass__") : nullptr;
			metaclass = m ? m : (Object *)&g_classType;
		}
	}
	return callFunction(metaclass.get(), 3, name, bases, methods);
}

static ObjRef execStatement(Frame *f, Object *prog, Object *globals, Object *locals) {
	bool plain = false;
	ObjRef rprog(prog), rglobals(globals), rlocals(locals);
	if (isInstanceOf(prog, &g_tupleType) && globals == g_none && locals == g_none) {
		Tuple *t = (Tuple *)prog;
		if (t->_size == 2 || t->_size == 3) {
			rglobals = t->get(1);
			if (t->_size == 3)
				rlocals = t->get(2);
			rprog = t->get(0);
		}
	}
	if (rglobals.get() == g_none) {
		rglobals = f->_globals.get();
		if (rlocals.get() == g_none) {
			fastToLocals(f);
			rlocals = f->_locals;
			plain = true;
		}
	} else if (rlocals.get() == g_none) {
		rlocals = rglobals;
	}
	prog = rprog.get();
	if (!isInstanceOf(prog, &g_strType) && !isInstanceOf(prog, &g_unicodeType) && !isType(prog, &g_codeType)) {
		setErrorString(g_excTypeError, "exec: arg 1 must be a string, file, or code object");
		return ObjRef();
	}
	if (!isInstanceOf(rglobals.get(), &g_dictType)) {
		setErrorString(g_excTypeError, "exec: arg 2 must be a dictionary or None");
		return ObjRef();
	}
	if (!isInstanceOf(rlocals.get(), &g_dictType)) {
		setErrorString(g_excTypeError, "exec: arg 3 must be a dictionary or None");
		return ObjRef();
	}
	Dict *g = (Dict *)rglobals.get();
	if (!g->getItemString("__builtins__"))
		g->setItemString("__builtins__", f->_builtins.get());
	ObjRef code;
	if (isType(prog, &g_codeType)) {
		code = prog;
	} else {
		ObjRef s(prog);
		if (isInstanceOf(prog, &g_unicodeType)) {
			s = unicodeToStr(prog);
			if (!s)
				return ObjRef();
		}
		Str *str = (Str *)s.get();
		if (strlen(str->_data) != str->_len) {
			setErrorString(g_excTypeError, "embedded '\\0' in exec string");
			return ObjRef();
		}
		code = compileString(str->_data, "<string>", kCompileExec);
		if (!code)
			return ObjRef();
	}
	ObjRef v = evalCode((Code *)code.get(), g, (Dict *)rlocals.get());
	if (plain)
		localsToFast(f, false);
	return v;
}

// Unpacks an iterable into n stack slots, as CPython's unpack_iterable
static bool unpackIterable(Object *v, int argcnt, Object **sp) {
	ObjRef it = getIter(v);
	if (!it)
		return false;
	int i;
	for (i = 0; i < argcnt; i++) {
		ObjRef w = iterNext(it.get());
		if (!w) {
			if (!errorOccurred())
				setErrorFormat(g_excValueError, "need more than %d value%s to unpack", i, i == 1 ? "" : "s");
			for (; i > 0; i--) {
				decRef(*sp);
				*sp++ = nullptr;
			}
			return false;
		}
		*--sp = w.release();
	}
	ObjRef w = iterNext(it.get());
	if (!w) {
		if (errorOccurred())
			goto error;
		return true;
	}
	setErrorString(g_excValueError, "too many values to unpack");
error:
	for (; i > 0; i--) {
		decRef(*sp);
		*sp++ = nullptr;
	}
	return false;
}

// How CPython's PyEval_GetFuncName and PyEval_GetFuncDesc name a callable
static Common::String funcNameDesc(Object *func) {
	if (isType(func, &g_methodType))
		return funcNameDesc(((Method *)func)->_func.get());
	if (isType(func, &g_functionType))
		return ((Function *)func)->_name->toString() + "()";
	if (isType(func, &g_builtinFunctionType))
		return Common::String(((BuiltinFunction *)func)->_def->name) + "()";
	if (isType(func, &g_classType))
		return ((ClassObj *)func)->_name->toString() + " constructor";
	if (isType(func, &g_instanceType))
		return ((Instance *)func)->_class->_name->toString() + " instance";
	return Common::String(typeName(func)) + " object";
}

// Python functions (and methods bound to them) take their arguments from the
// stack; other calls pop them first, as CPython's do_call, which matters to
// reference counts (cPickle's memo depends on them)
static bool callKeepsStack(Object *func) {
	if (isType(func, &g_functionType))
		return true;
	return isType(func, &g_methodType) && ((Method *)func)->_self && isType(((Method *)func)->_func.get(), &g_functionType);
}

// Builds a keyword dict for a call, as CPython's update_keyword_args
static ObjRef callWithStack(Object *func, Object **stack, int na, int nk, Object *stararg, Object *kwdict, bool popArgs = false) {
	Ref<Dict> kwargs;
	if (kwdict) {
		if (!isInstanceOf(kwdict, &g_dictType)) {
			setErrorFormat(g_excTypeError, "%s argument after ** must be a dictionary", funcNameDesc(func).c_str());
			return ObjRef();
		}
		kwargs = dictCopy((Dict *)kwdict).as<Dict>();
	}
	if (nk > 0) {
		if (!kwargs)
			kwargs = newDict();
		Object **kp = stack + na;
		for (int i = 0; i < nk; i++) {
			Object *key = kp[2 * i];
			Object *value = kp[2 * i + 1];
			if (kwargs->getItem(key)) {
				setErrorFormat(g_excTypeError, "%.200s got multiple values for keyword argument '%.200s'",
				               typeName(func), isInstanceOf(key, &g_strType) ? ((Str *)key)->_data : "?");
				return ObjRef();
			}
			if (kwargs->setItem(key, value) < 0)
				return ObjRef();
		}
	}
	Ref<Tuple> args;
	if (stararg) {
		ObjRef t = sequenceTuple(stararg);
		if (!t) {
			if (errorMatches(g_excTypeError)) {
				clearError();
				setErrorFormat(g_excTypeError, "%s argument after * must be a sequence", funcNameDesc(func).c_str());
			}
			return ObjRef();
		}
		Tuple *st = (Tuple *)t.get();
		args = newTuple(na + st->_size);
		for (int i = 0; i < na; i++)
			args->set(i, stack[i]);
		for (uint32 i = 0; i < st->_size; i++)
			args->set(na + i, st->get(i));
	} else {
		args = newTuple(na);
		for (int i = 0; i < na; i++)
			args->set(i, stack[i]);
	}
	if (popArgs) {
		for (int i = 0; i < na + 2 * nk; i++) {
			Object *o = stack[i];
			incRef(g_none);
			stack[i] = g_none;
			decRef(o);
		}
	}
	return call(func, args.get(), kwargs.get());
}

// Function calls without keywords go straight to the code, as CPython's fast_function
static ObjRef fastCall(Object *func, Object **args, int n) {
	if (isType(func, &g_functionType)) {
		Function *f = (Function *)func;
		Tuple *defs = f->_defaults.get();
		return evalCodeEx(f->_code.get(), f->_globals.get(), nullptr, args, n, nullptr, 0,
		                  defs && defs->_size ? defs->_items : nullptr, defs ? defs->_size : 0, f->_closure.get());
	}
	if (isType(func, &g_methodType) && ((Method *)func)->_self) {
		Method *m = (Method *)func;
		Common::Array<Object *> newArgs;
		newArgs.push_back(m->_self.get());
		for (int i = 0; i < n; i++)
			newArgs.push_back(args[i]);
		ObjRef keep(func);
		return fastCall(m->_func.get(), &newArgs[0], n + 1);
	}
	return callWithStack(func, args, n, 0, nullptr, nullptr);
}

// ---- The main loop ------------------------------------------------------------------

// Every so many instructions, log where the scripts are, so that a hang shows up
static uint32 g_instructionCount = 0;

static void logStack() {
	Common::String out = Common::String::format("python: %u instructions; stack:", g_instructionCount);
	for (Frame *f = g_ts->frame; f; f = f->_back.get()) {
		Code *co = f->_code.get();
		out += Common::String::format("\n  %s:%d in %s", co->_filename ? co->_filename->_data : "?", f->_lineno,
		                              co->_name ? co->_name->_data : "?");
	}
	debug(1, "%s", out.c_str());
}

ObjRef evalFrame(Frame *f) {
	Code *co = f->_code.get();
	const uint8 *first = (const uint8 *)co->_code->_data;
	const uint8 *next = first;
	Object **stack = f->_stack;
	int &sp = f->_stackTop;
	Dict *names = nullptr;
	(void)names;
	Tuple *consts = co->_consts.get();
	Tuple *nameTuple = co->_names.get();
	Object **fast = f->_fast.empty() ? nullptr : &f->_fast[0];
	Why why = kWhyNot;
	int opcode = 0, oparg = 0;
	ObjRef retval;
	Object *stream = nullptr;
	int err = 0;

	if (!enterRecursiveCall())
		return ObjRef();
	f->_back = g_ts->frame;
	g_ts->frame = f;
	if (f->_resumeAt >= 0) {
		next = first + f->_resumeAt;
		f->_resumeAt = -1;
	}

#define PUSH(o) (stack[sp++] = (o))
#define POP() (stack[--sp])
#define TOP() (stack[sp - 1])
#define SECOND() (stack[sp - 2])
#define THIRD() (stack[sp - 3])
#define FOURTH() (stack[sp - 4])
#define SET_TOP(o) (stack[sp - 1] = (o))
#define SET_SECOND(o) (stack[sp - 2] = (o))
#define SET_THIRD(o) (stack[sp - 3] = (o))
#define SET_FOURTH(o) (stack[sp - 4] = (o))
#define STACK_LEVEL() (sp)
#define GETITEM(t, i) ((t)->get(i))
#define GETNAME(i) ((Str *)nameTuple->get(i))
#define INSTR_OFFSET() ((int)(next - first))
#define JUMPTO(x) (next = first + (x))
#define JUMPBY(x) (next += (x))
#define DECREF(o) decRef(o)
#define XDECREF(o) do { if (o) decRef(o); } while (0)
#define INCREF(o) incRef(o)
// Takes the object out of a reference, for pushing
#define OWN(r) ((r).release())

	for (;;) {
		// Fetch the next instruction
		f->_lasti = INSTR_OFFSET();
		if ((++g_instructionCount & 0x3ffffff) == 0)
			logStack();

		opcode = *next++;
		if (opcode >= kHaveArgument) {
			oparg = next[0] | (next[1] << 8);
			next += 2;
		}
	dispatchOpcode:
		Object *v, *w, *u, *x;
		x = g_none; // anything non-null means "no error"
		err = 0;

		switch (opcode) {
		case kSetLineno:
			f->_lineno = oparg;
			continue;

		case kPopTop:
			v = POP();
			DECREF(v);
			continue;

		case kRotTwo:
			v = TOP();
			w = SECOND();
			SET_TOP(w);
			SET_SECOND(v);
			continue;

		case kRotThree:
			v = TOP();
			w = SECOND();
			x = THIRD();
			SET_TOP(w);
			SET_SECOND(x);
			SET_THIRD(v);
			continue;

		case kRotFour:
			u = TOP();
			v = SECOND();
			w = THIRD();
			x = FOURTH();
			SET_TOP(v);
			SET_SECOND(w);
			SET_THIRD(x);
			SET_FOURTH(u);
			continue;

		case kDupTop:
			v = TOP();
			INCREF(v);
			PUSH(v);
			continue;

		case kDupTopX:
			if (oparg < 1 || oparg > 5) {
				setErrorString(g_excSystemError, "invalid argument to DUP_TOPX (bytecode corruption?)");
				x = nullptr;
				break;
			}
			for (int i = 0; i < oparg; i++) {
				v = stack[sp - oparg];
				INCREF(v);
				PUSH(v);
			}
			continue;

		case kUnaryPositive: {
			v = POP();
			ObjRef r = numberPositive(v);
			DECREF(v);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

		case kUnaryNegative: {
			v = POP();
			ObjRef r = numberNegative(v);
			DECREF(v);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

		case kUnaryNot: {
			v = POP();
			err = isTrue(v);
			DECREF(v);
			if (err >= 0) {
				ObjRef r = newBool(err == 0);
				PUSH(OWN(r));
				err = 0;
				continue;
			}
			x = nullptr;
			break;
		}

		case kUnaryConvert: {
			v = POP();
			ObjRef r = repr(v);
			DECREF(v);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

		case kUnaryInvert: {
			v = POP();
			ObjRef r = numberInvert(v);
			DECREF(v);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

#define BINARY(opc, func) \
		case opc: { \
			w = POP(); \
			v = POP(); \
			ObjRef r = func(v, w); \
			DECREF(v); \
			DECREF(w); \
			x = OWN(r); \
			if (x) \
				PUSH(x); \
			break; \
		}

		case kBinaryPower:
		case kInplacePower: {
			w = POP();
			v = POP();
			ObjRef r = opcode == kBinaryPower ? numberPower(v, w, g_none) : numberInplacePower(v, w, g_none);
			DECREF(v);
			DECREF(w);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

		BINARY(kBinaryMultiply, numberMultiply)
		BINARY(kBinaryDivide, numberDivide)
		BINARY(kBinaryFloorDivide, numberFloorDivide)
		BINARY(kBinaryTrueDivide, numberTrueDivide)
		BINARY(kBinaryModulo, numberRemainder)
		BINARY(kBinaryLshift, numberLshift)
		BINARY(kBinaryRshift, numberRshift)
		BINARY(kBinaryAnd, numberAnd)
		BINARY(kBinaryXor, numberXor)
		BINARY(kBinaryOr, numberOr)
		BINARY(kInplaceMultiply, numberInplaceMultiply)
		BINARY(kInplaceDivide, numberInplaceDivide)
		BINARY(kInplaceFloorDivide, numberInplaceFloorDivide)
		BINARY(kInplaceTrueDivide, numberInplaceTrueDivide)
		BINARY(kInplaceModulo, numberInplaceRemainder)
		BINARY(kInplaceLshift, numberInplaceLshift)
		BINARY(kInplaceRshift, numberInplaceRshift)
		BINARY(kInplaceAnd, numberInplaceAnd)
		BINARY(kInplaceXor, numberInplaceXor)
		BINARY(kInplaceOr, numberInplaceOr)
		BINARY(kBinaryAdd, numberAdd)
		BINARY(kBinarySubtract, numberSubtract)
		BINARY(kInplaceAdd, numberInplaceAdd)
		BINARY(kInplaceSubtract, numberInplaceSubtract)

		case kBinarySubscr: {
			w = POP();
			v = POP();
			ObjRef r;
			if (isType(v, &g_listType) && isType(w, &g_intType)) {
				List *l = (List *)v;
				int32 i = ((Int *)w)->_value;
				if (i < 0)
					i += l->size();
				if (i < 0 || i >= (int32)l->size())
					setErrorString(g_excIndexError, "list index out of range");
				else
					r = ObjRef(l->get(i));
			} else {
				r = getItem(v, w);
			}
			DECREF(v);
			DECREF(w);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

		case kSlice:
		case kSlice + 1:
		case kSlice + 2:
		case kSlice + 3: {
			w = (opcode - kSlice) & 2 ? POP() : nullptr;
			v = (opcode - kSlice) & 1 ? POP() : nullptr;
			u = POP();
			ObjRef r = applySlice(u, v, w);
			DECREF(u);
			XDECREF(v);
			XDECREF(w);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

		case kStoreSlice:
		case kStoreSlice + 1:
		case kStoreSlice + 2:
		case kStoreSlice + 3: {
			w = (opcode - kStoreSlice) & 2 ? POP() : nullptr;
			v = (opcode - kStoreSlice) & 1 ? POP() : nullptr;
			u = POP();
			Object *t = POP();
			err = assignSlice(u, v, w, t);
			DECREF(t);
			DECREF(u);
			XDECREF(v);
			XDECREF(w);
			if (err == 0)
				continue;
			x = nullptr;
			break;
		}

		case kDeleteSlice:
		case kDeleteSlice + 1:
		case kDeleteSlice + 2:
		case kDeleteSlice + 3: {
			w = (opcode - kDeleteSlice) & 2 ? POP() : nullptr;
			v = (opcode - kDeleteSlice) & 1 ? POP() : nullptr;
			u = POP();
			err = assignSlice(u, v, w, nullptr);
			DECREF(u);
			XDECREF(v);
			XDECREF(w);
			if (err == 0)
				continue;
			x = nullptr;
			break;
		}

		case kStoreSubscr:
			w = POP();
			v = POP();
			u = POP();
			// v[w] = u
			err = setItem(v, w, u);
			DECREF(u);
			DECREF(v);
			DECREF(w);
			if (err == 0)
				continue;
			x = nullptr;
			break;

		case kDeleteSubscr:
			w = POP();
			v = POP();
			err = delItem(v, w);
			DECREF(v);
			DECREF(w);
			if (err == 0)
				continue;
			x = nullptr;
			break;

		case kPrintExpr: {
			v = POP();
			ObjRef hook = sysGetObject("displayhook");
			if (!hook) {
				setErrorString(g_excRuntimeError, "lost sys.displayhook");
				x = nullptr;
			} else {
				ObjRef r = callFunction(hook.get(), 1, v);
				if (!r)
					x = nullptr;
			}
			DECREF(v);
			break;
		}

		case kPrintItemTo:
			stream = POP();
			// fall through
		case kPrintItem: {
			v = POP();
			ObjRef out;
			if (!stream || stream == g_none) {
				out = sysGetObject("stdout");
				if (!out) {
					setErrorString(g_excRuntimeError, "lost sys.stdout");
					err = -1;
				}
			} else {
				out = stream;
			}
			if (out && fileSoftSpace(out.get(), 1))
				err = fileWriteString(" ", out.get());
			if (err == 0)
				err = fileWriteObject(v, out.get(), true);
			if (err == 0 && isInstanceOf(v, &g_strType)) {
				Str *s = (Str *)v;
				if (s->_len > 0) {
					char c = s->_data[s->_len - 1];
					if ((c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') && c != ' ')
						fileSoftSpace(out.get(), 0);
				}
			}
			DECREF(v);
			if (stream)
				DECREF(stream);
			stream = nullptr;
			if (err == 0)
				continue;
			x = nullptr;
			break;
		}

		case kPrintNewlineTo:
			stream = POP();
			// fall through
		case kPrintNewline: {
			ObjRef out;
			if (!stream || stream == g_none) {
				out = sysGetObject("stdout");
				if (!out) {
					setErrorString(g_excRuntimeError, "lost sys.stdout");
					x = nullptr;
				}
			} else {
				out = stream;
			}
			if (out) {
				err = fileWriteString("\n", out.get());
				if (err == 0)
					fileSoftSpace(out.get(), 0);
				else
					x = nullptr;
			}
			if (stream)
				DECREF(stream);
			stream = nullptr;
			break;
		}

		case kBreakLoop:
			why = kWhyBreak;
			break;

		case kContinueLoop:
			retval = newInt(oparg);
			why = kWhyContinue;
			break;

		case kRaiseVarargs:
			u = v = w = nullptr;
			switch (oparg) {
			case 3:
				u = POP(); // traceback
				// fall through
			case 2:
				v = POP(); // value
				// fall through
			case 1:
				w = POP(); // exc
				// fall through
			case 0:
				why = doRaise(w, v, u);
				break;
			default:
				setErrorString(g_excSystemError, "bad RAISE_VARARGS oparg");
				why = kWhyException;
				break;
			}
			XDECREF(u);
			XDECREF(v);
			XDECREF(w);
			break;

		case kLoadLocals:
			if (!f->_locals) {
				setErrorString(g_excSystemError, "no locals");
				x = nullptr;
				break;
			}
			INCREF(f->_locals.get());
			PUSH(f->_locals.get());
			continue;

		case kReturnValue:
			retval = ObjRef(POP());
			DECREF(retval.get());
			why = kWhyReturn;
			break;

		case kYieldValue:
			retval = ObjRef(POP());
			DECREF(retval.get());
			f->_resumeAt = INSTR_OFFSET();
			why = kWhyYield;
			break;

		case kExecStmt: {
			w = POP();
			v = POP();
			u = POP();
			ObjRef r = execStatement(f, u, v, w);
			DECREF(u);
			DECREF(v);
			DECREF(w);
			if (!r)
				x = nullptr;
			break;
		}

		case kPopBlock: {
			Block *b = &f->_blocks[--f->_blockCount];
			while (STACK_LEVEL() > b->level) {
				v = POP();
				XDECREF(v);
			}
			continue;
		}

		case kEndFinally:
			v = POP();
			if (isInstanceOf(v, &g_intType)) {
				why = (Why)((Int *)v)->_value;
				if (why == kWhyReturn || why == kWhyContinue) {
					retval = ObjRef(POP());
					DECREF(retval.get());
				}
			} else if (isInstanceOf(v, &g_strType) || isType(v, &g_classType)) {
				w = POP();
				u = POP();
				restoreError(v, w, u);
				XDECREF(w);
				XDECREF(u);
				why = kWhyReraise;
				DECREF(v);
				break;
			} else if (v != g_none) {
				setErrorString(g_excSystemError, "'finally' pops bad exception");
				why = kWhyException;
			}
			DECREF(v);
			break;

		case kBuildClass: {
			u = POP();
			v = POP();
			w = POP();
			ObjRef r = buildClass(u, v, w);
			x = OWN(r);
			if (x)
				PUSH(x);
			DECREF(u);
			DECREF(v);
			DECREF(w);
			break;
		}

		case kStoreName:
			w = GETNAME(oparg);
			v = POP();
			if (!f->_locals) {
				setErrorFormat(g_excSystemError, "no locals found when storing %s", ((Str *)w)->_data);
				x = nullptr;
				DECREF(v);
				break;
			}
			if (isType(f->_locals.get(), &g_dictType))
				err = ((Dict *)f->_locals.get())->setItem(w, v);
			else
				err = setItem(f->_locals.get(), w, v);
			DECREF(v);
			if (err == 0)
				continue;
			x = nullptr;
			break;

		case kDeleteName:
			w = GETNAME(oparg);
			if (!f->_locals) {
				setErrorFormat(g_excSystemError, "no locals when deleting %s", ((Str *)w)->_data);
				x = nullptr;
				break;
			}
			err = delItem(f->_locals.get(), w);
			if (err != 0) {
				clearError();
				setErrorFormat(g_excNameError, "name '%.200s' is not defined", ((Str *)w)->_data);
				x = nullptr;
			}
			break;

		case kUnpackSequence:
			v = POP();
			if (isType(v, &g_tupleType)) {
				Tuple *t = (Tuple *)v;
				if ((int)t->_size != oparg) {
					setErrorString(g_excValueError, "unpack tuple of wrong size");
					why = kWhyException;
				} else {
					for (int i = oparg; --i >= 0;) {
						w = t->get(i);
						INCREF(w);
						PUSH(w);
					}
				}
			} else if (isType(v, &g_listType)) {
				List *l = (List *)v;
				if ((int)l->size() != oparg) {
					setErrorString(g_excValueError, "unpack list of wrong size");
					why = kWhyException;
				} else {
					for (int i = oparg; --i >= 0;) {
						w = l->get(i);
						INCREF(w);
						PUSH(w);
					}
				}
			} else if (unpackIterable(v, oparg, stack + sp + oparg)) {
				sp += oparg;
			} else {
				if (errorMatches(g_excTypeError)) {
					clearError();
					setErrorString(g_excTypeError, "unpack non-sequence");
				}
				why = kWhyException;
			}
			DECREF(v);
			break;

		case kStoreAttr:
			w = GETNAME(oparg);
			v = POP();
			u = POP();
			err = setAttr(v, (Str *)w, u); // v.w = u
			DECREF(v);
			DECREF(u);
			if (err == 0)
				continue;
			x = nullptr;
			break;

		case kDeleteAttr:
			w = GETNAME(oparg);
			v = POP();
			err = setAttr(v, (Str *)w, nullptr);
			DECREF(v);
			if (err != 0)
				x = nullptr;
			break;

		case kStoreGlobal:
			w = GETNAME(oparg);
			v = POP();
			err = f->_globals->setItem(w, v);
			DECREF(v);
			if (err != 0)
				x = nullptr;
			break;

		case kDeleteGlobal:
			w = GETNAME(oparg);
			if (f->_globals->delItem(w) != 0) {
				clearError();
				setErrorFormat(g_excNameError, "global name '%.200s' is not defined", ((Str *)w)->_data);
				x = nullptr;
			}
			break;

		case kLoadConst:
			x = GETITEM(consts, oparg);
			INCREF(x);
			PUSH(x);
			continue;

		case kLoadName: {
			w = GETNAME(oparg);
			if (!f->_locals) {
				setErrorFormat(g_excSystemError, "no locals when loading %s", ((Str *)w)->_data);
				x = nullptr;
				break;
			}
			ObjRef found;
			if (isType(f->_locals.get(), &g_dictType)) {
				Object *r = ((Dict *)f->_locals.get())->getItem(w);
				if (r)
					found = r;
			} else {
				found = getItem(f->_locals.get(), w);
				if (!found)
					clearError();
			}
			if (!found) {
				Object *r = f->_globals->getItem(w);
				if (!r)
					r = f->_builtins->getItem(w);
				if (!r) {
					setErrorFormat(g_excNameError, "name '%.200s' is not defined", ((Str *)w)->_data);
					x = nullptr;
					break;
				}
				found = r;
			}
			PUSH(OWN(found));
			continue;
		}

		case kLoadGlobal:
			w = GETNAME(oparg);
			x = f->_globals->getItem(w);
			if (!x) {
				x = f->_builtins->getItem(w);
				if (!x) {
					setErrorFormat(g_excNameError, "global name '%.200s' is not defined", ((Str *)w)->_data);
					break;
				}
			}
			INCREF(x);
			PUSH(x);
			continue;

		case kDeleteFast:
			x = fast[oparg];
			if (!x) {
				setErrorFormat(g_excUnboundLocalError, "local variable '%.200s' referenced before assignment",
				               ((Str *)co->_varNames->get(oparg))->_data);
				break;
			}
			fast[oparg] = nullptr;
			DECREF(x);
			continue;

		case kLoadFast:
			x = fast[oparg];
			if (!x) {
				setErrorFormat(g_excUnboundLocalError, "local variable '%.200s' referenced before assignment",
				               ((Str *)co->_varNames->get(oparg))->_data);
				break;
			}
			INCREF(x);
			PUSH(x);
			continue;

		case kStoreFast:
			v = POP();
			x = fast[oparg];
			fast[oparg] = v;
			if (x)
				DECREF(x);
			continue;

		case kLoadClosure:
			x = fast[co->_nLocals + oparg];
			INCREF(x);
			PUSH(x);
			continue;

		case kLoadDeref: {
			x = fast[co->_nLocals + oparg];
			Object *cv = ((Cell *)x)->_ref.get();
			if (!cv) {
				uint32 ncell = co->_cellVars ? co->_cellVars->_size : 0;
				if ((uint32)oparg < ncell)
					setErrorFormat(g_excUnboundLocalError, "local variable '%.200s' referenced before assignment",
					               ((Str *)co->_cellVars->get(oparg))->_data);
				else
					setErrorFormat(g_excNameError, "free variable '%.200s' referenced before assignment in enclosing scope",
					               ((Str *)co->_freeVars->get(oparg - ncell))->_data);
				x = nullptr;
				break;
			}
			INCREF(cv);
			PUSH(cv);
			continue;
		}

		case kStoreDeref:
			w = POP();
			x = fast[co->_nLocals + oparg];
			((Cell *)x)->_ref = w;
			DECREF(w);
			continue;

		case kBuildTuple: {
			if (oparg == 0) {
				Ref<Tuple> empty = newTuple(0);
				PUSH(empty.release());
				continue;
			}
			Tuple *t = new Tuple(oparg);

			for (int i = oparg; --i >= 0;) {
				w = POP();
				t->_items[i] = w; // takes the reference
			}
			incRef(t);
			PUSH(t);
			continue;
		}

		case kBuildList: {
			List *l = new List();
			l->_items.resize(oparg);
			for (int i = oparg; --i >= 0;) {
				w = POP();
				l->_items[i] = w;
			}
			incRef(l);
			PUSH(l);
			continue;
		}

		case kBuildMap: {
			Dict *d = new Dict();
			incRef(d);
			PUSH(d);
			continue;
		}

		case kLoadAttr: {
			w = GETNAME(oparg);
			v = TOP();
			ObjRef r = getAttr(v, (Str *)w);
			DECREF(v);
			x = OWN(r);
			SET_TOP(x);
			if (!x)
				sp--;
			break;
		}

		case kCompareOp: {
			w = POP();
			v = POP();
			ObjRef r;
			if (isType(v, &g_intType) && isType(w, &g_intType) && oparg <= kCmpIsNot && oparg != kCmpIn && oparg != kCmpNotIn) {
				int32 a = ((Int *)v)->_value, b = ((Int *)w)->_value;
				bool res;
				switch (oparg) {
				case kLT: res = a < b; break;
				case kLE: res = a <= b; break;
				case kEQ: res = a == b; break;
				case kNE: res = a != b; break;
				case kGT: res = a > b; break;
				case kGE: res = a >= b; break;
				case kCmpIs: res = v == w; break;
				default: res = v != w; break;
				}
				r = newBool(res);
			} else {
				r = cmpOutcome(oparg, v, w);
			}
			DECREF(v);
			DECREF(w);
			x = OWN(r);
			if (x) {
				PUSH(x);
				continue;
			}
			break;
		}

		case kImportName: {
			w = GETNAME(oparg);
			Object *importFunc = f->_builtins->getItemString("__import__");
			if (!importFunc) {
				setErrorString(g_excImportError, "__import__ not found");
				x = nullptr;
				break;
			}
			ObjRef keepImport(importFunc);
			u = POP();
			ObjRef r = callFunction(importFunc, 4, w, f->_globals.get(), f->_locals ? f->_locals.get() : g_none, u);
			DECREF(u);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

		case kImportStar:
			v = POP();
			fastToLocals(f);
			if (!f->_locals) {
				setErrorString(g_excSystemError, "no locals found during 'import *'");
				x = nullptr;
				DECREF(v);
				break;
			}
			err = importAllFrom(f->_locals.get(), v);
			localsToFast(f, false);
			DECREF(v);
			if (err == 0)
				continue;
			x = nullptr;
			break;

		case kImportFrom: {
			w = GETNAME(oparg);
			v = TOP();
			ObjRef r = importFrom(v, (Str *)w);
			x = OWN(r);
			if (x)
				PUSH(x);
			break;
		}

		case kJumpForward:
			JUMPBY(oparg);
			continue;

		case kJumpIfFalse: {
			w = TOP();
			if (isType(w, &g_intType)) {
				if (((Int *)w)->_value == 0)
					JUMPBY(oparg);
				continue;
			}
			err = isTrue(w);
			if (err > 0)
				err = 0;
			else if (err == 0)
				JUMPBY(oparg);
			else
				x = nullptr;
			break;
		}

		case kJumpIfTrue: {
			w = TOP();
			if (isType(w, &g_intType)) {
				if (((Int *)w)->_value != 0)
					JUMPBY(oparg);
				continue;
			}
			err = isTrue(w);
			if (err > 0) {
				err = 0;
				JUMPBY(oparg);
			} else if (err < 0) {
				x = nullptr;
			}
			break;
		}

		case kJumpAbsolute:
			JUMPTO(oparg);
			continue;

		case kGetIter: {
			v = TOP();
			ObjRef r = getIter(v);
			DECREF(v);
			x = OWN(r);
			if (x) {
				SET_TOP(x);
				continue;
			}
			sp--;
			break;
		}

		case kForIter: {
			v = TOP();
			ObjRef r = v->_type->iterNext(v);
			if (r) {
				PUSH(OWN(r));
				continue;
			}
			if (errorOccurred()) {
				if (!errorMatches(g_excStopIteration)) {
					x = nullptr;
					break;
				}
				clearError();
			}
			// Iterator ended normally
			x = POP();
			DECREF(x);
			x = g_none;
			JUMPBY(oparg);
			continue;
		}

		case kForLoop: {
			// for v in s: ... (the old sequence protocol); stack: s, i
			w = POP(); // loop index
			v = POP(); // sequence object
			int32 idx = ((Int *)w)->_value;
			ObjRef item = sequenceGetItem(v, idx);
			if (item) {
				PUSH(v);
				ObjRef ni = newInt(idx + 1);
				PUSH(OWN(ni));
				PUSH(OWN(item));
				DECREF(w);
				continue;
			}
			if (errorMatches(g_excIndexError)) {
				clearError();
				DECREF(v);
				DECREF(w);
				JUMPBY(oparg);
				continue;
			}
			DECREF(v);
			DECREF(w);
			x = nullptr;
			break;
		}

		case kSetupLoop:
		case kSetupExcept:
		case kSetupFinally:
			if (f->_blockCount >= kMaxBlocks) {
				setErrorString(g_excSystemError, "block stack overflow");
				x = nullptr;
				break;
			}
			f->_blocks[f->_blockCount].type = opcode;
			f->_blocks[f->_blockCount].handler = INSTR_OFFSET() + oparg;
			f->_blocks[f->_blockCount].level = STACK_LEVEL();
			f->_blockCount++;
			continue;

		case kCallFunction: {
			int na = oparg & 0xff;
			int nk = (oparg >> 8) & 0xff;
			int n = na + 2 * nk;
			Object **pfunc = stack + sp - n - 1;
			Object *func = *pfunc;
			ObjRef r;
			bool keepStack = callKeepsStack(func);
			if (nk == 0 && keepStack)
				r = fastCall(func, pfunc + 1, na);
			else
				r = callWithStack(func, pfunc + 1, na, nk, nullptr, nullptr, !keepStack);
			while (sp > pfunc - stack) {
				w = POP();
				DECREF(w);
			}
			x = OWN(r);
			if (x) {
				PUSH(x);
				continue;
			}
			break;
		}

		case kCallFunctionVar:
		case kCallFunctionKw:
		case kCallFunctionVarKw: {
			int na = oparg & 0xff;
			int nk = (oparg >> 8) & 0xff;
			int flags = (opcode - kCallFunction) & 3;
			int n = na + 2 * nk;
			Object *kwdict = (flags & 2) ? stack[sp - 1] : nullptr;
			Object *stararg = (flags & 1) ? stack[sp - 1 - ((flags & 2) ? 1 : 0)] : nullptr;
			int extra = ((flags & 1) ? 1 : 0) + ((flags & 2) ? 1 : 0);
			Object **pfunc = stack + sp - n - 1 - extra;
			Object *func = *pfunc;
			ObjRef r = callWithStack(func, pfunc + 1, na, nk, stararg, kwdict, true);
			while (sp > pfunc - stack) {
				w = POP();
				DECREF(w);
			}
			x = OWN(r);
			if (x) {
				PUSH(x);
				continue;
			}
			break;
		}

		case kMakeFunction: {
			v = POP(); // code object
			Function *fn = new Function((Code *)v, f->_globals.get());
			ObjRef rf(fn);
			DECREF(v);
			if (oparg > 0) {
				Ref<Tuple> defs = newTuple(oparg);
				while (--oparg >= 0) {
					w = POP();
					defs->set(oparg, w);
					DECREF(w);
				}
				fn->_defaults = defs;
			}
			PUSH(OWN(rf));
			continue;
		}

		case kMakeClosure: {
			v = POP(); // code object
			Function *fn = new Function((Code *)v, f->_globals.get());
			ObjRef rf(fn);
			DECREF(v);
			Code *fco = (Code *)fn->_code.get();
			uint32 nfree = fco->_freeVars ? fco->_freeVars->_size : 0;
			if (nfree) {
				Ref<Tuple> closure = newTuple(nfree);
				for (int i = nfree; --i >= 0;) {
					w = POP();
					closure->set(i, w);
					DECREF(w);
				}
				fn->_closure = closure;
			}
			if (oparg > 0) {
				Ref<Tuple> defs = newTuple(oparg);
				while (--oparg >= 0) {
					w = POP();
					defs->set(oparg, w);
					DECREF(w);
				}
				fn->_defaults = defs;
			}
			PUSH(OWN(rf));
			continue;
		}

		case kBuildSlice: {
			w = oparg == 3 ? POP() : nullptr;
			v = POP();
			u = TOP();
			Slice *s = new Slice(u, v, w);
			incRef(s);
			DECREF(u);
			DECREF(v);
			XDECREF(w);
			SET_TOP(s);
			continue;
		}

		case kExtendedArg:
			opcode = *next++;
			oparg = (oparg << 16) | next[0] | (next[1] << 8);
			next += 2;
			goto dispatchOpcode;

		default:
			setErrorFormat(g_excSystemError, "unknown opcode %d", opcode);
			why = kWhyException;
			break;
		}

		// An error occurred: x == null, or err != 0, or why was set
		if (why == kWhyNot) {
			if (err == 0 && x)
				continue;
			why = kWhyException;
			if (!errorOccurred()) {
				// Some ops (e.g. a failed lookup leaving x null) always set an error
				setErrorString(g_excSystemError, "error return without exception set");
			}
		}

		if (why == kWhyException || why == kWhyReraise) {
			if (!errorOccurred()) {
				setErrorString(g_excSystemError, "error return without exception set");
				why = kWhyException;
			}
		}

		// Log traceback info if this is a real exception
		if (why == kWhyException) {
			f->_lasti = INSTR_OFFSET() - 1;
			if (opcode >= kHaveArgument)
				f->_lasti -= 2;
			addTraceback(f);
		}
		if (why == kWhyReraise)
			why = kWhyException;

		// Unwind stacks if a (pseudo) exception occurred
		while (why != kWhyNot && why != kWhyYield && f->_blockCount > 0) {
			Block *b = &f->_blocks[--f->_blockCount];
			if (b->type == kSetupLoop && why == kWhyContinue) {
				// For a continue inside a try block, don't pop the block for the loop
				f->_blockCount++;
				why = kWhyNot;
				JUMPTO(((Int *)retval.get())->_value);
				retval.reset();
				break;
			}
			while (STACK_LEVEL() > b->level) {
				v = POP();
				XDECREF(v);
			}
			if (b->type == kSetupLoop && why == kWhyBreak) {
				why = kWhyNot;
				JUMPTO(b->handler);
				break;
			}
			if (b->type == kSetupFinally || (b->type == kSetupExcept && why == kWhyException)) {
				if (why == kWhyException) {
					ObjRef exc, val, tb;
					fetchError(&exc, &val, &tb);
					if (!val)
						val = g_none;
					// Make the raw exception data available to the handler
					if (b->type == kSetupExcept) {
						normalizeException(&exc, &val, &tb);
						setExcInfo(exc.get(), val.get(), tb.get());
					}
					PUSH(tb ? OWN(tb) : (incRef(g_none), g_none));
					PUSH(OWN(val));
					PUSH(OWN(exc));
				} else {
					if (why == kWhyReturn || why == kWhyContinue)
						PUSH(OWN(retval));
					ObjRef wv = newInt((int32)why);
					PUSH(OWN(wv));
				}
				why = kWhyNot;
				JUMPTO(b->handler);
				break;
			}
		}

		// End the loop if we still have an error (or return)
		if (why != kWhyNot)
			break;
	}

	// Pop the remaining stack entries, unless suspended in a yield
	if (why != kWhyYield) {
		while (sp > 0) {
			Object *v = POP();
			XDECREF(v);
		}
		f->_finished = true;
	}
	if (why != kWhyReturn && why != kWhyYield)
		retval.reset();
	resetExcInfo();
	g_ts->frame = f->_back.get();
	f->_back.reset();
	leaveRecursiveCall();
	return retval;
}

ObjRef evalCodeEx(Code *co, Dict *globals, Object *locals,
                  Object **args, uint32 argcount, Object **kws, uint32 kwcount,
                  Object **defs, uint32 defcount, Tuple *closure) {
	if (!globals) {
		setErrorString(g_excSystemError, "PyEval_EvalCodeEx: NULL globals");
		return ObjRef();
	}
	Frame *f = new Frame(co, globals, locals, g_ts->frame);
	Ref<Frame> rf(f);
	Object **fast = f->_fast.empty() ? nullptr : &f->_fast[0];
	const char *coName = co->_name ? co->_name->_data : "?";
#define SETLOCAL(i, value) do { Object *tmp_ = fast[i]; fast[i] = (value); if (tmp_) decRef(tmp_); } while (0)

	if (co->_argCount > 0 || (co->_flags & (kCoVarArgs | kCoVarKeywords))) {
		uint32 i;
		uint32 n = argcount;
		Dict *kwdict = nullptr;
		if (co->_flags & kCoVarKeywords) {
			kwdict = new Dict();
			incRef(kwdict);
			i = co->_argCount;
			if (co->_flags & kCoVarArgs)
				i++;
			SETLOCAL(i, kwdict);
		}
		if (argcount > (uint32)co->_argCount) {
			if (!(co->_flags & kCoVarArgs)) {
				setErrorFormat(g_excTypeError, "%.200s() takes %s %d %sargument%s (%d given)", coName,
				               defcount ? "at most" : "exactly", co->_argCount, kwcount ? "non-keyword " : "",
				               co->_argCount == 1 ? "" : "s", argcount);
				return ObjRef();
			}
			n = co->_argCount;
		}
		for (i = 0; i < n; i++) {
			incRef(args[i]);
			SETLOCAL(i, args[i]);
		}
		if (co->_flags & kCoVarArgs) {
			Tuple *u = new Tuple(argcount - n);
			incRef(u);
			SETLOCAL(co->_argCount, u);
			for (i = n; i < argcount; i++)
				u->set(i - n, args[i]);
		}
		for (i = 0; i < kwcount; i++) {
			Object *keyword = kws[2 * i];
			Object *value = kws[2 * i + 1];
			if (!keyword || !isInstanceOf(keyword, &g_strType)) {
				setErrorFormat(g_excTypeError, "%.200s() keywords must be strings", coName);
				return ObjRef();
			}
			int32 j;
			for (j = 0; j < co->_argCount; j++) {
				Object *nm = co->_varNames->get(j);
				int cmp = richCompareBool(keyword, nm, kEQ);
				if (cmp > 0)
					break;
				if (cmp < 0)
					return ObjRef();
			}
			if (errorOccurred())
				return ObjRef();
			if (j >= co->_argCount) {
				if (!kwdict) {
					setErrorFormat(g_excTypeError, "%.200s() got an unexpected keyword argument '%.400s'", coName, ((Str *)keyword)->_data);
					return ObjRef();
				}
				kwdict->setItem(keyword, value);
			} else {
				if (fast[j]) {
					setErrorFormat(g_excTypeError, "%.200s() got multiple values for keyword argument '%.400s'", coName, ((Str *)keyword)->_data);
					return ObjRef();
				}
				incRef(value);
				SETLOCAL(j, value);
			}
		}
		if (argcount < (uint32)co->_argCount) {
			int32 m = co->_argCount - defcount;
			for (int32 k = argcount; k < m; k++) {
				if (!fast[k]) {
					setErrorFormat(g_excTypeError, "%.200s() takes %s %d %sargument%s (%d given)", coName,
					               ((co->_flags & kCoVarArgs) || defcount) ? "at least" : "exactly", m,
					               kwcount ? "non-keyword " : "", m == 1 ? "" : "s", k);
					return ObjRef();
				}
			}
			int32 k = (int32)n > m ? (int32)n - m : 0;
			for (; k < (int32)defcount; k++) {
				if (!fast[m + k]) {
					incRef(defs[k]);
					SETLOCAL(m + k, defs[k]);
				}
			}
		}
	} else if (argcount > 0 || kwcount > 0) {
		setErrorFormat(g_excTypeError, "%.200s() takes no arguments (%d given)", coName, argcount + kwcount);
		return ObjRef();
	}

	// Code that is not optimised (it uses exec or import *) looks its
	// arguments up by name
	if ((co->_flags & (kCoNewLocals | kCoOptimized)) == kCoNewLocals && co->_argCount > 0 &&
	    f->_locals && isInstanceOf(f->_locals.get(), &g_dictType)) {
		Dict *ld = (Dict *)f->_locals.get();
		for (int32 j = 0; j < co->_nLocals && j < (int32)co->_varNames->_size; j++)
			if (fast[j])
				ld->setItem(co->_varNames->get(j), fast[j]);
	}

	// Cells for this function's cell variables, and the free variables passed in

	uint32 ncells = co->_cellVars ? co->_cellVars->_size : 0;
	uint32 nfree = co->_freeVars ? co->_freeVars->_size : 0;
	for (uint32 i = 0; i < ncells; i++) {
		// A cell variable that is also an argument starts with the argument's value
		Object *cellName = co->_cellVars->get(i);
		Object *init = nullptr;
		for (int32 j = 0; j < co->_argCount + ((co->_flags & kCoVarArgs) ? 1 : 0) + ((co->_flags & kCoVarKeywords) ? 1 : 0); j++) {
			if (j < (int32)co->_varNames->_size && compare(cellName, co->_varNames->get(j)) == 0) {
				init = fast[j];
				break;
			}
		}
		Cell *c = new Cell(init);
		incRef(c);
		SETLOCAL(co->_nLocals + i, c);
	}
	for (uint32 i = 0; i < nfree && closure; i++) {
		Object *o = closure->get(i);
		incRef(o);
		SETLOCAL(co->_nLocals + ncells + i, o);
	}
#undef SETLOCAL

	if (co->_flags & kCoGenerator)
		return newGenerator(f);
	return evalFrame(f);

}

ObjRef evalCode(Code *code, Dict *globals, Dict *locals) {
	return evalCodeEx(code, globals, locals, nullptr, 0, nullptr, 0, nullptr, 0, nullptr);
}

} // End of namespace Python
} // End of namespace Yaga

namespace Yaga {
namespace Python {

// ---- Generators (Python 2.2's genobject in ceval.c) ----------------------------------

Type g_generatorType("generator", &g_objectType);

struct Generator : public Object {
	Ref<Frame> _frame;
	bool _running;
	explicit Generator(Frame *f) : Object(&g_generatorType), _frame(f), _running(false) {}
};

ObjRef newGenerator(Frame *f) {
	if (!(g_generatorType._flags & kTypeReady))
		initGeneratorType();
	return ObjRef(new Generator(f));
}

static ObjRef generatorIterNext(Object *o) {
	Generator *gen = (Generator *)o;
	if (gen->_running) {
		setErrorString(g_excValueError, "generator already executing");
		return ObjRef();
	}
	Frame *f = gen->_frame.get();
	if (f->_finished)
		return ObjRef();
	gen->_running = true;
	ObjRef result = evalFrame(f);
	gen->_running = false;
	// A return at the end gives None, which ends the iteration
	if (result && result.get() == g_none && f->_finished)
		return ObjRef();
	return result;
}

static ObjRef generatorIter(Object *o) {
	return ObjRef(o);
}

static ObjRef generatorNext(Object *self, Tuple *, Dict *) {
	ObjRef r = generatorIterNext(self);
	if (!r && !errorOccurred())
		setError(g_excStopIteration, g_none);
	return r;
}

static const MethodDef g_generatorMethods[] = {
	{ "next", generatorNext, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef generatorGetFrame(Object *o) {
	Generator *gen = (Generator *)o;
	return gen->_frame->_finished ? ObjRef(g_none) : ObjRef(gen->_frame.get());
}

static ObjRef generatorGetRunning(Object *o) {
	return newBool(((Generator *)o)->_running);
}

static const GetSetDef g_generatorGetSets[] = {
	{ "gi_frame", generatorGetFrame, nullptr },
	{ "gi_running", generatorGetRunning, nullptr },
	{ nullptr, nullptr, nullptr }
};

void initGeneratorType() {
	g_generatorType.iter = generatorIter;
	g_generatorType.iterNext = generatorIterNext;
	g_generatorType.methods = g_generatorMethods;
	g_generatorType.getSets = g_generatorGetSets;
	g_generatorType.getAttr = genericGetAttr;
	typeReady(&g_generatorType);
}

} // End of namespace Python
} // End of namespace Yaga
