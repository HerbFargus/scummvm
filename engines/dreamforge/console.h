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

#ifndef DREAMFORGE_CONSOLE_H
#define DREAMFORGE_CONSOLE_H

#include "gui/debugger.h"

namespace DreamForge {

class DreamForgeEngine;

class Console : public GUI::Debugger {
public:
	Console(DreamForgeEngine *vm);
	~Console() override {}

private:
	bool cmdScene(int argc, const char **argv);
	bool cmdScenes(int argc, const char **argv);
	bool cmdHotspots(int argc, const char **argv);
	bool cmdLayers(int argc, const char **argv);
	bool cmdLayer(int argc, const char **argv);
	bool cmdScroll(int argc, const char **argv);
	bool cmdArchives(int argc, const char **argv);
	bool cmdShot(int argc, const char **argv);

	DreamForgeEngine *_vm;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_CONSOLE_H
