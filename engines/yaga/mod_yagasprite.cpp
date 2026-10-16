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
#include "common/memstream.h"
#include "video/bink_decoder.h"

#include "yaga/detection.h"
#include "yaga/display.h"
#include "yaga/modules.h"
#include "yaga/pyhost.h"
#include "yaga/python/internal.h"

// yagasprite: so far only IVideoElement, the Bink movies (.da2) that
// CBinkSprite wraps. Image sprites are still stand-ins.

namespace Yaga {

using namespace Python;

static Type g_videoType("yagasprite.IVideoElement", &g_objectType);

struct VideoObject : public EngineObject {
	Common::String _path;
	Ref<Str> _data; // the decoder reads from it
	Video::BinkDecoder *_decoder;
	const Graphics::Surface *_frame;
	bool _started;

	explicit VideoObject(Type *t) : EngineObject(t), _decoder(nullptr), _frame(nullptr), _started(false) {}
	~VideoObject() override {
		delete _decoder;
	}

	// Decodes the frames that have come due, keeping the latest
	void advance() {
		if (!_decoder || !_started)
			return;
		while (_decoder->needsUpdate()) {
			const Graphics::Surface *f = _decoder->decodeNextFrame();
			if (!f)
				break;
			_frame = f;
		}
	}

	bool isPlaying() {
		advance();
		return _decoder && _started && !_decoder->endOfVideo();
	}
};

// IVideoElement(resource)
static ObjRef videoNew(Type *t, Tuple *args, Dict *) {
	if (!checkArgs("IVideoElement", args, 1, 1))
		return ObjRef();
	Object *res = args->get(0);
	Str *data = resourceData(res);
	if (!data)
		return ObjRef();
	Ref<VideoObject> v(new VideoObject(t));
	v->_path = ((ResourceObject *)res)->_path;
	v->_data = data;
	v->_decoder = new Video::BinkDecoder();
	if (g_display && g_display->isOpen())
		v->_decoder->setOutputPixelFormat(g_display->format());
	Common::MemoryReadStream *stream = new Common::MemoryReadStream((const byte *)data->_data, data->_len);
	if (!v->_decoder->loadStream(stream)) {
		warning("yaga: %s is not a Bink movie", v->_path.c_str());
		delete v->_decoder;
		v->_decoder = nullptr;
	} else {
		debugC(1, kDebugScript, "yagasprite: movie %s, %ux%u, %u frames", v->_path.c_str(),
			v->_decoder->getWidth(), v->_decoder->getHeight(), v->_decoder->getFrameCount());
	}
	setEngineAttr(v.get(), "volume", newFloat(1.0).get());
	return ObjRef(v.get());
}

static ObjRef videoRun(Object *self, Tuple *, Dict *) {
	VideoObject *v = (VideoObject *)self;
	if (v->_decoder && !v->_started) {
		v->_decoder->start();
		v->_started = true;
		debugC(1, kDebugScript, "yagasprite: playing %s", v->_path.c_str());
	}
	return ObjRef(g_none);
}

static ObjRef videoStop(Object *self, Tuple *, Dict *) {
	VideoObject *v = (VideoObject *)self;
	if (v->_decoder && v->_started)
		v->_decoder->stop();
	v->_started = false;
	return ObjRef(g_none);
}

static ObjRef videoPause(Object *self, Tuple *, Dict *) {
	VideoObject *v = (VideoObject *)self;
	if (v->_decoder && v->_started)
		v->_decoder->pauseVideo(!v->_decoder->isPaused());
	return ObjRef(g_none);
}

// Render(camera): the movie fills the screen, centred if it is smaller
static ObjRef videoRender(Object *self, Tuple *, Dict *) {
	VideoObject *v = (VideoObject *)self;
	v->advance();
	if (!v->_frame || !v->_started || !g_display || !g_display->isOpen())
		return ObjRef(g_none);
	Graphics::ManagedSurface &screen = g_display->surface();
	Common::Point at((screen.w - v->_frame->w) / 2, (screen.h - v->_frame->h) / 2);
	screen.blitFrom(*v->_frame, at);
	return ObjRef(g_none);
}

static ObjRef videoIsPlaying(Object *o) {
	return newBool(((VideoObject *)o)->isPlaying());
}

static ObjRef videoGetVolume(Object *o) {
	Object *v = o->getDict()->getItemString("volume");
	return v ? ObjRef(v) : ObjRef(newFloat(1.0));
}

static int videoSetVolume(Object *o, Object *value) {
	VideoObject *v = (VideoObject *)o;
	double d;
	if (!value || !asDouble(value, &d))
		return -1;
	setEngineAttr(o, "volume", value);
	if (v->_decoder)
		v->_decoder->setVolume((byte)CLIP<double>(d * 255.0, 0.0, 255.0));
	return 0;
}

static ObjRef videoGetPath(Object *o) {
	return newStr(((VideoObject *)o)->_path);
}

static const MethodDef g_videoMethods[] = {
	{ "Run", videoRun, kMethVarArgs, nullptr },
	{ "Stop", videoStop, kMethVarArgs, nullptr },
	{ "Pause", videoPause, kMethVarArgs, nullptr },
	{ "Render", videoRender, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_videoGetSets[] = {
	{ "isPlaying", videoIsPlaying, nullptr },
	{ "volume", videoGetVolume, videoSetVolume },
	{ "path", videoGetPath, nullptr },
	{ nullptr, nullptr, nullptr }
};

static void fillYagasprite(Module *m) {
	g_videoType.newObj = videoNew;
	readyEngineType(&g_videoType, g_videoMethods, g_videoGetSets, true);
	moduleAddObject(m, "IVideoElement", &g_videoType);
}

void registerYagasprite() {
	registerEngineModule("yagasprite", fillYagasprite);
}

} // End of namespace Yaga
