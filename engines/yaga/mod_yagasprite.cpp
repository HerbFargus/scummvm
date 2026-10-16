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
#include "common/hash-str.h"
#include "common/memstream.h"
#include "common/system.h"
#include "graphics/managed_surface.h"
#include "video/bink_decoder.h"

#include "yaga/detection.h"
#include "yaga/display.h"
#include "yaga/imageanim.h"
#include "yaga/modules.h"
#include "yaga/pyhost.h"
#include "yaga/yaga.h"
#include "yaga/python/internal.h"

// yagasprite: sprites (an animation, a position and playback state) and
// IVideoElement, the Bink movies (.da2) that CBinkSprite wraps.

namespace Yaga {

using namespace Python;

static Type g_videoType("yagasprite.IVideoElement", &g_objectType);

struct VideoObject;
static Common::Array<VideoObject *> g_videos; // every live movie, so input can end one

struct VideoObject : public EngineObject {
	Common::String _path;
	Ref<Str> _data; // the decoder reads from it
	Video::BinkDecoder *_decoder;
	const Graphics::Surface *_frame;
	bool _started;

	explicit VideoObject(Type *t) : EngineObject(t), _decoder(nullptr), _frame(nullptr), _started(false) {
		g_videos.push_back(this);
	}
	~VideoObject() override {
		delete _decoder;
		for (uint i = 0; i < g_videos.size(); i++) {
			if (g_videos[i] == this) {
				g_videos.remove_at(i);
				break;
			}
		}
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

// Ends the movies that are playing. The scripts have no way to skip one
// (the intro runs almost three minutes), so a click or a key does it
void skipVideos() {
	for (uint i = 0; i < g_videos.size(); i++) {
		VideoObject *v = g_videos[i];
		if (v->_decoder && v->_started && !v->_decoder->endOfVideo()) {
			debugC(1, kDebugScript, "yagasprite: skipped %s", v->_path.c_str());
			v->_decoder->stop();
			v->_started = false;
		}
	}
}

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

// ---- Talkies ------------------------------------------------------------------------------

// sprite.talkies: the lines a character speaks. character.PlayTalkie does
//   talkies.Stop(scene); talkies.Clear(); talkies.sounds.__iadd__(sound); talkies.Run(scene)
// and scripts wait on talkies.isPlaying and talkies.duration

static Type g_talkieListType("yagasprite.TalkieList", &g_objectType);
static Type g_soundListType("yagasprite.SoundList", &g_objectType);

struct TalkieListObject : public EngineObject {
	Ref<List> _sounds;
	Ref<Dict> _indexed; // talkies[i] = sound
	explicit TalkieListObject(Type *t) : EngineObject(t) {
		_sounds = newList();
		_indexed = newDict();
		setEngineAttr(this, "continuous", newInt(0).get());
		setEngineAttr(this, "queue", newInt(0).get());
	}
	Ref<List> all() const {
		Ref<List> l = newList();
		ObjRef values = dictValues(_indexed.get());
		for (uint32 i = 0; values && i < ((List *)values.get())->size(); i++)
			l->append(((List *)values.get())->get(i));
		for (uint32 i = 0; i < _sounds->size(); i++)
			l->append(_sounds->get(i));
		return l;
	}
};

struct SoundListObject : public Object {
	Ref<List> _sounds;
	explicit SoundListObject(Type *t) : Object(t) {}
};

static ObjRef newTalkieList() {
	return ObjRef(new TalkieListObject(&g_talkieListType));
}

static ObjRef talkiesClear(Object *self, Tuple *, Dict *) {
	TalkieListObject *t = (TalkieListObject *)self;
	t->_sounds->clear();
	t->_indexed = newDict();
	return ObjRef(g_none);
}

static ObjRef talkiesCallAll(Object *self, Tuple *args, const char *method) {
	Ref<List> sounds = ((TalkieListObject *)self)->all();
	ObjRef scene = args->_size ? ObjRef(args->get(0)) : ObjRef(g_none);
	for (uint32 i = 0; i < sounds->size(); i++)
		if (!callMethod(sounds->get(i), method, 1, scene.get()))
			return ObjRef();
	return ObjRef(g_none);
}

static ObjRef talkiesRun(Object *self, Tuple *args, Dict *) {
	return talkiesCallAll(self, args, "Run");
}

static ObjRef talkiesStop(Object *self, Tuple *args, Dict *) {
	return talkiesCallAll(self, args, "Stop");
}

static ObjRef talkiesIsPlaying(Object *o) {
	Ref<List> sounds = ((TalkieListObject *)o)->all();
	for (uint32 i = 0; i < sounds->size(); i++)
		if (soundIsPlaying(sounds->get(i)))
			return newInt(1);
	return newInt(0);
}

static ObjRef talkiesDuration(Object *o) {
	Ref<List> sounds = ((TalkieListObject *)o)->all();
	double longest = 0;
	for (uint32 i = 0; i < sounds->size(); i++)
		longest = MAX(longest, soundDuration(sounds->get(i)));
	return newFloat(longest);
}

static ObjRef talkiesSounds(Object *o) {
	Ref<SoundListObject> l(new SoundListObject(&g_soundListType));
	l->_sounds = ((TalkieListObject *)o)->_sounds;
	return ObjRef(l.get());
}

static ObjRef talkiesGetItem(Object *o, Object *key) {
	Object *v = ((TalkieListObject *)o)->_indexed->getItem(key);
	return ObjRef(v ? v : g_none);
}

static int talkiesSetItem(Object *o, Object *key, Object *value) {
	Dict *d = ((TalkieListObject *)o)->_indexed.get();
	if (value)
		return d->setItem(key, value);
	return d->delItem(key);
}

static int32 talkiesLength(Object *o) {
	return ((TalkieListObject *)o)->all()->size();
}

static const MethodDef g_talkieMethods[] = {
	{ "Clear", talkiesClear, kMethVarArgs, nullptr },
	{ "Run", talkiesRun, kMethVarArgs, nullptr },
	{ "Stop", talkiesStop, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_talkieGetSets[] = {
	{ "isPlaying", talkiesIsPlaying, nullptr },
	{ "duration", talkiesDuration, nullptr },
	{ "sounds", talkiesSounds, nullptr },
	{ nullptr, nullptr, nullptr }
};

static MappingMethods g_talkieAsMapping;

static ObjRef soundListIadd(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("__iadd__", args, 1, 1))
		return ObjRef();
	((SoundListObject *)self)->_sounds->append(args->get(0));
	return ObjRef(self);
}

static ObjRef soundListClear(Object *self, Tuple *, Dict *) {
	((SoundListObject *)self)->_sounds->clear();
	return ObjRef(g_none);
}

static ObjRef soundListIter(Object *o) {
	return getIter(((SoundListObject *)o)->_sounds.get());
}

static int32 soundListLength(Object *o) {
	return ((SoundListObject *)o)->_sounds->size();
}

static const MethodDef g_soundListMethods[] = {
	{ "__iadd__", soundListIadd, kMethVarArgs, nullptr },
	{ "Clear", soundListClear, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static SequenceMethods g_soundListAsSequence;

static void readyTalkieTypes() {
	g_talkieAsMapping.length = talkiesLength;
	g_talkieAsMapping.subscript = talkiesGetItem;
	g_talkieAsMapping.assSubscript = talkiesSetItem;
	g_talkieListType.asMapping = &g_talkieAsMapping;
	readyEngineType(&g_talkieListType, g_talkieMethods, g_talkieGetSets, true);
	g_soundListAsSequence.length = soundListLength;
	g_soundListType.asSequence = &g_soundListAsSequence;
	g_soundListType.iter = soundListIter;
	g_soundListType.methods = g_soundListMethods;
	g_soundListType.getAttr = genericGetAttr;
	typeReady(&g_soundListType);
}

// ---- Image sprites ----------------------------------------------------------------------

enum {
	kSceneRun = 0,
	kSceneStop = 1
};

enum {
	kPhonemeRest = 1 // the mouth at rest; masked layers are lipsync shapes
};

static const ConstDef g_layerFlags[] = {
	{ "LF_LAYER_ON", 1 },
	{ nullptr, 0 }
};

static Type g_spriteType("yagasprite.ISprite", &g_objectType);
static Type g_plainSpriteType("yagasprite.Sprite", &g_spriteType);
static Type g_talkieSpriteType("yagasprite.TalkieSprite", &g_spriteType);

struct PlacedLayer {
	const AnimLayer *layer;
	int x, y;
};

struct SpriteObject;
static Common::Array<SpriteObject *> g_sprites; // every live sprite, to advance

struct SpriteObject : public EngineObject {
	bool _playing;
	uint32 _started;
	const AnimData *_animSeen; // a new animation starts from its first frame
	uint32 _loopsSeen;
	uint32 _blinkAt;
	Common::HashMap<Common::String, bool> _layerFlags; // names the game turned on or off
	Common::Array<ObjRef> _sinks;
	Common::Array<ObjRef> _children;
	ObjRef _drawnAnim; // keeps the layers in _drawn alive
	Common::Array<PlacedLayer> _drawn;

	explicit SpriteObject(Type *t);
	~SpriteObject() override;
};

SpriteObject::SpriteObject(Type *t) : EngineObject(t), _playing(false), _started(0), _animSeen(nullptr), _loopsSeen(0) {
	_blinkAt = g_system->getMillis() + 1000 + g_engine->getRandomNumber(4000);
	setEngineAttr(this, "position", newPoint(0, 0, 0).get());
	setEngineAttr(this, "anim", g_none);
	// An empty rect, not None: the room manager hit-tests sprites before they draw
	setEngineAttr(this, "renderRect", newRect(0, 0, 0, 0).get());
	setEngineAttr(this, "currentFrame", newInt(0).get());
	setEngineAttr(this, "frameCount", newInt(0).get());
	setEngineAttr(this, "loopCount", newInt(0).get());
	setEngineAttr(this, "time", newFloat(0.0).get());
	setEngineAttr(this, "duration", newFloat(0.0).get());
	setEngineAttr(this, "hFlip", newInt(0).get());
	setEngineAttr(this, "vFlip", newInt(0).get());
	setEngineAttr(this, "visible", newInt(1).get());
	setEngineAttr(this, "opacity", newFloat(1.0).get());
	setEngineAttr(this, "renderMask", newInt(kPhonemeRest).get());
	setEngineAttr(this, "blinkEnabled", newInt(1).get());
	setEngineAttr(this, "talkies", newTalkieList().get());
	g_sprites.push_back(this);
}

SpriteObject::~SpriteObject() {
	for (uint i = 0; i < g_sprites.size(); i++) {
		if (g_sprites[i] == this) {
			g_sprites.remove_at(i);
			break;
		}
	}
}

static int32 dictInt(Object *o, const char *name, int32 fallback) {
	Object *v = o->getDict()->getItemString(name);
	double d;
	if (!v || !asDouble(v, &d)) {
		clearError();
		return fallback;
	}
	return (int32)d;
}

static double dictDouble(Object *o, const char *name, double fallback) {
	Object *v = o->getDict()->getItemString(name);
	double d;
	if (!v || !asDouble(v, &d)) {
		clearError();
		return fallback;
	}
	return d;
}

static Object *spriteAnim(SpriteObject *s) {
	return s->getDict()->getItemString("anim");
}

static AnimData *spriteAnimData(SpriteObject *s) {
	Object *a = spriteAnim(s);
	return a ? imageAnimData(a) : nullptr;
}

// Tells the sprite's sinks that its animation started over or finished;
// a character's exit animation finishing is what changes rooms
static void fireSinks(SpriteObject *s, int32 eventType) {
	Common::Array<ObjRef> sinks = s->_sinks;
	Ref<Int> type = newInt(eventType);
	for (uint i = 0; i < sinks.size(); i++) {
		if (!callMethod(sinks[i].get(), "Event", 2, type.get(), (Object *)s)) {
			warning("yaga: sprite event sink raised: %s", formatError().c_str());
			clearError();
		}
	}
}

// Steps currentFrame by the clock, at the animation's own rate. Sprites
// advance whether or not they are drawn: scene changes wait on them
static void advanceSprite(SpriteObject *s) {
	AnimData *anim = spriteAnimData(s);
	if (!s->_playing || !anim || anim->frames.empty())
		return;
	uint32 frameCount = anim->frames.size();
	uint32 now = g_system->getMillis();
	if (s->_animSeen != anim) {
		s->_animSeen = anim;
		s->_started = now;
		s->_loopsSeen = 0;
	}
	double fps = imageAnimFps(spriteAnim(s));
	double elapsed = (now - s->_started) / 1000.0;
	uint32 step = (uint32)(elapsed * fps);
	int32 loopCount = dictInt(s, "loopCount", 0);
	double duration = frameCount / fps * (loopCount > 0 ? loopCount : 1);
	setEngineAttr(s, "duration", newFloat(duration).get());
	setEngineAttr(s, "time", newFloat(MIN(elapsed, duration)).get());
	if (loopCount > 0 && step >= (uint32)loopCount * frameCount) {
		s->_playing = false;
		setEngineAttr(s, "time", newFloat(duration).get());
		setEngineAttr(s, "currentFrame", newInt(frameCount - 1).get());
		fireSinks(s, kSceneStop);
		return;
	}
	// SCENE_RUN again each time it starts over: scripts poll on it
	uint32 wraps = step / frameCount;
	if (wraps > s->_loopsSeen) {
		s->_loopsSeen = wraps;
		fireSinks(s, kSceneRun);
	}
	setEngineAttr(s, "currentFrame", newInt(step % frameCount).get());
}

void advanceSprites() {
	Common::Array<ObjRef> live;
	for (uint i = 0; i < g_sprites.size(); i++)
		live.push_back(ObjRef(g_sprites[i]));
	for (uint i = 0; i < live.size(); i++)
		advanceSprite((SpriteObject *)live[i].get());
}

// Which blink layer is showing, if any: the engine blinks characters
// itself (open, half, shut, half, open), unless the game turns it off
static const char *blinkLayer(SpriteObject *s) {
	if (!dictInt(s, "blinkEnabled", 1))
		return nullptr;
	uint32 now = g_system->getMillis();
	if ((int32)(now - s->_blinkAt) < 0)
		return nullptr;
	uint32 t = now - s->_blinkAt;
	if (t < 70)
		return "BLINK1";
	if (t < 160)
		return "BLINK2";
	if (t < 230)
		return "BLINK1";
	s->_blinkAt = now + 2500 + g_engine->getRandomNumber(4000);
	return nullptr;
}

// Where the layers' coordinates start: the position, except for cursor art,
// which is authored with its hotspot at the centre of the screen
static void spriteOrigin(SpriteObject *s, int *x, int *y) {
	int32 px = 0, py = 0;
	Object *pos = s->getDict()->getItemString("position");
	if (pos)
		pointXY(pos, &px, &py);
	Object *a = spriteAnim(s);
	Common::String locator = a ? imageAnimLocator(a) : Common::String();
	locator.toLowercase();
	for (uint i = 0; i < locator.size(); i++)
		if (locator[i] == '\\')
			locator.setChar('/', i);
	if (locator.contains("interface/cursors/")) {
		px -= 320;
		py -= 240;
	}
	*x = px;
	*y = py;
}

// The current frame's layers that draw, and where: masked layers only for
// the mouth shape wanted, blink layers only mid-blink, and none the game
// switched off
static void placeLayers(SpriteObject *s, const char *blink, Common::Array<PlacedLayer> &out) {
	out.clear();
	AnimData *anim = spriteAnimData(s);
	if (!anim || anim->frames.empty())
		return;
	uint32 wanted = (uint32)dictInt(s, "renderMask", kPhonemeRest);
	int32 index = dictInt(s, "currentFrame", 0);
	if (index < 0)
		index = 0;
	const AnimFrame &frame = anim->frames[index % anim->frames.size()];
	int ox, oy;
	spriteOrigin(s, &ox, &oy);
	for (uint i = 0; i < frame.layers.size(); i++) {
		const AnimLayer &l = frame.layers[i];
		if (!l.surface.getPixels())
			continue;
		if (l.mask && !(l.mask & wanted))
			continue;
		Common::String name = l.name;
		name.toUppercase();
		if (name.hasPrefix("BLINK")) {
			if (!blink || name != blink)
				continue;
		} else if (s->_layerFlags.contains(name) && !s->_layerFlags[name]) {
			continue;
		}
		PlacedLayer p = { &l, ox + l.x, oy + l.y };
		out.push_back(p);
	}
}

static void setRenderRect(SpriteObject *s, const Common::Array<PlacedLayer> &placed) {
	if (placed.empty())
		return;
	Common::Rect r(placed[0].x, placed[0].y, placed[0].x + placed[0].layer->surface.w, placed[0].y + placed[0].layer->surface.h);
	for (uint i = 1; i < placed.size(); i++)
		r.extend(Common::Rect(placed[i].x, placed[i].y, placed[i].x + placed[i].layer->surface.w, placed[i].y + placed[i].layer->surface.h));
	setEngineAttr(s, "renderRect", newRect(r.left, r.top, r.width(), r.height()).get());
}

static ObjRef spriteNew(Type *t, Tuple *, Dict *) {
	return ObjRef(new SpriteObject(t));
}

static ObjRef spriteInit(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

// Reading a position gives a copy: a Point is a value in the engine
static ObjRef spriteGetAttr(Object *o, Str *name) {
	if (name->equals("position")) {
		Object *pos = o->getDict()->getItemString("position");
		if (pos)
			return copyPoint(pos);
	}
	return stubFallbackGetAttr(o, name);
}

static int spriteSetAttr(Object *o, Str *name, Object *value) {
	SpriteObject *s = (SpriteObject *)o;
	ObjRef v(value);
	if (value && name->equals("position")) {
		v = copyPoint(value);
	} else if (value && name->equals("currentFrame")) {
		// Kept within the animation: scripts step past the end and test for the last frame
		double d;
		AnimData *anim = spriteAnimData(s);
		if (anim && !anim->frames.empty() && asDouble(value, &d))
			v = newInt(CLIP<int32>((int32)d, 0, anim->frames.size() - 1));
		clearError();
	}
	if (genericSetAttr(o, name, v.get()) < 0)
		return -1;
	if (value && name->equals("anim")) {
		// The animation's length is read off the sprite
		AnimData *anim = spriteAnimData(s);
		setEngineAttr(s, "frameCount", newInt(anim ? anim->frames.size() : 0).get());
		setEngineAttr(s, "currentFrame", newInt(0).get());
	}
	if (value && (name->equals("anim") || name->equals("position"))) {
		// Measured now, before it draws: the save screen centres labels on it at once
		Common::Array<PlacedLayer> placed;
		placeLayers(s, nullptr, placed);
		setRenderRect(s, placed);
	}
	return 0;
}

// Run is idempotent: the game calls it every loop for characters
static ObjRef spriteRun(Object *self, Tuple *, Dict *) {
	SpriteObject *s = (SpriteObject *)self;
	if (!s->_playing) {
		s->_playing = true;
		s->_started = g_system->getMillis();
		s->_animSeen = spriteAnimData(s);
		s->_loopsSeen = 0;
		fireSinks(s, kSceneRun);
	}
	return ObjRef(g_none);
}

static ObjRef spriteStop(Object *self, Tuple *, Dict *) {
	((SpriteObject *)self)->_playing = false;
	return ObjRef(g_none);
}

static ObjRef spriteRender(Object *self, Tuple *, Dict *) {
	SpriteObject *s = (SpriteObject *)self;
	advanceSprite(s);
	if (!g_display || !g_display->isOpen())
		return ObjRef(g_none);
	placeLayers(s, blinkLayer(s), s->_drawn);
	Object *anim = spriteAnim(s);
	s->_drawnAnim = anim ? ObjRef(anim) : ObjRef();
	double opacity = CLIP(dictDouble(s, "opacity", 1.0), 0.0, 1.0);
	uint alpha = (uint)(opacity * 255.0 + 0.5);
	Graphics::ManagedSurface &screen = g_display->surface();
	for (uint i = 0; i < s->_drawn.size(); i++) {
		const PlacedLayer &p = s->_drawn[i];
		screen.blendBlitFrom(p.layer->surface, Common::Point(p.x, p.y), Graphics::FLIP_NONE, MS_ARGB(alpha, 255, 255, 255));
	}
	setRenderRect(s, s->_drawn);
	return ObjRef(g_none);
}

// Intersect(collider): a per-pixel test against what draws, transparency included
static ObjRef spriteIntersect(Object *self, Tuple *args, Dict *) {
	SpriteObject *s = (SpriteObject *)self;
	if (!checkArgs("Intersect", args, 1, 1))
		return ObjRef();
	ObjRef xo = getAttrString(args->get(0), "x"), yo = xo ? getAttrString(args->get(0), "y") : ObjRef();
	double x, y;
	if (!xo || !yo || !asDouble(xo.get(), &x) || !asDouble(yo.get(), &y)) {
		clearError();
		return newInt(0);
	}
	Common::Array<PlacedLayer> placed;
	// Hidden sprites are still collided with (Putt-Putt's bunny maze is one)
	if (s->_drawnAnim.get() == spriteAnim(s) && !s->_drawn.empty())
		placed = s->_drawn;
	else
		placeLayers(s, nullptr, placed);
	for (uint i = 0; i < placed.size(); i++) {
		const Graphics::Surface &surf = placed[i].layer->surface;
		int lx = (int)x - placed[i].x, ly = (int)y - placed[i].y;
		if (lx < 0 || ly < 0 || lx >= surf.w || ly >= surf.h)
			continue;
		byte a, r, g, b;
		surf.format.colorToARGB(surf.getPixel(lx, ly), a, r, g, b);
		if (a)
			return newInt(1);
	}
	return newInt(0);
}

// SetLayerFlag(name, flag, on): turns a named layer on or off, which is how
// the game picks one of several alternatives (eye directions, say)
static ObjRef spriteSetLayerFlag(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("SetLayerFlag", args, 1, 3))
		return ObjRef();
	ObjRef name = str(args->get(0));
	if (!name)
		return ObjRef();
	int on = args->_size > 2 ? isTrue(args->get(2)) : 1;
	if (on < 0)
		return ObjRef();
	Common::String key = ((Str *)name.get())->toString();
	key.toUppercase();
	((SpriteObject *)self)->_layerFlags[key] = on != 0;
	return ObjRef(g_none);
}

static ObjRef spriteGetLayerFlag(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("GetLayerFlag", args, 1, 2))
		return ObjRef();
	ObjRef name = str(args->get(0));
	if (!name)
		return ObjRef();
	Common::String key = ((Str *)name.get())->toString();
	key.toUppercase();
	SpriteObject *s = (SpriteObject *)self;
	return newBool(!s->_layerFlags.contains(key) || s->_layerFlags[key]);
}

static ObjRef addToList(Common::Array<ObjRef> &list, Object *o) {
	for (uint i = 0; i < list.size(); i++)
		if (list[i].get() == o)
			return ObjRef(g_none);
	list.push_back(ObjRef(o));
	return ObjRef(g_none);
}

static ObjRef removeFromList(Common::Array<ObjRef> &list, Object *o) {
	for (uint i = 0; i < list.size(); i++) {
		if (list[i].get() == o) {
			list.remove_at(i);
			break;
		}
	}
	return ObjRef(g_none);
}

static ObjRef spriteRegisterSink(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("RegisterEventSink", args, 1, 1))
		return ObjRef();
	return addToList(((SpriteObject *)self)->_sinks, args->get(0));
}

static ObjRef spriteUnregisterSink(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("UnregisterEventSink", args, 1, 1))
		return ObjRef();
	return removeFromList(((SpriteObject *)self)->_sinks, args->get(0));
}

// Children: how a talkie's event stream is attached to the sprite
static ObjRef spriteAddChild(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("AddChild", args, 1, 2))
		return ObjRef();
	return addToList(((SpriteObject *)self)->_children, args->get(args->_size - 1));
}

static ObjRef spriteRemoveChild(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("RemoveChild", args, 1, 1))
		return ObjRef();
	return removeFromList(((SpriteObject *)self)->_children, args->get(0));
}

static ObjRef spriteRemoveChildren(Object *self, Tuple *, Dict *) {
	((SpriteObject *)self)->_children.clear();
	return ObjRef(g_none);
}

static ObjRef spriteNoop(Object *, Tuple *, Dict *) {
	return ObjRef(g_none);
}

static const MethodDef g_spriteMethods[] = {
	{ "__init__", spriteInit, kMethVarArgs | kMethKeywords, nullptr },
	{ "Run", spriteRun, kMethVarArgs, nullptr },
	{ "Stop", spriteStop, kMethVarArgs, nullptr },
	{ "Render", spriteRender, kMethVarArgs, nullptr },
	{ "Intersect", spriteIntersect, kMethVarArgs, nullptr },
	{ "SetLayerFlag", spriteSetLayerFlag, kMethVarArgs, nullptr },
	{ "GetLayerFlag", spriteGetLayerFlag, kMethVarArgs, nullptr },
	{ "RegisterEventSink", spriteRegisterSink, kMethVarArgs, nullptr },
	{ "UnregisterEventSink", spriteUnregisterSink, kMethVarArgs, nullptr },
	{ "AddChild", spriteAddChild, kMethVarArgs, nullptr },
	{ "InsertChild", spriteAddChild, kMethVarArgs, nullptr },
	{ "RemoveChild", spriteRemoveChild, kMethVarArgs, nullptr },
	{ "RemoveChildren", spriteRemoveChildren, kMethVarArgs, nullptr },
	{ "Seek", spriteNoop, kMethVarArgs, nullptr },
	{ "SetVolume", spriteNoop, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static void fillYagasprite(Module *m) {
	if (!(g_videoType._flags & kTypeReady)) {
		g_videoType.newObj = videoNew;
		readyTalkieTypes();
		readyEngineType(&g_videoType, g_videoMethods, g_videoGetSets, true);
		Type *spriteTypes[] = { &g_spriteType, &g_plainSpriteType, &g_talkieSpriteType };
		for (uint i = 0; i < ARRAYSIZE(spriteTypes); i++) {
			Type *t = spriteTypes[i];
			t->_flags |= kTypeBaseType | kTypeHasDict;
			t->newObj = spriteNew;
			t->methods = i == 0 ? g_spriteMethods : nullptr;
			t->getAttr = spriteGetAttr;
			t->setAttr = spriteSetAttr;
			typeReady(t);
		}
	}
	addConstants(m, "LayerFlags", g_layerFlags);
	moduleAddObject(m, "IVideoElement", &g_videoType);
	moduleAddObject(m, "ISprite", &g_spriteType);
	moduleAddObject(m, "Sprite", &g_plainSpriteType);
	moduleAddObject(m, "TalkieSprite", &g_talkieSpriteType);
}

void registerYagasprite() {
	registerEngineModule("yagasprite", fillYagasprite);
}

} // End of namespace Yaga
