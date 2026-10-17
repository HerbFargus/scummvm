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
#include "common/archive.h"
#include "common/debug.h"
#include "common/stream.h"
#include "common/textconsole.h"

#include "dreamforge/detection.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/resource.h"
#include "dreamforge/sound.h"

namespace DreamForge {

Sound::Sound(DreamForgeEngine *vm, Audio::Mixer *mixer) : _vm(vm), _mixer(mixer), _music(kNoMusic) {
	memset(_sfxBanks, 0, sizeof(_sfxBanks));
	memset(_voiceBanks, 0, sizeof(_voiceBanks));
}

Sound::~Sound() {
	stopAll();
}

// INIT.PIG is 31 int16 words (C.PRG 0x246c0); words 0-3 and 18-20 aren't sound banks
bool Sound::init() {
	Common::SeekableReadStream *in = SearchMan.createReadStreamForMember("INIT.PIG");
	if (!in) {
		warning("Sound: no INIT.PIG");
		return false;
	}
	int16 w[31] = {};
	for (uint i = 0; i < 31 && !in->eos(); i++)
		w[i] = in->readSint16LE();
	delete in;

	const Bank sfx[12] = {
		{ 11000, w[4] }, { 12000, w[4] + w[5] }, { 13000, w[4] + w[6] }, { 14000, w[4] + w[7] },
		{ 20000, w[10] + w[11] }, { 21000, w[10] + w[12] }, { 22000, w[10] + w[13] }, { 23000, w[10] + w[14] },
		{ 30000, w[15] + w[16] }, { 31000, w[15] + w[17] }, { 40000, w[4] + w[9] }, { 41000, w[4] + w[8] }
	};
	const Bank voice[10] = {
		{ 10000, w[21] }, { 11000, w[22] }, { 20000, w[23] }, { 21000, w[24] }, { 22000, w[25] },
		{ 30000, w[26] }, { 42000, w[27] }, { 43000, w[28] }, { 45000, w[29] }, { 46000, w[30] }
	};
	memcpy(_sfxBanks, sfx, sizeof(sfx));
	memcpy(_voiceBanks, voice, sizeof(voice));
	return true;
}

// The original tests the banks from the top down: a number belongs to the
// highest bank that starts at or below it
int Sound::lookup(const Bank *banks, uint count, int id) {
	for (int i = count - 1; i >= 0; i--)
		if (id >= banks[i].first)
			return banks[i].base + id - banks[i].first;
	return -1;
}

int Sound::sfxIndex(int id) const {
	return lookup(_sfxBanks, ARRAYSIZE(_sfxBanks), id);
}

int Sound::voiceIndex(int id) const {
	return lookup(_voiceBanks, ARRAYSIZE(_voiceBanks), id);
}

bool Sound::play(ResourceArchive *archive, int index, uint rate, bool loop,
		Audio::Mixer::SoundType type, Audio::SoundHandle *handle) {
	if (!archive || index < 0 || (uint)index >= archive->size())
		return false;
	Resource *res = archive->getResource(index);
	if (!res || res->type != kResSound) {
		delete res;
		return false;
	}
	uint32 size = res->body->readUint32LE();
	size = MIN<uint32>(size, res->body->size() - 4) & ~1;
	byte *data = (byte *)malloc(size);
	res->body->read(data, size);
	delete res;

	Audio::SeekableAudioStream *raw = Audio::makeRawStream(data, size, rate,
		Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN, DisposeAfterUse::YES);
	Audio::AudioStream *stream = loop ? (Audio::AudioStream *)Audio::makeLoopingAudioStream(raw, 0) : raw;
	_mixer->playStream(type, handle, stream);
	return true;
}

void Sound::playSfx(int id, uint channel, bool loop) {
	if (channel >= kSfxChannels)
		return;
	int index = sfxIndex(id);
	debugC(1, kDebugSound, "sfx %d -> PIG1 #%d, channel %u%s", id, index, channel, loop ? ", loop" : "");
	_mixer->stopHandle(_sfxHandles[channel]);
	if (!play(_vm->getArchive(1), index, 22050, loop, Audio::Mixer::kSFXSoundType, &_sfxHandles[channel]))
		warning("Sound: bad effect number %d", id);
}

void Sound::stopSfx(uint channel) {
	if (channel < kSfxChannels)
		_mixer->stopHandle(_sfxHandles[channel]);
}

void Sound::playVoice(int id) {
	int index = voiceIndex(id);
	debugC(1, kDebugSound, "voice %d -> PIG3 #%d", id, index);
	_mixer->stopHandle(_voiceHandle);
	if (!play(_vm->getArchive(3), index, 22050, false, Audio::Mixer::kSpeechSoundType, &_voiceHandle))
		warning("Sound: bad voice number %d", id);
}

void Sound::stopVoice() {
	_mixer->stopHandle(_voiceHandle);
}

bool Sound::isVoicePlaying() const {
	return _mixer->isSoundHandleActive(_voiceHandle);
}

// C.PRG 0x11aa0: -666 leaves the music alone; any other track replaces it
void Sound::playMusic(int track, bool loop) {
	if (track == kNoMusic)
		return;
	debugC(1, kDebugSound, "music %d%s", track, loop ? ", loop" : "");
	_mixer->stopHandle(_musicHandle);
	_music = track;
	if (!play(_vm->getArchive(4), track, 11025, loop, Audio::Mixer::kMusicSoundType, &_musicHandle))
		warning("Sound: bad music track %d", track);
}

void Sound::stopMusic() {
	_mixer->stopHandle(_musicHandle);
	_music = kNoMusic;
}

void Sound::stopAll() {
	for (uint i = 0; i < kSfxChannels; i++)
		_mixer->stopHandle(_sfxHandles[i]);
	_mixer->stopHandle(_voiceHandle);
	stopMusic();
}

} // End of namespace DreamForge
