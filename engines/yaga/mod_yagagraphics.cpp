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
#include "common/hashmap.h"
#include "common/memstream.h"
#include "common/system.h"
#include "graphics/blit.h"
#include "graphics/managed_surface.h"
#include "graphics/cursor.h"
#include "graphics/cursorman.h"
#include "image/icocur.h"
#include "image/png.h"

#include "yaga/detection.h"
#include "yaga/display.h"
#include "yaga/imageanim.h"
#include "yaga/modules.h"
#include "yaga/resources.h"
#include "yaga/pyhost.h"
#include "yaga/python/internal.h"

// yagagraphics: the graphics system, its one video device, the render
// target the game draws into and its cursors, and the animations (IImageAnim)
// sprites draw. Drawing into images (IImage) is still a stand-in.

namespace Yaga {

using namespace Python;

enum {
	kPixelR5G6B5 = 1
};

enum {
	kTargetWindowed = 1,
	kClearBack = 1
};

static const ConstDef g_hardware[] = {
	{ "SYSTEM_SOFTWARE", 0 }, { "SYSTEM_HARDWARE", 1 },
	{ nullptr, 0 }
};

static const ConstDef g_pixelFormats[] = {
	{ "PXL_UNKNOWN", 0 }, { "PXL_R5G6B5", kPixelR5G6B5 }, { "PXL_X1R5G5B5", 2 }, { "PXL_A1R5G5B5", 3 },
	{ "PXL_R8G8B8", 4 }, { "PXL_X8R8G8B8", 5 }, { "PXL_A8R8G8B8", 6 }, { "PXL_PAL8", 7 },
	{ "PXL_APAL8", 8 }, { "PXL_A8PAL8", 9 }, { "PXL_LUM8", 10 }, { "PXL_A8LUM8", 11 }, { "PXL_A8", 12 },
	{ nullptr, 0 }
};

static const ConstDef g_targetTypes[] = {
	{ "TARGET_DEFAULT", 0 }, { "TARGET_WINDOWED", kTargetWindowed }, { "TARGET_FULL_SCREEN", 2 },
	{ nullptr, 0 }
};

static const ConstDef g_clearFlags[] = {
	{ "CLEAR_BACK", kClearBack }, { "CLEAR_ZBUFFER", 2 }, { "CLEAR_STENCIL", 4 },
	{ nullptr, 0 }
};

static const ConstDef g_renderHints[] = {
	{ "RENDER_DEFAULT", 0 },
	{ nullptr, 0 }
};

static const ConstDef g_compressionTypes[] = {
	{ "COMPRESS_NONE", 0 }, { "COMPRESS_YRLE", 1 },
	{ nullptr, 0 }
};

static const ConstDef g_geomFlags[] = {
	{ "GEOMFLAG_XYZ", 1 }, { "GEOMFLAG_DIFFUSE", 2 },
	{ nullptr, 0 }
};

static const ConstDef g_geomTypes[] = {
	{ "GEOMTYPE_LINE_LIST", 1 }, { "GEOMTYPE_LINE_STRIP", 2 },
	{ nullptr, 0 }
};

static Type g_videoModeType("yagagraphics.VideoMode", &g_objectType);
static Type g_rectType("yagagraphics.Rect", &g_objectType);
static Type g_colorType("yagagraphics.Color", &g_objectType);
static Type g_systemType("yagagraphics.GraphicsSystem", &g_objectType);
static Type g_deviceType("yagagraphics.VideoDevice", &g_objectType);
static Type g_targetType("yagagraphics.RenderTarget", &g_objectType);
static Type g_imageAnimType("yagagraphics.IImageAnim", &g_objectType);
static Type g_animFrameType("yagagraphics.Frame", &g_objectType);
static Type g_animLayerType("yagagraphics.Layer", &g_objectType);
static Type g_imageType("yagagraphics.IImage", &g_objectType);
static Type g_cameraType("yagagraphics.Camera", &g_objectType);

static Resources *g_res = nullptr;

// Fills attributes from positional or keyword arguments, with defaults
static bool fillAttrs(Object *o, const char *fname, const char *const *names, const int32 *defaults, uint32 count, Tuple *args, Dict *kw) {
	if (!checkArgs(fname, args, 0, count))
		return false;
	for (uint32 i = 0; i < count; i++) {
		Object *v = i < args->_size ? args->get(i) : nullptr;
		if (!v && kw)
			v = kw->getItemString(names[i]);
		if (v)
			setEngineAttr(o, names[i], v);
		else
			setEngineAttr(o, names[i], newInt(defaults[i]).get());
	}
	return true;
}

static ObjRef attrsRepr(Object *o, const char *prefix, const char *const *names, uint32 count) {
	Common::String s = Common::String(prefix) + "(";
	for (uint32 i = 0; i < count; i++) {
		Object *v = o->getDict()->getItemString(names[i]);
		ObjRef r = v ? repr(v) : ObjRef();
		if (!r) {
			clearError();
			r = newStr("?");
		}
		s += Common::String::format("%s%s", i ? ", " : "", ((Str *)r.get())->_data);
	}
	return newStr(s + ")");
}

static const char *const g_modeNames[] = { "width", "height", "format" };
static const int32 g_modeDefaults[] = { 640, 480, kPixelR5G6B5 };

static ObjRef newVideoMode(int32 w, int32 h, int32 format) {
	ObjRef m(new EngineObject(&g_videoModeType));
	setEngineAttr(m.get(), "width", newInt(w).get());
	setEngineAttr(m.get(), "height", newInt(h).get());
	setEngineAttr(m.get(), "format", newInt(format).get());
	return m;
}

static ObjRef videoModeNew(Type *t, Tuple *args, Dict *kw) {
	ObjRef m(new EngineObject(t));
	if (!fillAttrs(m.get(), "VideoMode", g_modeNames, g_modeDefaults, 3, args, kw))
		return ObjRef();
	return m;
}

static ObjRef videoModeRepr(Object *o) {
	return attrsRepr(o, "VideoMode", g_modeNames, 3);
}

static const char *const g_rectNames[] = { "x", "y", "width", "height" };
static const int32 g_rectDefaults[] = { 0, 0, 0, 0 };

static ObjRef rectNew(Type *t, Tuple *args, Dict *kw) {
	ObjRef r(new EngineObject(t));
	if (!fillAttrs(r.get(), "Rect", g_rectNames, g_rectDefaults, 4, args, kw))
		return ObjRef();
	return r;
}

static ObjRef rectRepr(Object *o) {
	return attrsRepr(o, "Rect", g_rectNames, 4);
}

static const char *const g_colorNames[] = { "r", "g", "b", "a" };
static const int32 g_colorDefaults[] = { 0, 0, 0, 255 };

static ObjRef colorNew(Type *t, Tuple *args, Dict *kw) {
	ObjRef c(new EngineObject(t));
	if (!fillAttrs(c.get(), "Color", g_colorNames, g_colorDefaults, 4, args, kw))
		return ObjRef();
	return c;
}

static ObjRef colorRepr(Object *o) {
	return attrsRepr(o, "Color", g_colorNames, 4);
}

static int32 attrInt(Object *o, const char *name, int32 fallback);

ObjRef newRect(int32 x, int32 y, int32 w, int32 h) {
	ObjRef r(new EngineObject(&g_rectType));
	setEngineAttr(r.get(), "x", newInt(x).get());
	setEngineAttr(r.get(), "y", newInt(y).get());
	setEngineAttr(r.get(), "width", newInt(w).get());
	setEngineAttr(r.get(), "height", newInt(h).get());
	return r;
}

// ---- Images and animations -----------------------------------------------------------

// IImage: a layer's picture, or a blank one from CreateImage
struct ImageObject : public EngineObject {
	Common::SharedPtr<AnimData> _anim; // keeps a layer's pixels alive
	const Graphics::Surface *_surface;
	Graphics::ManagedSurface _own; // pixels of its own, once drawn into
	explicit ImageObject(Type *t) : EngineObject(t), _surface(nullptr) {}
	void setSize() {
		setEngineAttr(this, "width", newInt(_surface ? _surface->w : 0).get());
		setEngineAttr(this, "height", newInt(_surface ? _surface->h : 0).get());
		setEngineAttr(this, "pixelFmt", newInt(6).get()); // PXL_A8R8G8B8
	}
};

static ObjRef newImage(const Common::SharedPtr<AnimData> &anim, const Graphics::Surface *surface) {
	Ref<ImageObject> img(new ImageObject(&g_imageType));
	img->_anim = anim;
	img->_surface = surface;
	img->setSize();
	return ObjRef(img.get());
}

const Graphics::Surface *imageSurface(Object *o) {
	return isInstanceOf(o, &g_imageType) ? ((ImageObject *)o)->_surface : nullptr;
}

// IImage(other) shares the other image's pixels; IImage(resource) reads a PNG
static ObjRef imageNew(Type *t, Tuple *args, Dict *) {
	Ref<ImageObject> img(new ImageObject(t));
	if (args->_size && isInstanceOf(args->get(0), &g_resourceType)) {
		ResourceObject *r = (ResourceObject *)args->get(0);
		Common::MemoryReadStream stream((const byte *)r->_data->_data, r->_data->_len);
		Image::PNGDecoder png;
		if (png.loadStream(stream) && png.getSurface()) {
			Graphics::Surface *converted = png.getSurface()->convertTo(Graphics::BlendBlit::getSupportedPixelFormat(), png.getPalette().data(), png.getPalette().size());
			img->_own.copyFrom(*converted);
			converted->free();
			delete converted;
			img->_surface = &img->_own.rawSurface();
		} else {
			warning("yaga: %s is not a PNG image", r->_path.c_str());
		}
	} else if (args->_size && isInstanceOf(args->get(0), &g_imageType)) {
		ImageObject *src = (ImageObject *)args->get(0);
		if (src->_surface == &src->_own.rawSurface()) {
			img->_own.copyFrom(src->_own);
			img->_surface = &img->_own.rawSurface();
		} else {
			img->_anim = src->_anim;
			img->_surface = src->_surface;
		}
	}
	img->setSize();
	return ObjRef(img.get());
}

static ObjRef imageRepr(Object *o) {
	ImageObject *img = (ImageObject *)o;
	return newStr(Common::String::format("<IImage %dx%d>", img->_surface ? img->_surface->w : 0, img->_surface ? img->_surface->h : 0));
}

// CreateImage(width, height, format): a blank image
static ObjRef systemCreateImage(Object *, Tuple *args, Dict *) {
	if (!checkArgs("CreateImage", args, 2, 3))
		return ObjRef();
	int32 w, h;
	if (!asInt(args->get(0), &w) || !asInt(args->get(1), &h))
		return ObjRef();
	Ref<ImageObject> img(new ImageObject(&g_imageType));
	if (w > 0 && h > 0 && w < 4096 && h < 4096) {
		img->_own.create(w, h, Graphics::BlendBlit::getSupportedPixelFormat());
		img->_own.clear();
		img->_surface = &img->_own.rawSurface();
	}
	img->setSize();
	return ObjRef(img.get());
}

struct ImageAnimObject : public EngineObject {
	Common::SharedPtr<AnimData> _anim;
	Common::String _locator;
	Ref<List> _frames; // made when first asked for
	explicit ImageAnimObject(Type *t) : EngineObject(t) {}
};

ObjRef imageAnimReplacedImage(Object *o, uint frame, uint layer) {
	if (!o || !isInstanceOf(o, &g_imageAnimType))
		return ObjRef();
	ImageAnimObject *a = (ImageAnimObject *)o;
	if (!a->_frames || frame >= a->_frames->size() || frame >= a->_anim->frames.size())
		return ObjRef();
	Object *layers = a->_frames->get(frame)->getDict()->getItemString("layers");
	if (!layers || !isInstanceOf(layers, &g_listType) || layer >= ((List *)layers)->size())
		return ObjRef();
	Object *l = ((List *)layers)->get(layer);
	Object *img = l->getDict() ? l->getDict()->getItemString("image") : nullptr;
	if (!img || !isInstanceOf(img, &g_imageType))
		return ObjRef();
	if (((ImageObject *)img)->_surface == &a->_anim->frames[frame].layers[layer].surface)
		return ObjRef(); // still its own picture
	return ObjRef(img);
}

Common::SharedPtr<AnimData> imageAnimShared(Object *o) {
	if (!isInstanceOf(o, &g_imageAnimType))
		return Common::SharedPtr<AnimData>();
	return ((ImageAnimObject *)o)->_anim;
}

AnimData *imageAnimData(Object *o) {
	if (!isInstanceOf(o, &g_imageAnimType))
		return nullptr;
	return ((ImageAnimObject *)o)->_anim.get();
}

double imageAnimFps(Object *o) {
	Object *v = o->getDict() ? o->getDict()->getItemString("framesPerSecond") : nullptr;
	double fps;
	if (!v || !asDouble(v, &fps) || fps <= 0) {
		clearError();
		return 15.0;
	}
	return fps;
}

Common::String imageAnimLocator(Object *o) {
	return isInstanceOf(o, &g_imageAnimType) ? ((ImageAnimObject *)o)->_locator : Common::String();
}

// IImageAnim(resource): the resource's frames, decoded once
static ObjRef imageAnimNew(Type *t, Tuple *args, Dict *) {
	if (!checkArgs("IImageAnim", args, 1, 1))
		return ObjRef();
	Object *res = args->get(0);
	Str *data = resourceData(res);
	if (!data)
		return ObjRef();
	ResourceObject *r = (ResourceObject *)res;
	if (!r->_anim) {
		AnimData *decoded = decodeAnim((const byte *)data->_data, data->_len, r->_path);
		if (!decoded) {
			warning("yaga: %s is not an animation", r->_path.c_str());
			decoded = new AnimData();
		}
		r->_anim = Common::SharedPtr<AnimData>(decoded);
	}
	Ref<ImageAnimObject> a(new ImageAnimObject(t));
	a->_anim = r->_anim;
	a->_locator = r->_path;
	Common::Rect b = a->_anim->bounds();
	setEngineAttr(a.get(), "framesPerSecond", newInt(15).get());
	setEngineAttr(a.get(), "locator", newStr(r->_path).get());
	setEngineAttr(a.get(), "width", newInt(b.width()).get());
	setEngineAttr(a.get(), "height", newInt(b.height()).get());
	debugC(2, kDebugScript, "yagagraphics: %s, %u frames", r->_path.c_str(), a->_anim->frames.size());
	return ObjRef(a.get());
}

// frames[i].layers[j], with each layer's name, pos, mask and image
static ObjRef imageAnimGetFrames(Object *o) {
	ImageAnimObject *a = (ImageAnimObject *)o;
	if (a->_frames)
		return ObjRef(a->_frames.get());
	Ref<List> frames = newList();
	for (uint i = 0; i < a->_anim->frames.size(); i++) {
		const AnimFrame &f = a->_anim->frames[i];
		ObjRef frame(new EngineObject(&g_animFrameType));
		Ref<List> layers = newList();
		for (uint j = 0; j < f.layers.size(); j++) {
			const AnimLayer &l = f.layers[j];
			ObjRef layer(new EngineObject(&g_animLayerType));
			setEngineAttr(layer.get(), "name", newStr(l.name).get());
			setEngineAttr(layer.get(), "pos", newPoint(l.x, l.y, 0).get());
			setEngineAttr(layer.get(), "mask", newIntOrLong(l.mask).get());
			setEngineAttr(layer.get(), "image", newImage(a->_anim, &l.surface).get());
			layers->append(layer.get());
		}
		setEngineAttr(frame.get(), "layers", layers.get());
		setEngineAttr(frame.get(), "pos", newPoint(0, 0, 0).get());
		frames->append(frame.get());
	}
	a->_frames = frames;
	return ObjRef(frames.get());
}

// The engine's in-memory compression; nothing to gain from it here
static ObjRef imageAnimCompress(Object *self, Tuple *, Dict *) {
	return ObjRef(self);
}

static ObjRef imageAnimIsDone(Object *, Tuple *, Dict *) {
	return newInt(1);
}

static const MethodDef g_imageAnimMethods[] = {
	{ "Compress", imageAnimCompress, kMethVarArgs, nullptr },
	{ "IsAnimDone", imageAnimIsDone, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_imageAnimGetSets[] = {
	{ "frames", imageAnimGetFrames, nullptr },
	{ nullptr, nullptr, nullptr }
};

// ---- Drawing into images, and through cameras -----------------------------------------

static Common::Rect rectArg(Object *r, int w, int h) {
	if (!r || r == g_none)
		return Common::Rect(0, 0, w, h);
	int32 x = attrInt(r, "x", 0), y = attrInt(r, "y", 0);
	return Common::Rect(x, y, x + attrInt(r, "width", w), y + attrInt(r, "height", h));
}

// Draws src into dst: scaled when the rectangles differ, blended with
// opacity, or (exact) replacing the pixels, alpha included
static void drawSurface(Graphics::ManagedSurface &dst, const Graphics::Surface &src, Common::Rect srcRect, const Common::Rect &dstRect, double opacity, bool exact) {
	srcRect.clip(Common::Rect(0, 0, src.w, src.h));
	if (srcRect.isEmpty() || dstRect.isEmpty())
		return;
	if (exact) {
		dst.blitFrom(src, srcRect, dstRect);
		return;
	}
	blendClipped(dst, src, srcRect, dstRect, (uint)(CLIP(opacity, 0.0, 1.0) * 255.0 + 0.5));
}

ObjRef newLayerImage(const Common::SharedPtr<AnimData> &anim, const Graphics::Surface *surface) {
	return newImage(anim, surface);
}

ObjRef newImageCopy(const Graphics::Surface &surface) {
	Ref<ImageObject> img(new ImageObject(&g_imageType));
	img->_own.create(surface.w, surface.h, Graphics::BlendBlit::getSupportedPixelFormat());
	img->_own.blitFrom(surface);
	img->_surface = &img->_own.rawSurface();
	img->setSize();
	return ObjRef(img.get());
}

// A layer's picture belongs to its animation: drawing into one gets a copy first
static ImageObject *writableImage(Object *o) {
	ImageObject *img = (ImageObject *)o;
	if (img->_surface != &img->_own.rawSurface()) {
		if (img->_surface)
			img->_own.copyFrom(*img->_surface);
		img->_anim.reset();
		img->_surface = img->_own.getPixels() ? &img->_own.rawSurface() : nullptr;
	}
	return img;
}

// Fill(color, rect)
static ObjRef imageFill(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("Fill", args, 1, 2))
		return ObjRef();
	ImageObject *img = writableImage(self);
	if (!img->_surface)
		return ObjRef(g_none);
	Object *c = args->get(0);
	uint32 color = img->_own.format.ARGBToColor(attrInt(c, "a", 255), attrInt(c, "r", 0), attrInt(c, "g", 0), attrInt(c, "b", 0));
	Common::Rect r = rectArg(args->_size > 1 ? args->get(1) : nullptr, img->_own.w, img->_own.h);
	r.clip(Common::Rect(0, 0, img->_own.w, img->_own.h));
	if (!r.isEmpty())
		img->_own.fillRect(r, color);
	return ObjRef(g_none);
}

static const Graphics::Surface *sourceSurface(Object *o) {
	const Graphics::Surface *s = imageSurface(o);
	if (!s || !s->getPixels()) {
		setErrorFormat(g_excTypeError, "expected an image, got %.100s", typeName(o));
		return nullptr;
	}
	return s;
}

// Composite(source, opacity, rcDst, rcSrc): blends source over this image
static ObjRef imageComposite(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("Composite", args, 1, 4))
		return ObjRef();
	const Graphics::Surface *src = sourceSurface(args->get(0));
	if (!src)
		return ObjRef();
	double opacity = 1.0;
	if (args->_size > 1 && !asDouble(args->get(1), &opacity))
		return ObjRef();
	Common::Rect srcRect = rectArg(args->_size > 3 ? args->get(3) : nullptr, src->w, src->h);
	Common::Rect dstRect = rectArg(args->_size > 2 ? args->get(2) : nullptr, srcRect.width(), srcRect.height());
	ImageObject *img = writableImage(self);
	if (img->_surface)
		drawSurface(img->_own, *src, srcRect, dstRect, opacity, false);
	return ObjRef(g_none);
}

// Copy(source, rcDst, rcSrc): replaces this image's pixels with source's
static ObjRef imageCopy(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("Copy", args, 1, 3))
		return ObjRef();
	const Graphics::Surface *src = sourceSurface(args->get(0));
	if (!src)
		return ObjRef();
	Common::Rect srcRect = rectArg(args->_size > 2 ? args->get(2) : nullptr, src->w, src->h);
	Common::Rect dstRect = rectArg(args->_size > 1 ? args->get(1) : nullptr, srcRect.width(), srcRect.height());
	ImageObject *img = writableImage(self);
	if (img->_surface)
		drawSurface(img->_own, *src, srcRect, dstRect, 1.0, true);
	return ObjRef(g_none);
}

// imgData: the pixels as bytes, A8R8G8B8 as it sits in memory (B, G, R, A),
// which the save code reads and writes one byte at a time
static Type g_pixelDataType("yagagraphics.ImageData", &g_objectType);

struct PixelDataObject : public Object {
	Ref<ImageObject> _image;
	explicit PixelDataObject(Type *t) : Object(t) {}
};

static int32 pixelDataLength(Object *o) {
	ImageObject *img = ((PixelDataObject *)o)->_image.get();
	return img->_surface ? img->_surface->w * img->_surface->h * 4 : 0;
}

static bool pixelAt(Object *o, int32 i, uint32 **pixel, int *channel) {
	ImageObject *img = ((PixelDataObject *)o)->_image.get();
	int32 n = pixelDataLength(o);
	if (i < 0)
		i += n;
	if (i < 0 || i >= n) {
		setErrorString(g_excIndexError, "image data index out of range");
		return false;
	}
	int p = i / 4;
	// Written only once writableImage has made the pixels the image's own
	*pixel = (uint32 *)const_cast<void *>(img->_surface->getBasePtr(p % img->_surface->w, p / img->_surface->w));
	*channel = i % 4;
	return true;
}

static byte getChannel(const Graphics::PixelFormat &f, uint32 c, int channel) {
	byte a, r, g, b;
	f.colorToARGB(c, a, r, g, b);
	return channel == 0 ? b : channel == 1 ? g : channel == 2 ? r : a;
}

static ObjRef pixelDataItem(Object *o, int32 i) {
	uint32 *pixel;
	int channel;
	if (!pixelAt(o, i, &pixel, &channel))
		return ObjRef();
	return newInt(getChannel(((PixelDataObject *)o)->_image->_surface->format, *pixel, channel));
}

static int pixelDataSetItem(Object *o, int32 i, Object *v) {
	int32 value;
	PixelDataObject *d = (PixelDataObject *)o;
	writableImage(d->_image.get());
	uint32 *pixel;
	int channel;
	if (!v || !asInt(v, &value) || !pixelAt(o, i, &pixel, &channel))
		return -1;
	const Graphics::PixelFormat &f = d->_image->_own.format;
	byte c[4];
	for (int k = 0; k < 4; k++)
		c[k] = getChannel(f, *pixel, k);
	c[channel] = (byte)value;
	*pixel = f.ARGBToColor(c[3], c[2], c[1], c[0]);
	return 0;
}

static ObjRef pixelDataStr(Object *o) {
	int32 n = pixelDataLength(o);
	Ref<Str> s(new Str(n));
	ImageObject *img = ((PixelDataObject *)o)->_image.get();
	for (int32 p = 0; p < n / 4; p++) {
		uint32 c = *(const uint32 *)img->_surface->getBasePtr(p % img->_surface->w, p / img->_surface->w);
		for (int k = 0; k < 4; k++)
			s->_data[p * 4 + k] = getChannel(img->_surface->format, c, k);
	}
	return ObjRef(s.get());
}

static SequenceMethods g_pixelDataAsSequence;

static ObjRef imageGetData(Object *o) {
	Ref<PixelDataObject> d(new PixelDataObject(&g_pixelDataType));
	d->_image = (ImageObject *)o;
	return ObjRef(d.get());
}

static const MethodDef g_imageMethods[] = {
	{ "Fill", imageFill, kMethVarArgs, nullptr },
	{ "Composite", imageComposite, kMethVarArgs, nullptr },
	{ "Copy", imageCopy, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_imageGetSets[] = {
	{ "imgData", imageGetData, nullptr },
	{ nullptr, nullptr, nullptr }
};

static void readyImageData() {
	g_pixelDataAsSequence.length = pixelDataLength;
	g_pixelDataAsSequence.item = pixelDataItem;
	g_pixelDataAsSequence.assItem = pixelDataSetItem;
	g_pixelDataType.asSequence = &g_pixelDataAsSequence;
	g_pixelDataType.str = pixelDataStr;
	g_pixelDataType.getAttr = genericGetAttr;
	typeReady(&g_pixelDataType);
}

// The camera sprites render through. Its target is the screen, or one of
// the game's own (a screen capture draws the room into an image)
static ObjRef systemCreateCamera(Object *, Tuple *, Dict *) {
	ObjRef c(new EngineObject(&g_cameraType));
	setEngineAttr(c.get(), "target", g_none);
	return c;
}

ObjRef offscreenTarget(Object *camera) {
	if (!camera || camera == g_none)
		return ObjRef();
	ObjRef target = getAttrString(camera, "target");
	if (!target) {
		clearError();
		return ObjRef();
	}
	if (target.get() == g_none || isInstanceOf(target.get(), &g_targetType))
		return ObjRef();
	return target;
}

bool renderThrough(Object *target, Object *image, double opacity, const Common::Rect &dst, const Common::Rect &src) {
	Ref<Float> o = newFloat(opacity);
	ObjRef d = newRect(dst.left, dst.top, dst.width(), dst.height());
	ObjRef s = newRect(src.left, src.top, src.width(), src.height());
	if (!callMethod(target, "RenderImage", 4, image, o.get(), d.get(), s.get())) {
		warning("yaga: RenderImage raised: %s", formatError().c_str());
		clearError();
		return false;
	}
	return true;
}

// RenderImage(image, opacity, rcDst, rcSrc) on the screen
static ObjRef targetRenderImage(Object *, Tuple *args, Dict *) {
	if (!checkArgs("RenderImage", args, 1, 4))
		return ObjRef();
	const Graphics::Surface *src = sourceSurface(args->get(0));
	if (!src)
		return ObjRef();
	double opacity = 1.0;
	if (args->_size > 1 && !asDouble(args->get(1), &opacity))
		return ObjRef();
	Common::Rect srcRect = rectArg(args->_size > 3 ? args->get(3) : nullptr, src->w, src->h);
	Common::Rect dstRect = rectArg(args->_size > 2 ? args->get(2) : nullptr, srcRect.width(), srcRect.height());
	if (g_display && g_display->isOpen())
		drawSurface(g_display->surface(), *src, srcRect, dstRect, opacity, false);
	return ObjRef(g_none);
}

static const MethodDef g_systemMethods[] = {
	{ "CreateImage", systemCreateImage, kMethVarArgs, nullptr },
	{ "CreateCamera", systemCreateCamera, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// ---- Cursors --------------------------------------------------------------------------

// The game loads its .cur files under ids of its own choosing
static Common::HashMap<int32, Graphics::Cursor *> g_cursors;
static int32 g_cursorId = -1;

static void freeCursors() {
	for (Common::HashMap<int32, Graphics::Cursor *>::iterator it = g_cursors.begin(); it != g_cursors.end(); ++it)
		delete it->_value;
	g_cursors.clear();
	g_cursorId = -1;
}

// LoadCursorFile(path, id): returns the id, or 0 if it cannot be read
static ObjRef targetLoadCursor(Object *, Tuple *args, Dict *) {
	if (!checkArgs("LoadCursorFile", args, 2, 2))
		return ObjRef();
	ObjRef pathObj = str(args->get(0));
	int32 id;
	if (!pathObj || !asInt(args->get(1), &id))
		return ObjRef();
	if (g_cursors.contains(id))
		return newInt(id);
	Common::String path = ((Str *)pathObj.get())->toString();
	Common::String bytes;
	if (!g_res->read(path, bytes)) {
		debugC(1, kDebugScript, "yagagraphics: no cursor %s", path.c_str());
		return newInt(0);
	}
	Common::MemoryReadStream stream((const byte *)bytes.c_str(), bytes.size());
	Image::IcoCurDecoder decoder;
	Graphics::Cursor *cursor = nullptr;
	if (decoder.open(stream) && decoder.numItems())
		cursor = decoder.loadItemAsCursor(0);
	if (!cursor) {
		warning("yaga: %s is not a cursor", path.c_str());
		return newInt(0);
	}
	g_cursors[id] = cursor;
	return newInt(id);
}

static ObjRef targetSetCursor(Object *, Tuple *args, Dict *) {
	if (!checkArgs("SetCursorByID", args, 1, 1))
		return ObjRef();
	int32 id;
	if (!asInt(args->get(0), &id))
		return ObjRef();
	if (id != g_cursorId && g_cursors.contains(id)) {
		CursorMan.replaceCursor(g_cursors[id]);
		g_cursorId = id;
	}
	return ObjRef(g_none);
}

static ObjRef targetGetCursorVisible(Object *) {
	return newBool(CursorMan.isVisible());
}

static int targetSetCursorVisible(Object *, Object *v) {
	int on = v ? isTrue(v) : 0;
	if (on < 0)
		return -1;
	CursorMan.showMouse(on != 0);
	return 0;
}

static const GetSetDef g_targetGetSets[] = {
	{ "cursorVisible", targetGetCursorVisible, targetSetCursorVisible },
	{ nullptr, nullptr, nullptr }
};

// ---- The render target ---------------------------------------------------------------

static int32 attrInt(Object *o, const char *name, int32 fallback) {
	ObjRef v = getAttrString(o, name);
	double d;
	if (!v || !asDouble(v.get(), &d)) {
		clearError();
		return fallback;
	}
	return (int32)d;
}

static ObjRef targetRenderBegin(Object *, Tuple *args, Dict *) {
	if (!checkArgs("RenderBegin", args, 0, 1))
		return ObjRef();
	int32 flags = 0;
	if (args->_size && !asInt(args->get(0), &flags))
		return ObjRef();
	if (flags & kClearBack)
		g_display->clear();
	return ObjRef(g_none);
}

static ObjRef targetRenderEnd(Object *, Tuple *, Dict *) {
	uint32 t0 = g_system->getMillis();
	g_display->present();
	debugC(3, kDebugScript, "yagagraphics: present took %u ms", g_system->getMillis() - t0);
	return ObjRef(g_none);
}

static const MethodDef g_targetMethods[] = {
	{ "RenderBegin", targetRenderBegin, kMethVarArgs, nullptr },
	{ "RenderEnd", targetRenderEnd, kMethVarArgs, nullptr },
	{ "RenderImage", targetRenderImage, kMethVarArgs, nullptr },
	{ "LoadCursorFile", targetLoadCursor, kMethVarArgs, nullptr },
	{ "SetCursorByID", targetSetCursor, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// CreateRenderTarget(mode, buffers, targetType) sets the screen mode
static ObjRef deviceCreateTarget(Object *, Tuple *args, Dict *) {
	if (!checkArgs("CreateRenderTarget", args, 1, 3))
		return ObjRef();
	Object *mode = args->get(0);
	if (!isInstanceOf(mode, &g_videoModeType)) {
		setErrorFormat(g_excTypeError, "CreateRenderTarget() expects a VideoMode, got %.100s", typeName(mode));
		return ObjRef();
	}
	int32 w = attrInt(mode, "width", 640), h = attrInt(mode, "height", 480);
	g_display->open(w, h);
	ObjRef t(new EngineObject(&g_targetType));
	setEngineAttr(t.get(), "width", newInt(w).get());
	setEngineAttr(t.get(), "height", newInt(h).get());
	setEngineAttr(t.get(), "format", newInt(attrInt(mode, "format", kPixelR5G6B5)).get());
	setEngineAttr(t.get(), "mode", args->_size > 2 ? args->get(2) : newInt(kTargetWindowed).get());
	// Render target events carry this as their deviceID
	setEngineAttr(t.get(), "identity", newInt(1).get());
	return t;
}

static const MethodDef g_deviceMethods[] = {
	{ "CreateRenderTarget", deviceCreateTarget, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

// GraphicsSystem() is a singleton with one video device
static Object *g_graphicsSystem = nullptr;

static ObjRef systemNew(Type *, Tuple *, Dict *) {
	if (g_graphicsSystem)
		return ObjRef(g_graphicsSystem);
	g_graphicsSystem = new EngineObject(&g_systemType);
	incRef(g_graphicsSystem);
	ObjRef device(new EngineObject(&g_deviceType));
	Ref<List> modes = newList();
	modes->append(newVideoMode(640, 480, kPixelR5G6B5).get());
	setEngineAttr(device.get(), "modes", modes.get());
	setEngineAttr(device.get(), "currentMode", newVideoMode(640, 480, kPixelR5G6B5).get());
	setEngineAttr(device.get(), "name", newStr("primary").get());
	Ref<List> devices = newList();
	devices->append(device.get());
	setEngineAttr(g_graphicsSystem, "systemType", newInt(0).get());
	setEngineAttr(g_graphicsSystem, "primaryVideoDev", device.get());
	setEngineAttr(g_graphicsSystem, "videoDevices", devices.get());
	return ObjRef(g_graphicsSystem);
}

static void fillYagagraphics(Module *m) {
	if (!(g_systemType._flags & kTypeReady)) {
		g_videoModeType.newObj = videoModeNew;
		g_videoModeType.repr = videoModeRepr;
		readyEngineType(&g_videoModeType, nullptr, nullptr, false);
		g_rectType.newObj = rectNew;
		g_rectType.repr = rectRepr;
		readyEngineType(&g_rectType, nullptr, nullptr, false);
		g_colorType.newObj = colorNew;
		g_colorType.repr = colorRepr;
		readyEngineType(&g_colorType, nullptr, nullptr, false);
		g_systemType.newObj = systemNew;
		readyEngineType(&g_systemType, g_systemMethods, nullptr, true);
		readyEngineType(&g_deviceType, g_deviceMethods, nullptr, true);
		readyEngineType(&g_targetType, g_targetMethods, g_targetGetSets, true);
		g_imageAnimType.newObj = imageAnimNew;
		readyEngineType(&g_imageAnimType, g_imageAnimMethods, g_imageAnimGetSets, true);
		readyEngineType(&g_animFrameType, nullptr, nullptr, true);
		readyEngineType(&g_animLayerType, nullptr, nullptr, true);
		g_imageType.newObj = imageNew;
		g_imageType.repr = imageRepr;
		readyEngineType(&g_imageType, g_imageMethods, g_imageGetSets, true);
		readyImageData();
		readyEngineType(&g_cameraType, nullptr, nullptr, true);
	}
	addConstants(m, "GraphicsHardware", g_hardware);
	addConstants(m, "PixelFormat", g_pixelFormats);
	addConstants(m, "TargetType", g_targetTypes);
	addConstants(m, "ClearFlags", g_clearFlags);
	addConstants(m, "RenderHint", g_renderHints);
	addConstants(m, "CompressionType", g_compressionTypes);
	addConstants(m, "GeomFlags", g_geomFlags);
	addConstants(m, "GeomType", g_geomTypes);
	moduleAddObject(m, "VideoMode", &g_videoModeType);
	moduleAddObject(m, "Rect", &g_rectType);
	moduleAddObject(m, "Color", &g_colorType);
	moduleAddObject(m, "GraphicsSystem", &g_systemType);
	moduleAddObject(m, "VideoDevice", &g_deviceType);
	moduleAddObject(m, "RenderTarget", &g_targetType);
	moduleAddObject(m, "IImageAnim", &g_imageAnimType);
	moduleAddObject(m, "IImage", &g_imageType);
	moduleAddObject(m, "Camera", &g_cameraType);
}

void registerYagagraphics(Resources *resources) {
	g_res = resources;
	g_graphicsSystem = nullptr;
	freeCursors();
	registerEngineModule("yagagraphics", fillYagagraphics);
}

} // End of namespace Yaga
