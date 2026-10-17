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

#ifndef MSPAM_SCRIPT_H
#define MSPAM_SCRIPT_H

#include "common/array.h"
#include "common/str.h"

namespace MSPAM {

/** Element properties of the title's expressions (MSBSOLAR.DLL FUN_1008_5c6e) */
enum Property {
	kPropPibList = 1,   ///< pi: the current path segment's PibList
	kPropSound = 2,     ///< so: the current path segment's sound
	kPropPathSeg = 3,   ///< pa: the current path segment, 0-based (also any unknown name)
	kPropStart = 4,     ///< st: start (non-zero) or stop; reads as 0
	kPropShow = 5,      ///< sh: visibility
	kPropX = 7,
	kPropY = 8,
	kPropZ = 9,
	kPropDuration = 10, ///< du: the current path segment's duration
	kPropNone = 11,     ///< reads as 0, can't be set
	kPropPlay = 12,     ///< pl: video playback
	kPropWidth = 13,    ///< xs
	kPropHeight = 14,   ///< ys (never produced by the original parser)
	kPropCursor = 15    ///< c: reads as 0, can't be set
};

/** The layout side of the expressions: elements of the current layout by name */
class ScriptHost {
public:
	virtual ~ScriptHost() {}
	virtual bool hasElement(const Common::String &name) = 0;
	virtual int32 getProperty(const Common::String &element, int prop) = 0;
	virtual void setProperty(const Common::String &element, int prop, int32 value) = 0;
};

/**
 * The title's state variables: strings by name in creation order. The GVar*
 * globals are only a naming convention; layouts free their own with Free State
 * Vars. Reading a missing variable creates it as "0".
 */
class Variables {
public:
	int find(const Common::String &name) const;
	const Common::String &get(const Common::String &name);
	void set(const Common::String &name, const Common::String &value);
	/** Free State Vars: a trailing '%' matches a prefix, an empty pattern frees all */
	void free(const Common::String &pattern);
	void clear() { _vars.clear(); }
	/** The bit set behind Eval: Rand number's uniqueness variable */
	uint32 &randomMask(const Common::String &name);

private:
	struct Var {
		Common::String name;
		Common::String value;
		uint32 mask;
	};
	Common::Array<Var> _vars;
};

/**
 * The expression language of MSBSOLAR.DLL (FUN_1008_4f44 executes, FUN_1008_4c3a
 * tests, FUN_1008_563a tokenizes). Expressions run strictly left to right: a
 * value replaces the accumulator, an operator (+ - * /) combines it with the next
 * value, and in a statement containing '=' the result is assigned to the first
 * token (an element property or a variable). ';' separates statements, and a
 * quote takes the rest of the whole expression as a string.
 */
class Evaluator {
public:
	Evaluator(Variables &vars, ScriptHost &host) : _vars(vars), _host(host) {}

	/** Eval: Expr; returns the value of the last statement */
	int32 execute(const Common::String &expr);
	/** The If of Eval: If Then Else: the last comparison in the expression */
	bool test(const Common::String &expr);

	/** A variable's value read as a number, like the original (letters count from '0', '-' reads 0) */
	static int32 parseNumber(const char *&p);

private:
	enum Token {
		kTokNumber = 1,
		kTokProperty = 2,
		kTokVariable = 3,
		kTokOperator = 4,
		kTokCompare = 5,
		kTokAssign = 6,
		kTokEnd = 7,
		kTokString = 8,
		kTokSeparator = 9
	};

	struct TokenInfo {
		Token type;
		const char *start;
		int32 value;
		bool hasValue;
		Common::String element;
		int prop;
		Common::String variable;
	};

	void next(const char *&p, TokenInfo &tok);
	static int propertyCode(const char *p);
	static int32 apply(char op, int32 a, int32 b);

	Variables &_vars;
	ScriptHost &_host;
};

} // End of namespace MSPAM

#endif // MSPAM_SCRIPT_H
