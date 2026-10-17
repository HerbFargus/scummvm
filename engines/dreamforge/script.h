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

#ifndef DREAMFORGE_SCRIPT_H
#define DREAMFORGE_SCRIPT_H

#include "common/rect.h"

namespace DreamForge {

class DreamForgeEngine;

enum {
	kScriptInstrSize = 21,  // u8 opcode + 10 u16 arguments
	kScriptMaxInstrs = 26   // (0x224 - 2) / 21
};

/**
 * The action script interpreter (C.PRG 0x345a0). Scripts run to the end in
 * one go, as in the original; the waiting opcodes (talking, cinematics,
 * cutscenes) run their own loops.
 */
class ScriptRunner {
public:
	ScriptRunner(DreamForgeEngine *vm);

	/**
	 * Runs a record's script. visits is the record's click count, centre the
	 * point the puppies turn to face.
	 */
	void run(const byte *script, int visits, const Common::Point &centre);

private:
	bool compare(uint mode, int value, int against) const;

	DreamForgeEngine *_vm;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_SCRIPT_H
