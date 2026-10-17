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

#include "common/util.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"
#include "yaga/python/ast.h"
#include "yaga/python/parser.h"

// The tokenizer and parser for Python 2.2 source (Parser/tokenizer.c and
// Grammar/Grammar), building the tree in ast.h

namespace Yaga {
namespace Python {

enum TokenType {
	kTokEnd, kTokNewline, kTokIndent, kTokDedent, kTokName, kTokNumber, kTokString, kTokOp
};

struct Token {
	TokenType type;
	Common::String text;  // the name, operator or number as written
	Common::String bytes; // a string's value
	bool unicode;
	int line;
	int col;
	Token() : type(kTokEnd), unicode(false), line(0), col(0) {}
};

class Tokenizer {
public:
	Tokenizer(const char *source, const char *filename) : _src(source), _p(source), _filename(filename),
		_line(1), _parenLevel(0), _atLineStart(true), _pendingDedents(0), _failed(false), _lineStart(source) {
		_indents.push_back(0);
	}

	bool failed() const { return _failed; }
	const Common::String &errorMsg() const { return _errorMsg; }
	int errorLine() const { return _errorLine; }
	int errorCol() const { return _errorCol; }
	Common::String errorText() const { return _errorText; }
	bool isIndentationError() const { return _indentError; }

	bool next(Token &t);

private:
	bool fail(const char *msg, bool indent = false) {
		if (!_failed) {
			_failed = true;
			_errorMsg = msg;
			_errorLine = _line;
			_errorCol = (int)(_p - _lineStart);
			_indentError = indent;
			const char *e = _lineStart;
			while (*e && *e != '\n')
				e++;
			_errorText = Common::String(_lineStart, e - _lineStart) + "\n";
		}
		return false;
	}
	bool readString(Token &t, bool raw, bool unicode);
	bool readNumber(Token &t);

	const char *_src;
	const char *_p;
	const char *_filename;
	int _line;
	int _parenLevel;
	bool _atLineStart;
	int _pendingDedents;
	Common::Array<int> _indents;
	bool _failed;
	bool _indentError;
	Common::String _errorMsg;
	int _errorLine, _errorCol;
	Common::String _errorText;
	const char *_lineStart;
};

static bool isNameStart(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (uint8)c >= 128;
}

static bool isNameChar(char c) {
	return isNameStart(c) || (c >= '0' && c <= '9');
}

bool Tokenizer::next(Token &t) {
	t = Token();
	if (_pendingDedents > 0) {
		_pendingDedents--;
		t.type = kTokDedent;
		t.line = _line;
		return true;
	}
again:
	if (_atLineStart && _parenLevel == 0) {
		// Measure the indentation of a new logical line
		int col = 0;
		const char *q = _p;
		for (;;) {
			if (*q == ' ')
				col++;
			else if (*q == '\t')
				col = (col / 8 + 1) * 8;
			else if (*q == '\f')
				col = 0;
			else
				break;
			q++;
		}
		_p = q;
		if (*q == '#' || *q == '\n' || *q == '\r' || *q == 0) {
			// Blank lines and comment lines do not count
			while (*_p && *_p != '\n')
				_p++;
			if (*_p == '\n') {
				_p++;
				_line++;
				_lineStart = _p;
				goto again;
			}
			// End of input
			_atLineStart = false;
		} else {
			_atLineStart = false;
			if (col > _indents.back()) {
				_indents.push_back(col);
				t.type = kTokIndent;
				t.line = _line;
				return true;
			}
			if (col < _indents.back()) {
				int n = 0;
				while (_indents.size() > 1 && col < _indents.back()) {
					_indents.pop_back();
					n++;
				}
				if (col != _indents.back())
					return fail("unindent does not match any outer indentation level", true);
				_pendingDedents = n - 1;
				t.type = kTokDedent;
				t.line = _line;
				return true;
			}
		}
	}
	// Skip spaces and comments within the line
	for (;;) {
		while (*_p == ' ' || *_p == '\t' || *_p == '\f' || *_p == '\r')
			_p++;
		if (*_p == '#') {
			while (*_p && *_p != '\n')
				_p++;
		}
		if (*_p == '\\' && (_p[1] == '\n' || (_p[1] == '\r' && _p[2] == '\n'))) {
			// Line continuation
			_p += _p[1] == '\r' ? 3 : 2;
			_line++;
			_lineStart = _p;
			continue;
		}
		break;
	}
	t.line = _line;
	t.col = (int)(_p - _lineStart);
	char c = *_p;
	if (c == 0) {
		if (!_atLineStart) {
			// The last line may lack its newline
			_atLineStart = true;
			if (_lineStart != _p || t.col > 0) {
				t.type = kTokNewline;
				return true;
			}
		}
		if (_indents.size() > 1) {
			_indents.pop_back();
			t.type = kTokDedent;
			return true;
		}
		t.type = kTokEnd;
		return true;
	}
	if (c == '\n') {
		_p++;
		_line++;
		_lineStart = _p;
		if (_parenLevel > 0)
			goto again;
		_atLineStart = true;
		t.type = kTokNewline;
		return true;
	}
	// Names, which may also be string prefixes
	if (isNameStart(c)) {
		const char *start = _p;
		bool raw = false, uni = false;
		const char *q = _p;
		if (*q == 'u' || *q == 'U') {
			uni = true;
			q++;
		}
		if (*q == 'r' || *q == 'R') {
			raw = true;
			q++;
		}
		if ((uni || raw) && (*q == '\'' || *q == '"')) {
			_p = q;
			return readString(t, raw, uni);
		}
		while (isNameChar(*_p))
			_p++;
		t.type = kTokName;
		t.text = Common::String(start, _p - start);
		return true;
	}
	if (c == '\'' || c == '"')
		return readString(t, false, false);
	if ((c >= '0' && c <= '9') || (c == '.' && _p[1] >= '0' && _p[1] <= '9'))
		return readNumber(t);
	// Operators, longest first
	static const char *const ops3[] = { "**=", "//=", ">>=", "<<=", nullptr };
	static const char *const ops2[] = { "**", "//", ">>", "<<", "<=", ">=", "==", "!=", "<>",
	                                    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", nullptr };
	for (int i = 0; ops3[i]; i++) {
		if (strncmp(_p, ops3[i], 3) == 0) {
			t.type = kTokOp;
			t.text = ops3[i];
			_p += 3;
			return true;
		}
	}
	for (int i = 0; ops2[i]; i++) {
		if (strncmp(_p, ops2[i], 2) == 0) {
			t.type = kTokOp;
			t.text = ops2[i];
			_p += 2;
			return true;
		}
	}
	if (strchr("()[]{}:,;+-*/|&<>=.%`^~", c)) {
		if (c == '(' || c == '[' || c == '{')
			_parenLevel++;
		else if ((c == ')' || c == ']' || c == '}') && _parenLevel > 0)
			_parenLevel--;
		t.type = kTokOp;
		t.text = Common::String(&c, 1);
		_p++;
		return true;
	}
	return fail("invalid syntax");
}

bool Tokenizer::readString(Token &t, bool raw, bool unicode) {
	char quote = *_p;
	bool triple = _p[1] == quote && _p[2] == quote;
	_p += triple ? 3 : 1;
	t.type = kTokString;
	t.unicode = unicode;
	Common::String raws;
	for (;;) {
		char c = *_p;
		if (c == 0)
			return fail(triple ? "EOF while scanning triple-quoted string" : "EOL while scanning single-quoted string");
		if (c == quote) {
			if (!triple) {
				_p++;
				break;
			}
			if (_p[1] == quote && _p[2] == quote) {
				_p += 3;
				break;
			}
		}
		if (c == '\n') {
			if (!triple)
				return fail("EOL while scanning single-quoted string");
			_line++;
			_lineStart = _p + 1;
		}
		if (c == '\\' && _p[1]) {
			raws += c;
			_p++;
			c = *_p;
			if (c == '\n') {
				_line++;
				_lineStart = _p + 1;
			}
		}
		if (c == '\r' && _p[1] == '\n') {
			// Source with Windows line endings: keep just the newline
			_p++;
			continue;
		}
		raws += c;
		_p++;
	}
	if (raw) {
		t.bytes = raws;
		return true;
	}
	// Decode the escapes, as PyString_DecodeEscape
	Common::String out;
	for (uint i = 0; i < raws.size(); i++) {
		char c = raws[i];
		if (c != '\\' || i + 1 >= raws.size()) {
			out += c;
			continue;
		}
		c = raws[++i];
		switch (c) {
		case '\n': break;
		case '\\': out += '\\'; break;
		case '\'': out += '\''; break;
		case '"': out += '"'; break;
		case 'b': out += '\b'; break;
		case 'f': out += '\014'; break;
		case 't': out += '\t'; break;
		case 'n': out += '\n'; break;
		case 'r': out += '\r'; break;
		case 'v': out += '\013'; break;
		case 'a': out += '\007'; break;
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': {
			int v = c - '0';
			if (i + 1 < raws.size() && raws[i + 1] >= '0' && raws[i + 1] <= '7') {
				v = v * 8 + (raws[++i] - '0');
				if (i + 1 < raws.size() && raws[i + 1] >= '0' && raws[i + 1] <= '7')
					v = v * 8 + (raws[++i] - '0');
			}
			out += (char)v;
			break;
		}
		case 'x': {
			if (i + 2 < raws.size() && Common::isXDigit(raws[i + 1]) && Common::isXDigit(raws[i + 2])) {
				int v = 0;
				for (int k = 0; k < 2; k++) {
					char h = raws[++i];
					v = v * 16 + (h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
				}
				out += (char)v;
			} else {
				return fail("invalid \\x escape");
			}
			break;
		}
		case 'u':
			if (unicode && i + 4 < raws.size()) {
				uint32 v = 0;
				for (int k = 0; k < 4; k++) {
					char h = raws[++i];
					v = v * 16 + (h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
				}
				// Unicode literals are carried as UTF-8 to the code generator
				if (v < 0x80) {
					out += (char)v;
				} else if (v < 0x800) {
					out += (char)(0xc0 | (v >> 6));
					out += (char)(0x80 | (v & 0x3f));
				} else {
					out += (char)(0xe0 | (v >> 12));
					out += (char)(0x80 | ((v >> 6) & 0x3f));
					out += (char)(0x80 | (v & 0x3f));
				}
				break;
			}
			out += '\\';
			out += c;
			break;
		default:
			out += '\\';
			out += c;
			break;
		}
	}
	if (unicode) {
		// Non-ASCII bytes written in a unicode literal are Latin-1 characters
		Common::String utf8;
		for (uint i = 0; i < out.size(); i++)
			utf8 += out[i];
		t.bytes = utf8;
	} else {
		t.bytes = out;
	}
	return true;
}

bool Tokenizer::readNumber(Token &t) {
	const char *start = _p;
	t.type = kTokNumber;
	if (_p[0] == '0' && (_p[1] == 'x' || _p[1] == 'X')) {
		_p += 2;
		while (Common::isXDigit(*_p))
			_p++;
		if (*_p == 'l' || *_p == 'L')
			_p++;
	} else {
		while (Common::isDigit(*_p))
			_p++;
		if (*_p == '.') {
			_p++;
			while (Common::isDigit(*_p))
				_p++;
		}
		if (*_p == 'e' || *_p == 'E') {
			const char *q = _p + 1;
			if (*q == '+' || *q == '-')
				q++;
			if (Common::isDigit(*q)) {
				_p = q;
				while (Common::isDigit(*_p))
					_p++;
			}
		}
		if (*_p == 'l' || *_p == 'L' || *_p == 'j' || *_p == 'J')
			_p++;
	}
	t.text = Common::String(start, _p - start);
	if (isNameChar(*_p))
		return fail("invalid syntax");
	return true;
}

// ---- Parser ---------------------------------------------------------------------

class Parser {
public:
	Parser(const char *source, const char *filename) : _tok(source, filename), _filename(filename), _failed(false),
		_futureAllowed(true), _statements(0) {
		advance();
	}

	Node *parseFileInput();
	Node *parseEvalInput();
	Node *parseSingleInput();

	bool failed() const { return _failed || _tok.failed(); }
	void reportError();

private:
	void advance() {
		if (!_tok.next(_cur))
			_failed = true;
	}
	bool isOp(const char *op) const { return _cur.type == kTokOp && _cur.text == op; }
	bool isName(const char *kw) const { return _cur.type == kTokName && _cur.text == kw; }
	bool isKeyword() const;
	bool accept(const char *op) {
		if (isOp(op)) {
			advance();
			return true;
		}
		return false;
	}
	bool acceptName(const char *kw) {
		if (isName(kw)) {
			advance();
			return true;
		}
		return false;
	}
	bool expect(const char *op) {
		if (accept(op))
			return true;
		return error();
	}
	bool expectName(const char *kw) {
		if (acceptName(kw))
			return true;
		return error();
	}
	bool error(const char *msg = "invalid syntax") {
		if (!_failed) {
			_failed = true;
			_errorMsg = msg;
			_errorLine = _cur.line;
			_errorCol = _cur.col;
		}
		return false;
	}
	Node *fail(Node *n, const char *msg = "invalid syntax") {
		delete n;
		error(msg);
		return nullptr;
	}

	Node *parseStatement();
	bool parseSimpleStatement(Node *suite);
	Node *parseSmallStatement();
	Node *parseExprStatement();
	Node *parsePrint();
	Node *parseImport();
	Node *parseFrom();
	Node *parseCompound();
	Node *parseIf();
	Node *parseWhile();
	Node *parseFor();
	Node *parseTry();
	Node *parseFuncDef();
	Node *parseClassDef();
	Node *parseSuite();
	Node *parseParams(const char *closer);
	Node *parseFpdef();
	Node *parseDotted(Common::String &name);

	Node *parseTest();
	Node *parseOldTest() { return parseTest(); }
	Node *parseAndTest();
	Node *parseNotTest();
	Node *parseComparison();
	Node *parseExpr();
	Node *parseXorExpr();
	Node *parseAndExpr();
	Node *parseShiftExpr();
	Node *parseArithExpr();
	Node *parseTerm();
	Node *parseFactor();
	Node *parsePower();
	Node *parseAtom();
	Node *parseTrailers(Node *atom);
	Node *parseSubscript();
	Node *parseArgs(Node *func);
	Node *parseTestList(bool allowTrailing = true);
	Node *parseExprList();
	Node *parseListMaker(int line);
	Node *parseLambda();
	Node *makeNumber(const Token &t);
	bool checkTarget(Node *n, bool del);

	Tokenizer _tok;
	Token _cur;
	const char *_filename;
	bool _failed;
	// from __future__ imports may only come first (after a doc string)
	bool _futureAllowed;
	int _statements;
	Common::String _errorMsg;
	int _errorLine, _errorCol;
};

static const char *const g_keywords[] = {
	"and", "assert", "break", "class", "continue", "def", "del", "elif", "else", "except", "exec",
	"finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "not", "or", "pass",
	"print", "raise", "return", "try", "while", "yield", nullptr
};

bool Parser::isKeyword() const {
	if (_cur.type != kTokName)
		return false;
	for (int i = 0; g_keywords[i]; i++)
		if (_cur.text == g_keywords[i])
			return true;
	return false;
}

void Parser::reportError() {
	Common::String msg;
	int line, col;
	Common::String text;
	Object *type = g_excSyntaxError;
	if (_tok.failed()) {
		msg = _tok.errorMsg();
		line = _tok.errorLine();
		col = _tok.errorCol();
		text = _tok.errorText();
		if (_tok.isIndentationError())
			type = g_excIndentationError;
	} else {
		msg = _errorMsg.empty() ? "invalid syntax" : _errorMsg;
		line = _errorLine;
		col = _errorCol;
	}
	ObjRef textObj = text.empty() ? ObjRef(g_none) : ObjRef(newStr(text));
	Ref<Tuple> info = tuplePack(4, newStr(_filename).get(), newInt(line).get(), newInt(col + 1).get(), textObj.get());
	Ref<Tuple> args = tuplePack(2, newStr(msg).get(), info.get());
	setError(type, args.get());
}

Node *Parser::parseFileInput() {
	Node *mod = new Node(kNodeModule, 1);
	while (!failed() && _cur.type != kTokEnd) {
		if (_cur.type == kTokNewline) {
			advance();
			continue;
		}
		Node *s = parseStatement();
		if (!s)
			return fail(mod);
		if (s->kind == kNodeSuite) {
			for (uint i = 0; i < s->kids.size(); i++)
				mod->add(s->kids[i]);
			s->kids.clear();
			delete s;
		} else {
			mod->add(s);
		}
	}
	if (failed())
		return fail(mod);
	return mod;
}

Node *Parser::parseEvalInput() {
	Node *e = parseTestList();
	if (!e)
		return nullptr;
	while (_cur.type == kTokNewline)
		advance();
	if (_cur.type != kTokEnd)
		return fail(e);
	return e;
}

Node *Parser::parseSingleInput() {
	return parseFileInput();
}

// A statement; a line of several simple statements comes back as a Suite
Node *Parser::parseStatement() {
	if (_cur.type == kTokName) {
		if (isName("if") || isName("while") || isName("for") || isName("try") || isName("def") || isName("class"))
			return parseCompound();
	}
	Node *suite = new Node(kNodeSuite, _cur.line);
	if (!parseSimpleStatement(suite))
		return fail(suite);
	if (suite->kids.size() == 1) {
		Node *s = suite->kids[0];
		suite->kids.clear();
		delete suite;
		return s;
	}
	return suite;
}

bool Parser::parseSimpleStatement(Node *suite) {
	for (;;) {
		Node *s = parseSmallStatement();
		if (!s)
			return false;
		suite->add(s);
		if (!accept(";"))
			break;
		if (_cur.type == kTokNewline)
			break;
	}
	if (_cur.type != kTokNewline && _cur.type != kTokEnd)
		return error();
	if (_cur.type == kTokNewline)
		advance();
	return true;
}

Node *Parser::parseSmallStatement() {
	int line = _cur.line;
	bool docString = _statements == 0 && _cur.type == kTokString;
	_statements++;
	if (!isName("from") && !docString)
		_futureAllowed = false;
	if (_cur.type == kTokName) {
		if (acceptName("print"))
			return parsePrint();
		if (acceptName("del")) {
			Node *n = new Node(kNodeDelete, line);
			Node *targets = parseExprList();
			if (!targets)
				return fail(n);
			if (targets->kind == kNodeTuple && targets->op == 1) {
				for (uint i = 0; i < targets->kids.size(); i++)
					n->add(targets->kids[i]);
				targets->kids.clear();
				delete targets;
			} else {
				n->add(targets);
			}
			for (uint i = 0; i < n->kids.size(); i++)
				if (!checkTarget(n->kids[i], true))
					return fail(n, "can't delete that");
			return n;
		}
		if (acceptName("pass"))
			return new Node(kNodePass, line);
		if (acceptName("break"))
			return new Node(kNodeBreak, line);
		if (acceptName("continue"))
			return new Node(kNodeContinue, line);
		if (acceptName("return")) {
			Node *n = new Node(kNodeReturn, line);
			if (_cur.type != kTokNewline && !isOp(";") && _cur.type != kTokEnd) {
				n->a = parseTestList();
				if (!n->a)
					return fail(n);
			}
			return n;
		}
		if (acceptName("yield")) {
			Node *n = new Node(kNodeYield, line);
			n->a = parseTestList();
			if (!n->a)
				return fail(n);
			return n;
		}
		if (acceptName("raise")) {
			Node *n = new Node(kNodeRaise, line);
			if (_cur.type != kTokNewline && !isOp(";") && _cur.type != kTokEnd) {
				n->a = parseTest();
				if (!n->a)
					return fail(n);
				if (accept(",")) {
					n->b = parseTest();
					if (!n->b)
						return fail(n);
					if (accept(",")) {
						n->c = parseTest();
						if (!n->c)
							return fail(n);
					}
				}
			}
			return n;
		}
		if (acceptName("import"))
			return parseImport();
		if (acceptName("from"))
			return parseFrom();
		if (acceptName("global")) {
			Node *n = new Node(kNodeGlobal, line);
			do {
				if (_cur.type != kTokName)
					return fail(n);
				Node *name = new Node(kNodeName, _cur.line);
				name->str = _cur.text;
				n->add(name);
				advance();
			} while (accept(","));
			return n;
		}
		if (acceptName("exec")) {
			Node *n = new Node(kNodeExec, line);
			n->a = parseExpr();
			if (!n->a)
				return fail(n);
			if (acceptName("in")) {
				n->b = parseTest();
				if (!n->b)
					return fail(n);
				if (accept(",")) {
					n->c = parseTest();
					if (!n->c)
						return fail(n);
				}
			}
			return n;
		}
		if (acceptName("assert")) {
			Node *n = new Node(kNodeAssert, line);
			n->a = parseTest();
			if (!n->a)
				return fail(n);
			if (accept(",")) {
				n->b = parseTest();
				if (!n->b)
					return fail(n);
			}
			return n;
		}
	}
	return parseExprStatement();
}

bool Parser::checkTarget(Node *n, bool del) {
	switch (n->kind) {
	case kNodeName:
		return true;
	case kNodeAttribute:
	case kNodeSubscript:
		return true;
	case kNodeTuple:
	case kNodeList:
		for (uint i = 0; i < n->kids.size(); i++)
			if (!checkTarget(n->kids[i], del))
				return false;
		return true;
	default:
		return false;
	}
}

static const char *assignError(Node *n) {
	switch (n->kind) {
	case kNodeConst: return "can't assign to literal";
	case kNodeCall: return "can't assign to function call";
	case kNodeLambda: return "can't assign to lambda";
	case kNodeBinOp: case kNodeUnaryOp: case kNodeBoolOp: case kNodeCompare: return "can't assign to operator";
	case kNodeListComp: return "can't assign to list comprehension";
	case kNodeTuple: case kNodeList:
		for (uint i = 0; i < n->kids.size(); i++) {
			const char *e = assignError(n->kids[i]);
			if (e)
				return e;
		}
		return nullptr;
	case kNodeName: case kNodeAttribute: case kNodeSubscript:
		return nullptr;
	default:
		return "can't assign to that";
	}
}

Node *Parser::parseExprStatement() {
	int line = _cur.line;
	Node *first = parseTestList();
	if (!first)
		return nullptr;
	static const struct { const char *op; int code; } augs[] = {
		{ "+=", kOpAdd }, { "-=", kOpSub }, { "*=", kOpMul }, { "/=", kOpDiv }, { "//=", kOpFloorDiv },
		{ "%=", kOpMod }, { "**=", kOpPow }, { ">>=", kOpRshift }, { "<<=", kOpLshift },
		{ "&=", kOpBitAnd }, { "^=", kOpBitXor }, { "|=", kOpBitOr }
	};
	if (_cur.type == kTokOp) {
		for (uint i = 0; i < ARRAYSIZE(augs); i++) {
			if (_cur.text == augs[i].op) {
				advance();
				if (first->kind != kNodeName && first->kind != kNodeAttribute && first->kind != kNodeSubscript)
					return fail(first, "augmented assign to tuple not possible");
				Node *n = new Node(kNodeAugAssign, line);
				n->a = first;
				n->op = augs[i].code;
				n->b = parseTestList();
				if (!n->b)
					return fail(n);
				return n;
			}
		}
	}
	if (!isOp("=")) {
		Node *n = new Node(kNodeExprStmt, line);
		n->a = first;
		return n;
	}
	Node *n = new Node(kNodeAssign, line);
	n->add(first);
	while (accept("=")) {
		Node *next = parseTestList();
		if (!next)
			return fail(n);
		n->add(next);
	}
	n->a = n->kids.back();
	n->kids.pop_back();
	for (uint i = 0; i < n->kids.size(); i++) {
		const char *e = assignError(n->kids[i]);
		if (e)
			return fail(n, e);
	}
	return n;
}

Node *Parser::parsePrint() {
	Node *n = new Node(kNodePrint, _cur.line);
	n->op = 1;
	if (accept(">>")) {
		n->a = parseTest();
		if (!n->a)
			return fail(n);
		if (!accept(","))
			return n;
	}
	while (_cur.type != kTokNewline && !isOp(";") && _cur.type != kTokEnd) {
		Node *v = parseTest();
		if (!v)
			return fail(n);
		n->add(v);
		n->op = 1;
		if (!accept(","))
			break;
		n->op = 0;
	}
	if (n->kids.empty())
		n->op = 1;
	return n;
}

Node *Parser::parseDotted(Common::String &name) {
	if (_cur.type != kTokName)
		return nullptr;
	name = _cur.text;
	advance();
	while (accept(".")) {
		if (_cur.type != kTokName)
			return nullptr;
		name += ".";
		name += _cur.text;
		advance();
	}
	return (Node *)1;
}

Node *Parser::parseImport() {
	Node *n = new Node(kNodeImport, _cur.line);
	do {
		Node *alias = new Node(kNodeAlias, _cur.line);
		n->add(alias);
		if (!parseDotted(alias->str))
			return fail(n);
		if (isName("as")) {
			advance();
			if (_cur.type != kTokName)
				return fail(n);
			alias->value = newStr(_cur.text);
			advance();
		}
	} while (accept(","));
	return n;
}

Node *Parser::parseFrom() {
	Node *n = new Node(kNodeImportFrom, _cur.line);
	if (!parseDotted(n->str))
		return fail(n);
	if (!expectName("import"))
		return fail(n);
	bool future = n->str == "__future__";
	if (future && !_futureAllowed) {
		int line = n->line;
		fail(n, "from __future__ imports must occur at the beginning of the file");
		_errorLine = line;
		return nullptr;
	}

	if (!future)
		_futureAllowed = false;
	if (accept("*")) {
		if (future)
			return fail(n, "future statement does not support import *");
		n->op = 1;
		return n;
	}
	if (future && _cur.type == kTokName && _cur.text != "nested_scopes" && _cur.text != "generators" && _cur.text != "division") {
		Common::String msg = "future feature " + _cur.text + " is not defined";
		_errorLine = _cur.line;
		return fail(n, msg.c_str());
	}

	do {
		if (_cur.type != kTokName)
			return fail(n);
		Node *alias = new Node(kNodeAlias, _cur.line);
		alias->str = _cur.text;
		n->add(alias);
		advance();
		if (isName("as")) {
			advance();
			if (_cur.type != kTokName)
				return fail(n);
			alias->value = newStr(_cur.text);
			advance();
		}
	} while (accept(","));
	return n;
}

Node *Parser::parseCompound() {
	_futureAllowed = false;
	_statements++;
	if (isName("if"))
		return parseIf();
	if (isName("while"))
		return parseWhile();
	if (isName("for"))
		return parseFor();
	if (isName("try"))
		return parseTry();
	if (isName("def"))
		return parseFuncDef();
	return parseClassDef();
}

Node *Parser::parseSuite() {
	Node *suite = new Node(kNodeSuite, _cur.line);
	if (_cur.type != kTokNewline) {
		if (!parseSimpleStatement(suite))
			return fail(suite);
		return suite;
	}
	advance();
	if (_cur.type != kTokIndent)
		return fail(suite, "expected an indented block");
	advance();
	while (!failed() && _cur.type != kTokDedent && _cur.type != kTokEnd) {
		if (_cur.type == kTokNewline) {
			advance();
			continue;
		}
		Node *s = parseStatement();
		if (!s)
			return fail(suite);
		if (s->kind == kNodeSuite) {
			for (uint i = 0; i < s->kids.size(); i++)
				suite->add(s->kids[i]);
			s->kids.clear();
			delete s;
		} else {
			suite->add(s);
		}
	}
	if (failed())
		return fail(suite);
	if (_cur.type == kTokDedent)
		advance();
	return suite;
}

Node *Parser::parseIf() {
	Node *n = new Node(kNodeIf, _cur.line);
	advance(); // if / elif
	n->a = parseTest();
	if (!n->a || !expect(":"))
		return fail(n);
	n->c = parseSuite();
	if (!n->c)
		return fail(n);
	if (isName("elif")) {
		n->d = parseIf();
		if (!n->d)
			return fail(n);
	} else if (acceptName("else")) {
		if (!expect(":"))
			return fail(n);
		n->d = parseSuite();
		if (!n->d)
			return fail(n);
	}
	return n;
}

Node *Parser::parseWhile() {
	Node *n = new Node(kNodeWhile, _cur.line);
	advance();
	n->a = parseTest();
	if (!n->a || !expect(":"))
		return fail(n);
	n->c = parseSuite();
	if (!n->c)
		return fail(n);
	if (acceptName("else")) {
		if (!expect(":"))
			return fail(n);
		n->d = parseSuite();
		if (!n->d)
			return fail(n);
	}
	return n;
}

Node *Parser::parseFor() {
	Node *n = new Node(kNodeFor, _cur.line);
	advance();
	n->a = parseExprList();
	if (!n->a)
		return fail(n);
	const char *e = assignError(n->a);
	if (e)
		return fail(n, e);
	if (!expectName("in"))
		return fail(n);
	n->b = parseTestList();
	if (!n->b || !expect(":"))
		return fail(n);
	n->c = parseSuite();
	if (!n->c)
		return fail(n);
	if (acceptName("else")) {
		if (!expect(":"))
			return fail(n);
		n->d = parseSuite();
		if (!n->d)
			return fail(n);
	}
	return n;
}

Node *Parser::parseTry() {
	int line = _cur.line;
	advance();
	if (!expect(":"))
		return nullptr;
	Node *body = parseSuite();
	if (!body)
		return nullptr;
	if (acceptName("finally")) {
		Node *n = new Node(kNodeTryFinally, line);
		n->a = body;
		if (!expect(":"))
			return fail(n);
		n->b = parseSuite();
		if (!n->b)
			return fail(n);
		return n;
	}
	Node *n = new Node(kNodeTryExcept, line);
	n->a = body;
	if (!isName("except"))
		return fail(n);
	while (isName("except")) {
		Node *h = new Node(kNodeExceptHandler, _cur.line);
		n->add(h);
		advance();
		if (!isOp(":")) {
			h->a = parseTest();
			if (!h->a)
				return fail(n);
			if (accept(",")) {
				h->b = parseTest();
				if (!h->b)
					return fail(n);
				const char *e = assignError(h->b);
				if (e)
					return fail(n, e);
			}
		}
		if (!expect(":"))
			return fail(n);
		h->c = parseSuite();
		if (!h->c)
			return fail(n);
	}
	if (acceptName("else")) {
		if (!expect(":"))
			return fail(n);
		n->d = parseSuite();
		if (!n->d)
			return fail(n);
	}
	return n;
}

Node *Parser::parseFpdef() {
	Node *p = new Node(kNodeParam, _cur.line);
	if (accept("(")) {
		// A tuple parameter
		p->op = 1;
		do {
			if (isOp(")"))
				break;
			Node *sub = parseFpdef();
			if (!sub)
				return fail(p);
			p->add(sub);
		} while (accept(","));
		if (!expect(")"))
			return fail(p);
		return p;
	}
	if (_cur.type != kTokName || isKeyword())
		return fail(p);
	p->str = _cur.text;
	advance();
	return p;
}

Node *Parser::parseParams(const char *closer) {
	Node *params = new Node(kNodeParams, _cur.line);
	bool sawDefault = false;
	while (!isOp(closer)) {
		if (accept("*")) {
			if (_cur.type != kTokName)
				return fail(params);
			Node *p = new Node(kNodeParam, _cur.line);
			p->str = _cur.text;
			p->op = 2; // *args
			params->add(p);
			advance();
			if (accept(",")) {
				if (!expect("**"))
					return fail(params);
				if (_cur.type != kTokName)
					return fail(params);
				Node *k = new Node(kNodeParam, _cur.line);
				k->str = _cur.text;
				k->op = 3; // **kwargs
				params->add(k);
				advance();
			}
			break;
		}
		if (accept("**")) {
			if (_cur.type != kTokName)
				return fail(params);
			Node *k = new Node(kNodeParam, _cur.line);
			k->str = _cur.text;
			k->op = 3;
			params->add(k);
			advance();
			break;
		}
		Node *p = parseFpdef();
		if (!p)
			return fail(params);
		params->add(p);
		if (accept("=")) {
			p->b = parseTest();
			if (!p->b)
				return fail(params);
			sawDefault = true;
		} else if (sawDefault) {
			return fail(params, "non-default argument follows default argument");
		}
		if (!accept(","))
			break;
	}
	return params;
}

Node *Parser::parseFuncDef() {
	Node *n = new Node(kNodeFunctionDef, _cur.line);
	advance();
	if (_cur.type != kTokName)
		return fail(n);
	n->str = _cur.text;
	advance();
	if (!expect("("))
		return fail(n);
	n->a = parseParams(")");
	if (!n->a || !expect(")") || !expect(":"))
		return fail(n);
	n->b = parseSuite();
	if (!n->b)
		return fail(n);
	return n;
}

Node *Parser::parseClassDef() {
	Node *n = new Node(kNodeClassDef, _cur.line);
	advance();
	if (_cur.type != kTokName)
		return fail(n);
	n->str = _cur.text;
	advance();
	if (accept("(")) {
		if (!isOp(")")) {
			Node *bases = parseTestList();
			if (!bases)
				return fail(n);
			if (bases->kind == kNodeTuple && bases->op == 1) {
				for (uint i = 0; i < bases->kids.size(); i++)
					n->add(bases->kids[i]);
				bases->kids.clear();
				delete bases;
			} else {
				n->add(bases);
			}
		}
		if (!expect(")"))
			return fail(n);
	}
	if (!expect(":"))
		return fail(n);
	n->b = parseSuite();
	if (!n->b)
		return fail(n);
	return n;
}

// testlist: a bare tuple gets op = 1
Node *Parser::parseTestList(bool allowTrailing) {
	int line = _cur.line;
	Node *first = parseTest();
	if (!first)
		return nullptr;
	if (!isOp(","))
		return first;
	Node *t = new Node(kNodeTuple, line);
	t->op = 1;
	t->add(first);
	while (accept(",")) {
		if (_cur.type == kTokNewline || _cur.type == kTokEnd || isOp(")") || isOp("=") || isOp(":") || isOp(";") ||
		    isOp("]") || isOp("}") || (_cur.type == kTokOp && _cur.text.size() >= 2 && _cur.text.lastChar() == '=' &&
		    _cur.text != "==" && _cur.text != "<=" && _cur.text != ">=" && _cur.text != "!="))
			break;
		Node *next = parseTest();
		if (!next)
			return fail(t);
		t->add(next);
	}
	return t;
}

Node *Parser::parseExprList() {
	int line = _cur.line;
	Node *first = parseExpr();
	if (!first)
		return nullptr;
	if (!isOp(","))
		return first;
	Node *t = new Node(kNodeTuple, line);
	t->op = 1;
	t->add(first);
	while (accept(",")) {
		if (isName("in") || _cur.type == kTokNewline || isOp("=") || isOp(";") || _cur.type == kTokEnd)
			break;
		Node *next = parseExpr();
		if (!next)
			return fail(t);
		t->add(next);
	}
	return t;
}

Node *Parser::parseLambda() {
	Node *n = new Node(kNodeLambda, _cur.line);
	advance();
	n->a = parseParams(":");
	if (!n->a || !expect(":"))
		return fail(n);
	n->b = parseTest();
	if (!n->b)
		return fail(n);
	return n;
}

Node *Parser::parseTest() {
	if (isName("lambda"))
		return parseLambda();
	int line = _cur.line;
	Node *first = parseAndTest();
	if (!first)
		return nullptr;
	if (!isName("or"))
		return first;
	Node *n = new Node(kNodeBoolOp, line);
	n->op = kOpOr;
	n->add(first);
	while (acceptName("or")) {
		Node *next = parseAndTest();
		if (!next)
			return fail(n);
		n->add(next);
	}
	return n;
}

Node *Parser::parseAndTest() {
	int line = _cur.line;
	Node *first = parseNotTest();
	if (!first)
		return nullptr;
	if (!isName("and"))
		return first;
	Node *n = new Node(kNodeBoolOp, line);
	n->op = kOpAnd;
	n->add(first);
	while (acceptName("and")) {
		Node *next = parseNotTest();
		if (!next)
			return fail(n);
		n->add(next);
	}
	return n;
}

Node *Parser::parseNotTest() {
	if (isName("not")) {
		Node *n = new Node(kNodeUnaryOp, _cur.line);
		advance();
		n->op = kOpNot;
		n->a = parseNotTest();
		if (!n->a)
			return fail(n);
		return n;
	}
	return parseComparison();
}

Node *Parser::parseComparison() {
	int line = _cur.line;
	Node *first = parseExpr();
	if (!first)
		return nullptr;
	Node *n = nullptr;
	for (;;) {
		int op = -1;
		if (_cur.type == kTokOp) {
			if (_cur.text == "<") op = kLT;
			else if (_cur.text == "<=") op = kLE;
			else if (_cur.text == "==") op = kEQ;
			else if (_cur.text == "!=" || _cur.text == "<>") op = kNE;
			else if (_cur.text == ">") op = kGT;
			else if (_cur.text == ">=") op = kGE;
			if (op >= 0)
				advance();
		} else if (isName("in")) {
			op = 6;
			advance();
		} else if (isName("not")) {
			advance();
			if (!expectName("in")) {
				delete n;
				delete first;
				return nullptr;
			}
			op = 7;
		} else if (isName("is")) {
			advance();
			op = 8;
			if (acceptName("not"))
				op = 9;
		}
		if (op < 0)
			break;
		if (!n) {
			n = new Node(kNodeCompare, line);
			n->a = first;
		}
		Node *right = parseExpr();
		if (!right)
			return fail(n);
		Node *wrap
 = new Node(kNodeSuite, right->line); // holds the operator for this comparison
		wrap->op = op;
		wrap->a = right;
		n->add(wrap);
	}
	return n ? n : first;
}

#define BINARY_LEVEL(fname, sub, test, opcode) \
	Node *Parser::fname() { \
		int line = _cur.line; \
		Node *left = sub(); \
		if (!left) \
			return nullptr; \
		for (;;) { \
			int op = -1; \
			test \
			if (op < 0) \
				return left; \
			advance(); \
			Node *right = sub(); \
			if (!right) \
				return fail(left); \
			Node *n = new Node(kNodeBinOp, line); \
			n->op = op; \
			n->a = left; \
			n->b = right; \
			left = n; \
		} \
	}

BINARY_LEVEL(parseExpr, parseXorExpr, if (isOp("|")) op = kOpBitOr;, 0)
BINARY_LEVEL(parseXorExpr, parseAndExpr, if (isOp("^")) op = kOpBitXor;, 0)
BINARY_LEVEL(parseAndExpr, parseShiftExpr, if (isOp("&")) op = kOpBitAnd;, 0)
BINARY_LEVEL(parseShiftExpr, parseArithExpr, if (isOp("<<")) op = kOpLshift; else if (isOp(">>")) op = kOpRshift;, 0)
BINARY_LEVEL(parseArithExpr, parseTerm, if (isOp("+")) op = kOpAdd; else if (isOp("-")) op = kOpSub;, 0)
BINARY_LEVEL(parseTerm, parseFactor, if (isOp("*")) op = kOpMul; else if (isOp("/")) op = kOpDiv; else if (isOp("%")) op = kOpMod; else if (isOp("//")) op = kOpFloorDiv;, 0)

Node *Parser::parseFactor() {
	int op = -1;
	if (isOp("+"))
		op = kOpPos;
	else if (isOp("-"))
		op = kOpNeg;
	else if (isOp("~"))
		op = kOpInvert;
	if (op < 0)
		return parsePower();
	int line = _cur.line;
	advance();
	// "-NUMBER" is a constant, as CPython 2.2 folds it (so -2147483648 is an int)
	if (op == kOpNeg && _cur.type == kTokNumber) {
		Token t = _cur;
		t.text = "-" + t.text;
		advance();
		if (!isOp("**") && !isOp(".") && !isOp("[") && !isOp("(")) {
			Node *c = makeNumber(t);
			return c;
		}
		// Not a plain number after all (e.g. -2**2): negate the power
		Token plain = t;
		plain.text = Common::String(t.text.c_str() + 1);
		Node *atom = makeNumber(plain);
		if (!atom)
			return nullptr;
		Node *pw = parseTrailers(atom);
		if (!pw)
			return nullptr;
		if (accept("**")) {
			Node *b = new Node(kNodeBinOp, line);
			b->op = kOpPow;
			b->a = pw;
			b->b = parseFactor();
			if (!b->b)
				return fail(b);
			pw = b;
		}
		Node *n = new Node(kNodeUnaryOp, line);
		n->op = kOpNeg;
		n->a = pw;
		return n;
	}
	Node *n = new Node(kNodeUnaryOp, line);
	n->op = op;
	n->a = parseFactor();
	if (!n->a)
		return fail(n);
	return n;
}

Node *Parser::parsePower() {
	int line = _cur.line;
	Node *atom = parseAtom();
	if (!atom)
		return nullptr;
	Node *n = parseTrailers(atom);
	if (!n)
		return nullptr;
	if (accept("**")) {
		Node *b = new Node(kNodeBinOp, line);
		b->op = kOpPow;
		b->a = n;
		b->b = parseFactor();
		if (!b->b)
			return fail(b);
		return b;
	}
	return n;
}

Node *Parser::parseTrailers(Node *n) {
	for (;;) {
		int line = _cur.line;
		if (accept("(")) {
			n = parseArgs(n);
			if (!n)
				return nullptr;
		} else if (accept("[")) {
			Node *s = new Node(kNodeSubscript, line);
			s->a = n;
			Node *first = parseSubscript();
			if (!first)
				return fail(s);
			if (isOp(",")) {
				Node *t = new Node(kNodeTuple, line);
				t->add(first);
				while (accept(",")) {
					if (isOp("]"))
						break;
					Node *next = parseSubscript();
					if (!next) {
						delete t;
						return fail(s);
					}
					t->add(next);
				}
				first = t;
			}
			s->b = first;
			if (!expect("]"))
				return fail(s);
			n = s;
		} else if (accept(".")) {
			if (_cur.type != kTokName)
				return fail(n);
			Node *a = new Node(kNodeAttribute, line);
			a->a = n;
			a->str = _cur.text;
			advance();
			n = a;
		} else {
			return n;
		}
	}
}

Node *Parser::parseSubscript() {
	int line = _cur.line;
	if (isOp(".")) {
		advance();
		if (!expect(".") || !expect("."))
			return nullptr;
		return new Node(kNodeEllipsis, line);
	}
	Node *lower = nullptr;
	if (!isOp(":")) {
		lower = parseTest();
		if (!lower)
			return nullptr;
		if (!isOp(":"))
			return lower;
	}
	advance(); // ':'
	Node *upper = nullptr;
	if (!isOp("]") && !isOp(",") && !isOp(":")) {
		upper = parseTest();
		if (!upper) {
			delete lower;
			return nullptr;
		}
	}
	if (isOp(":")) {
		advance();
		Node *s = new Node(kNodeSliceObj, line);
		s->a = lower;
		s->b = upper;
		if (!isOp("]") && !isOp(",")) {
			s->c = parseTest();
			if (!s->c)
				return fail(s);
		}
		s->op = 1; // the step was written, even if empty
		return s;
	}
	Node *s = new Node(kNodeSlice, line);
	s->a = lower;
	s->b = upper;
	return s;
}

Node *Parser::parseArgs(Node *func) {
	Node *n = new Node(kNodeCall, func->line);
	n->a = func;
	bool sawKeyword = false;
	while (!isOp(")")) {
		if (accept("*")) {
			n->b = parseTest();
			if (!n->b)
				return fail(n);
			if (accept(",")) {
				if (!expect("**"))
					return fail(n);
				n->c = parseTest();
				if (!n->c)
					return fail(n);
			}
			break;
		}
		if (accept("**")) {
			n->c = parseTest();
			if (!n->c)
				return fail(n);
			break;
		}
		Node *arg = parseTest();
		if (!arg)
			return fail(n);
		if (isOp("=")) {
			advance();
			if (arg->kind != kNodeName) {
				delete arg;
				return fail(n, "keyword can't be an expression");
			}
			Node *kw = new Node(kNodeKeyword, arg->line);
			kw->str = arg->str;
			delete arg;
			kw->a = parseTest();
			if (!kw->a) {
				delete kw;
				return fail(n);
			}
			n->add(kw);
			sawKeyword = true;
		} else {
			if (sawKeyword) {
				delete arg;
				return fail(n, "non-keyword arg after keyword arg");
			}
			n->add(arg);
		}
		if (!accept(","))
			break;
	}
	if (!expect(")"))
		return fail(n);
	return n;
}

Node *Parser::makeNumber(const Token &t) {
	Node *n = new Node(kNodeConst, t.line);
	Common::String s = t.text;
	char last = s.lastChar();
	if (last == 'j' || last == 'J') {
		Common::String num(s.c_str(), s.size() - 1);
		n->value = ObjRef(new Complex(0.0, strtod(num.c_str(), nullptr)));
		return n;
	}
	if (last == 'l' || last == 'L') {
		Common::String num(s.c_str(), s.size() - 1);
		n->value = longFromString(num.c_str(), 0, false);
		if (!n->value) {
			clearError();
			return fail(n);
		}
		return n;
	}
	bool isFloat = false;
	bool hex = s.contains("0x") || s.contains("0X");
	if (!hex) {
		for (uint i = 0; i < s.size(); i++)
			if (s[i] == '.' || s[i] == 'e' || s[i] == 'E')
				isFloat = true;
	}
	if (isFloat) {
		n->value = newFloat(strtod(s.c_str(), nullptr));
		return n;
	}
	// An int; hex and octal literals wrap to 32 bits, decimals too large become longs
	bool neg = s[0] == '-';
	const char *digits = s.c_str() + (neg ? 1 : 0);
	uint64 v = 0;
	bool big = false;
	int base = 10;
	const char *d = digits;
	if (d[0] == '0' && (d[1] == 'x' || d[1] == 'X')) {
		base = 16;
		d += 2;
	} else if (d[0] == '0' && d[1]) {
		base = 8;
		d++;
	}
	for (; *d; d++) {
		int dv = (*d <= '9') ? *d - '0' : (*d | 0x20) - 'a' + 10;
		v = v * base + dv;
		if (v > 0xffffffffULL)
			big = true;
	}
	if (base != 10 && !big) {
		int32 x = (int32)(uint32)v;
		n->value = newInt(neg ? -x : x);
		return n;
	}
	if (!big && ((!neg && v <= 0x7fffffffULL) || (neg && v <= 0x80000000ULL))) {
		n->value = newInt(neg ? (int32)(-(int64)v) : (int32)v);
		return n;
	}
	n->value = longFromString(s.c_str(), base == 10 ? 10 : 0, false);
	if (!n->value) {
		clearError();
		return fail(n);
	}
	return n;
}

Node *Parser::parseListMaker(int line) {
	Node *first = parseTest();
	if (!first)
		return nullptr;
	if (isName("for")) {
		Node *lc = new Node(kNodeListComp, line);
		lc->a = first;
		while (isName("for") || isName("if")) {
			if (acceptName("for")) {
				Node *f = new Node(kNodeCompFor, _cur.line);
				lc->add(f);
				f->a = parseExprList();
				if (!f->a || !expectName("in"))
					return fail(lc);
				// testlist_safe: tests separated by commas, at least two if a tuple
				f->b = parseTest();
				if (!f->b)
					return fail(lc);
				if (isOp(",")) {
					Node *t = new Node(kNodeTuple, f->b->line);
					t->add(f->b);
					while (accept(",")) {
						if (isOp("]"))
							break;
						Node *next = parseTest();
						if (!next) {
							f->b = t;
							return fail(lc);
						}
						t->add(next);
					}
					f->b = t;
				}
			} else {
				advance();
				Node *c = new Node(kNodeCompIf, _cur.line);
				lc->add(c);
				c->a = parseTest();
				if (!c->a)
					return fail(lc);
			}
		}
		return lc;
	}
	Node *l = new Node(kNodeList, line);
	l->add(first);
	while (accept(",")) {
		if (isOp("]"))
			break;
		Node *next = parseTest();
		if (!next)
			return fail(l);
		l->add(next);
	}
	return l;
}

Node *Parser::parseAtom() {
	int line = _cur.line;
	if (_cur.type == kTokOp) {
		if (accept("(")) {
			if (accept(")"))
				return new Node(kNodeTuple, line);
			Node *e = parseTestList();
			if (!e)
				return nullptr;
			if (!expect(")"))
				return fail(e);
			if (e->kind == kNodeTuple)
				e->op = 0; // a parenthesised tuple
			return e;
		}
		if (accept("[")) {
			if (accept("]"))
				return new Node(kNodeList, line);
			Node *l = parseListMaker(line);
			if (!l)
				return nullptr;
			if (!expect("]"))
				return fail(l);
			return l;
		}
		if (accept("{")) {
			Node *d = new Node(kNodeDict, line);
			while (!isOp("}")) {
				Node *k = parseTest();
				if (!k)
					return fail(d);
				d->add(k);
				if (!expect(":"))
					return fail(d);
				Node *v = parseTest();
				if (!v)
					return fail(d);
				d->add(v);
				if (!accept(","))
					break;
			}
			if (!expect("}"))
				return fail(d);
			return d;
		}
		if (accept("`")) {
			Node *r = new Node(kNodeRepr, line);
			r->a = parseTestList();
			if (!r->a || !expect("`"))
				return fail(r);
			return r;
		}
		error();
		return nullptr;
	}
	if (_cur.type == kTokNumber) {
		Token t = _cur;
		advance();
		return makeNumber(t);
	}
	if (_cur.type == kTokString) {
		Common::String bytes;
		bool unicode = false;
		while (_cur.type == kTokString) {
			bytes += _cur.bytes;
			unicode |= _cur.unicode;
			advance();
		}
		Node *c = new Node(kNodeConst, line);
		if (unicode) {
			c->value = unicodeFromStr(newStr(bytes).get(), "utf-8", "replace");
			clearError();
		} else {
			c->value = newStr(bytes);
		}
		return c;
	}
	if (_cur.type == kTokName && !isKeyword()) {
		Node *n = new Node(kNodeName, line);
		n->str = _cur.text;
		advance();
		return n;
	}
	error();
	return nullptr;
}

Node *parseSource(const char *source, const char *filename, int mode) {
	Parser p(source, filename);
	Node *tree = mode == kCompileEval ? p.parseEvalInput() : p.parseFileInput();
	if (!tree || p.failed()) {
		delete tree;
		p.reportError();
		return nullptr;
	}
	return tree;
}

} // End of namespace Python
} // End of namespace Yaga
