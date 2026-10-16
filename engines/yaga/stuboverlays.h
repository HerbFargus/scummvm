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
// boot path needs to get through (one graphics system, a 640x480 mode). Run
// in a stand-in module's namespace; anything not defined here stays a traced
// stand-in. The real engine modules replace all of this.

namespace Yaga {

struct StubOverlay {
	const char *module;
	const char *source;
};

static const StubOverlay g_stubOverlays[] = {
	{ "yagagraphics", R"PY(
class GraphicsHardware:
    SYSTEM_SOFTWARE = 0
    SYSTEM_HARDWARE = 1

class PixelFormat:
    PXL_UNKNOWN = 0
    PXL_R5G6B5 = 1
    PXL_X1R5G5B5 = 2
    PXL_A1R5G5B5 = 3
    PXL_R8G8B8 = 4
    PXL_X8R8G8B8 = 5
    PXL_A8R8G8B8 = 6

class TargetType:
    TARGET_WINDOWED = 0
    TARGET_FULL_SCREEN = 1

class VideoMode:
    def __init__(self, width=640, height=480, format=PixelFormat.PXL_R5G6B5):
        self.width = width
        self.height = height
        self.format = format

class _VideoDevice(_Stub):
    def __init__(self):
        self.modes = [VideoMode(640, 480, PixelFormat.PXL_R5G6B5), VideoMode(640, 480, PixelFormat.PXL_X1R5G5B5)]
        self.currentMode = VideoMode(640, 480, PixelFormat.PXL_R5G6B5)
    def CreateRenderTarget(self, mode, buffers, targetType):
        target = _stub('yagagraphics.RenderTarget')
        target.width = mode.width
        target.height = mode.height
        target.format = mode.format
        return target

class _GraphicsSystem(_Stub):
    def __init__(self):
        self.systemType = GraphicsHardware.SYSTEM_SOFTWARE
        self.primaryVideoDev = _VideoDevice()
        self.videoDevices = [self.primaryVideoDev]

_graphicsSystem = None

def GraphicsSystem():
    global _graphicsSystem
    if _graphicsSystem is None:
        _graphicsSystem = _GraphicsSystem()
    return _graphicsSystem
)PY" },
	{ "yaga", R"PY(
class _System(_Stub):
    def __init__(self):
        self.systemMemoryTotal = 512 * 1024 * 1024
        self.systemMemoryAvail = 256 * 1024 * 1024

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
