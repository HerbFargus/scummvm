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

#ifndef DREAMFORGE_CUTSCENE_H
#define DREAMFORGE_CUTSCENE_H

#include "common/array.h"
#include "common/str.h"

namespace DreamForge {

class DreamForgeEngine;

/**
 * The .ACT file that goes with each CINE\NNNN.DFA (read by C.PRG 0x5249c,
 * run frame by frame by 0x52698). 16-bit words: a list of voice numbers
 * to preload, a list of effect numbers to preload (each ended by 0xFFFF),
 * then for every frame (opcode, argument) pairs ended by 0xFFFF.
 */
struct ActScript {
	struct Cue {
		uint16 op;
		uint16 arg;
	};

	Common::Array<uint16> voices;
	Common::Array<uint16> effects;
	Common::Array<Common::Array<Cue> > frames;

	bool load(const Common::String &name);
};

// ACT opcodes (the cases of 0x52698) and what the cutaway callback (0x15210) does with them
enum ActOp {
	kActEnd = 0xFFC4,         // stop the cutscene
	kActSfxLoop3 = 0xFFC5,    // effect on channel 3, looping ... down to
	kActSfxLoop0 = 0xFFC8,    //   channel 0, looping
	kActSfx3 = 0xFFC9,        // effect on channel 3 ... down to
	kActSfx1 = 0xFFCB,        //   channel 1
	kActSfx0 = 0xFFCC,        // effect on channel 0
	kActMusic = 0xFFCD,       // music once; 0xFFFD stops it, 0xFFFC does nothing
	kActVoice = 0xFFCE,       // voice line (and lip sync); 0xFFFE ends the talking
	kActMusicLoop = 0xFFCF    // music, looping
};

class Cutscene {
public:
	Cutscene(DreamForgeEngine *vm);

	/**
	 * Plays CINE\<number>.DFA with its .ACT cues, centred on the screen.
	 * Returns false if it couldn't be loaded. Escape skips.
	 */
	bool play(int number);

	// Test hooks
	bool _fast;                         // don't wait between frames
	Common::Array<int> _shotFrames;     // frames to save as PNG
	Common::String _shotDir;

private:
	void runCues(const Common::Array<ActScript::Cue> &cues, bool &stop);

	DreamForgeEngine *_vm;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_CUTSCENE_H
