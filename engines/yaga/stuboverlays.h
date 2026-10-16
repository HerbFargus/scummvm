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

#ifndef YAGA_STUBOVERLAYS_H
#define YAGA_STUBOVERLAYS_H

// Temporary scaffolding for the tracing stand-ins: the little behaviour the
// boot path needs to get through (the system object). Run
// in a stand-in module's namespace; anything not defined here stays a traced
// stand-in. The real engine modules replace all of this.

namespace Yaga {

struct StubOverlay {
	const char *module;
	const char *source;
};

static const StubOverlay g_stubOverlays[] = {
	{ "yaga", R"PY(
class _UInt32Seq:
    def __init__(self):
        self.items = []
    def __iadd__(self, value):
        self.items.append(value)
        return self
    def __iter__(self):
        return iter(self.items)
    def __len__(self):
        return len(self.items)

class _System(_Stub):
    def __init__(self):
        self.systemMemoryTotal = 512 * 1024 * 1024
        self.systemMemoryAvail = 256 * 1024 * 1024
    def CreateUInt32Seq(self):
        return _UInt32Seq()

_system = None

def System():
    global _system
    if _system is None:
        _system = _System()
    return _system
)PY" },
	{ nullptr, nullptr }

};

} // End of namespace Yaga

#endif // YAGA_STUBOVERLAYS_H
