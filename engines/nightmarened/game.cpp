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
#include "common/endian.h"
#include "common/system.h"

#include <math.h>

#include "nightmarened/detection.h"
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/naf.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"

namespace NightmareNed {

Scene::~Scene() {
	for (Scene *sub : _subs)
		delete sub;
}

void Scene::addSubScene(Scene *sub) {
	sub->_parent = this;
	_subs.push_back(sub);
}

Game::Game(NightmareNedEngine *vm) : _vm(vm), _display(&_res), _ned(nullptr), _current(nullptr),
	_inputHook(nullptr), _optionsAllowed(false) {
	for (int i = 0; i < 12; i++)
		_nafs[i] = nullptr;
	_startMillis = g_system->getMillis();
	_randomSeed = 1; // the CRT default until a scene calls srand (0x48c5d0)
	_media = new Media(this);
	_hints = new Hints(this);
}

Game::~Game() {
	for (Scene *s : _scenes)
		delete s;
	delete _hints;
	delete _media;
	delete _ned;
	for (int i = 0; i < 12; i++)
		delete _nafs[i];
}

uint32 Game::crtRandom() {
	_randomSeed = _randomSeed * 0x343fd + 0x269ec3;
	return (_randomSeed >> 16) & 0x7fff;
}

const ExeImage &Game::exe() const {
	return _vm->getExe();
}

uint32 Game::now() const {
	return g_system->getMillis() - _startMillis;
}

NafArchive *Game::getNaf(int fileIndex) {
	if (fileIndex < 0 || fileIndex >= 12)
		return nullptr;
	if (!_nafs[fileIndex])
		_nafs[fileIndex] = _vm->openNaf(kNafNumbers[fileIndex]);
	return _nafs[fileIndex];
}

void Game::closeNaf(int fileIndex) {
	if (fileIndex >= 0 && fileIndex < 12) {
		delete _nafs[fileIndex];
		_nafs[fileIndex] = nullptr;
	}
}

bool Game::loadList(int fileIndex, Common::Array<Request> &list, int scene) {
	NafArchive *naf = getNaf(fileIndex);
	return naf && _res.loadList(naf, list, scene);
}

void Game::initScenes() {
	_hints->newGame();
	for (Scene *s : _scenes) {
		s->init();
		for (Scene *sub : s->getSubScenes())
			sub->init();
	}
}

Scene *Game::findScene(int scene, int sub) const {
	for (Scene *s : _scenes) {
		if (s->getId() != scene)
			continue;
		if (sub == -1)
			return s;
		for (Scene *child : s->getSubScenes()) {
			if (child->getId() == sub)
				return child;
		}
	}
	return nullptr;
}

int Game::getCurrentSceneId() const {
	if (!_current)
		return -1;
	return _current->getParent() ? _current->getParent()->getId() : _current->getId();
}

// The old scene's leave, the new one's load, the old one's unload, the new one's enter.
// The new scene is told where it came from as (scene, sub) or (scene, -1).
void Game::switchScene(int scene, int sub, int originScene, int originSub) {
	Scene *target = findScene(scene, sub);
	if (!target) {
		warning("No scene %d:%d", scene, sub);
		return;
	}
	debugC(1, kDebugLoading, "Scene %d:%d", scene, sub);
	if (_ned)
		_ned->sceneSwitchBegin();

	int fromScene = -1, fromSub = -1;
	if (!_current) {
		fromScene = originScene;
		fromSub = originSub;
		target->load(fromScene, fromSub);
		if (_ned)
			_ned->markTime();
	} else {
		if (_current->getParent()) {
			fromScene = _current->getParent()->getId();
			fromSub = _current->getId();
		} else {
			fromScene = _current->getId();
		}
		_current->leave(scene, sub);
		target->load(fromScene, fromSub);
		_current->unload(scene, sub);
		if (_ned) {
			_ned->show(false);
			_ned->markTime();
		}
	}
	target->enter(fromScene, fromSub);
	if (_ned)
		_ned->markTime();
	_current = target;
}

void Game::jumpToScene(int scene, int sub) {
	Scene *target = findScene(scene, sub);
	if (!target) {
		warning("No scene %d:%d", scene, sub);
		return;
	}
	debugC(1, kDebugLoading, "Scene %d:%d (restart)", scene, sub);
	if (_ned)
		_ned->sceneSwitchBegin();
	if (_current) {
		_current->leave(-1, -1);
		_current->unload(-1, -1);
	}
	target->load(-1, -1);
	if (_ned)
		_ned->show(false);
	target->enter(-1, -1);
	_current = target;
}

int Game::tick(uint32 input, uint32 now) {
	if (_inputHook)
		_inputHook(this, input, now, input);
	return _current ? _current->tick(input, now) : 0;
}

// DirectSound hundredths of a dB -> mixer volume and balance
void Game::playVoice(Voice &voice, const byte *waveFormat, const byte *pcm, uint32 size, int volume, int pan, bool loop) {
	stopVoice(voice);
	uint16 channels = READ_LE_UINT16(waveFormat + 2);
	uint32 rate = READ_LE_UINT32(waveFormat + 4);
	uint16 bits = READ_LE_UINT16(waveFormat + 14);
	byte flags = (bits == 16 ? Audio::FLAG_16BITS | Audio::FLAG_LITTLE_ENDIAN : Audio::FLAG_UNSIGNED) |
		(channels == 2 ? Audio::FLAG_STEREO : 0);
	byte *copy = (byte *)malloc(size);
	memcpy(copy, pcm, size);
	Audio::SeekableAudioStream *raw = Audio::makeRawStream(copy, size, rate, flags, DisposeAfterUse::YES);
	Audio::AudioStream *stream = loop ? (Audio::AudioStream *)Audio::makeLoopingAudioStream(raw, 0) : raw;

	double gain = pow(10.0, CLIP(volume, -10000, 0) / 2000.0);
	int sidePan = CLIP(pan, -10000, 10000);
	double side = 1.0 - pow(10.0, -ABS(sidePan) / 2000.0);
	int balance = (int)(side * 127) * (sidePan < 0 ? -1 : 1);
	g_system->getMixer()->playStream(Audio::Mixer::kSFXSoundType, &voice.handle, stream, -1,
		(byte)(gain * Audio::Mixer::kMaxChannelVolume), (int8)balance);
	voice.active = true;
}

void Game::stopVoice(Voice &voice) {
	if (voice.active)
		g_system->getMixer()->stopHandle(voice.handle);
	voice.active = false;
}

bool Game::isVoicePlaying(const Voice &voice) const {
	return voice.active && g_system->getMixer()->isSoundHandleActive(voice.handle);
}

bool Game::playSlotSound(Slot &slot, Slot *stop) {
	if (stop)
		stopSlotSound(*stop);
	stopSlotSound(slot);
	if (slot.sound.size() < 18)
		return false;
	uint32 pcmOffset = 18 + READ_LE_UINT16(slot.sound.data() + 16);
	if (pcmOffset >= slot.sound.size())
		return false;
	playVoice(slot.voice, slot.sound.data(), slot.sound.data() + pcmOffset, slot.sound.size() - pcmOffset, 0, 0, false);
	return true;
}

void Game::stopSlotSound(Slot &slot) {
	stopVoice(slot.voice);
}

} // End of namespace NightmareNed
