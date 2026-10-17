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

#ifndef YAGA_PYTHON_AST_H
#define YAGA_PYTHON_AST_H

#include "common/array.h"
#include "common/str.h"

#include "yaga/python/object.h"

// The syntax tree the compiler's parser builds

namespace Yaga {
namespace Python {

enum NodeKind {
	// Statements
	kNodeModule, kNodeFunctionDef, kNodeClassDef, kNodeReturn, kNodeYield, kNodeDelete,
	kNodeAssign, kNodeAugAssign, kNodePrint, kNodeFor, kNodeWhile, kNodeIf, kNodeRaise,
	kNodeTryExcept, kNodeExceptHandler, kNodeTryFinally, kNodeAssert, kNodeImport,
	kNodeImportFrom, kNodeAlias, kNodeExec, kNodeGlobal, kNodeExprStmt, kNodePass,
	kNodeBreak, kNodeContinue, kNodeSuite,
	// Expressions
	kNodeBoolOp, kNodeBinOp, kNodeUnaryOp, kNodeLambda, kNodeCompare, kNodeCall,
	kNodeKeyword, kNodeRepr, kNodeConst, kNodeName, kNodeAttribute, kNodeSubscript,
	kNodeSlice, kNodeSliceObj, kNodeEllipsis, kNodeTuple, kNodeList, kNodeDict,
	kNodeListComp, kNodeCompFor, kNodeCompIf,
	// Function parameters
	kNodeParams, kNodeParam
};

// Operators, as the code generator needs them
enum {
	kOpAnd, kOpOr,
	kOpAdd, kOpSub, kOpMul, kOpDiv, kOpFloorDiv, kOpMod, kOpPow, kOpLshift, kOpRshift,
	kOpBitAnd, kOpBitXor, kOpBitOr,
	kOpNot, kOpNeg, kOpPos, kOpInvert
};

struct Node {
	NodeKind kind;
	int line;
	int op;                  // operator, or a flag
	Common::String str;      // names
	ObjRef value;            // constants
	Common::Array<Node *> kids;
	// Extra children for nodes that need named parts (may be null)
	Node *a, *b, *c, *d;

	Node(NodeKind k, int l) : kind(k), line(l), op(0), a(nullptr), b(nullptr), c(nullptr), d(nullptr) {}
	~Node() {
		for (uint i = 0; i < kids.size(); i++)
			delete kids[i];
		delete a;
		delete b;
		delete c;
		delete d;
	}
	void add(Node *n) { kids.push_back(n); }
};

// Node layouts:
//  Module: kids = statements
//  FunctionDef: str = name, a = Params, b = Suite
//  Lambda: a = Params, b = expression
//  Params: kids = Param (with b = default value or null); op: 1 = *args, 2 = **kwargs (str of the Param)
//  Param: str = name, or kids = nested Params for tuple parameters (op = 1)
//  ClassDef: str = name, kids = bases, b = Suite
//  Return: a = value or null; Yield: a = value
//  Delete: kids = targets
//  Assign: kids = targets, a = value
//  AugAssign: a = target, op = operator, b = value
//  Print: a = destination or null, kids = values, op = 1 if it ends with a newline
//  For: a = target, b = iterable, c = body, d = else or null
//  While: a = test, c = body, d = else
//  If: a = test, c = body, d = else (an If for elif, or a Suite)
//  Raise: a, b, c = type, value, traceback (each may be null)
//  TryExcept: a = body, kids = ExceptHandler, d = else
//  ExceptHandler: a = type or null, b = target or null, c = body
//  TryFinally: a = body, b = final body
//  Assert: a = test, b = message
//  Import: kids = Alias (str = dotted name, value = as-name or null)
//  ImportFrom: str = module, kids = Alias, op = 1 for "import *"
//  Exec: a = code, b = globals, c = locals
//  Global: kids = Name
//  ExprStmt: a = expression
//  Suite: kids = statements
//  BoolOp: op = kOpAnd/kOpOr, kids = values
//  BinOp: op, a, b; UnaryOp: op, a
//  Compare: a = left, kids = comparators, each with op = the comparison (COMPARE_OP numbering)
//  Call: a = function, kids = positional, then Keyword (str = name, a = value); b = *args, c = **kwargs
//  Repr: a; Const: value; Name: str
//  Attribute: a = object, str = name
//  Subscript: a = object, b = index (Slice/SliceObj/Ellipsis/expression/Tuple of those)
//  Slice: a = lower or null, b = upper or null (a simple slice)
//  SliceObj: a, b, c = start, stop, step (each may be null)
//  Tuple, List: kids; Dict: kids = key, value, key, value...
//  ListComp: a = element, kids = CompFor/CompIf in order
//  CompFor: a = target, b = iterable; CompIf: a = test

} // End of namespace Python
} // End of namespace Yaga

#endif // YAGA_PYTHON_AST_H
