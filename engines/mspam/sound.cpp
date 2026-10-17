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
#include "audio/decoders/wave.h"
#include "common/debug.h"
#include "common/textconsole.h"

#include "mspam/detection.h"
#include "mspam/media.h"
#include "mspam/sound.h"

namespace MSPAM {

SoundManager::SoundManager(Audio::Mixer *mixer, MediaManager *media) : _mixer(mixer), _media(media) {
}

SoundManager::~SoundManager() {
	killAll();
}

Audio::AudioStream *SoundManager::makeStream(uint32 id, bool loop) {
	const MediaEntry *entry = _media->find(id);
	if (!entry || entry->type != kMediaSound) {
		warning("Sound %x is missing", id);
		return nullptr;
	}
	Common::SeekableReadStream *s = _media->open(id);
	if (!s)
		return nullptr;
	Audio::RewindableAudioStream *wave = Audio::makeWAVStream(s, DisposeAfterUse::YES);
	if (!wave) {
		warning("Sound %x isn't a WAV", id);
		return nullptr;
	}
	if (loop)
		return Audio::makeLoopingAudioStream(wave, 0);
	return wave;
}

bool SoundManager::start(Sound &sound) {
	Audio::AudioStream *stream = makeStream(sound.id, sound.flags & kFlagAutoLoop);
	if (!stream)
		return false;
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &sound.handle, stream);
	sound.started = true;
	debugC(1, kDebugSound, "Sound %x plays in group %s", sound.id, sound.group.c_str());
	return true;
}

void SoundManager::play(uint32 id, const Common::String &group, bool exclusive, uint32 flags) {
	if (exclusive)
		stopGroup(group);
	Sound sound;
	sound.id = id;
	sound.group = group;
	sound.flags = flags;
	sound.started = false;
	if (start(sound))
		_sounds.push_back(sound);
}

void SoundManager::add(uint32 id, const Common::String &group, bool exclusive) {
	if (exclusive)
		killGroup(group);
	Sound sound;
	sound.id = id;
	sound.group = group;
	sound.flags = kFlagAutoKill;
	sound.started = false;
	_sounds.push_back(sound);
}

void SoundManager::playGroup(const Common::String &group) {
	for (Sound &s : _sounds) {
		if (s.group == group && !s.started)
			start(s);
	}
}

void SoundManager::stopGroup(const Common::String &group) {
	for (int i = _sounds.size() - 1; i >= 0; i--) {
		if (_sounds[i].group == group) {
			_mixer->stopHandle(_sounds[i].handle);
			_sounds.remove_at(i);
		}
	}
}

void SoundManager::pauseGroup(const Common::String &group, bool pause) {
	for (Sound &s : _sounds) {
		if (s.group == group && s.started)
			_mixer->pauseHandle(s.handle, pause);
	}
}

void SoundManager::killGroup(const Common::String &group) {
	stopGroup(group);
}

void SoundManager::killAll() {
	for (Sound &s : _sounds)
		_mixer->stopHandle(s.handle);
	_sounds.clear();
}

void SoundManager::playChannel(Audio::SoundHandle &handle, uint32 id) {
	_mixer->stopHandle(handle);
	Audio::AudioStream *stream = makeStream(id, false);
	if (stream) {
		_mixer->playStream(Audio::Mixer::kSFXSoundType, &handle, stream);
		debugC(1, kDebugSound, "Sound %x plays on a sprite", id);
	}
}

void SoundManager::stopChannel(Audio::SoundHandle &handle) {
	_mixer->stopHandle(handle);
}

bool SoundManager::isPlaying(const Audio::SoundHandle &handle) const {
	return _mixer->isSoundHandleActive(handle);
}

// Finished sounds leave their groups, except those still waiting for Play Group
void SoundManager::update() {
	for (int i = _sounds.size() - 1; i >= 0; i--) {
		if (_sounds[i].started && !_mixer->isSoundHandleActive(_sounds[i].handle))
			_sounds.remove_at(i);
	}
}

} // End of namespace MSPAM
