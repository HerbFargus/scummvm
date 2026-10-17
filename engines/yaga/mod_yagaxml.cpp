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

#include "yaga/detection.h"
#include "yaga/modules.h"
#include "yaga/resources.h"
#include "yaga/python/internal.h"

// yagaxml: the engine's XML parser (expat underneath, in the original). The
// game drives it SAX-style:
//
//     parser = yagaxml.XmlSystem().CreateParser()
//     parser.contentHandler = handler   # an IContentHandler subclass
//     if not parser.ParseFile(path): raise parser.errorString, path
//
// and the handler gets StartElement(name, attrs) and EndElement(name), attrs
// being a list of objects with .name and .value. The game's data carries
// everything in attributes; its handlers' Text and Comment do nothing, so
// character data and comments are not passed on.

namespace Yaga {

using namespace Python;

static Resources *g_res = nullptr;

static Type g_attributeType("yagaxml.Attribute", &g_objectType);
static Type g_contentHandlerType("yagaxml.IContentHandler", &g_objectType);
static Type g_parserType("yagaxml.Parser", &g_objectType);
static Type g_xmlSystemType("yagaxml.XmlSystem", &g_objectType);

struct AttributeObject : public Object {
	Ref<Str> _name, _value;
	AttributeObject(Str *n, Str *v) : Object(&g_attributeType), _name(n), _value(v) {}
};

static ObjRef attrGetName(Object *o) { return ObjRef(((AttributeObject *)o)->_name.get()); }
static ObjRef attrGetValue(Object *o) { return ObjRef(((AttributeObject *)o)->_value.get()); }

static const GetSetDef g_attributeGetSets[] = {
	{ "name", attrGetName, nullptr },
	{ "value", attrGetValue, nullptr },
	{ nullptr, nullptr, nullptr }
};

static ObjRef attrRepr(Object *o) {
	AttributeObject *a = (AttributeObject *)o;
	ObjRef v = repr(a->_value.get());
	if (!v)
		return ObjRef();
	return newStr(a->_name->toString() + "=" + ((Str *)v.get())->toString());
}

// ---- A small XML reader ------------------------------------------------------------

// Reports elements to a content handler; errors are worded as expat's
class XmlReader {
public:
	XmlReader(const Common::String &text, Object *handler) : _s(text), _pos(0), _line(1), _col(0), _handler(handler) {}

	// false with _error set for bad XML, or with a Python error raised by the handler
	bool parse();
	Common::String _error;
	bool _pythonError = false;

private:
	bool fail(const char *what) {
		_error = Common::String::format("%s: line %d, column %d", what, _line, _col);
		return false;
	}
	bool atEnd() const { return _pos >= _s.size(); }
	char peek(uint ahead = 0) const { return _pos + ahead < _s.size() ? _s[_pos + ahead] : 0; }
	void advance(uint n = 1) {
		for (uint i = 0; i < n && _pos < _s.size(); i++) {
			if (_s[_pos] == '\n') {
				_line++;
				_col = 0;
			} else {
				_col++;
			}
			_pos++;
		}
	}
	bool startsWith(const char *t) const {
		uint n = strlen(t);
		return _pos + n <= _s.size() && strncmp(_s.c_str() + _pos, t, n) == 0;
	}
	void skipSpace() {
		while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r'))
			advance();
	}
	static bool isNameChar(char c) {
		return Common::isAlnum(c) || c == '_' || c == ':' || c == '-' || c == '.' || (uint8)c >= 0x80;
	}
	bool readName(Common::String &name) {
		name.clear();
		char c = peek();
		if (!(Common::isAlpha(c) || c == '_' || c == ':' || (uint8)c >= 0x80))
			return false;
		while (!atEnd() && isNameChar(peek())) {
			name += peek();
			advance();
		}
		return true;
	}
	bool decode(const Common::String &raw, Common::String &out);
	bool element();
	bool skipUntil(const char *end) {
		while (!atEnd() && !startsWith(end))
			advance();
		if (atEnd())
			return fail("unclosed token");
		advance(strlen(end));
		return true;
	}

	Common::String _s;
	uint _pos;
	int _line, _col;
	Object *_handler;
	Common::Array<Common::String> _open;
};

bool XmlReader::decode(const Common::String &raw, Common::String &out) {
	out.clear();
	for (uint i = 0; i < raw.size(); i++) {
		char c = raw[i];
		if (c != '&') {
			out += c;
			continue;
		}
		const char *semi = strchr(raw.c_str() + i, ';');
		if (!semi)
			return fail("not well-formed (invalid token)");
		Common::String ent(raw.c_str() + i + 1, semi - raw.c_str() - i - 1);
		if (ent == "amp")
			out += '&';
		else if (ent == "lt")
			out += '<';
		else if (ent == "gt")
			out += '>';
		else if (ent == "quot")
			out += '"';
		else if (ent == "apos")
			out += '\'';
		else if (ent.hasPrefix("#x"))
			out += (char)strtol(ent.c_str() + 2, nullptr, 16);
		else if (ent.hasPrefix("#"))
			out += (char)strtol(ent.c_str() + 1, nullptr, 10);
		else
			return fail("undefined entity");
		i = semi - raw.c_str();
	}
	return true;
}

bool XmlReader::element() {
	// At '<' of a start tag
	advance();
	Common::String name;
	if (!readName(name))
		return fail("not well-formed (invalid token)");
	Ref<List> attrs = newList();
	for (;;) {
		skipSpace();
		if (atEnd())
			return fail("unclosed token");
		if (peek() == '/' || peek() == '>')
			break;
		Common::String an;
		if (!readName(an))
			return fail("not well-formed (invalid token)");
		skipSpace();
		if (peek() != '=')
			return fail("not well-formed (invalid token)");
		advance();
		skipSpace();
		char q = peek();
		if (q != '"' && q != '\'')
			return fail("not well-formed (invalid token)");
		advance();
		Common::String raw;
		while (!atEnd() && peek() != q) {
			if (peek() == '<')
				return fail("not well-formed (invalid token)");
			raw += peek();
			advance();
		}
		if (atEnd())
			return fail("unclosed token");
		advance();
		Common::String value;
		if (!decode(raw, value))
			return false;
		// Line breaks and tabs in attribute values become spaces, as XML requires
		for (uint i = 0; i < value.size(); i++)
			if (value[i] == '\n' || value[i] == '\r' || value[i] == '\t')
				value.setChar(' ', i);
		ObjRef a(new AttributeObject(internStr(an.c_str()).get(), newStr(value).get()));
		attrs->append(a.get());
	}
	bool empty = peek() == '/';
	if (empty) {
		advance();
		if (peek() != '>')
			return fail("not well-formed (invalid token)");
	}
	advance();
	Ref<Str> nameStr = internStr(name.c_str());
	ObjRef r = callMethod(_handler, "StartElement", 2, nameStr.get(), attrs.get());
	if (!r) {
		_pythonError = true;
		return false;
	}
	if (empty) {
		r = callMethod(_handler, "EndElement", 1, nameStr.get());
		if (!r) {
			_pythonError = true;
			return false;
		}
	} else {
		_open.push_back(name);
	}
	return true;
}

bool XmlReader::parse() {
	bool sawRoot = false;
	while (!atEnd()) {
		if (peek() != '<') {
			// Character data: only whitespace is allowed outside the root element
			if (_open.empty() && peek() != ' ' && peek() != '\t' && peek() != '\n' && peek() != '\r' && (uint8)peek() != 0xef && (uint8)peek() != 0xbb && (uint8)peek() != 0xbf)
				return fail(sawRoot ? "junk after document element" : "syntax error");
			advance();
			continue;
		}
		if (startsWith("<?")) {
			if (!skipUntil("?>"))
				return false;
		} else if (startsWith("<!--")) {
			if (!skipUntil("-->"))
				return false;
		} else if (startsWith("<![CDATA[")) {
			if (_open.empty())
				return fail("syntax error");
			if (!skipUntil("]]>"))
				return false;
		} else if (startsWith("<!")) {
			if (!skipUntil(">"))
				return false;
		} else if (startsWith("</")) {
			advance(2);
			Common::String name;
			if (!readName(name))
				return fail("not well-formed (invalid token)");
			skipSpace();
			if (peek() != '>')
				return fail("not well-formed (invalid token)");
			advance();
			if (_open.empty() || _open.back() != name)
				return fail("mismatched tag");
			_open.pop_back();
			ObjRef r = callMethod(_handler, "EndElement", 1, internStr(name.c_str()).get());
			if (!r) {
				_pythonError = true;
				return false;
			}
		} else {
			if (_open.empty() && sawRoot)
				return fail("junk after document element");
			sawRoot = true;
			if (!element())
				return false;
		}
	}
	if (!_open.empty())
		return fail("no element found");
	if (!sawRoot)
		return fail("no element found");
	return true;
}

// ---- Python objects -------------------------------------------------------------------

struct ParserObject : public Object {
	ObjRef _handler;
	Ref<Str> _errorString;
	explicit ParserObject(Type *t) : Object(t), _handler(g_none) {
		_errorString = newStr("");
	}
};

static ObjRef parserGetHandler(Object *o) { return ((ParserObject *)o)->_handler; }
static int parserSetHandler(Object *o, Object *v) {
	((ParserObject *)o)->_handler = v ? v : g_none;
	return 0;
}
static ObjRef parserGetError(Object *o) { return ObjRef(((ParserObject *)o)->_errorString.get()); }
static int parserSetError(Object *o, Object *v) {
	Str *s = v ? asStr(v) : nullptr;
	if (!s)
		return -1;
	((ParserObject *)o)->_errorString = s;
	return 0;
}

static const GetSetDef g_parserGetSets[] = {
	{ "contentHandler", parserGetHandler, parserSetHandler },
	{ "errorString", parserGetError, parserSetError },
	{ nullptr, nullptr, nullptr }
};

static ObjRef parserParseFile(Object *self, Tuple *args, Dict *) {
	ParserObject *p = (ParserObject *)self;
	if (!checkArgs("ParseFile", args, 1, 1))
		return ObjRef();
	ObjRef pathObj = str(args->get(0));
	if (!pathObj)
		return ObjRef();
	Common::String path = ((Str *)pathObj.get())->toString();
	Common::String text;
	if (!g_res->read(path, text)) {
		p->_errorString = newStr(Common::String::format("could not find '%s'", path.c_str()));
		debugC(1, kDebugScript, "yagaxml: %s not found", path.c_str());
		return newInt(0);
	}
	if (p->_handler.get() == g_none) {
		// Nothing to report to: just check it parses
		Ref<Object> dummy(new HeapObject(&g_contentHandlerType));
		XmlReader reader(text, dummy.get());
		return newInt(reader.parse() ? 1 : 0);
	}
	XmlReader reader(text, p->_handler.get());
	if (!reader.parse()) {
		if (reader._pythonError)
			return ObjRef(); // the handler raised
		p->_errorString = newStr(Common::String::format("%s in '%s'", reader._error.c_str(), path.c_str()));
		debugC(1, kDebugScript, "yagaxml: %s", p->_errorString->_data);
		return newInt(0);
	}
	debugC(2, kDebugScript, "yagaxml: parsed %s, %u bytes", path.c_str(), text.size());
	return newInt(1);
}

static const MethodDef g_parserMethods[] = {
	{ "ParseFile", parserParseFile, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef systemCreateParser(Object *, Tuple *, Dict *) {
	return ObjRef(new ParserObject(&g_parserType));
}

// The script system makes its parsers with yagaxml.Parser()
static ObjRef parserNew(Type *type, Tuple *, Dict *) {
	return ObjRef(new ParserObject(type));
}

static const MethodDef g_systemMethods[] = {
	{ "CreateParser", systemCreateParser, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// XmlSystem() is a singleton
static Object *g_xmlSystem = nullptr;

static ObjRef systemNew(Type *, Tuple *, Dict *) {
	if (!g_xmlSystem) {
		g_xmlSystem = new HeapObject(&g_xmlSystemType);
		incRef(g_xmlSystem);
	}
	return ObjRef(g_xmlSystem);
}

// IContentHandler: the base of the game's loaders; every method does nothing
static ObjRef handlerNoop(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static const MethodDef g_handlerMethods[] = {
	{ "__init__", handlerNoop, kMethVarArgs | kMethKeywords, nullptr },
	{ "__del__", handlerNoop, kMethVarArgs, nullptr },
	{ "StartElement", handlerNoop, kMethVarArgs, nullptr },
	{ "EndElement", handlerNoop, kMethVarArgs, nullptr },
	{ "Text", handlerNoop, kMethVarArgs, nullptr },
	{ "Comment", handlerNoop, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef handlerNew(Type *type, Tuple *, Dict *) {
	return ObjRef(new HeapObject(type));
}

static void initYagaxml(Module *m) {
	if (!(g_parserType._flags & kTypeReady)) {
		g_attributeType.getSets = g_attributeGetSets;
		g_attributeType.repr = attrRepr;
		g_attributeType.getAttr = genericGetAttr;
		typeReady(&g_attributeType);
		g_contentHandlerType._flags |= kTypeBaseType | kTypeHasDict;
		g_contentHandlerType.methods = g_handlerMethods;
		g_contentHandlerType.newObj = handlerNew;
		g_contentHandlerType.getAttr = genericGetAttr;
		g_contentHandlerType.setAttr = genericSetAttr;
		typeReady(&g_contentHandlerType);
		g_parserType.getSets = g_parserGetSets;
		g_parserType.newObj = parserNew;

		g_parserType.methods = g_parserMethods;
		g_parserType.getAttr = genericGetAttr;
		g_parserType.setAttr = genericSetAttr;
		typeReady(&g_parserType);
		g_xmlSystemType.methods = g_systemMethods;
		g_xmlSystemType.newObj = systemNew;
		g_xmlSystemType.getAttr = genericGetAttr;
		typeReady(&g_xmlSystemType);
	}
	moduleAddObject(m, "Attribute", &g_attributeType);
	moduleAddObject(m, "IContentHandler", &g_contentHandlerType);
	moduleAddObject(m, "Parser", &g_parserType);
	moduleAddObject(m, "XmlSystem", &g_xmlSystemType);
}

void registerYagaxml(Interpreter *vm, Resources *resources) {
	g_res = resources;
	vm->addNativeModule("yagaxml", initYagaxml, true);
}

} // End of namespace Yaga
