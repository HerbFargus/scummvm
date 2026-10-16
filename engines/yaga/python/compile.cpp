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

#include "common/hashmap.h"
#include "common/hash-str.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/ast.h"
#include "yaga/python/opcode.h"
#include "yaga/python/parser.h"

// The code generator: turns the parser's tree into Python 2.2 bytecode, in the
// shapes CPython 2.2's compile.c emits (including SET_LINENO and nested
// scopes). The games ship most of their code compiled; this is for the few
// scripts that come as source, and for exec and eval.

namespace Yaga {
namespace Python {

namespace {

enum ScopeKind {
	kScopeModule, kScopeClass, kScopeFunction
};

enum {
	kSymLocal = 1,     // bound here
	kSymGlobal = 2,    // declared global
	kSymParam = 4,
	kSymUsed = 8,
	kSymFree = 16,     // comes from an enclosing function
	kSymCell = 32,     // used by a nested function
	kSymImplicitGlobal = 64
};

struct Scope {
	ScopeKind kind;
	Common::String name;
	Scope *parent;
	Node *node;
	Common::HashMap<Common::String, int> symbols;
	Common::Array<Common::String> order; // names in order of first appearance
	Common::Array<Common::String> params;
	Common::Array<Common::String> freeNames, cellNames;
	Common::Array<Scope *> children;
	bool hasExec, hasImportStar, isGenerator;
	Common::String className; // the enclosing class, for name mangling

	Scope(ScopeKind k, const Common::String &n, Scope *p, Node *nd) : kind(k), name(n), parent(p), node(nd),
		hasExec(false), hasImportStar(false), isGenerator(false) {}
	~Scope() {
		for (uint i = 0; i < children.size(); i++)
			delete children[i];
	}
	void mark(const Common::String &sym, int f) {
		if (!symbols.contains(sym)) {
			symbols[sym] = 0;
			order.push_back(sym);
		}
		symbols[sym] |= f;
	}
	int flags(const Common::String &sym) const {
		return symbols.contains(sym) ? symbols[sym] : 0;
	}

	bool optimized() const {
		return kind == kScopeFunction && !hasExec && !hasImportStar;
	}
};

Common::String mangle(const Common::String &className, const Common::String &name) {
	if (className.empty() || !name.hasPrefix("__") || name.hasSuffix("__") || name.contains('.'))
		return name;
	const char *p = className.c_str();
	while (*p == '_')
		p++;
	if (!*p)
		return name;
	return "_" + Common::String(p) + name;
}

// ---- Symbol tables ------------------------------------------------------------------

class SymbolBuilder {
public:
	explicit SymbolBuilder(Scope *top) : _scope(top), _tmpCount(0) {}

	void visitSuite(Node *n) {
		for (uint i = 0; i < n->kids.size(); i++)
			visitStmt(n->kids[i]);
	}

	void store(Node *t) {
		switch (t->kind) {
		case kNodeName:
			bind(t->str);
			break;
		case kNodeTuple:
		case kNodeList:
			for (uint i = 0; i < t->kids.size(); i++)
				store(t->kids[i]);
			break;
		case kNodeAttribute:
			visitExpr(t->a);
			break;
		case kNodeSubscript:
			visitExpr(t->a);
			visitExpr(t->b);
			break;
		default:
			visitExpr(t);
			break;
		}
	}

	void bind(const Common::String &rawName) {
		Common::String name = mangle(_scope->className, rawName);
		_scope->mark(name, kSymLocal);
	}

	void use(const Common::String &rawName) {
		Common::String name = mangle(_scope->className, rawName);
		_scope->mark(name, kSymUsed);
	}

	void visitParams(Node *params, Scope *fs, int &tupleIndex) {
		for (uint i = 0; i < params->kids.size(); i++) {
			Node *p = params->kids[i];
			if (p->op == 1) {
				// A tuple parameter gets a hidden name, then its parts are locals
				Common::String hidden = Common::String::format(".%d", i);
				fs->params.push_back(hidden);
				fs->mark(hidden, kSymLocal | kSymParam);
				storeParamTuple(p, fs);
			} else {
				fs->params.push_back(p->str);
				fs->mark(p->str, kSymLocal | kSymParam);
			}
		}
		(void)tupleIndex;
	}

	void storeParamTuple(Node *p, Scope *fs) {
		for (uint i = 0; i < p->kids.size(); i++) {
			if (p->kids[i]->op == 1)
				storeParamTuple(p->kids[i], fs);
			else
				fs->mark(p->kids[i]->str, kSymLocal);
		}
	}

	Scope *newFunction(Node *n, const Common::String &name, Node *params) {
		Scope *fs = new Scope(kScopeFunction, name, _scope, n);
		fs->className = _scope->kind == kScopeClass ? _scope->name : _scope->className;
		_scope->children.push_back(fs);
		// Defaults are evaluated in the enclosing scope
		for (uint i = 0; i < params->kids.size(); i++)
			if (params->kids[i]->b)
				visitExpr(params->kids[i]->b);
		int ti = 0;
		visitParams(params, fs, ti);
		return fs;
	}

	void visitStmt(Node *n) {
		switch (n->kind) {
		case kNodeFunctionDef: {
			bind(n->str);
			Scope *fs = newFunction(n, n->str, n->a);
			Scope *saved = _scope;
			_scope = fs;
			visitSuite(n->b);
			_scope = saved;
			break;
		}
		case kNodeClassDef: {
			bind(n->str);
			for (uint i = 0; i < n->kids.size(); i++)
				visitExpr(n->kids[i]);
			Scope *cs = new Scope(kScopeClass, n->str, _scope, n);
			cs->className = n->str;
			_scope->children.push_back(cs);
			Scope *saved = _scope;
			_scope = cs;
			visitSuite(n->b);
			_scope = saved;
			break;
		}
		case kNodeReturn:
			if (n->a)
				visitExpr(n->a);
			break;
		case kNodeYield:
			_scope->isGenerator = true;
			visitExpr(n->a);
			break;
		case kNodeDelete:
			for (uint i = 0; i < n->kids.size(); i++)
				store(n->kids[i]);
			break;
		case kNodeAssign:
			visitExpr(n->a);
			for (uint i = 0; i < n->kids.size(); i++)
				store(n->kids[i]);
			break;
		case kNodeAugAssign:
			visitExpr(n->b);
			if (n->a->kind == kNodeName) {
				use(n->a->str);
				bind(n->a->str);
			} else {
				store(n->a);
			}
			break;
		case kNodePrint:
			if (n->a)
				visitExpr(n->a);
			for (uint i = 0; i < n->kids.size(); i++)
				visitExpr(n->kids[i]);
			break;
		case kNodeFor:
			visitExpr(n->b);
			store(n->a);
			visitSuite(n->c);
			if (n->d)
				visitSuite(n->d);
			break;
		case kNodeWhile:
			visitExpr(n->a);
			visitSuite(n->c);
			if (n->d)
				visitSuite(n->d);
			break;
		case kNodeIf:
			visitExpr(n->a);
			visitSuite(n->c);
			if (n->d) {
				if (n->d->kind == kNodeIf)
					visitStmt(n->d);
				else
					visitSuite(n->d);
			}
			break;
		case kNodeRaise:
			if (n->a) visitExpr(n->a);
			if (n->b) visitExpr(n->b);
			if (n->c) visitExpr(n->c);
			break;
		case kNodeTryExcept:
			visitSuite(n->a);
			for (uint i = 0; i < n->kids.size(); i++) {
				Node *h = n->kids[i];
				if (h->a)
					visitExpr(h->a);
				if (h->b)
					store(h->b);
				visitSuite(h->c);
			}
			if (n->d)
				visitSuite(n->d);
			break;
		case kNodeTryFinally:
			visitSuite(n->a);
			visitSuite(n->b);
			break;
		case kNodeAssert:
			use("__debug__");
			use("AssertionError");
			visitExpr(n->a);
			if (n->b)
				visitExpr(n->b);
			break;
		case kNodeImport:
			for (uint i = 0; i < n->kids.size(); i++) {
				Node *al = n->kids[i];
				if (al->value) {
					bind(((Str *)al->value.get())->toString());
				} else {
					Common::String first = al->str;
					const char *dot = strchr(first.c_str(), '.');
					if (dot)
						first = Common::String(first.c_str(), dot - first.c_str());
					bind(first);
				}
			}
			break;
		case kNodeImportFrom:
			if (n->op == 1) {
				_scope->hasImportStar = true;
			} else {
				for (uint i = 0; i < n->kids.size(); i++) {
					Node *al = n->kids[i];
					bind(al->value ? ((Str *)al->value.get())->toString() : al->str);
				}
			}
			break;
		case kNodeExec:
			_scope->hasExec = true;
			visitExpr(n->a);
			if (n->b) visitExpr(n->b);
			if (n->c) visitExpr(n->c);
			break;
		case kNodeGlobal:
			for (uint i = 0; i < n->kids.size(); i++)
				_scope->mark(n->kids[i]->str, kSymGlobal);
			break;
		case kNodeExprStmt:
			visitExpr(n->a);
			break;
		case kNodeSuite:
			visitSuite(n);
			break;
		default:
			break;
		}
	}

	void visitExpr(Node *n) {
		if (!n)
			return;
		switch (n->kind) {
		case kNodeName:
			use(n->str);
			break;
		case kNodeLambda: {
			Scope *fs = newFunction(n, "<lambda>", n->a);
			Scope *saved = _scope;
			_scope = fs;
			visitExpr(n->b);
			_scope = saved;
			break;
		}
		case kNodeListComp: {
			// The hidden list and the loop variables belong to this scope
			_tmpCount++;
			bind(Common::String::format("_[%d]", _tmpCount));
			for (uint i = 0; i < n->kids.size(); i++) {
				Node *k = n->kids[i];
				if (k->kind == kNodeCompFor) {
					visitExpr(k->b);
					store(k->a);
				} else {
					visitExpr(k->a);
				}
			}
			visitExpr(n->a);
			break;
		}
		case kNodeKeyword:
			visitExpr(n->a);
			break;
		case kNodeCall:
			// The same order the code generator evaluates in
			visitExpr(n->a);
			for (uint i = 0; i < n->kids.size(); i++)
				visitExpr(n->kids[i]);
			visitExpr(n->b);
			visitExpr(n->c);
			break;
		case kNodeDict:
			for (uint i = 0; i + 1 < n->kids.size(); i += 2) {
				visitExpr(n->kids[i + 1]);
				visitExpr(n->kids[i]);
			}
			break;
		default:
			if (n->a) visitExpr(n->a);
			if (n->b) visitExpr(n->b);
			if (n->c) visitExpr(n->c);
			if (n->d) visitExpr(n->d);
			for (uint i = 0; i < n->kids.size(); i++)
				visitExpr(n->kids[i]);
			break;
		}
	}

	int tmpCount() const { return _tmpCount; }

private:
	Scope *_scope;
	int _tmpCount;
};

// Decides which names are free and which are cells, as CPython 2.2's symtable
static bool boundInEnclosingFunction(Scope *s, const Common::String &name) {
	for (Scope *p = s->parent; p; p = p->parent) {
		if (p->kind != kScopeFunction)
			continue;
		int f = p->flags(name);
		if (f & kSymGlobal)
			return false;
		if (f & (kSymLocal | kSymFree))
			return true;
	}
	return false;
}

static void addUnique(Common::Array<Common::String> &list, const Common::String &name) {
	for (uint i = 0; i < list.size(); i++)
		if (list[i] == name)
			return;
	list.push_back(name);
}

static void resolveScope(Scope *s) {
	for (uint i = 0; i < s->children.size(); i++)
		resolveScope(s->children[i]);
	if (s->kind == kScopeModule)
		return;
	for (uint i = 0; i < s->order.size(); i++) {
		const Common::String &name = s->order[i];
		int f = s->flags(name);
		if (f & kSymGlobal)
			continue;
		bool local = (f & kSymLocal) != 0 && s->kind == kScopeFunction;
		if (local)
			continue;
		if ((f & (kSymUsed | kSymFree)) && !(s->kind == kScopeClass && (f & kSymLocal)) && boundInEnclosingFunction(s, name)) {
			s->symbols[name] |= kSymFree;
			addUnique(s->freeNames, name);
			// Mark the defining function's variable as a cell, and pass it through those between
			for (Scope *p = s->parent; p; p = p->parent) {
				int pf = p->flags(name);
				if (p->kind == kScopeFunction && (pf & kSymLocal)) {
					p->symbols[name] |= kSymCell;
					addUnique(p->cellNames, name);
					break;
				}
				// Functions and classes in between pass the variable through
				p->mark(name, kSymFree);
				addUnique(p->freeNames, name);
			}
		} else if (s->kind == kScopeFunction && (f & kSymUsed)) {
			s->symbols[name] |= kSymImplicitGlobal;
		}
	}
}

// ---- Code generation ----------------------------------------------------------------

struct Label {
	int pos;
	Common::Array<int> refs; // offsets of jump arguments to patch
	Common::Array<bool> absolute;
	Label() : pos(-1) {}
};

enum BlockKind {
	kBlockLoop, kBlockExcept, kBlockFinallyTry, kBlockFinallyEnd
};

struct LoopBlock {
	BlockKind kind;
	int continueLabel;
};

class CodeGen {
public:
	CodeGen(Scope *scope, const char *filename, int firstLine, int *tmpCounter) : _scope(scope), _filename(filename),
		_firstLine(firstLine), _lastLine(firstLine), _lastLineAddr(0), _tmpCounter(tmpCounter), _failed(false), _interactive(false) {
		if (scope->kind == kScopeFunction) {
			for (uint i = 0; i < scope->params.size(); i++)
				_varnames.push_back(scope->params[i]);
			if (scope->optimized() || true) {
				for (uint i = 0; i < scope->order.size(); i++) {
					const Common::String &name = scope->order[i];
					int f = scope->flags(name);
					if ((f & kSymLocal) && !(f & kSymGlobal) && !(f & kSymParam))
						addUnique(_varnames, name);
				}
			}
		}
	}

	bool failed() const { return _failed; }
	void setInteractive(bool i) { _interactive = i; }

	ObjRef makeCode(Node *bodyNode, int kind);

	// Instruction emission
	int pos() const { return _code.size(); }
	void emit(int op) { _code.push_back((uint8)op); }
	void emitArg(int op, int arg) {
		if (arg > 0xffff) {
			_code.push_back(kExtendedArg);
			_code.push_back((arg >> 16) & 0xff);
			_code.push_back((arg >> 24) & 0xff);
		}
		_code.push_back((uint8)op);
		_code.push_back(arg & 0xff);
		_code.push_back((arg >> 8) & 0xff);
	}
	int newLabel() {
		_labels.push_back(Label());
		return _labels.size() - 1;
	}
	void bind(int l) { _labels[l].pos = pos(); }
	void jump(int op, int l) {
		_code.push_back((uint8)op);
		_labels[l].refs.push_back(pos());
		_labels[l].absolute.push_back(op == kJumpAbsolute || op == kContinueLoop);
		_code.push_back(0);
		_code.push_back(0);
	}
	void setLine(int line) {
		if (line <= 0)
			return;
		emitArg(kSetLineno, line);
		addLnotab(line);
	}

	int addConst(Object *v);
	int addName(const Common::String &name);
	int varIndex(const Common::String &name);
	void error(int line, const char *msg) {
		if (!_failed) {
			_failed = true;
			Ref<Tuple> info = tuplePack(4, newStr(_filename).get(), newInt(line).get(), g_none, g_none);
			Ref<Tuple> args = tuplePack(2, newStr(msg).get(), info.get());
			setError(g_excSyntaxError, args.get());
		}
	}

	void stmt(Node *n);
	void suite(Node *n);
	void expr(Node *n);
	void nameOp(const Common::String &rawName, int ctx); // 0 load, 1 store, 2 delete
	void store(Node *target);
	void del(Node *target, int line);
	void call(Node *n);
	void compare(Node *n);
	void boolOp(Node *n);
	void listComp(Node *n);
	void listCompIter(Node *n, uint index, const Common::String &tmp);
	void function(Node *n, const Common::String &name, Node *params, Node *body, bool isLambda);
	void classDef(Node *n);
	void makeClosure(Scope *child, ObjRef code, int ndefaults);
	void subscript(Node *n, int ctx);
	void sliceIndex(Node *n);
	int binaryOpcode(int op, bool inplace);

	Scope *_scope;
	const char *_filename;
	int _firstLine, _lastLine, _lastLineAddr;
	int *_tmpCounter;
	bool _failed;
	bool _interactive;
	Common::Array<uint8> _code;
	Common::Array<uint8> _lnotab;
	Common::Array<Label> _labels;
	Common::Array<ObjRef> _consts;
	Common::Array<Common::String> _names;
	Common::Array<Common::String> _varnames;
	Common::Array<LoopBlock> _blocks;
	uint _childIndex = 0;
	// For the stack depth pass: END_FINALLYs that end an except chain (they
	// only re-raise), and those of try/finally with the SETUP_FINALLY they close
	Common::Array<int> _exceptEnds;
	Common::HashMap<int, int> _finallyEnds;

	void addLnotab(int line) {
		int addrDelta = pos() - _lastLineAddr;
		int lineDelta = line - _lastLine;
		if (lineDelta <= 0 && addrDelta == 0)
			return;
		if (lineDelta < 0)
			return;
		while (addrDelta > 255) {
			_lnotab.push_back(255);
			_lnotab.push_back(0);
			addrDelta -= 255;
		}
		while (lineDelta > 255) {
			_lnotab.push_back(addrDelta);
			_lnotab.push_back(255);
			addrDelta = 0;
			lineDelta -= 255;
		}
		_lnotab.push_back(addrDelta);
		_lnotab.push_back(lineDelta);
		_lastLine = line;
		_lastLineAddr = pos();
	}

	Scope *nextChild() {
		if (_childIndex < _scope->children.size())
			return _scope->children[_childIndex++];
		return nullptr;
	}
};

int CodeGen::addConst(Object *v) {
	for (uint i = 0; i < _consts.size(); i++) {
		Object *c = _consts[i].get();
		if (c->_type != v->_type)
			continue;
		if (c == v)
			return i;
		if (isType(v, &g_codeType))
			continue;
		int eq = richCompareBool(c, v, kEQ);
		if (eq < 0) {
			clearError();
			continue;
		}
		if (eq)
			return i;
	}
	_consts.push_back(ObjRef(v));
	return _consts.size() - 1;
}

int CodeGen::addName(const Common::String &name) {
	for (uint i = 0; i < _names.size(); i++)
		if (_names[i] == name)
			return i;
	_names.push_back(name);
	return _names.size() - 1;
}

int CodeGen::varIndex(const Common::String &name) {
	for (uint i = 0; i < _varnames.size(); i++)
		if (_varnames[i] == name)
			return i;
	_varnames.push_back(name);
	return _varnames.size() - 1;
}

void CodeGen::nameOp(const Common::String &rawName, int ctx) {
	Common::String name = mangle(_scope->className, rawName);
	int f = _scope->flags(name);
	static const int nameOps[3] = { kLoadName, kStoreName, kDeleteName };
	static const int fastOps[3] = { kLoadFast, kStoreFast, kDeleteFast };
	static const int globalOps[3] = { kLoadGlobal, kStoreGlobal, kDeleteGlobal };
	static const int derefOps[3] = { kLoadDeref, kStoreDeref, kLoadDeref };
	if (f & kSymGlobal) {
		emitArg(globalOps[ctx], addName(name));
		return;
	}
	if ((f & (kSymCell | kSymFree)) && _scope->kind != kScopeModule) {
		int idx = -1;
		for (uint i = 0; i < _scope->cellNames.size(); i++)
			if (_scope->cellNames[i] == name)
				idx = i;
		if (idx < 0) {
			for (uint i = 0; i < _scope->freeNames.size(); i++)
				if (_scope->freeNames[i] == name)
					idx = _scope->cellNames.size() + i;
		}
		if (idx >= 0 && !(_scope->kind == kScopeClass && (f & kSymLocal))) {
			if (ctx == 2) {
				error(_lastLine, "can not delete variable referenced in nested scope");
				return;
			}
			emitArg(derefOps[ctx], idx);
			return;
		}
	}
	if (_scope->kind == kScopeFunction) {
		if (_scope->optimized()) {
			if (f & kSymLocal) {
				emitArg(fastOps[ctx], varIndex(name));
				return;
			}
			emitArg(globalOps[ctx], addName(name));
			return;
		}
		// exec or import * in the function: names are looked up by name
		emitArg(nameOps[ctx], addName(name));
		return;
	}
	emitArg(nameOps[ctx], addName(name));
}

int CodeGen::binaryOpcode(int op, bool inplace) {
	switch (op) {
	case kOpAdd: return inplace ? kInplaceAdd : kBinaryAdd;
	case kOpSub: return inplace ? kInplaceSubtract : kBinarySubtract;
	case kOpMul: return inplace ? kInplaceMultiply : kBinaryMultiply;
	case kOpDiv: return inplace ? kInplaceDivide : kBinaryDivide;
	case kOpFloorDiv: return inplace ? kInplaceFloorDivide : kBinaryFloorDivide;
	case kOpMod: return inplace ? kInplaceModulo : kBinaryModulo;
	case kOpPow: return inplace ? kInplacePower : kBinaryPower;
	case kOpLshift: return inplace ? kInplaceLshift : kBinaryLshift;
	case kOpRshift: return inplace ? kInplaceRshift : kBinaryRshift;
	case kOpBitAnd: return inplace ? kInplaceAnd : kBinaryAnd;
	case kOpBitXor: return inplace ? kInplaceXor : kBinaryXor;
	case kOpBitOr: return inplace ? kInplaceOr : kBinaryOr;
	default: return kStopCode;
	}
}

void CodeGen::sliceIndex(Node *n) {
	if (n)
		expr(n);
	else
		emitArg(kLoadConst, addConst(g_none));
}

// Loads (ctx 0) the subscript part of an expression already on the stack
void CodeGen::subscript(Node *n, int ctx) {
	Node *idx = n->b;
	expr(n->a);
	if (idx->kind == kNodeSlice) {
		int which = (idx->a ? 1 : 0) | (idx->b ? 2 : 0);
		if (idx->a)
			expr(idx->a);
		if (idx->b)
			expr(idx->b);
		emit((ctx == 0 ? kSlice : ctx == 1 ? kStoreSlice : kDeleteSlice) + which);
		return;
	}
	if (idx->kind == kNodeSliceObj) {
		sliceIndex(idx->a);
		sliceIndex(idx->b);
		if (idx->c || idx->op) {
			sliceIndex(idx->c);
			emitArg(kBuildSlice, 3);
		} else {
			emitArg(kBuildSlice, 2);
		}
	} else if (idx->kind == kNodeEllipsis) {
		emitArg(kLoadConst, addConst(g_ellipsis));
	} else if (idx->kind == kNodeTuple) {
		for (uint i = 0; i < idx->kids.size(); i++) {
			Node *k = idx->kids[i];
			if (k->kind == kNodeSlice || k->kind == kNodeSliceObj) {
				sliceIndex(k->a);
				sliceIndex(k->b);
				if (k->kind == kNodeSliceObj && (k->c || k->op)) {
					sliceIndex(k->c);
					emitArg(kBuildSlice, 3);
				} else {
					emitArg(kBuildSlice, 2);
				}
			} else if (k->kind == kNodeEllipsis) {
				emitArg(kLoadConst, addConst(g_ellipsis));
			} else {
				expr(k);
			}
		}
		emitArg(kBuildTuple, idx->kids.size());
	} else {
		expr(idx);
	}
	emit(ctx == 0 ? kBinarySubscr : ctx == 1 ? kStoreSubscr : kDeleteSubscr);
}

void CodeGen::store(Node *t) {
	switch (t->kind) {
	case kNodeName:
		nameOp(t->str, 1);
		break;
	case kNodeAttribute:
		expr(t->a);
		emitArg(kStoreAttr, addName(mangle(_scope->className, t->str)));
		break;
	case kNodeSubscript:
		subscript(t, 1);
		break;
	case kNodeTuple:
	case kNodeList:
		emitArg(kUnpackSequence, t->kids.size());
		for (uint i = 0; i < t->kids.size(); i++)
			store(t->kids[i]);
		break;
	default:
		error(t->line, "can't assign to that");
		break;
	}
}

void CodeGen::del(Node *t, int line) {
	switch (t->kind) {
	case kNodeName:
		nameOp(t->str, 2);
		break;
	case kNodeAttribute:
		expr(t->a);
		emitArg(kDeleteAttr, addName(mangle(_scope->className, t->str)));
		break;
	case kNodeSubscript:
		subscript(t, 2);
		break;
	case kNodeTuple:
	case kNodeList:
		for (uint i = 0; i < t->kids.size(); i++)
			del(t->kids[i], line);
		break;
	default:
		error(line, "can't delete that");
		break;
	}
}

void CodeGen::boolOp(Node *n) {
	int end = newLabel();
	for (uint i = 0; i < n->kids.size(); i++) {
		expr(n->kids[i]);
		if (i + 1 < n->kids.size()) {
			jump(n->op == kOpAnd ? kJumpIfFalse : kJumpIfTrue, end);
			emit(kPopTop);
		}
	}
	bind(end);
}

void CodeGen::compare(Node *n) {
	expr(n->a);
	if (n->kids.size() == 1) {
		expr(n->kids[0]->a);
		emitArg(kCompareOp, n->kids[0]->op);
		return;
	}
	int cleanup = newLabel();
	for (uint i = 0; i < n->kids.size(); i++) {
		expr(n->kids[i]->a);
		if (i + 1 < n->kids.size()) {
			emit(kDupTop);
			emit(kRotThree);
			emitArg(kCompareOp, n->kids[i]->op);
			jump(kJumpIfFalse, cleanup);
			emit(kPopTop);
		} else {
			emitArg(kCompareOp, n->kids[i]->op);
		}
	}
	int end = newLabel();
	jump(kJumpForward, end);
	bind(cleanup);
	emit(kRotTwo);
	emit(kPopTop);
	bind(end);
}

void CodeGen::call(Node *n) {
	expr(n->a);
	int na = 0, nk = 0;
	for (uint i = 0; i < n->kids.size(); i++) {
		Node *k = n->kids[i];
		if (k->kind == kNodeKeyword) {
			emitArg(kLoadConst, addConst(internStr(k->str.c_str()).get()));
			expr(k->a);
			nk++;
		} else {
			expr(k);
			na++;
		}
	}
	int op = kCallFunction;
	if (n->b) {
		expr(n->b);
		op = kCallFunctionVar;
	}
	if (n->c) {
		expr(n->c);
		op = n->b ? kCallFunctionVarKw : kCallFunctionKw;
	}
	emitArg(op, na | (nk << 8));
}

void CodeGen::listCompIter(Node *n, uint index, const Common::String &tmp) {
	if (index >= n->kids.size()) {
		nameOp(tmp, 0);
		expr(n->a);
		emitArg(kCallFunction, 1);
		emit(kPopTop);
		return;
	}
	Node *k = n->kids[index];
	if (k->kind == kNodeCompFor) {
		expr(k->b);
		emit(kGetIter);
		int start = newLabel(), exit = newLabel();
		bind(start);
		setLine(k->line);
		jump(kForIter, exit);
		store(k->a);
		listCompIter(n, index + 1, tmp);
		jump(kJumpAbsolute, start);
		bind(exit);
	} else {
		expr(k->a);
		int anchor = newLabel(), after = newLabel();
		jump(kJumpIfFalse, anchor);
		emit(kPopTop);
		listCompIter(n, index + 1, tmp);
		jump(kJumpForward, after);
		bind(anchor);
		emit(kPopTop);
		bind(after);
	}
}

void CodeGen::listComp(Node *n) {
	(*_tmpCounter)++;
	Common::String tmp = Common::String::format("_[%d]", *_tmpCounter);
	emitArg(kBuildList, 0);
	emit(kDupTop);
	emitArg(kLoadAttr, addName("append"));
	nameOp(tmp, 1);
	listCompIter(n, 0, tmp);
	nameOp(tmp, 2);
}

void CodeGen::expr(Node *n) {
	if (_failed)
		return;
	switch (n->kind) {
	case kNodeConst:
		emitArg(kLoadConst, addConst(n->value.get()));
		break;
	case kNodeName:
		nameOp(n->str, 0);
		break;
	case kNodeAttribute:
		expr(n->a);
		emitArg(kLoadAttr, addName(mangle(_scope->className, n->str)));
		break;
	case kNodeSubscript:
		subscript(n, 0);
		break;
	case kNodeCall:
		call(n);
		break;
	case kNodeBinOp:
		expr(n->a);
		expr(n->b);
		emit(binaryOpcode(n->op, false));
		break;
	case kNodeUnaryOp:
		expr(n->a);
		emit(n->op == kOpNot ? kUnaryNot : n->op == kOpNeg ? kUnaryNegative : n->op == kOpPos ? kUnaryPositive : kUnaryInvert);
		break;
	case kNodeBoolOp:
		boolOp(n);
		break;
	case kNodeCompare:
		compare(n);
		break;
	case kNodeTuple:
		for (uint i = 0; i < n->kids.size(); i++)
			expr(n->kids[i]);
		emitArg(kBuildTuple, n->kids.size());
		break;
	case kNodeList:
		for (uint i = 0; i < n->kids.size(); i++)
			expr(n->kids[i]);
		emitArg(kBuildList, n->kids.size());
		break;
	case kNodeDict:
		emitArg(kBuildMap, 0);
		for (uint i = 0; i + 1 < n->kids.size(); i += 2) {
			emit(kDupTop);
			expr(n->kids[i + 1]);
			emit(kRotTwo);
			expr(n->kids[i]);
			emit(kStoreSubscr);
		}
		break;
	case kNodeRepr:
		expr(n->a);
		emit(kUnaryConvert);
		break;
	case kNodeLambda:
		function(n, "<lambda>", n->a, n->b, true);
		break;
	case kNodeListComp:
		listComp(n);
		break;
	case kNodeEllipsis:
		emitArg(kLoadConst, addConst(g_ellipsis));
		break;
	default:
		error(n->line, "invalid syntax");
		break;
	}
}

void CodeGen::makeClosure(Scope *child, ObjRef code, int ndefaults) {
	if (child->freeNames.empty()) {
		emitArg(kLoadConst, addConst(code.get()));
		emitArg(kMakeFunction, ndefaults);
		return;
	}
	for (uint i = 0; i < child->freeNames.size(); i++) {
		const Common::String &name = child->freeNames[i];
		int idx = -1;
		for (uint k = 0; k < _scope->cellNames.size(); k++)
			if (_scope->cellNames[k] == name)
				idx = k;
		if (idx < 0)
			for (uint k = 0; k < _scope->freeNames.size(); k++)
				if (_scope->freeNames[k] == name)
					idx = _scope->cellNames.size() + k;
		emitArg(kLoadClosure, idx < 0 ? 0 : idx);
	}
	emitArg(kLoadConst, addConst(code.get()));
	emitArg(kMakeClosure, ndefaults);
}

static Node *docString(Node *body) {
	if (!body || body->kids.empty())
		return nullptr;
	Node *first = body->kids[0];
	if (first->kind == kNodeExprStmt && first->a->kind == kNodeConst &&
	    (isType(first->a->value.get(), &g_strType) || isType(first->a->value.get(), &g_unicodeType)))
		return first->a;
	return nullptr;
}

void CodeGen::function(Node *n, const Common::String &name, Node *params, Node *body, bool isLambda) {
	Scope *child = nextChild();
	if (!child) {
		error(n->line, "internal compiler error: missing scope");
		return;
	}
	int ndefaults = 0;
	for (uint i = 0; i < params->kids.size(); i++) {
		if (params->kids[i]->b) {
			expr(params->kids[i]->b);
			ndefaults++;
		}
	}
	CodeGen sub(child, _filename, n->line, _tmpCounter);
	ObjRef code = sub.makeCode(isLambda ? body : body, isLambda ? 2 : 1);
	if (!code) {
		_failed = true;
		return;
	}
	Code *co = (Code *)code.get();
	co->_name = internStr(name.c_str());
	makeClosure(child, code, ndefaults);
}

void CodeGen::classDef(Node *n) {
	Scope *child = nextChild();
	if (!child) {
		error(n->line, "internal compiler error: missing scope");
		return;
	}
	emitArg(kLoadConst, addConst(newStr(n->str).get()));
	for (uint i = 0; i < n->kids.size(); i++)
		expr(n->kids[i]);
	emitArg(kBuildTuple, n->kids.size());
	CodeGen sub(child, _filename, n->line, _tmpCounter);
	ObjRef code = sub.makeCode(n->b, 3);
	if (!code) {
		_failed = true;
		return;
	}
	((Code *)code.get())->_name = internStr(n->str.c_str());
	makeClosure(child, code, 0);
	emitArg(kCallFunction, 0);
	emit(kBuildClass);
	nameOp(n->str, 1);
}

void CodeGen::suite(Node *n) {
	for (uint i = 0; i < n->kids.size() && !_failed; i++)
		stmt(n->kids[i]);
}

void CodeGen::stmt(Node *n) {
	if (_failed)
		return;
	if (n->kind == kNodeSuite) {
		suite(n);
		return;
	}
	setLine(n->line);
	switch (n->kind) {
	case kNodeFunctionDef:
		function(n, n->str, n->a, n->b, false);
		nameOp(n->str, 1);
		break;
	case kNodeClassDef:
		classDef(n);
		break;
	case kNodeReturn:
		if (_scope->kind != kScopeFunction) {
			error(n->line, "'return' outside function");
			return;
		}
		if (n->a) {
			if (_scope->isGenerator) {
				error(n->line, "'return' with argument inside generator");
				return;
			}
			expr(n->a);
		} else {
			emitArg(kLoadConst, addConst(g_none));
		}
		emit(kReturnValue);
		break;
	case kNodeYield:
		if (_scope->kind != kScopeFunction) {
			error(n->line, "'yield' outside function");
			return;
		}
		for (uint i = 0; i < _blocks.size(); i++) {
			if (_blocks[i].kind == kBlockFinallyTry) {
				error(n->line, "'yield' not allowed in a 'try' block with a 'finally' clause");
				return;
			}
		}
		expr(n->a);
		emit(kYieldValue);
		break;
	case kNodeDelete:
		for (uint i = 0; i < n->kids.size(); i++)
			del(n->kids[i], n->line);
		break;
	case kNodeAssign:
		expr(n->a);
		for (uint i = 0; i < n->kids.size(); i++) {
			if (i + 1 < n->kids.size())
				emit(kDupTop);
			store(n->kids[i]);
		}
		break;
	case kNodeAugAssign: {
		Node *t = n->a;
		int op = binaryOpcode(n->op, true);
		if (t->kind == kNodeName) {
			nameOp(t->str, 0);
			expr(n->b);
			emit(op);
			nameOp(t->str, 1);
		} else if (t->kind == kNodeAttribute) {
			expr(t->a);
			emit(kDupTop);
			int name = addName(mangle(_scope->className, t->str));
			emitArg(kLoadAttr, name);
			expr(n->b);
			emit(op);
			emit(kRotTwo);
			emitArg(kStoreAttr, name);
		} else if (t->b->kind == kNodeSlice) {
			Node *idx = t->b;
			int which = (idx->a ? 1 : 0) | (idx->b ? 2 : 0);
			expr(t->a);
			if (idx->a)
				expr(idx->a);
			if (idx->b)
				expr(idx->b);
			int count = 1 + (idx->a ? 1 : 0) + (idx->b ? 1 : 0);
			if (count == 1)
				emit(kDupTop);
			else
				emitArg(kDupTopX, count);
			emit(kSlice + which);
			expr(n->b);
			emit(op);
			emit(count == 1 ? kRotTwo : count == 2 ? kRotThree : kRotFour);
			emit(kStoreSlice + which);
		} else {
			// obj[key] op= value
			expr(t->a);
			Node *idx = t->b;
			if (idx->kind == kNodeTuple) {
				for (uint i = 0; i < idx->kids.size(); i++)
					expr(idx->kids[i]);
				emitArg(kBuildTuple, idx->kids.size());
			} else if (idx->kind == kNodeSliceObj) {
				sliceIndex(idx->a);
				sliceIndex(idx->b);
				if (idx->c || idx->op) {
					sliceIndex(idx->c);
					emitArg(kBuildSlice, 3);
				} else {
					emitArg(kBuildSlice, 2);
				}
			} else {
				expr(idx);
			}
			emitArg(kDupTopX, 2);
			emit(kBinarySubscr);
			expr(n->b);
			emit(op);
			emit(kRotThree);
			emit(kStoreSubscr);
		}
		break;
	}
	case kNodePrint: {
		if (n->a)
			expr(n->a);
		for (uint i = 0; i < n->kids.size(); i++) {
			if (n->a)
				emit(kDupTop);
			expr(n->kids[i]);
			if (n->a) {
				emit(kRotTwo);
				emit(kPrintItemTo);
			} else {
				emit(kPrintItem);
			}
		}
		if (n->op) {
			emit(n->a ? kPrintNewlineTo : kPrintNewline);
		} else if (n->a) {
			emit(kPopTop);
		}
		break;
	}
	case kNodeFor: {
		int end = newLabel(), start = newLabel(), exit = newLabel();
		jump(kSetupLoop, end);
		expr(n->b);
		emit(kGetIter);
		bind(start);
		setLine(n->line);
		jump(kForIter, exit);
		store(n->a);
		LoopBlock b = { kBlockLoop, start };
		_blocks.push_back(b);
		suite(n->c);
		_blocks.pop_back();
		jump(kJumpAbsolute, start);
		bind(exit);
		emit(kPopBlock);
		if (n->d)
			suite(n->d);
		bind(end);
		break;
	}
	case kNodeWhile: {
		int end = newLabel(), start = newLabel(), exit = newLabel();
		jump(kSetupLoop, end);
		bind(start);
		setLine(n->line);
		expr(n->a);
		jump(kJumpIfFalse, exit);
		emit(kPopTop);
		LoopBlock b = { kBlockLoop, start };
		_blocks.push_back(b);
		suite(n->c);
		_blocks.pop_back();
		jump(kJumpAbsolute, start);
		bind(exit);
		emit(kPopTop);
		emit(kPopBlock);
		if (n->d)
			suite(n->d);
		bind(end);
		break;
	}
	case kNodeIf: {
		int end = newLabel();
		Node *cur = n;
		for (;;) {
			int next = newLabel();
			if (cur != n)
				setLine(cur->line);
			expr(cur->a);
			jump(kJumpIfFalse, next);
			emit(kPopTop);
			suite(cur->c);
			jump(kJumpForward, end);
			bind(next);
			emit(kPopTop);
			if (cur->d && cur->d->kind == kNodeIf) {
				cur = cur->d;
				continue;
			}
			if (cur->d)
				suite(cur->d);
			break;
		}
		bind(end);
		break;
	}
	case kNodeRaise: {
		int count = 0;
		if (n->a) {
			expr(n->a);
			count++;
		}
		if (n->b) {
			expr(n->b);
			count++;
		}
		if (n->c) {
			expr(n->c);
			count++;
		}
		emitArg(kRaiseVarargs, count);
		break;
	}
	case kNodeTryExcept: {
		int handlers = newLabel(), elseLabel = newLabel(), end = newLabel();
		jump(kSetupExcept, handlers);
		LoopBlock b = { kBlockExcept, -1 };
		_blocks.push_back(b);
		suite(n->a);
		_blocks.pop_back();
		emit(kPopBlock);
		jump(kJumpForward, elseLabel);
		bind(handlers);
		for (uint i = 0; i < n->kids.size(); i++) {
			Node *h = n->kids[i];
			setLine(h->line);
			int next = newLabel();
			if (h->a) {
				emit(kDupTop);
				expr(h->a);
				emitArg(kCompareOp, kCmpExcMatch);
				jump(kJumpIfFalse, next);
				emit(kPopTop);
			}
			emit(kPopTop);
			if (h->b)
				store(h->b);
			else
				emit(kPopTop);
			emit(kPopTop);
			suite(h->c);
			jump(kJumpForward, end);
			bind(next);
			if (h->a)
				emit(kPopTop);
		}
		_exceptEnds.push_back(pos());
		emit(kEndFinally);
		bind(elseLabel);
		if (n->d)
			suite(n->d);
		bind(end);
		break;
	}
	case kNodeTryFinally: {
		int final = newLabel();
		int setupPos = pos();
		jump(kSetupFinally, final);
		LoopBlock b = { kBlockFinallyTry, -1 };
		_blocks.push_back(b);
		suite(n->a);
		_blocks.pop_back();
		emit(kPopBlock);
		emitArg(kLoadConst, addConst(g_none));
		bind(final);
		LoopBlock e = { kBlockFinallyEnd, -1 };
		_blocks.push_back(e);
		suite(n->b);
		_blocks.pop_back();
		_finallyEnds[pos()] = setupPos;
		emit(kEndFinally);
		break;
	}
	case kNodeAssert: {
		int skip = newLabel(), end = newLabel(), ok = newLabel();
		emitArg(kLoadGlobal, addName("__debug__"));
		jump(kJumpIfFalse, skip);
		emit(kPopTop);
		expr(n->a);
		jump(kJumpIfTrue, ok);
		emit(kPopTop);
		emitArg(kLoadGlobal, addName("AssertionError"));
		if (n->b) {
			expr(n->b);
			emitArg(kRaiseVarargs, 2);
		} else {
			emitArg(kRaiseVarargs, 1);
		}
		bind(ok);
		emit(kPopTop);
		jump(kJumpForward, end);
		bind(skip);
		emit(kPopTop);
		bind(end);
		break;
	}
	case kNodeImport:
		for (uint i = 0; i < n->kids.size(); i++) {
			Node *al = n->kids[i];
			emitArg(kLoadConst, addConst(g_none));
			emitArg(kImportName, addName(al->str));
			if (al->value) {
				// import a.b.c as d: walk down to c
				const char *p = strchr(al->str.c_str(), '.');
				while (p) {
					const char *q = strchr(p + 1, '.');
					Common::String part = q ? Common::String(p + 1, q - p - 1) : Common::String(p + 1);
					emitArg(kLoadAttr, addName(part));
					p = q;
				}
				nameOp(((Str *)al->value.get())->toString(), 1);
			} else {
				Common::String first = al->str;
				const char *dot = strchr(first.c_str(), '.');
				if (dot)
					first = Common::String(first.c_str(), dot - first.c_str());
				nameOp(first, 1);
			}
		}
		break;
	case kNodeImportFrom: {
		if (n->op == 1) {
			Ref<Tuple> star = tuplePack(1, newStr("*").get());
			emitArg(kLoadConst, addConst(star.get()));
			emitArg(kImportName, addName(n->str));
			emit(kImportStar);
			break;
		}
		Ref<Tuple> names = newTuple(n->kids.size());
		for (uint i = 0; i < n->kids.size(); i++)
			names->set(i, internStr(n->kids[i]->str.c_str()).get());
		emitArg(kLoadConst, addConst(names.get()));
		emitArg(kImportName, addName(n->str));
		for (uint i = 0; i < n->kids.size(); i++) {
			Node *al = n->kids[i];
			emitArg(kImportFrom, addName(al->str));
			nameOp(al->value ? ((Str *)al->value.get())->toString() : al->str, 1);
		}
		emit(kPopTop);
		break;
	}
	case kNodeExec:
		expr(n->a);
		if (n->b) {
			expr(n->b);
			if (n->c)
				expr(n->c);
			else
				emit(kDupTop);
		} else {
			emitArg(kLoadConst, addConst(g_none));
			emit(kDupTop);
		}
		emit(kExecStmt);
		break;
	case kNodeGlobal:
		break;
	case kNodeExprStmt:
		if (n->a->kind == kNodeConst && isType(n->a->value.get(), &g_strType) && !_interactive)
			break; // a doc string, or any other string on its own
		expr(n->a);
		emit(_interactive ? kPrintExpr : kPopTop);
		break;
	case kNodePass:
		break;
	case kNodeBreak: {
		bool inLoop = false;
		for (uint i = 0; i < _blocks.size(); i++)
			if (_blocks[i].kind == kBlockLoop)
				inLoop = true;
		if (!inLoop) {
			error(n->line, "'break' outside loop");
			return;
		}
		emit(kBreakLoop);
		break;
	}
	case kNodeContinue: {
		int i = (int)_blocks.size() - 1;
		bool inTry = false;
		for (; i >= 0; i--) {
			if (_blocks[i].kind == kBlockLoop)
				break;
			if (_blocks[i].kind == kBlockFinallyEnd) {
				error(n->line, "'continue' not supported inside 'finally' clause");
				return;
			}
			inTry = true;
		}
		if (i < 0) {
			error(n->line, "'continue' not properly in loop");
			return;
		}
		jump(inTry ? kContinueLoop : kJumpAbsolute, _blocks[i].continueLabel);
		break;
	}
	default:
		error(n->line, "invalid syntax");
		break;
	}
}

// The stack depth a code object needs, found by following its jumps
static int stackEffect(int op, int arg, bool jumped) {
	switch (op) {
	case kPopTop: return -1;
	case kRotTwo: case kRotThree: case kRotFour: return 0;
	case kDupTop: return 1;
	case kDupTopX: return arg;
	case kUnaryPositive: case kUnaryNegative: case kUnaryNot: case kUnaryConvert: case kUnaryInvert: return 0;
	case kSlice: return 0;
	case kSlice + 1: case kSlice + 2: return -1;
	case kSlice + 3: return -2;
	case kStoreSlice: return -2;
	case kStoreSlice + 1: case kStoreSlice + 2: return -3;
	case kStoreSlice + 3: return -4;
	case kDeleteSlice: return -1;
	case kDeleteSlice + 1: case kDeleteSlice + 2: return -2;
	case kDeleteSlice + 3: return -3;
	case kStoreSubscr: return -3;
	case kDeleteSubscr: return -2;
	case kPrintExpr: case kPrintItem: return -1;
	case kPrintItemTo: return -2;
	case kPrintNewline: return 0;
	case kPrintNewlineTo: return -1;
	case kBreakLoop: return 0;
	case kLoadLocals: return 1;
	case kReturnValue: return -1;
	case kImportStar: return -1;
	case kExecStmt: return -3;
	case kYieldValue: return -1;
	case kPopBlock: return 0;
	case kEndFinally: return -1;
	case kBuildClass: return -2;
	case kStoreName: return -1;
	case kDeleteName: return 0;
	case kUnpackSequence: return arg - 1;
	case kForIter: return jumped ? -1 : 1;
	case kStoreAttr: return -2;
	case kDeleteAttr: return -1;
	case kStoreGlobal: return -1;
	case kDeleteGlobal: return 0;
	case kLoadConst: case kLoadName: case kLoadGlobal: case kLoadFast: case kLoadClosure: case kLoadDeref: return 1;
	case kBuildTuple: case kBuildList: return 1 - arg;
	case kBuildMap: return 1;
	case kLoadAttr: return 0;
	case kCompareOp: return -1;
	case kImportName: return 0;
	case kImportFrom: return 1;
	case kJumpForward: case kJumpIfFalse: case kJumpIfTrue: case kJumpAbsolute: case kContinueLoop: return 0;
	case kSetupLoop: return 0;
	case kSetupExcept: case kSetupFinally: return jumped ? 3 : 0;
	case kStoreFast: case kStoreDeref: return -1;
	case kDeleteFast: return 0;
	case kSetLineno: return 0;
	case kRaiseVarargs: return -arg;
	case kCallFunction: return -((arg & 0xff) + 2 * ((arg >> 8) & 0xff));
	case kCallFunctionVar: case kCallFunctionKw: return -((arg & 0xff) + 2 * ((arg >> 8) & 0xff)) - 1;
	case kCallFunctionVarKw: return -((arg & 0xff) + 2 * ((arg >> 8) & 0xff)) - 2;
	case kMakeFunction: return -arg;
	case kMakeClosure: return -arg; // and the free variables, which the caller accounts for
	case kBuildSlice: return arg == 3 ? -2 : -1;
	default:
		if (op >= kBinaryPower && op <= kInplaceTrueDivide) return -1;
		if (op >= kInplaceAdd && op <= kInplaceModulo) return -1;
		if (op >= kBinaryLshift && op <= kInplacePower) return -1;
		if (op >= kInplaceLshift && op <= kInplaceOr) return -1;
		return 0;
	}
}

static int computeStackSize(const Common::Array<uint8> &code, const Common::Array<int> &freeCounts,
                            const Common::Array<int> &exceptEnds, const Common::HashMap<int, int> &finallyEnds) {
	int n = code.size();
	Common::Array<int> depth;
	depth.resize(n + 1);
	for (int i = 0; i <= n; i++)
		depth[i] = -1;
	Common::Array<int> work;
	depth[0] = 0;
	work.push_back(0);
	int maxDepth = 0;
	while (!work.empty()) {
		int pc = work.back();
		work.pop_back();
		int d = depth[pc];
		if (d > 10000)
			return d; // runaway: the analysis has a bug, but don't hang

		while (pc < n) {
			int op = code[pc];
			int arg = 0;
			int len = 1;
			if (op == kExtendedArg) {
				int ext = code[pc + 1] | (code[pc + 2] << 8);
				op = code[pc + 3];
				arg = (ext << 16) | code[pc + 4] | (code[pc + 5] << 8);
				len = 6;
			} else if (op >= kHaveArgument) {
				arg = code[pc + 1] | (code[pc + 2] << 8);
				len = 3;
			}
			int nextPc = pc + len;
			int extraFree = op == kMakeClosure && pc < (int)freeCounts.size() ? freeCounts[pc] : 0;
			bool isJump = op == kJumpForward || op == kJumpIfFalse || op == kJumpIfTrue || op == kJumpAbsolute ||
			              op == kContinueLoop || op == kForIter || op == kSetupLoop || op == kSetupExcept || op == kSetupFinally;
			if (isJump) {
				int target = (op == kJumpAbsolute || op == kContinueLoop) ? arg : nextPc + arg;
				int td = d + stackEffect(op, arg, true);
				if (target <= n && td > depth[target]) {
					depth[target] = td;
					work.push_back(target);
					if (td > maxDepth)
						maxDepth = td;
				}
			}
			d += stackEffect(op, arg, false) - extraFree;
			if (d > maxDepth)
				maxDepth = d;
			if (op == kEndFinally) {
				bool reraiseOnly = false;
				for (uint k = 0; k < exceptEnds.size(); k++)
					if (exceptEnds[k] == pc)
						reraiseOnly = true;
				if (reraiseOnly)
					break;
				// Leaving a finally clause: back to the depth before its SETUP_FINALLY
				if (finallyEnds.contains(pc) && depth[finallyEnds[pc]] >= 0)
					d = depth[finallyEnds[pc]];
			}
			if (op == kJumpForward || op == kJumpAbsolute || op == kContinueLoop || op == kReturnValue ||
			    op == kRaiseVarargs || op == kBreakLoop)
				break;

			pc = nextPc;
			if (pc <= n) {
				if (depth[pc] >= d)
					break;
				depth[pc] = d;
			}
		}
	}
	return maxDepth;
}

// kind: 0 module, 1 function, 2 lambda, 3 class, 4 eval, 5 interactive
ObjRef CodeGen::makeCode(Node *body, int kind) {
	ObjRef doc;
	if (kind == 1) {
		Node *d = docString(body);
		doc = d ? d->value : ObjRef(g_none);
		addConst(doc.get());
	}
	if (kind == 1 || kind == 2) {
		// Unpack tuple parameters into their names
		Node *params = _scope->node->a;
		for (uint i = 0; i < params->kids.size(); i++) {
			Node *p = params->kids[i];
			if (p->op != 1)
				continue;
			emitArg(kLoadFast, varIndex(Common::String::format(".%d", i)));
			Common::Array<Node *> stack;
			// Store the nested names
			struct Unpacker {
				static void run(CodeGen *g, Node *t) {
					g->emitArg(kUnpackSequence, t->kids.size());
					for (uint k = 0; k < t->kids.size(); k++) {
						if (t->kids[k]->op == 1)
							run(g, t->kids[k]);
						else
							g->nameOp(t->kids[k]->str, 1);
					}
				}
			};
			Unpacker::run(this, p);
		}
	}
	if (kind == 3) {
		emitArg(kLoadName, addName("__name__"));
		emitArg(kStoreName, addName("__module__"));
		Node *d = docString(body);
		if (d) {
			emitArg(kLoadConst, addConst(d->value.get()));
			emitArg(kStoreName, addName("__doc__"));
		}
	}
	if (kind == 0) {
		Node *d = docString(body);
		if (d) {
			setLine(d->line);
			emitArg(kLoadConst, addConst(d->value.get()));
			emitArg(kStoreName, addName("__doc__"));
		}
	}
	if (kind == 2 || kind == 4) {
		expr(body);
		emit(kReturnValue);
	} else {
		if (body->kind == kNodeModule || body->kind == kNodeSuite)
			suite(body);
		else
			stmt(body);
		if (kind == 3) {
			emit(kLoadLocals);
		} else {
			emitArg(kLoadConst, addConst(g_none));
		}
		emit(kReturnValue);
	}
	if (_failed)
		return ObjRef();
	// Patch the jumps
	for (uint i = 0; i < _labels.size(); i++) {
		Label &l = _labels[i];
		for (uint k = 0; k < l.refs.size(); k++) {
			int at = l.refs[k];
			int target = l.absolute[k] ? l.pos : l.pos - (at + 2);
			_code[at] = target & 0xff;
			_code[at + 1] = (target >> 8) & 0xff;
		}
	}
	Code *co = new Code();
	ObjRef rc(co);
	int flags = 0;
	if (_scope->kind == kScopeFunction) {
		flags |= kCoNewLocals;
		if (_scope->optimized())
			flags |= kCoOptimized;
		if (_scope->isGenerator)
			flags |= kCoGenerator;
	} else if (_scope->kind == kScopeClass) {
		flags |= kCoNewLocals;
	}
	int argcount = 0;
	if (kind == 1 || kind == 2) {
		Node *params = _scope->node->a;
		for (uint i = 0; i < params->kids.size(); i++) {
			if (params->kids[i]->op == 2)
				flags |= kCoVarArgs;
			else if (params->kids[i]->op == 3)
				flags |= kCoVarKeywords;
			else
				argcount++;
		}
	}
	co->_argCount = argcount;
	co->_flags = flags;
	co->_code = newStr((const char *)_code.data(), _code.size());
	Ref<Tuple> consts = newTuple(_consts.size());
	for (uint i = 0; i < _consts.size(); i++)
		consts->set(i, _consts[i].get());
	co->_consts = consts;
	Ref<Tuple> names = newTuple(_names.size());
	for (uint i = 0; i < _names.size(); i++)
		names->set(i, internStr(_names[i].c_str()).get());
	co->_names = names;
	Ref<Tuple> varnames = newTuple(_scope->optimized() || _scope->kind == kScopeFunction ? _varnames.size() : 0);
	for (uint i = 0; i < varnames->_size; i++)
		varnames->set(i, internStr(_varnames[i].c_str()).get());
	co->_varNames = varnames;
	co->_nLocals = varnames->_size;
	Ref<Tuple> freevars = newTuple(_scope->kind == kScopeModule ? 0 : _scope->freeNames.size());
	for (uint i = 0; i < freevars->_size; i++)
		freevars->set(i, internStr(_scope->freeNames[i].c_str()).get());
	co->_freeVars = freevars;
	Ref<Tuple> cellvars = newTuple(_scope->kind == kScopeModule ? 0 : _scope->cellNames.size());
	for (uint i = 0; i < cellvars->_size; i++)
		cellvars->set(i, internStr(_scope->cellNames[i].c_str()).get());
	co->_cellVars = cellvars;
	if (freevars->_size == 0 && cellvars->_size == 0)
		co->_flags |= kCoNoFree;
	co->_filename = newStr(_filename);
	co->_name = internStr(_scope->kind == kScopeModule ? "?" : _scope->name.c_str());
	co->_firstLineNo = _firstLine;
	co->_lnotab = newStr((const char *)_lnotab.data(), _lnotab.size());
	// MAKE_CLOSURE also pops the free variables pushed before it
	Common::Array<int> freeCounts;
	freeCounts.resize(_code.size() + 1);
	for (uint i = 0; i < freeCounts.size(); i++)
		freeCounts[i] = 0;
	{
		uint pc = 0;
		while (pc < _code.size()) {
			int op = _code[pc];
			int len = op >= kHaveArgument ? 3 : 1;
			if (op == kMakeClosure) {
				int ci = _code[pc - 3 + 1] | (_code[pc - 3 + 2] << 8); // the LOAD_CONST before
				Object *c = ci < (int)_consts.size() ? _consts[ci].get() : nullptr;
				if (c && isType(c, &g_codeType))
					freeCounts[pc] = ((Code *)c)->_freeVars->_size;
			}
			if (op == kExtendedArg)
				len = 3;
			pc += len;
		}
	}
	co->_stackSize = computeStackSize(_code, freeCounts, _exceptEnds, _finallyEnds) + 1;

	return rc;
}

} // End of anonymous namespace

ObjRef compileString(const char *source, const char *filename, int mode) {
	// Source with Windows line endings, or none at the end, is fine
	Common::String text;
	for (const char *p = source; *p; p++)
		if (*p != '\r' || p[1] != '\n')
			text += *p;
	if (text.empty() || text.lastChar() != '\n')
		text += '\n';
	Node *tree = parseSource(text.c_str(), filename, mode);
	if (!tree)
		return ObjRef();
	Scope *top = new Scope(kScopeModule, "?", nullptr, tree);
	SymbolBuilder sb(top);
	if (mode == kCompileEval)
		sb.visitExpr(tree);
	else
		sb.visitSuite(tree);
	resolveScope(top);
	int tmpCounter = 0;
	CodeGen gen(top, filename, 1, &tmpCounter);
	gen.setInteractive(mode == kCompileSingle);
	ObjRef code = gen.makeCode(tree, mode == kCompileEval ? 4 : 0);
	delete top;
	delete tree;
	return code;
}

} // End of namespace Python
} // End of namespace Yaga
