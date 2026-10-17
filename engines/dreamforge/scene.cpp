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
#include "common/endian.h"
#include "common/textconsole.h"

#include "dreamforge/detection.h"
#include "dreamforge/resource.h"
#include "dreamforge/scene.h"

namespace DreamForge {

static Common::String readName(const byte *rec, uint size) {
	Common::String s;
	for (uint i = kRecordNameOffset; i < size && rec[i]; i++)
		s += (char)rec[i];
	return s;
}

void Record::parse(uint idOffset) {
	const byte *d = data.data();
	int32 top = (int32)READ_LE_UINT32(d), left = (int32)READ_LE_UINT32(d + 4);
	int32 height = (int32)READ_LE_UINT32(d + 8), width = (int32)READ_LE_UINT32(d + 12);
	rect = Common::Rect(left, top, left + width, top + height);
	for (uint i = 0; i < 2; i++) {
		walkX[i] = (int32)READ_LE_UINT32(d + 0x14 + i * 12);
		walkY[i] = (int32)READ_LE_UINT32(d + 0x18 + i * 12);
		walkPose[i] = (int32)READ_LE_UINT32(d + 0x1C + i * 12);
	}
	who = (int32)READ_LE_UINT32(d + kWho);
	id = (int32)READ_LE_UINT32(d + idOffset);
	name = readName(d, data.size());
}

static void readBytes(Common::SeekableReadStream &in, Common::Array<byte> &out, uint size) {
	out.resize(size);
	if (size)
		in.read(out.data(), size);
}

Scene::Scene() : _act(0), _area(0), _room(0), _version(0), _width(0), _height(0), _music(-1), _refScale(0), _footstepSfx(0), _start(0) {
	_shade[0] = _shade[1] = 0;
	memset(_spots, 0, sizeof(_spots));
	memset(_palette, 0, sizeof(_palette));
}

Scene::~Scene() {
	clear();
}

void Scene::clear() {
	_background.free();
	for (auto &o : _objects)
		o.frame.free();
	for (uint i = 0; i < _layers.size(); i++) {
		_layers[i]->surface.free();
		delete _layers[i];
	}
	_layers.clear();
	_hotspots.clear();
	_objects.clear();
	_walkMap.clear();
	_edges.clear();
}

Common::String Scene::fileName(int act, int area, int room) {
	return Common::String::format("SCENES/%dX%02dX%02d.PIG", act, area, room);
}

bool Scene::load(int act, int area, int room) {
	Common::String file = fileName(act, area, room);
	Resource *res = readResource(SearchMan.createReadStreamForMember(Common::Path(file, '/')));
	if (!res || res->type != kResScene) {
		warning("Scene: can't load %s", file.c_str());
		delete res;
		return false;
	}
	clear();
	_act = act;
	_area = area;
	_room = room;
	Common::SeekableReadStream &in = *res->body;

	byte v[8];
	in.read(v, 8);
	_version = READ_LE_FLOAT64(v);
	char name[8] = {};
	in.read(name, 7);
	_name = name;
	_shade[0] = in.readByte();
	_shade[1] = in.readByte();
	_height = in.readUint32LE();
	_width = in.readUint32LE();
	_music = in.readSint32LE();
	in.skip(0x98);  // four ambient sound slots of 0x26 bytes
	_footstepSfx = in.readSint32LE();
	uint32 nObjects = in.readUint32LE();
	uint32 nLayers = in.readUint32LE();
	uint32 nHotspots = in.readUint32LE();
	_refScale = in.readSint16LE();
	for (uint i = 0; i < 5; i++) {
		_spots[i].y = in.readSint32LE();
		_spots[i].x = in.readSint32LE();
		_spots[i].dir = in.readSint32LE();
		_spots[i].type = in.readSint32LE();
	}
	for (uint i = 0; i < 256 * 3; i++)
		_palette[i] = (in.readByte() & 0x3F) * 255 / 63;

	_hotspots.resize(nHotspots);
	for (uint i = 0; i < nHotspots; i++) {
		readBytes(in, _hotspots[i].data, kHotspotSize);
		_hotspots[i].parse(0x4AC);
		_hotspots[i].mask = READ_LE_UINT16(_hotspots[i].data.data() + 0x494);
	}

	readImageBlock(in, _background, _width, _height);

	_objects.resize(nObjects);
	for (uint i = 0; i < nObjects; i++) {
		SceneObject &obj = _objects[i];
		readBytes(in, obj.data, kObjectSize);
		obj.parse(0x298);
		obj.unk = in.readUint32LE();
		char anim[9] = {};
		in.read(anim, 8);
		obj.anim = anim;
		// flags & 4: a byte, then (if set) a run as long as the object's travel range
		if (obj.data[0x32] & 4) {
			if (in.readByte()) {
				int32 a = (int32)READ_LE_UINT32(obj.data.data() + 0x284);
				int32 b = (int32)READ_LE_UINT32(obj.data.data() + 0x28C);
				in.skip(ABS(a - b) + 1);
			}
		}
	}

	for (uint i = 0; i < nLayers; i++) {
		Layer *layer = new Layer();
		readBytes(in, layer->data, kLayerSize);
		layer->parse(0x284);
		layer->x = layer->rect.left;
		layer->y = layer->rect.top;
		readImageBlock(in, layer->surface, layer->rect.width(), layer->rect.height());
		_layers.push_back(layer);
	}

	readBytes(in, _walkMap, in.readUint32LE());
	uint32 nEdges = in.readUint32LE();
	_edges.resize(nEdges);
	for (uint i = 0; i < nEdges; i++) {
		_edges[i].y1 = in.readSint16LE();
		_edges[i].x1 = in.readSint16LE();
		_edges[i].y2 = in.readSint16LE();
		_edges[i].x2 = in.readSint16LE();
	}
	_start = in.readSint32LE();

	int32 left = in.size() - in.pos();
	debugC(1, kDebugScene, "Scene %s (%s): %ux%u, music %d, %u hotspots, %u objects, %u layers, %d bytes left",
		file.c_str(), _name.c_str(), _width, _height, _music, nHotspots, nObjects, nLayers, left);
	if (left != 0 || in.err())
		warning("Scene %s: parse ended %d bytes from the end", file.c_str(), left);
	delete res;
	return true;
}

} // End of namespace DreamForge
