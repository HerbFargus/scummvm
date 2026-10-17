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

#ifndef BIGTUNA_SCRIPT_H
#define BIGTUNA_SCRIPT_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"

#include "bigtuna/value.h"

namespace BigTuna {

class BigTunaEngine;
class Movie;
struct Sprite;

/**
 * The Implay interpreter (FUN_00408c30): a stack machine running compiled
 * Lingo. A script resource is a chain of handlers, each { u32 event symbol,
 * u32 next handler offset, code }. Movie handlers are found through the
 * symbol table, whose names are also the global variables.
 */
class Script {
public:
	Script(BigTunaEngine *vm);

	// FUN_00408170: runs the handler for an event in a sprite or frame script
	bool runEvent(const Common::String &event, uint res);
	// FUN_004080e0: runs a movie handler by name
	bool runMovieHandler(const Common::String &name);

	void clearGlobals();
	bool isRunning() const { return _running; }
	// Stops the running script (a go to another movie)
	void stop() { _stopRequested = true; }

	bool _trace;

private:
	struct Code {
		Movie *movie;
		uint res;
		const Common::Array<byte> *data;
	};

	void execute(Movie *movie, uint res, uint32 pc);
	void run();

	byte readByte();
	uint32 readVarint();
	int16 readJump();
	bool loadCode(Movie *movie, uint res);

	void push(const Value &v);
	Value pop();
	int32 popInt() { return pop().toInt(); }
	Common::String popString();
	Value popList();
	Value popPropList();
	const Value &top(int depth = 0) const;
	// Object type of a stack value counted from the top, 5 for non-objects
	int topType(int depth = 0) const;
	// FUN_00408780: a member name or number; returns the resource number or 0
	uint popMember();

	const Common::String &symbolName(uint32 id) const;
	Value &global(const Common::String &name);

	void opCall(uint32 symbol, uint32 argc);
	// FUN_00408680: false when the outermost handler returned
	bool doReturn();
	void opGo();

	Value callBuiltin(uint32 id);
	void setProperty(uint32 id);
	Sprite &spriteArg(int32 channel);

	BigTunaEngine *_vm;
	Common::HashMap<Common::String, Value, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _globals;

	Common::Array<Value> _stack;
	uint _base;
	Code _code;
	uint32 _pc;
	bool _running;
	bool _stopRequested;
	bool _done;
};

} // End of namespace BigTuna

#endif // BIGTUNA_SCRIPT_H
