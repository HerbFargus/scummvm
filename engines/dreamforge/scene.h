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
#include "common/endian.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace DreamForge {

class DFADecoder;
struct ActScript;

// Record sizes in the scene file (the loader, C.PRG 0x1e430)
enum {
	kHotspotSize = 0x4B0,
	kObjectSize = 0x29C,
	kLayerSize = 0x288,
	kRecordNameOffset = 0x258
};

enum {
	kRecordScriptOffset = 0x34,
	kRecordScriptSize = 0x224,
	kWho = 0x2C
};

/**
 * The header hotspots, objects and layers share: a rect stored as top, left,
 * height, width; where each puppy walks to before acting (x, y, pose; -1 and
 * pose 8 mean none); which puppy acts; the action script; a debug name.
 */
struct Record {
	Common::Array<byte> data;
	Common::Rect rect;
	int32 walkX[2], walkY[2], walkPose[2];
	int32 who;  // 0 nobody, 1 or 2 that puppy, 3 both, 4 the current one
	int32 id;   // global id, the index of its saved state
	Common::String name;

	void parse(uint idOffset);
	const byte *script() const { return data.data() + kRecordScriptOffset; }
};

struct Hotspot : Record {
	uint16 mask;     // 0x494: 1 walk-in region, 4 click, 2 use, ...; also picks the cursor
	int32 exitDir;   // +0x10: an exit's direction (< 8) for the arrow cursor
};

/** An animated object: a record plus the CINE animation it plays */
struct SceneObject : Record {
	uint32 unk;
	Common::String anim;

	// Running state (the 0x2f0 in-memory object, C.PRG 0x231c0 / 0x22100)
	DFADecoder *dfa;
	ActScript *act;
	int frame;        // -1 before the first
	uint32 nextTick;  // game timer tick of the next frame
	int16 delay;      // ticks between frames (+0x296)
	byte flags;       // +0x30: 0x01 colour 0 see-through, 0x04 not started, 0x08 loops, 0x10 finished
	byte flags2;      // +0x31: 0x80 hidden
	byte flags3;      // +0x32: 0x01 steps only when started, 0x04 follows a path
	int depth;
	bool temporary;   // an in-room animation (script op 0x0f), removed when it finishes
	int hideActor;    // the puppy it stands in for (1, 2, 3 both, 0 none)
	// An object that slides down a track between two rows (+0x32 & 4), scaled per row
	// by pathScale (percent, from the scene file); parked while flags2 & 0x20
	Common::Array<byte> pathScale;
	int pathRow, pathX, pathW, pathH;  // +0x2e4, left, +0x2e8, +0x2ec
	int pathDepthOff;                  // +0x2d8: end row - depth
	int pathChance;                    // +0x294: percent chance per try to set off
	bool isPath() const { return !pathScale.empty(); }
	int pathEnd() const { return (int32)READ_LE_UINT32(data.data() + 0x284); }
	int pathEndX() const { return (int32)READ_LE_UINT32(data.data() + 0x288); }
	int pathStart() const { return (int32)READ_LE_UINT32(data.data() + 0x28C); }
	int pathStartX() const { return (int32)READ_LE_UINT32(data.data() + 0x290); }

	SceneObject() : unk(0), dfa(nullptr), act(nullptr), frame(-1), nextTick(0), delay(0), flags(0), flags2(0),
		flags3(0), depth(0), temporary(false), hideActor(0), pathRow(0), pathX(0), pathW(0), pathH(0),
		pathDepthOff(0), pathChance(0) {}
};

/** A state overlay or occlusion piece; drawn over the background when its flags say so */
struct Layer : Record {
	int32 x, y;
	Graphics::Surface surface;
};

void deleteObject(SceneObject *o);

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
	Common::Array<SceneObject *> _objects;
	Common::Array<Layer *> _layers;
	Common::Array<byte> _walkMap;  // per row [row, n, n x 6 ints] (C.PRG 0x19c20)
	byte _shade[2];                // puppy darkening with depth: percent step, amount per step
	int16 _refScale;               // the walk map scale at which the puppies are drawn 1:1
	int32 _footstepSfx;
	struct Spot {
		int32 y, x, dir, type;
	};
	Spot _spots[5];                // where puppy 2 may wander to; type 2..4 are real spots
	struct Edge {
		int16 y1, x1, y2, x2;
	};
	Common::Array<Edge> _edges;    // the waypoint graph for walking round obstacles
	int32 _start;

private:
	void clear();
};

} // End of namespace DreamForge

#endif // DREAMFORGE_SCENE_H
