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

#include "mspam/detection.h"
#include "mspam/script.h"

namespace MSPAM {

// The C runtime's character classes as the original tests them
static bool isDigitChar(char c) {
	return c >= '0' && c <= '9';
}

static bool isAlphaChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool isAlnumChar(char c) {
	return isAlphaChar(c) || isDigitChar(c);
}

// The name the variable table stores: up to the first character that isn't a letter or digit
static Common::String variableName(const char *p) {
	Common::String name;
	while (*p && isAlnumChar(*p))
		name += *p++;
	return name;
}

int Variables::find(const Common::String &name) const {
	for (uint i = 0; i < _vars.size(); i++) {
		if (_vars[i].name == name)
			return i;
	}
	return -1;
}

const Common::String &Variables::get(const Common::String &name) {
	int i = find(name);
	if (i < 0) {
		Var v;
		v.name = name;
		v.value = "0";
		v.mask = 0;
		_vars.push_back(v);
		i = _vars.size() - 1;
	}
	return _vars[i].value;
}

void Variables::set(const Common::String &name, const Common::String &value) {
	get(name);
	Var &v = _vars[find(name)];
	v.value = value;
	v.mask = 0;
	debugC(2, kDebugScript, "  %s = '%s'", name.c_str(), value.c_str());
}

void Variables::free(const Common::String &pattern) {
	bool prefix = pattern.hasSuffix("%");
	Common::String match = prefix ? Common::String(pattern.c_str(), pattern.size() - 1) : pattern;
	for (int i = _vars.size() - 1; i >= 0; i--) {
		if (pattern.empty() || (prefix ? _vars[i].name.hasPrefix(match) : _vars[i].name == match))
			_vars.remove_at(i);
	}
}

uint32 &Variables::randomMask(const Common::String &name) {
	get(name);
	return _vars[find(name)].mask;
}

int32 Evaluator::parseNumber(const char *&p) {
	// FUN_1008_5a94: anything but a letter or digit reads as 0 (so "-2" does too)
	if (!*p || !isAlnumChar(*p))
		return 0;
	int32 value = *p - '0';
	while (isDigitChar(*++p))
		value = value * 10 + (*p - '0');
	return value;
}

// FUN_1008_5c6e, with its quirks: an unknown name means the path segment, and
// "ys" is never recognised
int Evaluator::propertyCode(const char *p) {
	char c = p[0];
	if (c == 'z' || c == 'Z')
		return kPropZ;
	if ((byte)c > 'z')
		return kPropPathSeg;
	if (c == 'C' || c == 'c')
		return kPropCursor;
	if (c == 'D' || c == 'd') {
		p++;
		if (*p == 'U' || *p == 'u')
			return kPropDuration;
	} else if (c == 'S' || c == 's') {
		goto second;
	} else if (c == 'V' || c == 'v') {
		return kPropNone;
	} else if (c == 'X' || c == 'x') {
		return (p[1] == 'S' || p[1] == 's') ? kPropWidth : kPropX;
	} else if (c == 'Y' || c == 'y') {
		return kPropY;
	} else if (c != 'P' && c != 'p') {
		return kPropPathSeg;
	}
	p++;
	c = *p;
	if (c == 'l' || c == 'L')
		return kPropPlay;
	if (c == 'A' || c == 'a')
		return kPropPathSeg;
	if (c == 'I' || c == 'i')
		return kPropPibList;
second:
	c = p[1];
	if (c == 't' || c == 'T')
		return kPropStart;
	if ((byte)c > 't')
		return kPropNone;
	if (c == 'H' || c == 'h')
		return kPropShow;
	if (c == 'O' || c == 'o')
		return kPropSound;
	return kPropNone;
}

int32 Evaluator::apply(char op, int32 a, int32 b) {
	switch (op) {
	case '*':
		return a * b;
	case '+':
		return a + b;
	case '-':
		return a - b;
	case '/':
		return b ? a / b : 0;
	default:
		return a;
	}
}

// FUN_1008_563a
void Evaluator::next(const char *&p, TokenInfo &tok) {
	tok.start = p;
	tok.hasValue = false;
	tok.value = 0;
	char c = *p;
	if (!c) {
		tok.type = kTokEnd;
		return;
	}
	p++;
	switch (c) {
	case '>':
	case '!':
	case '<':
		tok.type = kTokCompare;
		return;
	case '\'':
		tok.type = kTokString;
		return;
	case '*':
	case '+':
	case '-':
	case '/':
		tok.type = kTokOperator;
		return;
	case ';':
		tok.type = kTokSeparator;
		return;
	case '=':
		tok.type = kTokAssign;
		return;
	default:
		break;
	}
	p--;
	tok.hasValue = true;
	if (isDigitChar(c)) {
		tok.type = kTokNumber;
		tok.value = parseNumber(p);
		return;
	}

	// FUN_1008_5b3e: the first character and the letters and digits after it,
	// then '.' for an element or '^' for an element named by a variable
	const char *q = p + 1;
	while (*q && isAlnumChar(*q))
		q++;
	Common::String name(p, q);
	char term = *q;
	Common::String element;
	if (term == '^')
		element = _vars.get(variableName(p));
	else if (term == '.')
		element = name;
	p = q;
	if ((term == '^' || term == '.') && _host.hasElement(element)) {
		p++;
		tok.type = kTokProperty;
		tok.element = element;
		tok.prop = propertyCode(p);
		tok.value = _host.getProperty(element, tok.prop);
		while (*p && isAlphaChar(*p))
			p++;
		return;
	}
	tok.type = kTokVariable;
	tok.variable = variableName(tok.start);
	const char *value = _vars.get(tok.variable).c_str();
	tok.value = parseNumber(value);
}

int32 Evaluator::execute(const Common::String &expr) {
	debugC(2, kDebugScript, "Eval \"%s\"", expr.c_str());
	const char *p = expr.c_str();
	TokenInfo tok;
	int32 acc = 0;
	for (;;) {
		const char *statement = p;
		bool assign = false, numeric = true;
		Common::String str;
		acc = 0;
		for (;;) {
			next(p, tok);
			if (tok.type == kTokSeparator || tok.type == kTokEnd)
				break;
			if (tok.hasValue) {
				acc = tok.value;
			} else if (tok.type == kTokOperator) {
				char op = *tok.start;
				TokenInfo rhs;
				next(p, rhs);
				acc = apply(op, acc, rhs.hasValue ? rhs.value : 0);
				if (rhs.type == kTokEnd)
					break;
			} else if (tok.type == kTokAssign) {
				assign = true;
			} else if (tok.type == kTokString) {
				numeric = false;
				str = p;
				p += strlen(p);
			}
		}
		if (assign) {
			const char *q = statement;
			TokenInfo lhs;
			next(q, lhs);
			if (lhs.type == kTokProperty)
				_host.setProperty(lhs.element, lhs.prop, acc);
			else if (lhs.type == kTokVariable)
				_vars.set(lhs.variable, numeric ? Common::String::format("%d", (int)acc) : str);
		}
		if (tok.type != kTokSeparator)
			return acc;
	}
}

bool Evaluator::test(const Common::String &expr) {
	const char *p = expr.c_str();
	TokenInfo tok;
	int32 acc = 0;
	bool result = false;
	for (;;) {
		next(p, tok);
		if (tok.type == kTokEnd)
			break;
		if (tok.hasValue) {
			acc = tok.value;
		} else if (tok.type == kTokOperator) {
			char op = *tok.start;
			TokenInfo rhs;
			next(p, rhs);
			acc = apply(op, acc, rhs.hasValue ? rhs.value : 0);
		} else if (tok.type == kTokCompare || tok.type == kTokAssign) {
			char op = *tok.start;
			bool orEqual = *p == '=';
			if (orEqual)
				p++;
			TokenInfo rhs;
			next(p, rhs);
			int32 b = rhs.hasValue ? rhs.value : 0;
			switch (op) {
			case '>':
				result = orEqual ? acc >= b : acc > b;
				break;
			case '<':
				result = orEqual ? acc <= b : acc < b;
				break;
			case '!':
				result = acc != b;
				break;
			default:
				result = acc == b;
				break;
			}
		}
	}
	debugC(2, kDebugScript, "If \"%s\" -> %d", expr.c_str(), result);
	return result;
}

} // End of namespace MSPAM
