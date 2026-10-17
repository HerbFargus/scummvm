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

#ifndef DREAMFORGE_SCENE_H
#define DREAMFORGE_SCENE_H

#include "common/array.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace DreamForge {

// Record sizes in the scene file (the loader, C.PRG 0x1e430)
enum {
	kHotspotSize = 0x4B0,
	kObjectSize = 0x29C,
	kLayerSize = 0x288,
	kRecordNameOffset = 0x258
};

struct Hotspot {
	Common::Array<byte> data;
	Common::String name;
};

/** An animated object: a record plus the CINE animation it plays */
struct SceneObject {
	Common::Array<byte> data;
	uint32 unk;
	Common::String anim;
};

/** A state overlay or occlusion piece; drawn over the background when its flags say so */
struct Layer {
	Common::Array<byte> data;
	Common::String name;
	int32 x, y;
	Graphics::Surface surface;
};

class Scene {
public:
	Scene();
	~Scene();

	/** Loads SCENES\<act>X<area>X<room>.PIG */
	bool load(int act, int area, int room);
	static Common::String fileName(int act, int area, int room);

	int _act, _area, _room;
	Common::String _name;
	double _version;
	uint32 _width, _height;
	int32 _music;
	byte _palette[256 * 3];  // 8-bit, scaled from the 6-bit VGA palette
	Graphics::Surface _background;
	Common::Array<Hotspot> _hotspots;
	Common::Array<SceneObject> _objects;
	Common::Array<Layer *> _layers;
	Common::Array<byte> _walkMap;  // per column [id, n, n x 6 ints]; see C.PRG 0x18990
	Common::Array<byte> _pairs;
	int32 _start;

private:
	void clear();
};

} // End of namespace DreamForge

#endif // DREAMFORGE_SCENE_H
