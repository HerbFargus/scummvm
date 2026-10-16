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

#include "audio/audiostream.h"
#include "audio/decoders/mp3.h"
#include "audio/decoders/wave.h"
#include "audio/mixer.h"
#include "common/debug.h"
#include "common/memstream.h"
#include "common/system.h"

#include "yaga/detection.h"
#include "yaga/modules.h"
#include "yaga/pyhost.h"
#include "yaga/python/internal.h"

// yagasound: sounds (WAV effects, MP3 dialogue and music) on the ScummVM
// mixer, and the sound system they belong to

namespace Yaga {

using namespace Python;

static const ConstDef g_systemFlags[] = {
	{ "SF_FORCEWAVOUT", 1 },
	{ nullptr, 0 }
};

static Type g_soundType("yagasound.ISound", &g_objectType);
static Type g_soundSystemType("yagasound.SoundSystem", &g_objectType);

struct SoundObject : public EngineObject {
	Common::String _path;
	Ref<Str> _data; // the streams read from it
	Audio::Mixer::SoundType _soundType;
	Audio::SoundHandle _handle;
	bool _started;
	double _volume;
	double _duration; // seconds; negative until measured

	explicit SoundObject(Type *t) : EngineObject(t), _soundType(Audio::Mixer::kSFXSoundType), _started(false), _volume(1.0), _duration(-1.0) {}
	~SoundObject() override {
		if (_started)
			g_system->getMixer()->stopHandle(_handle);
	}

	bool isMp3() const {
		return _path.hasSuffixIgnoreCase(".mp3");
	}

	// A fresh stream over the data each time it plays
	Audio::SeekableAudioStream *makeStream() const {
		Common::MemoryReadStream *s = new Common::MemoryReadStream((const byte *)_data->_data, _data->_len);
		if (isMp3()) {
#ifdef USE_MAD
			return Audio::makeMP3Stream(s, DisposeAfterUse::YES);
#else
			delete s;
			return nullptr;
#endif
		}
		return Audio::makeWAVStream(s, DisposeAfterUse::YES);
	}

	bool isPlaying() const {
		return _started && g_system->getMixer()->isSoundHandleActive(_handle);
	}
};

static byte mixerVolume(double v) {
	return (byte)(CLIP(v, 0.0, 1.0) * Audio::Mixer::kMaxChannelVolume + 0.5);
}

// ISound(resource)
static ObjRef soundNew(Type *t, Tuple *args, Dict *) {
	if (!checkArgs("ISound", args, 1, 1))
		return ObjRef();
	Str *data = resourceData(args->get(0));
	if (!data)
		return ObjRef();
	Ref<SoundObject> s(new SoundObject(t));
	s->_path = ((ResourceObject *)args->get(0))->_path;
	s->_data = data;
	// The mixer's volume controls: the score, dialogue, and everything else
	Common::String p = s->_path;
	p.toLowercase();
	for (uint i = 0; i < p.size(); i++)
		if (p[i] == '\\')
			p.setChar('/', i);
	while (p.hasPrefix("/") || p.hasPrefix("./"))
		p = Common::String(p.c_str() + (p[0] == '/' ? 1 : 2));
	if (p.hasPrefix("music/"))
		s->_soundType = Audio::Mixer::kMusicSoundType;
	else if (p.hasPrefix("talkies/"))
		s->_soundType = Audio::Mixer::kSpeechSoundType;
	return ObjRef(s.get());
}

static ObjRef soundRepr(Object *o) {
	return newStr("<ISound " + ((SoundObject *)o)->_path + ">");
}

static ObjRef soundRun(Object *self, Tuple *, Dict *) {
	SoundObject *s = (SoundObject *)self;
	Audio::Mixer *mixer = g_system->getMixer();
	if (s->_started)
		mixer->stopHandle(s->_handle);
	s->_started = false;
	Audio::SeekableAudioStream *stream = s->makeStream();
	if (!stream) {
		warning("yaga: cannot play %s", s->_path.c_str());
		return ObjRef(g_none);
	}
	mixer->playStream(s->_soundType, &s->_handle, stream, -1, mixerVolume(s->_volume));
	s->_started = true;
	debugC(2, kDebugScript, "yagasound: playing %s", s->_path.c_str());
	return ObjRef(g_none);
}

static ObjRef soundStop(Object *self, Tuple *, Dict *) {
	SoundObject *s = (SoundObject *)self;
	if (s->_started)
		g_system->getMixer()->stopHandle(s->_handle);
	s->_started = false;
	return ObjRef(g_none);
}

static ObjRef soundGetIsPlaying(Object *o) {
	return newBool(((SoundObject *)o)->isPlaying());
}

static ObjRef soundGetVolume(Object *o) {
	return newFloat(((SoundObject *)o)->_volume);
}

static int soundSetVolume(Object *o, Object *v) {
	SoundObject *s = (SoundObject *)o;
	double d;
	if (!v || !asDouble(v, &d))
		return -1;
	s->_volume = d;
	if (s->_started)
		g_system->getMixer()->setChannelVolume(s->_handle, mixerVolume(d));
	return 0;
}

// Seconds: the scripts time subtitles and cutscenes by it
static ObjRef soundGetDuration(Object *o) {
	SoundObject *s = (SoundObject *)o;
	if (s->_duration < 0) {
		s->_duration = 0;
		Audio::SeekableAudioStream *stream = s->makeStream();
		if (stream) {
			s->_duration = stream->getLength().msecs() / 1000.0;
			delete stream;
		}
	}
	return newFloat(s->_duration);
}

static ObjRef soundGetPath(Object *o) {
	return newStr(((SoundObject *)o)->_path);
}

static const MethodDef g_soundMethods[] = {
	{ "Run", soundRun, kMethVarArgs, nullptr },
	{ "Stop", soundStop, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static const GetSetDef g_soundGetSets[] = {
	{ "isPlaying", soundGetIsPlaying, nullptr },
	{ "volume", soundGetVolume, soundSetVolume },
	{ "duration", soundGetDuration, nullptr },
	{ "path", soundGetPath, nullptr },
	{ nullptr, nullptr, nullptr }
};

bool soundIsPlaying(Object *o) {
	if (isInstanceOf(o, &g_soundType))
		return ((SoundObject *)o)->isPlaying();
	ObjRef v = getAttrString(o, "isPlaying");
	int t = v ? isTrue(v.get()) : 0;
	clearError();
	return t > 0;
}

double soundDuration(Object *o) {
	ObjRef v = isInstanceOf(o, &g_soundType) ? soundGetDuration(o) : getAttrString(o, "duration");
	double d = 0;
	if (!v || !asDouble(v.get(), &d)) {
		clearError();
		return 0;
	}
	return d;
}

// SoundSystem() is a singleton
static Object *g_soundSystem = nullptr;

static ObjRef systemNew(Type *, Tuple *, Dict *) {
	if (!g_soundSystem) {
		g_soundSystem = new EngineObject(&g_soundSystemType);
		incRef(g_soundSystem);
		setEngineAttr(g_soundSystem, "flags", newInt(0).get());
	}
	return ObjRef(g_soundSystem);
}

static void fillYagasound(Module *m) {
	if (!(g_soundType._flags & kTypeReady)) {
		g_soundType.newObj = soundNew;
		g_soundType.repr = soundRepr;
		readyEngineType(&g_soundType, g_soundMethods, g_soundGetSets, true);
		g_soundSystemType.newObj = systemNew;
		readyEngineType(&g_soundSystemType, nullptr, nullptr, true);
	}
	addConstants(m, "SoundSystemFlags", g_systemFlags);
	moduleAddObject(m, "ISound", &g_soundType);
	moduleAddObject(m, "SoundSystem", &g_soundSystemType);
}

void registerYagasound() {
	g_soundSystem = nullptr;
	registerEngineModule("yagasound", fillYagasound);
}

} // End of namespace Yaga
