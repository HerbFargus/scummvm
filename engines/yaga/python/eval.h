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

#ifndef YAGA_PYTHON_EVAL_H
#define YAGA_PYTHON_EVAL_H

#include "yaga/python/object.h"

namespace Yaga {
namespace Python {

enum { kMaxBlocks = 20 };

struct Block {
	int type;    // the opcode that set it up
	int handler; // where to jump to
	int level;   // value stack level to pop to
};

struct Frame : public Object {
	Ref<Frame> _back;
	Ref<Code> _code;
	Ref<Dict> _globals;
	ObjRef _locals;
	Ref<Dict> _builtins;
	Common::Array<Object *> _fast; // fast locals (owned references, may be null)
	Object **_stack;               // value stack (owned references)
	int _stackTop;
	Block _blocks[kMaxBlocks];
	int _blockCount;
	int32 _lasti;
	int32 _lineno;
	// The exception being handled when this frame started handling one
	ObjRef _excType, _excValue, _excTraceback;
	bool _savedExc;

	Frame(Code *code, Dict *globals, Object *locals, Frame *back);
	~Frame() override;
	void clearLocals();
};

// The interpreter state
struct ThreadState {
	Frame *frame;
	int recursionDepth;
	int recursionLimit;
	ObjRef curExcType, curExcValue, curExcTraceback; // the pending error
	ObjRef excType, excValue, excTraceback;          // sys.exc_info()
	Ref<Dict> modules;
	Ref<Dict> sysDict;
	Ref<Dict> builtins;
	Ref<Dict> codecSearchPath;
	ThreadState();
};

extern ThreadState *g_ts;

ObjRef evalFrame(Frame *f);
void fastToLocals(Frame *f);
void localsToFast(Frame *f, bool clear);

} // End of namespace Python
} // End of namespace Yaga

#endif // YAGA_PYTHON_EVAL_H
