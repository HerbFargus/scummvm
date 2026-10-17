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

#ifndef DREAMFORGE_SOUND_H
#define DREAMFORGE_SOUND_H

#include "audio/mixer.h"

namespace DreamForge {

class DreamForgeEngine;
class ResourceArchive;

enum {
	kSfxChannels = 4,
	kNoMusic = -666  // the original's "leave the music alone"
};

/**
 * Sound effects, voices and music. All of it is 16-bit mono PCM in EH type 4
 * resources: effects in PIG1 and voices in PIG3 (22050 Hz), music in PIG4
 * (11025 Hz). Effects and voices are named by numbers in banks of 1000
 * (11000, 20000...), and INIT.PIG gives each bank's first archive index
 * (C.PRG 0x246c0, mapped by 0x14ae0 for effects and 0x14960 for voices).
 */
class Sound {
public:
	Sound(DreamForgeEngine *vm, Audio::Mixer *mixer);
	~Sound();

	/** Reads the bank table from INIT.PIG */
	bool init();

	/** Archive index of an effect (PIG1) or voice (PIG3) number, or -1 */
	int sfxIndex(int id) const;
	int voiceIndex(int id) const;

	void playSfx(int id, uint channel, bool loop);
	void stopSfx(uint channel);
	void playVoice(int id);
	void stopVoice();
	bool isVoicePlaying() const;
	/** Music track n is PIG4 entry n; kNoMusic keeps the current one */
	void playMusic(int track, bool loop);
	void stopMusic();
	int getMusic() const { return _music; }
	void stopAll();

private:
	struct Bank {
		int first;  // first number in the bank
		int base;   // archive index of that number
	};
	static int lookup(const Bank *banks, uint count, int id);
	bool play(ResourceArchive *archive, int index, uint rate, bool loop,
		Audio::Mixer::SoundType type, Audio::SoundHandle *handle);

	DreamForgeEngine *_vm;
	Audio::Mixer *_mixer;
	Bank _sfxBanks[12];
	Bank _voiceBanks[10];
	Audio::SoundHandle _sfxHandles[kSfxChannels];
	Audio::SoundHandle _voiceHandle;
	Audio::SoundHandle _musicHandle;
	int _music;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_SOUND_H
