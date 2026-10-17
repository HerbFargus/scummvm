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
#include "common/textconsole.h"

#include "bigtuna/bigtuna.h"
#include "bigtuna/detection.h"
#include "bigtuna/movie.h"
#include "bigtuna/score.h"
#include "bigtuna/script.h"

namespace BigTuna {

enum {
	kStackLimit = 100
};

enum Opcode {
	kOpAdd = 0x01,
	kOpAnd = 0x02,
	kOpJumpIfZero = 0x03,
	kOpCall = 0x04,
	kOpReturnValue = 0x05,
	kOpFunc = 0x06,
	kOpProc = 0x07,
	kOpConcat = 0x08,
	kOpDiv = 0x0B,
	kOpEq = 0x0C,
	kOpGe = 0x0D,
	kOpGo = 0x0E,
	kOpJump = 0x0F,
	kOpGt = 0x10,
	kOpLe = 0x11,
	kOpLt = 0x12,
	kOpMul = 0x13,
	kOpNeg = 0x14,
	kOpNe = 0x15,
	kOpNewList = 0x16,
	kOpMember = 0x17,
	kOpNot = 0x18,
	kOpOr = 0x19,
	kOpPushVoid = 0x1A,
	kOpPushOne = 0x1B,
	kOpPushString = 0x1C,
	kOpPushInt = 0x1E,
	kOpPushZero = 0x1F,
	kOpReturn = 0x20,
	kOpSub = 0x21,
	kOpGetParam = 0x22,
	kOpSetParam = 0x23,
	kOpGetGlobal = 0x24,
	kOpSetGlobal = 0x25,
	kOpGetProp = 0x26,
	kOpSetProp = 0x27,
	kOpGetLocal = 0x28,
	kOpSetLocal = 0x29,
	kOpSet = 0x2A,
	kOpNop = 0x2B,
	kOpPopMember = 0x2C,
	kOpMod = 0x2D,
	kOpAppend = 0x2E,
	kOpIterate = 0x2F,
	kOpNewXtra = 0x30,
	kOpExit = 0x31,
	kOpIntersects = 0x32
};

Script::Script(BigTunaEngine *vm) : _vm(vm), _trace(false), _base(0), _pc(0), _running(false),
	_stopRequested(false), _done(false) {
	_code.movie = nullptr;
	_code.res = 0;
	_code.data = nullptr;
}

bool Script::loadCode(Movie *movie, uint res) {
	_code.movie = movie;
	_code.res = res;
	_code.data = movie->getScriptData(res);
	if (!_code.data) {
		warning("Script: resource %d of %s is not a script", res, movie->getName().c_str());
		return false;
	}
	return true;
}

bool Script::runEvent(const Common::String &event, uint res) {
	Movie *movie = _vm->getMovie();
	const Common::Array<byte> *data = movie->getScriptData(res);
	if (!data)
		return false;
	uint32 offset = 0;
	for (;;) {
		if (offset + 8 > data->size())
			return false;
		uint32 symbol = READ_LE_UINT32(&(*data)[offset]);
		uint32 next = READ_LE_UINT32(&(*data)[offset + 4]);
		if (symbolName(symbol).equalsIgnoreCase(event)) {
			debugC(2, kDebugScript, "Event %s: script %d +%d", event.c_str(), res, offset + 8);
			execute(movie, res, offset + 8);
			return true;
		}
		if (!next)
			return false;
		offset = next;
	}
}

bool Script::runMovieHandler(const Common::String &name) {
	Movie *movie = _vm->getMovie();
	for (auto &s : movie->getSymbols()) {
		if (s.name.equalsIgnoreCase(name) && s.value) {
			debugC(2, kDebugScript, "Movie handler %s", name.c_str());
			execute(movie, s.value >> 16, s.value & 0xFFFF);
			return true;
		}
	}
	return false;
}

void Script::execute(Movie *movie, uint res, uint32 pc) {
	if (_running) {
		warning("Script: nested execution of %s", movie->getName().c_str());
		return;
	}
	_stack.clear();
	_base = 0;
	if (!loadCode(movie, res))
		return;
	_pc = pc;
	_stopRequested = _done = false;
	_running = true;
	run();
	_running = false;
	_stack.clear();
}

byte Script::readByte() {
	if (_pc >= _code.data->size()) {
		_done = true;
		return kOpExit;
	}
	return (*_code.data)[_pc++];
}

uint32 Script::readVarint() {
	uint32 v = 0;
	byte b;
	do {
		b = readByte();
		v = (v << 7) | (b & 0x7F);
	} while (b & 0x80 && !_done);
	return v;
}

int16 Script::readJump() {
	byte lo = readByte();
	byte hi = readByte();
	return (int16)(lo | (hi << 8));
}

void Script::push(const Value &v) {
	if (_stack.size() >= kStackLimit)
		error("Interpreter stack overflow");
	_stack.push_back(v);
}

Value Script::pop() {
	if (_stack.empty()) {
		warning("Script: stack underflow in %s script %d at %d", _code.movie->getName().c_str(), _code.res, _pc);
		return Value();
	}
	Value v = _stack.back();
	_stack.pop_back();
	return v;
}

Common::String Script::popString() {
	Value v = pop();
	if (v.isInt())
		return Common::String::format("%d", v.i);
	if (!v.isString()) {
		warning("expected string");
		return "";
	}
	return v.obj->str;
}

Value Script::popList() {
	Value v = pop();
	if (!v.isList()) {
		warning("popType failure");
		return Value::list();
	}
	return v;
}

Value Script::popPropList() {
	Value v = pop();
	if (!v.isPropList()) {
		warning("popType failure");
		return Value::propList();
	}
	return v;
}

const Value &Script::top(int depth) const {
	static const Value none;
	if ((int)_stack.size() <= depth)
		return none;
	return _stack[_stack.size() - 1 - depth];
}

int Script::topType(int depth) const {
	return top(depth).objectType();
}

uint Script::popMember() {
	Value v = pop();
	int32 n;
	if (v.isString()) {
		n = 0;
		for (auto &c : _vm->getMovie()->getCastNames()) {
			if (c.name.equalsIgnoreCase(v.obj->str)) {
				n = c.value;
				break;
			}
		}
		if (!n) {
			warning("Cast name not found: %s", v.obj->str.c_str());
			return 0;
		}
	} else if (v.isInt() || v.isVoid()) {
		n = v.i;
	} else {
		warning("Bad cast argument");
		return 0;
	}
	return n ? n + kMemberBase : 0;
}

const Common::String &Script::symbolName(uint32 id) const {
	static const Common::String none;
	const Common::Array<NameEntry> &symbols = _vm->getMovie()->getSymbols();
	return id < symbols.size() ? symbols[id].name : none;
}

Value &Script::global(const Common::String &name) {
	if (!_globals.contains(name))
		_globals[name] = Value((int32)0);
	return _globals[name];
}

void Script::clearGlobals() {
	for (auto &s : _vm->getMovie()->getSymbols())
		_globals[s.name] = Value();
}

void Script::opCall(uint32 symbol, uint32 argc) {
	const Common::Array<NameEntry> &symbols = _vm->getMovie()->getSymbols();
	const Common::String &name = symbolName(symbol);
	if (symbol < symbols.size() && symbols[symbol].value) {
		uint32 handler = symbols[symbol].value;
		push((int32)argc);
		push((int32)_base);
		push((int32)_pc);
		push((int32)_code.res);
		_base = _stack.size();
		if (!loadCode(_vm->getMovie(), handler >> 16)) {
			_done = true;
			return;
		}
		_pc = handler & 0xFFFF;
		return;
	}

	if (name.equalsIgnoreCase("rect")) {
		int32 l, t, r, b;
		if (argc == 2) {
			Value p2 = popPropList();
			Value p1 = popPropList();
			l = p1.prop("loch");
			t = p1.prop("locv");
			r = p2.prop("loch");
			b = p2.prop("locv");
		} else {
			if (argc != 4)
				warning("rect should have 2 or 4 arguments");
			b = argc == 4 ? popInt() : 0;
			r = argc == 4 ? popInt() : 0;
			t = argc == 4 ? popInt() : 0;
			l = argc == 4 ? popInt() : 0;
		}
		push(Value::rect(l, t, r, b));
		return;
	}

	// Methods on xtra objects (PrintOMatic) are not supported
	warning("undefined handler %s", name.c_str());
	for (uint32 i = 0; i < argc; i++)
		pop();
}

bool Script::doReturn() {
	while (_stack.size() > _base)
		pop();
	if (_stack.empty())
		return false;
	uint res = popInt();
	_pc = popInt();
	_base = popInt();
	int32 argc = popInt();
	for (int32 i = 0; i < argc; i++)
		pop();
	return loadCode(_code.movie, res);
}

void Script::opGo() {
	Value movie = pop();
	Value frame = pop();
	if (movie.isString()) {
		_stack.clear();
		_vm->requestMovie(movie.obj->str, frame);
		_done = true;
		return;
	}
	if (!movie.isVoid()) {
		warning("go: bad movie name");
		return;
	}
	if (frame.isInt())
		_vm->getPlayer()->seek(frame.i);
	else if (frame.isString())
		_vm->getPlayer()->seekLabel(frame.obj->str);
}

void Script::run() {
	while (!_done && !_stopRequested && !_vm->shouldQuit()) {
		uint32 at = _pc;
		byte op = readByte();
		if (_done)
			break;
		if (_trace)
			debug("%s:%d:%d op %02x sp %d", _code.movie->getName().c_str(), _code.res, at, op, _stack.size());

		switch (op) {
		case kOpAdd: {
			int t0 = topType(0), t1 = topType(1);
			if (t0 == kObjPropList || t1 == kObjPropList) {
				int32 bh, bv, ah, av;
				if (t0 == kObjPropList) {
					Value b = popPropList();
					bh = b.prop("loch");
					bv = b.prop("locv");
				} else {
					bh = bv = popInt();
				}
				if (t1 == kObjPropList) {
					Value a = popPropList();
					ah = a.prop("loch");
					av = a.prop("locv");
				} else {
					ah = av = popInt();
				}
				push(Value::point(ah + bh, av + bv));
			} else {
				int32 b = popInt();
				int32 a = popInt();
				push(a + b);
			}
			break;
		}
		case kOpSub:
			if (topType(0) == kObjPropList) {
				Value b = popPropList();
				Value a = popPropList();
				push(Value::point(a.prop("loch") - b.prop("loch"), a.prop("locv") - b.prop("locv")));
			} else {
				int32 b = popInt();
				int32 a = popInt();
				push(a - b);
			}
			break;
		case kOpAnd: {
			int32 b = popInt();
			int32 a = popInt();
			push(a & b);
			break;
		}
		case kOpOr: {
			int32 b = popInt();
			int32 a = popInt();
			push(a | b);
			break;
		}
		case kOpMul: {
			int32 b = popInt();
			int32 a = popInt();
			push(a * b);
			break;
		}
		case kOpDiv: {
			int32 b = popInt();
			int32 a = popInt();
			push(b ? a / b : 0x3FFFFFFF);
			break;
		}
		case kOpMod: {
			int32 b = popInt();
			int32 a = popInt();
			push(b ? a % b : 0);
			break;
		}
		case kOpNeg:
			push(-popInt());
			break;
		case kOpNot:
			push(popInt() == 0 ? 1 : 0);
			break;
		case kOpEq:
		case kOpNe:
		case kOpGe:
		case kOpGt:
		case kOpLe:
		case kOpLt: {
			Value b = pop();
			Value a = pop();
			int c = compareValues(a, b);
			bool r = false;
			switch (op) {
			case kOpEq: r = c == 0; break;
			case kOpNe: r = c != 0; break;
			case kOpGe: r = c >= 0; break;
			case kOpGt: r = c == 1; break;
			case kOpLe: r = c < 1; break;
			case kOpLt: r = c == -1; break;
			default: break;
			}
			push(r ? 1 : 0);
			break;
		}
		case kOpJumpIfZero: {
			uint32 base = _pc;
			int16 rel = readJump();
			if (popInt() == 0)
				_pc = base + rel;
			break;
		}
		case kOpJump: {
			uint32 base = _pc;
			_pc = base + readJump();
			break;
		}
		case kOpCall: {
			uint32 symbol = readVarint();
			uint32 argc = readVarint();
			opCall(symbol, argc);
			break;
		}
		case kOpReturnValue: {
			Value v = pop();
			if (!doReturn())
				_done = true;
			else
				push(v);
			break;
		}
		case kOpReturn:
			if (!doReturn())
				_done = true;
			break;
		case kOpExit:
			_stack.clear();
			_done = true;
			break;
		case kOpFunc:
			push(callBuiltin(readVarint()));
			break;
		case kOpProc:
			callBuiltin(readVarint());
			break;
		case kOpSet:
			setProperty(readVarint());
			break;
		case kOpConcat: {
			Common::String b = popString();
			Common::String a = popString();
			push(Value::string(a + b));
			break;
		}
		case kOpGo:
			opGo();
			break;
		case kOpNewList:
			push(Value::list());
			break;
		case kOpMember: {
			uint res = popMember();
			push((int32)(res ? res - kMemberBase : 0));
			break;
		}
		case kOpPushVoid:
			push(Value());
			break;
		case kOpPushOne:
			push(1);
			break;
		case kOpPushZero:
			push((int32)0);
			break;
		case kOpPushInt:
			push((int32)readVarint());
			break;
		case kOpPushString: {
			uint32 len = readVarint();
			if (_pc + len > _code.data->size()) {
				_done = true;
				break;
			}
			push(Value::string(Common::String((const char *)&(*_code.data)[_pc], len)));
			_pc += len;
			break;
		}
		case kOpGetParam:
		case kOpSetParam: {
			uint32 n = readVarint();
			int32 argc = _base >= 4 ? _stack[_base - 4].toInt() : 0;
			int slot = (int)_base - argc - 4 + n;
			if (slot < 0 || slot >= (int)_stack.size()) {
				warning("Script: bad parameter %d", n);
				if (op == kOpGetParam)
					push(Value());
				else
					pop();
				break;
			}
			if (op == kOpGetParam)
				push(_stack[slot]);
			else
				_stack[slot] = pop();
			break;
		}
		case kOpGetGlobal:
			push(global(symbolName(readVarint())));
			break;
		case kOpSetGlobal: {
			const Common::String &name = symbolName(readVarint());
			global(name) = pop();
			break;
		}
		case kOpGetProp: {
			const Common::String &name = symbolName(readVarint());
			Value obj = popPropList();
			push(obj.getProp(name.c_str()));
			break;
		}
		case kOpSetProp: {
			const Common::String &name = symbolName(readVarint());
			Value obj = popPropList();
			obj.setProp(name.c_str(), pop());
			break;
		}
		case kOpGetLocal: {
			uint32 slot = _base + readVarint();
			push(slot < _stack.size() ? _stack[slot] : Value());
			break;
		}
		case kOpSetLocal: {
			Value v = pop();
			uint32 slot = _base + readVarint();
			while (_stack.size() <= slot)
				push(Value());
			_stack[slot] = v;
			break;
		}
		case kOpNop:
			break;
		case kOpPopMember:
			popMember();
			popString();
			break;
		case kOpAppend: {
			Value v = pop();
			Value list = popList();
			list.obj->items.push_back(v);
			push(list);
			break;
		}
		case kOpIterate: {
			uint32 slot = _base + readVarint();
			Value list = slot < _stack.size() ? _stack[slot] : Value();
			if (!list.isList() || list.obj->items.empty()) {
				push((int32)0);
			} else {
				Value first = list.obj->items[0];
				list.obj->items.remove_at(0);
				push(first);
				push(1);
			}
			break;
		}
		case kOpNewXtra: {
			Common::String name = popString();
			if (name.equalsIgnoreCase("PrintOMatic"))
				push(Value(ObjectPtr(new Object(kObjXtra))));
			else
				push(Value());
			break;
		}
		case kOpIntersects: {
			int32 a = popInt();
			int32 b = popInt();
			push(_vm->spritesIntersect(b, a) ? 1 : 0);
			break;
		}
		default:
			warning("Unknown opcode %02x in %s script %d at %d", op, _code.movie->getName().c_str(), _code.res, at);
			break;
		}
	}
}

} // End of namespace BigTuna
