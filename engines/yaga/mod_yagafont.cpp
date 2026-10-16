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
#include "graphics/managed_surface.h"

#include "yaga/detection.h"
#include "yaga/display.h"
#include "yaga/modules.h"
#include "yaga/pyhost.h"
#include "yaga/python/internal.h"

// yagafont: bitmap fonts and the strings drawn with them (subtitles, menus,
// save names). Each font is one image holding its glyphs in a strip, from
// minChar to maxChar, separated by fully transparent columns.

namespace Yaga {

using namespace Python;

enum {
	kJustifyStart = 0, // left or top
	kJustifyCenter = 1,
	kJustifyEnd = 2,   // right or bottom
	kClip = 1
};

// An outlined glyph carries a column of outline down each side, meant to
// overlap its neighbour's: the advance is the glyph's width less this
static const int kOverlap = 2;

static const ConstDef g_hJustify[] = {
	{ "HJUSTIFY_LEFT", kJustifyStart }, { "HJUSTIFY_CENTER", kJustifyCenter }, { "HJUSTIFY_RIGHT", kJustifyEnd },
	{ nullptr, 0 }
};

static const ConstDef g_vJustify[] = {
	{ "VJUSTIFY_TOP", kJustifyStart }, { "VJUSTIFY_CENTER", kJustifyCenter }, { "VJUSTIFY_BOTTOM", kJustifyEnd },
	{ nullptr, 0 }
};

static const ConstDef g_stringFlags[] = {
	{ "IMGSTR_CLIP", kClip },
	{ nullptr, 0 }
};

static Type g_asciiDataType("yagafont.AsciiData", &g_objectType);
static Type g_fontType("yagafont.Font", &g_objectType);
static Type g_imageStringType("yagafont.IImageString", &g_objectType);
static Type g_fontManagerType("yagafont.FontManager", &g_objectType);

static int32 attrInt(Object *o, const char *name, int32 fallback) {
	ObjRef v = getAttrString(o, name);
	double d;
	if (!v || !asDouble(v.get(), &d)) {
		clearError();
		return fallback;
	}
	return (int32)d;
}

// ---- Fonts --------------------------------------------------------------------------

struct Glyph {
	int x, w;
};

struct FontObject : public EngineObject {
	ObjRef _image; // keeps the strip alive
	const Graphics::Surface *_strip;
	Glyph _glyphs[256]; // w 0: no glyph
	int _height;
	int _space;

	explicit FontObject(Type *t) : EngineObject(t), _strip(nullptr), _height(0), _space(6) {
		memset(_glyphs, 0, sizeof(_glyphs));
	}

	int advance(byte c, int kerning) const {
		if (c == ' ')
			return _space - kOverlap + kerning;
		return _glyphs[c].w ? _glyphs[c].w - kOverlap + kerning : 0;
	}

	int measure(const Common::String &text, int kerning) const {
		int w = 0;
		for (uint i = 0; i < text.size(); i++)
			w += advance((byte)text[i], kerning);
		return w;
	}

	bool hasGlyphs() const {
		return _strip != nullptr;
	}
};

// Cuts the strip at its fully transparent columns, one glyph per run
static void sliceFont(FontObject *f, int first, int last, int spaceChar, const Common::String &name) {
	const Graphics::Surface *s = f->_strip;
	Common::Array<Glyph> runs;
	int start = -1;
	for (int x = 0; x <= s->w; x++) {
		bool blank = true;
		for (int y = 0; x < s->w && y < s->h && blank; y++) {
			byte a, r, g, b;
			s->format.colorToARGB(s->getPixel(x, y), a, r, g, b);
			blank = a == 0;
		}
		if (!blank && start < 0) {
			start = x;
		} else if (blank && start >= 0) {
			Glyph g = { start, x - start };
			runs.push_back(g);
			start = -1;
		}
	}
	int expected = last - first + 1;
	if ((int)runs.size() != expected)
		warning("yaga: font %s has %u glyphs for %d characters", name.c_str(), runs.size(), expected);
	if ((int)runs.size() < expected) {
		f->_strip = nullptr; // every glyph after the gap would be the wrong letter
		return;
	}
	for (int i = 0; i < expected; i++)
		if (first + i >= 0 && first + i < 256)
			f->_glyphs[first + i] = runs[i];
	f->_height = s->h;
	if (spaceChar >= 0 && spaceChar < 256 && f->_glyphs[spaceChar].w)
		f->_space = f->_glyphs[spaceChar].w;
	debugC(1, kDebugScript, "yagafont: %s, %d glyphs, %d pixels tall", name.c_str(), expected, f->_height);
}

// GetStringRect(text): how big the string would be
static ObjRef fontGetStringRect(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("GetStringRect", args, 0, 2))
		return ObjRef();
	FontObject *f = (FontObject *)self;
	ObjRef text = args->_size ? str(args->get(0)) : ObjRef(newStr(""));
	if (!text)
		return ObjRef();
	return newRect(0, 0, f->measure(((Str *)text.get())->toString(), 0), f->_height);
}

static const MethodDef g_fontMethods[] = {
	{ "GetStringRect", fontGetStringRect, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef asciiDataNew(Type *t, Tuple *, Dict *) {
	ObjRef d(new EngineObject(t));
	setEngineAttr(d.get(), "minChar", newInt('!').get());
	setEngineAttr(d.get(), "maxChar", newInt('~').get());
	setEngineAttr(d.get(), "spaceChar", newInt('"').get());
	setEngineAttr(d.get(), "baselineChar", newInt('a').get());
	return d;
}

// ---- Image strings ----------------------------------------------------------------------

// Breaks the text at constrainedWidth, on spaces, keeping newlines
static void wrapText(const FontObject *f, const Common::String &text, int limit, int kerning, Common::Array<Common::String> &lines) {
	lines.clear();
	Common::String paragraph;
	for (uint i = 0; i <= text.size(); i++) {
		char c = i < text.size() ? text[i] : '\n';
		if (c == '\r')
			continue;
		if (c != '\n') {
			paragraph += c;
			continue;
		}
		Common::String current;
		uint pos = 0;
		while (pos <= paragraph.size()) {
			int sp = paragraph.find(' ', pos);
			uint end = sp < 0 ? paragraph.size() : (uint)sp;
			Common::String word(paragraph.c_str() + pos, end - pos);
			Common::String candidate = current.empty() ? word : current + " " + word;
			if (!current.empty() && limit > 0 && f->measure(candidate, kerning) > limit) {
				lines.push_back(current);
				current = word;
			} else {
				current = candidate;
			}
			pos = end + 1;
		}
		lines.push_back(current);
		paragraph.clear();
	}
}

static ObjRef imageStringNew(Type *t, Tuple *, Dict *) {
	ObjRef s(new EngineObject(t));
	setEngineAttr(s.get(), "font", g_none);
	setEngineAttr(s.get(), "position", newPoint(0, 0, 0).get());
	setEngineAttr(s.get(), "opacity", newFloat(1.0).get());
	setEngineAttr(s.get(), "hJustify", newInt(kJustifyStart).get());
	setEngineAttr(s.get(), "vJustify", newInt(kJustifyStart).get());
	setEngineAttr(s.get(), "constrainedWidth", newInt(0).get());
	setEngineAttr(s.get(), "constrainedHeight", newInt(0).get());
	setEngineAttr(s.get(), "kerningAdjust", newInt(0).get());
	setEngineAttr(s.get(), "flags", newInt(0).get());
	setEngineAttr(s.get(), "text", newStr("").get());
	setEngineAttr(s.get(), "renderRect", newRect(0, 0, 0, 0).get());
	return s;
}

// A Point is a value: positions are copied both ways
static ObjRef imageStringGetAttr(Object *o, Str *name) {
	if (name->equals("position")) {
		Object *pos = o->getDict()->getItemString("position");
		if (pos)
			return copyPoint(pos);
	}
	return stubFallbackGetAttr(o, name);
}

static int imageStringSetAttr(Object *o, Str *name, Object *value) {
	ObjRef v(value);
	if (value && name->equals("position"))
		v = copyPoint(value);
	return genericSetAttr(o, name, v.get());
}

static ObjRef imageStringSetText(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("SetText", args, 0, 1))
		return ObjRef();
	ObjRef text = (args->_size && args->get(0) != g_none) ? str(args->get(0)) : ObjRef(newStr(""));
	if (!text)
		return ObjRef();
	setEngineAttr(self, "text", text.get());
	return ObjRef(g_none);
}

static ObjRef imageStringGetText(Object *self, Tuple *, Dict *) {
	Object *t = self->getDict()->getItemString("text");
	return ObjRef(t ? t : newStr("").get());
}

static ObjRef imageStringNoop(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

// The position is an anchor: left or top text starts at it, centred text is
// centred on it, right or bottom text ends at it
static ObjRef imageStringRender(Object *self, Tuple *args, Dict *) {
	ObjRef target = offscreenTarget(args->_size ? args->get(0) : nullptr);
	Object *fontObj = self->getDict()->getItemString("font");
	if (!fontObj || !isInstanceOf(fontObj, &g_fontType) || !((FontObject *)fontObj)->hasGlyphs())
		return ObjRef(g_none);
	FontObject *f = (FontObject *)fontObj;
	ObjRef textObj = str(self->getDict()->getItemString("text") ? self->getDict()->getItemString("text") : g_none);
	if (!textObj) {
		clearError();
		return ObjRef(g_none);
	}
	Common::String text = ((Str *)textObj.get())->toString();
	int kerning = attrInt(self, "kerningAdjust", 0);
	int limit = attrInt(self, "constrainedWidth", 0);
	Common::Array<Common::String> lines;
	wrapText(f, text, limit, kerning, lines);
	int32 left = 0, top = 0;
	Object *pos = self->getDict()->getItemString("position");
	if (pos)
		pointXY(pos, &left, &top);
	int hJustify = attrInt(self, "hJustify", kJustifyStart);
	int vJustify = attrInt(self, "vJustify", kJustifyStart);
	int block = f->_height * lines.size();
	if (vJustify == kJustifyCenter)
		top -= block / 2;
	else if (vJustify == kJustifyEnd)
		top -= block;
	double opacity = 1.0;
	Object *o = self->getDict()->getItemString("opacity");
	if (!o || !asDouble(o, &opacity)) {
		clearError();
		opacity = 1.0;
	}
	uint alpha = (uint)(CLIP(opacity, 0.0, 1.0) * 255.0 + 0.5);

	Common::Rect clip(0, 0, 0x7fff, 0x7fff);
	if (attrInt(self, "flags", 0) & kClip) {
		int cw = limit, ch = attrInt(self, "constrainedHeight", 0);
		int cx = hJustify == kJustifyCenter ? left - cw / 2 : hJustify == kJustifyEnd ? left - cw : left;
		int cy = vJustify == kJustifyCenter ? top + block / 2 - ch / 2 : vJustify == kJustifyEnd ? top + block - ch : top;
		if (cw > 0 && ch > 0)
			clip = Common::Rect(cx, cy, cx + cw, cy + ch);
	}

	Common::Rect drawn;
	bool any = false;
	bool canDraw = g_display && g_display->isOpen();
	for (uint row = 0; row < lines.size(); row++) {
		const Common::String &line = lines[row];
		int width = f->measure(line, kerning);
		int x = left;
		if (hJustify == kJustifyCenter)
			x -= width / 2;
		else if (hJustify == kJustifyEnd)
			x -= width;
		int y = top + row * f->_height;
		Common::Rect lineRect(x, y, x + width, y + f->_height);
		if (any)
			drawn.extend(lineRect);
		else
			drawn = lineRect;
		any = true;
		for (uint i = 0; i < line.size(); i++) {
			byte c = (byte)line[i];
			const Glyph &g = f->_glyphs[c];
			if (c == ' ' || !g.w) {
				x += f->advance(' ', kerning);
				continue;
			}
			Common::Rect src(g.x, 0, g.x + g.w, f->_height);
			Common::Rect dst(x, y, x + g.w, y + f->_height);
			dst.clip(clip);
			if ((canDraw || target) && !dst.isEmpty()) {
				src.left += dst.left - x;
				src.top += dst.top - y;
				src.setWidth(dst.width());
				src.setHeight(dst.height());
				if (target)
					renderThrough(target.get(), f->_image.get(), opacity, dst, src);
				else
					g_display->surface().blendBlitFrom(*f->_strip, src, Common::Point(dst.left, dst.top),
						Graphics::FLIP_NONE, MS_ARGB(alpha, 255, 255, 255));
			}
			x += g.w - kOverlap + kerning;
		}
	}
	if (any)
		setEngineAttr(self, "renderRect", newRect(drawn.left, drawn.top, drawn.width(), drawn.height()).get());
	return ObjRef(g_none);
}

static const MethodDef g_imageStringMethods[] = {
	{ "SetText", imageStringSetText, kMethVarArgs, nullptr },
	{ "GetText", imageStringGetText, kMethVarArgs, nullptr },
	{ "Seek", imageStringNoop, kMethVarArgs, nullptr },
	{ "Render", imageStringRender, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// ---- The font manager --------------------------------------------------------------------

struct FontManagerObject : public EngineObject {
	Ref<Dict> _fonts;
	explicit FontManagerObject(Type *t) : EngineObject(t) {
		_fonts = newDict();
	}
};

static FontManagerObject *g_fontManager = nullptr;

static ObjRef managerNew(Type *, Tuple *, Dict *) {
	if (!g_fontManager) {
		g_fontManager = new FontManagerObject(&g_fontManagerType);
		incRef(g_fontManager);
	}
	return ObjRef(g_fontManager);
}

// CreateFontFromImage(image, name, asciiData)
static ObjRef managerCreateFont(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("CreateFontFromImage", args, 3, 3))
		return ObjRef();
	ObjRef name = str(args->get(1));
	if (!name)
		return ObjRef();
	Common::String fontName = ((Str *)name.get())->toString();
	Ref<FontObject> f(new FontObject(&g_fontType));
	setEngineAttr(f.get(), "name", name.get());
	f->_image = args->get(0);
	f->_strip = imageSurface(args->get(0));
	if (!f->_strip || !f->_strip->getPixels()) {
		warning("yaga: font %s has no image", fontName.c_str());
		f->_strip = nullptr;
		return ObjRef(f.get());
	}
	Object *ascii = args->get(2);
	sliceFont(f.get(), attrInt(ascii, "minChar", '!'), attrInt(ascii, "maxChar", '~'), attrInt(ascii, "spaceChar", '"'), fontName);
	setEngineAttr(f.get(), "height", newInt(f->_height).get());
	if (f->hasGlyphs())
		((FontManagerObject *)self)->_fonts->setItem(name.get(), f.get());
	return ObjRef(f.get());
}

static ObjRef managerGetFont(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("GetFont", args, 1, 1))
		return ObjRef();
	ObjRef name = str(args->get(0));
	if (!name)
		return ObjRef();
	Object *f = ((FontManagerObject *)self)->_fonts->getItem(name.get());
	if (!f)
		debugC(1, kDebugScript, "yagafont: no font %s", ((Str *)name.get())->_data);
	return ObjRef(f ? f : g_none);
}

static ObjRef managerCreateString(Object *, Tuple *, Dict *) {
	return imageStringNew(&g_imageStringType, nullptr, nullptr);
}

static const MethodDef g_managerMethods[] = {
	{ "CreateFontFromImage", managerCreateFont, kMethVarArgs, nullptr },
	{ "GetFont", managerGetFont, kMethVarArgs, nullptr },
	{ "CreateImageString", managerCreateString, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void fillYagafont(Module *m) {
	if (!(g_fontType._flags & kTypeReady)) {
		g_asciiDataType.newObj = asciiDataNew;
		readyEngineType(&g_asciiDataType, nullptr, nullptr, false);
		readyEngineType(&g_fontType, g_fontMethods, nullptr, true);
		g_imageStringType.newObj = imageStringNew;
		readyEngineType(&g_imageStringType, g_imageStringMethods, nullptr, true);
		g_imageStringType.getAttr = imageStringGetAttr;
		g_imageStringType.setAttr = imageStringSetAttr;
		g_fontManagerType.newObj = managerNew;
		readyEngineType(&g_fontManagerType, g_managerMethods, nullptr, true);
	}
	addEnum(m, "HorizontalJustification", g_hJustify);
	addEnum(m, "VerticalJustification", g_vJustify);
	addConstants(m, "ImageStringFlags", g_stringFlags);
	moduleAddObject(m, "AsciiData", &g_asciiDataType);
	moduleAddObject(m, "Font", &g_fontType);
	moduleAddObject(m, "IImageString", &g_imageStringType);
	moduleAddObject(m, "FontManager", &g_fontManagerType);
}

void registerYagafont() {
	g_fontManager = nullptr;
	registerEngineModule("yagafont", fillYagafont);
}

} // End of namespace Yaga
