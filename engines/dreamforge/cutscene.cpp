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

#include "common/archive.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"
#include "image/png.h"

#include "dreamforge/cutscene.h"
#include "dreamforge/detection.h"
#include "dreamforge/dfa.h"
#include "dreamforge/dreamforge.h"
#include "dreamforge/sound.h"

namespace DreamForge {

bool ActScript::load(const Common::String &name) {
	voices.clear();
	effects.clear();
	frames.clear();
	Common::SeekableReadStream *in = SearchMan.createReadStreamForMember(Common::Path(name, '/'));
	if (!in)
		return false;
	Common::Array<uint16> w;
	while (in->pos() + 2 <= in->size())
		w.push_back(in->readUint16LE());
	delete in;

	uint i = 0;
	while (i < w.size() && w[i] != 0xFFFF)
		voices.push_back(w[i++]);
	i++;
	while (i < w.size() && w[i] != 0xFFFF)
		effects.push_back(w[i++]);
	i++;
	while (i < w.size()) {
		Common::Array<Cue> cues;
		while (i + 1 < w.size() && w[i] != 0xFFFF) {
			Cue c = { w[i], w[i + 1] };
			cues.push_back(c);
			i += 2;
		}
		i++;
		frames.push_back(cues);
	}
	return true;
}

Cutscene::Cutscene(DreamForgeEngine *vm) : _vm(vm), _fast(false) {
}

void runActCues(DreamForgeEngine *vm, const Common::Array<ActScript::Cue> &cues, bool &stop) {
	Sound *sound = vm->getSound();
	for (auto &c : cues) {
		debugC(2, kDebugVideo, "  cue %04x %u", c.op, c.arg);
		if (c.op == kActEnd) {
			stop = true;
		} else if (c.op >= kActSfxLoop3 && c.op <= kActSfxLoop0) {
			sound->playSfx(c.arg, kActSfxLoop0 - c.op, true);
		} else if (c.op >= kActSfx3 && c.op <= kActSfx1) {
			sound->playSfx(c.arg, kActSfx0 - c.op, false);
		} else if (c.op == kActSfx0) {
			sound->playSfx(c.arg, 0, false);
		} else if (c.op == kActMusic) {
			if (c.arg == 0xFFFD)
				sound->stopMusic();
			else if (c.arg != 0xFFFC)
				sound->playMusic(c.arg, false);
		} else if (c.op == kActMusicLoop) {
			sound->playMusic(c.arg, true);
		} else if (c.op == kActVoice) {
			// TODO: the talking-head lip sync (C.PRG 0x14260) isn't drawn yet
			if (c.arg != 0xFFFE)
				sound->playVoice(c.arg);
		} else {
			warning("Cutscene: unknown ACT opcode %04x", c.op);
		}
	}
}

bool Cutscene::play(int number) {
	Common::String base = Common::String::format("CINE/%04d", number);
	DFADecoder dfa;
	if (!dfa.load(SearchMan.createReadStreamForMember(Common::Path(base + ".DFA", '/')))) {
		warning("Cutscene: can't load %s.DFA", base.c_str());
		return false;
	}
	ActScript act;
	if (!act.load(base + ".ACT"))
		debugC(1, kDebugVideo, "%s has no .ACT", base.c_str());
	debugC(1, kDebugVideo, "Cutscene %s: %dx%d, %d frames at %u ms, %u voices, %u effects, %u cue frames",
		base.c_str(), dfa.getWidth(), dfa.getHeight(), dfa.getFrameCount(), dfa.getFrameTime(),
		act.voices.size(), act.effects.size(), act.frames.size());

	Graphics::Surface screen;
	screen.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());
	screen.fillRect(Common::Rect(kScreenWidth, kScreenHeight), 0);
	int x = (kScreenWidth - dfa.getWidth()) / 2, y = (kScreenHeight - dfa.getHeight()) / 2;
	Common::Rect dst = Common::Rect(x, y, x + dfa.getWidth(), y + dfa.getHeight())
		.findIntersectingRect(Common::Rect(kScreenWidth, kScreenHeight));

	uint32 start = g_system->getMillis();
	bool stop = false;
	for (int frame = 0; frame < dfa.getFrameCount() && !stop && !_vm->shouldQuit(); frame++) {
		// The original runs the frame's cues, decodes it, waits, then shows it (0x528c8)
		if ((uint)frame < act.frames.size())
			runActCues(_vm, act.frames[frame], stop);
		const Graphics::Surface *surf = dfa.decodeNextFrame();
		if (!surf)
			break;
		if (dfa.paletteChanged())
			g_system->getPaletteManager()->setPalette(dfa.getPalette(), 0, 256);
		screen.copyRectToSurface(*surf, dst.left, dst.top,
			Common::Rect(dst.left - x, dst.top - y, dst.right - x, dst.bottom - y));

		if (!_fast) {
			uint32 due = start + (frame + 1) * dfa.getFrameTime();
			while (g_system->getMillis() < due && !stop) {
				Common::Event event;
				while (g_system->getEventManager()->pollEvent(event)) {
					if (event.type == Common::EVENT_KEYDOWN && event.kbd.keycode == Common::KEYCODE_ESCAPE)
						stop = true;
				}
				g_system->delayMillis(5);
			}
		}
		g_system->copyRectToScreen(screen.getPixels(), screen.pitch, 0, 0, kScreenWidth, kScreenHeight);
		g_system->updateScreen();

		for (int shot : _shotFrames) {
			if (shot != frame)
				continue;
			Common::DumpFile out;
			Common::String path = Common::String::format("%s/%04d_f%03d.png", _shotDir.c_str(), number, frame);
			if (out.open(Common::Path(path, '/'), true))
				Image::writePNG(out, *surf, dfa.getPalette());
		}
	}
	screen.free();
	_vm->getSound()->stopVoice();
	for (uint i = 0; i < kSfxChannels; i++)
		_vm->getSound()->stopSfx(i);
	return true;
}

} // End of namespace DreamForge
