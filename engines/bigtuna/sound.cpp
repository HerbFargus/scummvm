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
#include "audio/decoders/raw.h"
#include "common/debug.h"
#include "common/textconsole.h"

#include "bigtuna/detection.h"
#include "bigtuna/movie.h"
#include "bigtuna/sound.h"

namespace BigTuna {

Sound::Sound(Audio::Mixer *mixer) : _mixer(mixer), _time(0) {
	for (auto &ch : _channels) {
		ch.movie = nullptr;
		ch.res = 0;
		ch.volume = ch.fadeFrom = ch.fadeTo = 255;
		ch.fadeStart = ch.fadeLength = 0;
	}
}

Sound::~Sound() {
	stopAll();
}

void Sound::play(int channel, Movie *movie, uint res) {
	if (!valid(channel))
		return;
	Channel &ch = _channels[channel - 1];
	if (res && ch.movie == movie && ch.res == res && _mixer->isSoundHandleActive(ch.handle))
		return;
	_mixer->stopHandle(ch.handle);
	ch.movie = movie;
	ch.res = res;
	if (res == 0)
		return;
	if (movie->getType(res) != kResSound) {
		warning("Sound::play(): resource %d of %s is not a sound", res, movie->getName().c_str());
		return;
	}

	Common::SeekableReadStream *stream = movie->getResource(res);
	byte bits = stream->readByte();
	uint16 rate = stream->readUint16LE();
	uint32 size = stream->size() - 3;
	if (bits != 8 || rate == 0)
		warning("Sound::play(): unexpected format %d bits %d Hz", bits, rate);
	byte *data = (byte *)malloc(size);
	stream->read(data, size);
	delete stream;

	debugC(1, kDebugSound, "Sound channel %d: resource %d, %d Hz, %d bytes", channel, res, rate, size);
	Audio::AudioStream *audio = Audio::makeRawStream(data, size, rate, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &ch.handle, audio, -1, ch.volume);
}

void Sound::stop(int channel) {
	if (valid(channel))
		_mixer->stopHandle(_channels[channel - 1].handle);
}

void Sound::stopAll() {
	for (auto &ch : _channels)
		_mixer->stopHandle(ch.handle);
}

void Sound::reset() {
	for (auto &ch : _channels) {
		_mixer->stopHandle(ch.handle);
		ch.movie = nullptr;
		ch.res = 0;
		ch.volume = ch.fadeFrom = ch.fadeTo = 255;
		ch.fadeLength = 0;
	}
}

bool Sound::isBusy(int channel) const {
	if (!valid(channel))
		return false;
	return _mixer->isSoundHandleActive(_channels[channel - 1].handle);
}

void Sound::setVolume(int channel, int volume, uint fade) {
	if (!valid(channel))
		return;
	Channel &ch = _channels[channel - 1];
	volume = CLIP(volume, 0, 255);
	ch.fadeFrom = ch.volume;
	ch.fadeTo = volume;
	ch.fadeStart = _time;
	ch.fadeLength = fade * 1000 / 60;
	if (!ch.fadeLength)
		ch.volume = volume;
	applyVolume(ch);
}

int Sound::getVolume(int channel) const {
	return valid(channel) ? _channels[channel - 1].volume : 0;
}

void Sound::update(uint32 time) {
	_time = time;
	for (auto &ch : _channels) {
		if (!ch.fadeLength)
			continue;
		uint32 elapsed = time - ch.fadeStart;
		if (elapsed >= ch.fadeLength) {
			ch.volume = ch.fadeTo;
			ch.fadeLength = 0;
		} else {
			ch.volume = ch.fadeFrom + (ch.fadeTo - ch.fadeFrom) * (int)elapsed / (int)ch.fadeLength;
		}
		applyVolume(ch);
	}
}

void Sound::applyVolume(Channel &ch) {
	if (_mixer->isSoundHandleActive(ch.handle))
		_mixer->setChannelVolume(ch.handle, ch.volume);
}

} // End of namespace BigTuna
