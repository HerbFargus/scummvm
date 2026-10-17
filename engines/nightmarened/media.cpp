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
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/paletteman.h"
#include "image/png.h"
#include "video/smk_decoder.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/media.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

enum {
	kFileTable = 0x4a05d0,     // 0x14 bytes per file: type, CD dir, install dir, name
	kClipFiles = 0x4b4af4,     // file-table index per clip
	kFrameList = 0x4b4a68,     // NED01000 99:1, colour key 10 -> DAT_00520200
	kStillList = 0x4b4a8c,     // 99:2 with palette, for the still-picture path
	kEndFrameList = 0x4b4ab0,  // 99:1 without a key, clips 0x2f and 0x30
	kClipLists = 0x4b4b94,     // 98:clip, palette only -> DAT_00520208
	kClipMusic = 0x51,         // 87992208.wav
	kNafUi = 1                 // NED01000
};

Media::Media(Game *game) : _game(game), _transFps(15), _installLevel(1), _loadedClip(-1) {
}

Media::~Media() {
	stopMusic();
	freeClip();
}

Common::String Media::getFileName(int index) const {
	const ExeImage &exe = _game->exe();
	uint32 va = exe.readUint32(kFileTable + index * 0x14 + 0x0c);
	Common::String name;
	for (int i = 0; va && i < 64; i++) {
		char c = (char)(exe.readUint16(va + i) & 0xff);
		if (!c)
			break;
		name += c;
	}
	return name;
}

// The game fades in to the music volume preference (0x455650); this plays at full volume
void Media::playMusic(int fileIndex) {
	stopMusic();
	Common::String name = getFileName(fileIndex);
	Common::SeekableReadStream *stream = _game->getEngine()->openFile(name);
	if (!stream) {
		warning("Can't find music %s", name.c_str());
		return;
	}
	Audio::RewindableAudioStream *wav = Audio::makeWAVStream(stream, DisposeAfterUse::YES);
	if (!wav)
		return;
	debugC(1, kDebugSound, "Music %s", name.c_str());
	g_system->getMixer()->playStream(Audio::Mixer::kMusicSoundType, &_music, Audio::makeLoopingAudioStream(wav, 0));
}

void Media::stopMusic() {
	g_system->getMixer()->stopHandle(_music);
}

void Media::freeClip() {
	_game->freeList(_clipList);
	_game->freeList(_frameList);
	_clipList.clear();
	_frameList.clear();
	_loadedClip = -1;
}

// 0x479ff0
int Media::loadClip(int clip) {
	freeClip();
	if (!_game->getNaf(kNafUi))
		return 5;
	if (clip == 0x30 || clip == 0x2f) {
		_frameList = _game->readList(kEndFrameList, 1);
		_game->loadList(kNafUi, _frameList, 99);
		_clipList = _game->readList(kClipLists + clip * 0x24, 1);
		_game->loadList(kNafUi, _clipList, 0x62);
	} else if (_installLevel == 0 && clip != 0x19 && clip != 0x1a && clip != 0x1f && (clip < 0x21 || clip > 0x29)) {
		_frameList = _game->readList(kStillList, 1);
		_game->loadList(kNafUi, _frameList, 99);
	} else {
		_frameList = _game->readList(kFrameList, 1);
		_game->loadList(kNafUi, _frameList, 99);
		_clipList = _game->readList(kClipLists + clip * 0x24, 1);
		_game->loadList(kNafUi, _clipList, 0x62);
	}
	_loadedClip = clip;
	return 0;
}

// 0x47a3e0
int Media::playClip(int clip, int z, FrameCallback cb, void *user) {
	if (_installLevel == 0) {
		// 0x47a110 (a still and Ned's TV animation for 20 s) is not ported
		warning("TV clip %d: the Install_Level 0 still-picture path is not ported", clip);
		freeClip();
		_game->closeNaf(kNafUi);
		return -2;
	}
	if (_loadedClip != clip)
		loadClip(clip);
	playMusic(kClipMusic);

	Common::String name = getFileName(_game->exe().readUint32(kClipFiles + clip * 4));
	BitmapRec *frameRec = _frameList.empty() ? nullptr : _frameList[0].bitmap;
	Video::SmackerDecoder decoder;
	Common::SeekableReadStream *stream = _game->getEngine()->openFile(name);
	if (!frameRec || !frameRec->object || _clipList.empty() || !_clipList[0].hasPalette || !stream ||
		!decoder.loadStream(stream)) {
		warning("Can't play TV clip %s", name.c_str());
		stopMusic();
		freeClip();
		_game->closeNaf(kNafUi);
		return -0x20;
	}
	debugC(1, kDebugVideo, "TV clip %d: %s, %d frames at %d fps", clip, name.c_str(), decoder.getFrameCount(), _transFps);

	// Test hook: nightmarened_videoshot=<NAME.SMK>:<frame>:<png> saves that frame and quits
	Common::String shotPath;
	int shotFrame = -1;
	if (ConfMan.hasKey("nightmarened_videoshot")) {
		Common::String spec = ConfMan.get("nightmarened_videoshot");
		size_t a = spec.findFirstOf(':'), b = a == Common::String::npos ? a : spec.find(':', a + 1);
		if (b != Common::String::npos && spec.substr(0, a).equalsIgnoreCase(name)) {
			shotFrame = atoi(spec.substr(a + 1, b - a - 1).c_str());
			shotPath = spec.substr(b + 1);
		}
	}

	Display &display = _game->display();
	display.applyPalette(_clipList[0].palette, kPalAll);
	frameRec->object->setDestination(display.getPrimary());
	frameRec->object->setDrawOrder(z);
	frameRec->object->setVisibility(true);
	decoder.start();

	int count = decoder.getFrameCount();
	uint32 next = 0;
	Graphics::Surface &dst = frameRec->bitmap.surface;
	for (int frame = 0; frame < count && !_game->getEngine()->shouldQuit();) {
		uint32 now = _game->now();
		if (next < now) {
			next = 1000 / _transFps + now;
			if (cb)
				cb(user, frame, count, now);
			// SmackGoto + SmackToBuffer + SmackDoFrame into the bitmap's pixels
			const Graphics::Surface *src = decoder.decodeNextFrame();
			if (src) {
				int w = MIN<int>(src->w, dst.w), h = MIN<int>(src->h, dst.h);
				for (int y = 0; y < h; y++)
					memcpy(dst.getBasePtr(0, y), src->getBasePtr(0, y), w);
			}
			display.draw();
			if (frame == shotFrame) {
				Common::DumpFile out;
				byte pal[256 * 3];
				g_system->getPaletteManager()->grabPalette(pal, 0, 256);
				if (out.open(Common::Path(shotPath, '/'), true))
					Image::writePNG(out, display.getPrimary()->getBuffer(), pal);
				_game->getEngine()->quitGame();
			}
			frame++;
		} else {
			// 0x4471e0: the message pump until the next frame is due
			Common::Event event;
			while (g_system->getEventManager()->pollEvent(event)) {
			}
			g_system->delayMillis(MIN<uint32>(next - now, 10));
		}
	}

	stopMusic();
	freeClip();
	_game->closeNaf(kNafUi);
	if (_game->ned())
		_game->ned()->markTime();
	return 0;
}

} // End of namespace NightmareNed
