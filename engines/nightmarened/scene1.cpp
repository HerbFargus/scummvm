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

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "video/smk_decoder.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene1.h"
#include "nightmarened/scene1_enemies1.h"
#include "nightmarened/scene1_enemies2.h"
#include "nightmarened/scene1_graves.h"
#include "nightmarened/scene1_sub0.h"
#include "nightmarened/scene1_sub2.h"
#include "nightmarened/scene1_sub7.h"

namespace NightmareNed {

Scene1::Scene1(Game *game) : GraveyardRoom(game, 1, this) {
	_e1 = new Scene1Enemies1(this);
	_e2 = new Scene1Enemies2(this);
	// 0x459690: sub-scenes 0..7 (1 and 6 have no methods)
	addSubScene(_sub0 = new Scene1Sub0(game, this));
	addSubScene(new Scene(game, 1));
	addSubScene(_sub2 = new Scene1Sub2(game, this));
	addSubScene(new Scene1Sub3(game, this));
	addSubScene(new Scene1Sub4(game, this));
	addSubScene(new Scene1Sub5(game, this));
	addSubScene(new Scene(game, 6));
	addSubScene(_sub7 = new Scene1Sub7(game, this));

	_nedPos = Common::Point(0, 0);
	_tile = 0;
	_tileCount = kTiles;
	_yOff = _riseLeft = _rise = 0;
	_inputFree = 1;
	_grabbed = 0;
	_grabEnd = 0;
	_keeperTime = 0;
	_visionCheck = _toSub7 = _toSub2 = _toGrave = _graveNext = _toVideo = _exitTo = 0;
	_costumeMode = _flipped = 0;
	_grabSlot = (int16)_game->exe().readUint16(0x4a1c88);
	_bellSlot = (int16)_game->exe().readUint16(0x4a1c84);
	for (int i = 0; i < 8; i++)
		_done[i] = 0;

	_bkgTop = _bkgLow = nullptr;
	_frameShown = _game->exe().readSint32(0x4a1c90);
	_hillsTile = _game->exe().readSint32(0x4a3718);
	_standSlot = _game->exe().readSint32(0x4a3714);
	_clouds = nullptr;
	_cloudX = _cloudY = 0;
	_cloudTime = 0;
	for (int i = 0; i < kEnemies; i++)
		_enemyLoaded[i] = _enemyLoading[i] = _enemyShown[i] = 0;
	_nextDraw = 0;
	_gotoDone = false;
	_savedTile = 0;
	memset(_savedPalette, 0, sizeof(_savedPalette));
	memset(_gamePalette, 0, sizeof(_gamePalette));
	memset(_palette524d80, 0, sizeof(_palette524d80));
	_videoFrames = _videoFrame = 0;
	_diggerNoMirror = _game->exe().readSint32(0x4a38f0);
	_diggerTime = 0;
	_mainLoaded = _nedLoaded = 0;
	_toScene5Bits = 0;
	_costumeOn = _costumeOff = _bellRung = 0;
	for (int i = 0; i < 4; i++)
		_typed[i] = 0;
	_diggerLoaded = _diggerShown = _diggerReady = 0;
	_diggerList = _game->exe().readSint32(0x4a38f4);
	_diggerSlot = (int16)_game->exe().readUint16(0x4a38f8);
	_keeperSlot = -1;
	_keeperTurns = 0;
	_keeperGreets = _game->exe().readSint32(0x4a02b8);
	_keeperSpeech = 0;
	_ambientSlot = (int16)_game->exe().readUint16(0x4b447c);
	_ambientTime = 0;
	_puffSlot = (int16)_game->exe().readUint16(0x4b4478);

	_hooks.user = this;
	_hooks.use = hookUse;
	_hooks.region = hookRegion;
	_hooks.findTarget = hookFindTarget;
	_hooks.hitTarget = hookHitTarget;
}

Scene1::~Scene1() {
	freeAll();
	cloudsFree();
	delete _bkgTop;
	delete _bkgLow;
	_backdrop.free();
	delete _e1;
	delete _e2;
}

// ---- Shared helpers ----

void Scene1::xformWrap(void *user, int &x, int &y) {
	Scene1 *s = (Scene1 *)user;
	y += s->_yOff;
	if (0x4cc < s->_tile && x < 0x500)
		x += (kTiles - s->_tile) * kTileW;
	else if (s->_tile < 0x48 && 0x500 < x)
		x -= (s->_tile + kTiles) * kTileW;
	else
		x -= s->_tile * kTileW;
}

void Scene1::xformPlain(void *user, int &x, int &y) {
	((Scene1 *)user)->toScreen(x, y);
}

void Scene1::targetAddSlot(int16 id) {
	Slot &s = slot(id);
	Placement p;
	memset(&p, 0, sizeof(p));
	p.area = kArea;
	p.slot = id;
	p.sub = 1;
	p.x = (int16)s.x;
	p.y = (int16)s.y;
	p.z = (int16)s.z;
	p.kind = 2;
	p.offX = s.offsets[0];
	p.offY = s.offsets[1];
	p.msPerStep = kMsPerStep;
	p.transparentColor = s.key;
	p.flip = s.flags & 2;
	_targets.push_back(p);
}

void Scene1::targetRemoveSlot(int16 id) {
	targetRemove(kArea, id);
}

void Scene1::regionAdd(int16 type, int16 id, const int32 rect[4], int32 *outId) {
	addRegion(type, id, rect, outId);
}

void Scene1::regionRemove(int32 id) {
	removeRegion(id);
}

void Scene1::riseBy(int dx, int dy) {
	_yOff += dy;
	BitmapRec *front = bmp(kBmpFront);
	if (front && front->object)
		front->object->setVisibility(_yOff > 0);
	if (_bkgLow) {
		Common::Point p = _bkgLow->getPosition();
		_bkgLow->setPosition(p.x, p.y + dy);
	}
	if (_bkgTop) {
		Common::Point p = _bkgTop->getPosition();
		_bkgTop->setPosition(p.x, p.y + dy);
	}
	BitmapRec *hills = bmp(kBmpHills);
	if (hills && hills->object) {
		Common::Point p = hills->object->getPosition();
		hills->object->setPosition(p.x, p.y + dy);
	}
	cloudsRise(dy);
}

void Scene1::puffStart(int area, int id, int z, uint32 now, NedXform xf, void *user) {
	Slot &target = slotAt(area, id);
	Slot &puff = slotAt(kNedArea, 0x1a9);
	puff.x = (target.w - puff.w) / 2 + target.x;
	puff.y = target.y + (target.h - puff.h) / 2;
	if (puff.sprite) {
		puff.sprite->setDrawOrder((int16)z);
		puff.sprite->setDestination(_game->display().getPrimary());
	}
	_puffSlot = switchSlotAt(kNedArea, 0x1a9, _puffSlot);
	int duration = placeAt(kNedArea, _puffSlot, _puffSlot, true, xf, user);
	slotAt(kNedArea, _puffSlot).deadline = now + (int16)duration * kMsPerStep;
	stopAllSounds();
	playSoundAt(kNedArea, _puffSlot);
}

void Scene1::puffTick(uint32 now, NedXform xf, void *user) {
	if (_puffSlot == -1)
		return;
	Slot &s = slotAt(kNedArea, _puffSlot);
	if (s.deadline < now) {
		s.step++;
		if (s.step >= s.sequenceLength) {
			_puffSlot = switchSlotAt(kNedArea, -1, _puffSlot);
			return;
		}
		int duration = placeAt(kNedArea, _puffSlot, _puffSlot, true, xf, user);
		s.deadline = now + (int16)duration * kMsPerStep;
		return;
	}
	placeAt(kNedArea, _puffSlot, _puffSlot, true, xf, user);
}

void Scene1::puffScroll(int dx, int dy) {
	if (_puffSlot != -1)
		adjAt(kNedArea, _puffSlot, dx, dy);
}

void Scene1::ambientSchedule(uint32 now) {
	_game->crtSeed(now);
	_ambientTime = now + ((int)_game->crtRandom() % 0x23) * 1000;
}

void Scene1::ambientEnable(bool on) {
	if (!on) {
		_ambientTime = 0;
		_ambientSlot = switchSlot(-1, _ambientSlot);
		return;
	}
	ambientSchedule(_game->now());
}

void Scene1::cloudsCreate() {
	_cloudX = _cloudY = 0;
	cloudsFree();
	_clouds = new RdxGroup();
	for (int i = 0; i < 8; i++) {
		BitmapRec *rec = bmp(kBmpClouds + 4 * i);
		if (!rec || !rec->object)
			continue;
		uint32 t = 0x49e188 + 0x10 * i;
		rec->object->setPosition(_game->exe().readSint32(t) + 0x280, _game->exe().readSint32(t + 4));
		_clouds->addObject(rec->object);
	}
	_clouds->setDestination(_game->display().getPrimary());
	_clouds->setDrawOrder(0x117b);
	_clouds->setVisibility(false);
}

void Scene1::cloudsFree() {
	delete _clouds;
	_clouds = nullptr;
}

void Scene1::cloudsVisible(bool on) {
	if (_clouds)
		_clouds->setVisibility(on);
}

void Scene1::cloudsScroll(int a, int b) {
	_cloudX += a;
	_cloudY += b;
	if (_cloudY < -0xa00)
		_cloudY = 0x280;
	for (int i = 0; i < 8; i++) {
		BitmapRec *rec = bmp(kBmpClouds + 4 * i);
		if (!rec || !rec->object)
			continue;
		Common::Point p = rec->object->getPosition();
		int x = p.x;
		if (x <= -0x780)
			x = _game->exe().readSint32(0x49e188 + 0x10 * i) + _cloudY;
		else
			x += b;
		rec->object->setPosition(x, p.y);
	}
}

void Scene1::cloudsDrift(uint32 now) {
	if (_cloudTime == 0) {
		_cloudTime = now;
		return;
	}
	if (now - _cloudTime <= 0x7d)
		return;
	_cloudY -= 2;
	_cloudX--;
	if (_cloudY < -0xa00)
		_cloudY = 0x280;
	for (int i = 0; i < 8; i++) {
		BitmapRec *rec = bmp(kBmpClouds + 4 * i);
		if (!rec || !rec->object)
			continue;
		Common::Point p = rec->object->getPosition();
		int x = p.x;
		if (x <= -0x780)
			x = _game->exe().readSint32(0x49e188 + 0x10 * i) + _cloudY;
		else
			x -= 2;
		rec->object->setPosition(x, p.y);
	}
	_cloudTime = now;
}

void Scene1::cloudsRise(int dy) {
	for (int i = 0; i < 8; i++) {
		BitmapRec *rec = bmp(kBmpClouds + 4 * i);
		if (rec && rec->object)
			rec->object->adjPosition(0, dy);
	}
}

void Scene1::backdropVisible(bool on) {
	if (_bkgLow)
		_bkgLow->setVisibility(on);
	if (_bkgTop)
		_bkgTop->setVisibility(on);
}

void Scene1::hillsVisible(bool on) {
	BitmapRec *rec = bmp(kBmpHills);
	if (rec && rec->object)
		rec->object->setVisibility(on);
}

void Scene1::layersVisible(bool on) {
	BitmapRec *sky = bmp(kBmpSky), *front = bmp(kBmpFront);
	if (sky && sky->object)
		sky->object->setVisibility(on);
	if (front && front->object)
		front->object->setVisibility(on);
	cloudsVisible(on);
	hillsVisible(on);
	backdropVisible(on);
	ambientEnable(on);
}

void Scene1::exitsEnable() {
	CollisionMap *m = map(kMapExits);
	static const int32 ids[] = { 0x834, 0x835, 0x898, 0x899, 5000, 0x13ec, 0x13eb };
	for (int32 id : ids)
		regionEnable(m, id, true);
}

void Scene1::nedWrap() {
	_game->res().getSlot(kNedArea, 4).offsets[0] = 9;
	_game->res().getSlot(kNedArea, 0x4d).offsets[0] = 9;
	ned()->setWrap2(true, 0, kTiles, kTileW, xformWrap, this);
}

void Scene1::frameForTile() {
	if (_frameShown == _tile)
		return;
	if (ned()->getFacing() == 0)
		_backward.decodeFrame(_tileCount - _tile - 1, _backdrop);
	else
		_forward.decodeFrame(_tile, _backdrop);
	_frameShown = _tile;
}

void Scene1::drawBackdrop() {
	frameForTile();
	draw();
}

void Scene1::diggerFree() {
	stopAllSounds();
	if (_diggerSlot == -1)
		return;
	if (_diggerList == 0) {
		stopSound(0x9c);
		release(0x4a3900, 2);
	}
	if (_diggerList == 1 || _diggerList == 2) {
		stopSound(0x9a);
		release(0x4a3948, 2);
	}
	_diggerReady = _diggerShown = _diggerLoaded = 0;
	_diggerSlot = -1;
	_diggerList = -1;
}

void Scene1::diggerVisible(bool on) {
	if (_diggerSlot != -1)
		visible(_diggerSlot, on);
}

void Scene1::keeperFree() {
	for (int i = 0; i < 7; i++)
		release(0x4a02c0 + i * 0x70, 3);
}


// ---- GraveVideo ----

GraveVideo::GraveVideo() : _decoder(nullptr), _current(-1) {
}

GraveVideo::~GraveVideo() {
	close();
}

bool GraveVideo::open(Game *game, int fileIndex) {
	close();
	Common::String name = game->media().getFileName(fileIndex);
	Common::SeekableReadStream *stream = game->getEngine()->openFile(name);
	if (!stream) {
		warning("Can't find %s", name.c_str());
		return false;
	}
	_decoder = new Video::SmackerDecoder();
	if (!_decoder->loadStream(stream)) {
		warning("Can't open %s", name.c_str());
		delete _decoder;
		_decoder = nullptr;
		return false;
	}
	_current = -1;
	debugC(2, kDebugVideo, "Opened %s, %d frames", name.c_str(), _decoder->getFrameCount());
	return true;
}

void GraveVideo::close() {
	delete _decoder;
	_decoder = nullptr;
	_current = -1;
}

int GraveVideo::getFrameCount() const {
	return _decoder ? _decoder->getFrameCount() : 0;
}

void GraveVideo::decodeFrame(int frame, Graphics::Surface &dst) {
	if (!_decoder || frame < 0 || frame >= (int)_decoder->getFrameCount() || frame == _current)
		return;
	const Graphics::Surface *src = nullptr;
	if (frame < _current || frame > _current + 30) {
		src = _decoder->forceSeekToFrame(frame);
		_current = _decoder->getCurFrame();
	}
	while (_current < frame) {
		src = _decoder->decodeNextFrame();
		_current++;
	}
	if (!src)
		return;
	int w = MIN<int>(src->w, dst.w), h = MIN<int>(src->h, dst.h);
	for (int y = 0; y < h; y++)
		memcpy(dst.getBasePtr(0, y), src->getBasePtr(0, y), w);
}

// ---- Scene methods ----

// m0 0x43c3c0: also reads ned.ini [Grave Videos] (Forward/Backward Background/Foreground,
// GraveA, GraveB, masa, masar, masb, masbr), but nothing uses those names
int Scene1::init() {
	shutdown();
	return 0;
}

// m1 0x43c540
void Scene1::shutdown() {
	_done[0] = _nedPos.x;
	_done[1] = _nedPos.y;
	for (int i = 2; i < 8; i++)
		_done[i] = 0;
}

// m9 0x43eac0
void Scene1::saveState(int32 *out) {
	for (int i = 0; i < 8; i++)
		out[i] = _done[i];
}

// m10 0x43eae0
void Scene1::restoreState(const int32 *in, int size) {
	if (size == 0x20 && in) {
		for (int i = 0; i < 8; i++)
			_done[i] = in[i];
	} else {
		shutdown();
	}
	if (_done[2])
		_sub0->_revisit = 1;
	if (_done[3])
		_sub7->_revisit = 1;
}

void Scene1::listCallback(uint32 cb) {
	switch (cb) {
	case 0x43b8e0:
		_mainLoaded = 1;
		break;
	case 0x43b8f0: {
		// 0x43b8f0: the bird and the sprites shown with Ned go just behind him; some get a flip
		int16 z = ned()->getZ() - 1;
		static const int16 behind[] = { 0x109, 0x12, 0x13, 0x14 };
		for (int16 id : behind) {
			if (slot(id).sprite)
				slot(id).sprite->setDrawOrder(z);
		}
		static const int16 flipped[] = { 5, 0x102, 0x103, 0x104, 0xb, 0x105, 6, 9, 10, 0x106, 7 };
		for (int16 id : flipped) {
			if (!slot(id).sprite)
				continue;
			slot(id).sprite->setDrawOrder(z);
			slot(id).flip = slot(id).sprite->applyEffect(kFxHorizFlip);
		}
		_nedLoaded = 1;
		break;
	}
	case 0x43f7e0:
		_diggerReady = 1;
		break;
	case 0x42fbb0: {
		// The keeper's speech: on the primary surface, hidden, in front of Ned
		Slot &s = slot(_keeperSpeech);
		if (s.sprite) {
			s.sprite->setDestination(_game->display().getPrimary());
			s.sprite->setVisibility(false);
			s.sprite->setDrawOrder(ned()->getZ() + 1);
		}
		break;
	}
	default:
		break;
	}
}

// m2 0x43c580: the backdrop's buffer and its two backgrounds, Ned's list 1, NED99000
int Scene1::load(int fromScene, int fromSub) {
	if (fromScene == 1)
		return 0;
	_backdrop.create(0x280, 0x294, Graphics::PixelFormat::createFormatCLUT8());
	_backdropTop = _backdrop.getSubArea(Common::Rect(0, 0, 0x280, 0xdc));
	_backdropLow = _backdrop.getSubArea(Common::Rect(0, 0xdc, 0x280, 0x294));
	delete _bkgTop;
	delete _bkgLow;
	_bkgTop = new RdxBackground();
	_bkgTop->setBitmap(&_backdropTop, 10);
	_bkgTop->setDrawOrder(5);
	_bkgTop->setDestination(_game->display().getPrimary());
	_bkgTop->setVisibility(false);
	_bkgTop->setPosition(0, 0x104);
	_bkgLow = new RdxBackground();
	_bkgLow->setBitmap(&_backdropLow, 10);
	_bkgLow->setVisibility(false);
	_bkgLow->setDrawOrder(0x32);
	_mainLoaded = 0;
	ned()->listLoad(1);
	if (!_game->getNaf(kNafGrave))
		return 5;
	int r = submit(kMainList, kMainCount, 0);
	if (r)
		return r;
	return submit(kNedList, kNedCount, 0);
}

// m3 0x43eb60
int Scene1::enter(int fromScene, int fromSub) {
	if (fromScene == 1) {
		// Back from a sub-scene: everything is still loaded
		_game->_optionsAllowed = true;
		layersVisible(true);
		memcpy(_gamePalette, _savedPalette, sizeof(_gamePalette));
		ned()->copyPalette(_gamePalette);
		_game->display().applyPalette(_gamePalette, kPalAll);
		switch (fromSub) {
		case 0:
			_done[2] = 1;
			break;
		case 2:
			_done[4] = 1;
			break;
		case 3:
			_done[6] = 1;
			break;
		case 7:
			_done[3] = 1;
			break;
		default:
			break;
		}
		_visionCheck = 1;
	} else {
		_game->_optionsAllowed = true;
		_tile = 0x4c0;
		// Test hook: nightmarened_s1_tile=<n> starts the lap at tile n instead
		if (ConfMan.hasKey("nightmarened_s1_tile"))
			_tile = ConfMan.getInt("nightmarened_s1_tile");
		ned()->listShow(1);
		_toGrave = _bellRung = _toSub2 = _costumeOn = _costumeMode = _costumeOff = 0;
		for (int i = 0; i < kEnemies; i++)
			_enemyLoaded[i] = _enemyLoading[i] = _enemyShown[i] = 0;
		_inputFree = 1;
		_toVideo = 0;
		_toSub7 = 0;
		_grabbed = 0;
		cloudsCreate();
		setupWorld();
		_graveNext = 0;
		_toScene5Bits = 0;
		_targets.clear();
		showBitmap(kMainList, 0x1389, true);
		// (0x459100 is also given the list's cbix 7 entry, which it ignores)
		showBitmap(kMainList + 7 * 0x24, 0x1388, false);
		BitmapRec *front = bmp(kBmpFront), *sky = bmp(kBmpSky);
		if (front && front->object) {
			front->object->setPosition(0, sky ? sky->bitmap.surface.h : 0);
			front->object->setDrawOrder(0x1388);
		}
		ned()->setFacing(1);
		arrival();
		if (ConfMan.hasKey("nightmarened_s1_tile")) {
			// (away from the keeper, who would otherwise keep Ned waiting for his greeting)
			_keeperGreets = 0;
			ned()->enable(true);
		}
		release(kMainList, 1);
		cloudsVisible(true);
		ambientEnable(true);
		if (front && front->object)
			front->object->setVisibility(true);
		_nedPos.x = _tile * kTileW + 0x111;
		for (int y = 0x27f; y >= 0; y--) {
			if (solidAt(kMapFloor, _nedPos.x, y)) {
				_nedPos.y = y;
				break;
			}
		}
	}
	CollisionMap *exits = map(kMapExits);
	regionEnable(exits, 0x924, false);
	regionEnable(exits, 0x929, false);
	regionEnable(exits, 0x7cf, false);
	ned()->setFinishCallback(actionDone, this);
	return 0;
}

// 0x43eeb0: action 0x1f takes costume 2; 0x20 (the jump to an exit) leaves for scene 2
// or 3 (0x523a24) and stops Ned; 0x29 rings slot 0x12e
void Scene1::actionDone(void *user, int16 action) {
	Scene1 *s = (Scene1 *)user;
	switch (action) {
	case 0x1f:
		s->ned()->setCostume(2);
		break;
	case 0x20:
		s->_exitTo = s->_toScene5Bits;
		s->ned()->enable(false);
		break;
	case 0x29:
		s->playSound(0x12e);
		break;
	default:
		break;
	}
}

// m4 0x43c770
int Scene1::leave(int toScene, int toSub) {
	ned()->setCostume(1);
	stopAllSounds();
	if (toScene != 1) {
		_game->media().stopMusic();
		ned()->listFree(1);
	}
	_savedTile = _tile;
	memcpy(_savedPalette, _game->display().getPalette(), sizeof(_savedPalette));
	if (_exitTo == 1)
		_game->media().loadClip(8);
	else if (_exitTo == 2)
		_game->media().loadClip(7);
	if (toScene == 5)
		_game->media().loadClip(9);
	return 0;
}

// m5 0x43c880
int Scene1::unload(int toScene, int toSub) {
	ambientEnable(false);
	stopAllSounds();
	_yOff -= _rise;
	_riseLeft = 0;
	_rise = 0;
	if (toScene == 1)
		return 0;
	const ExeImage &exe = _game->exe();
	for (int k = 0; k < kEnemies; k++) {
		uint32 rec = 0x4a32fc + k * 0x64;
		release(exe.readUint32(rec), exe.readSint32(rec + 4));
		_enemyLoading[k] = _enemyShown[k] = 0;
	}
	_e1->pumpkinHide();
	_e1->reset();
	_e2->reset();
	_ambientSlot = -1;
	_bellSlot = -1;
	_grabSlot = -1;
	keeperFree();
	diggerFree();
	release(kNedList, kNedCount);
	release(kMainList, kMainCount);
	_mainLoaded = 0;
	delete _bkgLow;
	_bkgLow = nullptr;
	delete _bkgTop;
	_bkgTop = nullptr;
	_backdrop.free();
	_forward.close();
	_backward.close();
	_mausoleum.close();
	cloudsFree();
	_game->closeNaf(kNafGrave);
	_sst.clear();
	// 0x47a680: the TV clip for the world Ned goes to, with the TV-room music
	if (toScene == 2)
		_game->media().playClipWithMusic(1, 8);
	else if (toScene == 3)
		_game->media().playClipWithMusic(1, 7);
	else if (toScene == 5)
		_game->media().playClipWithMusic(1, 9);
	return 0;
}

// 0x43e6c0
void Scene1::setupWorld() {
	attachList(kMainList, kMainCount);
	loadSst(0, 999, 1, _sst);
	_world = CollisionWorld();
	addMapBounds(kMapFloor, 0, 0, 0x302b, 0x27f);
	addMap(kMapExits);
	addMap(kMap3);
	addMap(kMap4);
	addMap(kMap5);
	addMap(kMap7);
	_userMap = CollisionMap();
	_world.setUser(&_userMap);
	static const uint32 mapVAs[] = { kMapFloor, kMapExits, kMap3, kMap4, kMap5, kMap7 };
	for (uint32 va : mapVAs) {
		CollisionMap *m = map(va);
		if (m)
			debugC(2, kDebugLoading, "Scene 1: map %x bounds %d,%d,%d,%d mask %d,%d,%d,%d, %d regions", va,
				m->bounds.left, m->bounds.top, m->bounds.right, m->bounds.bottom, m->maskBounds.left,
				m->maskBounds.top, m->maskBounds.right, m->maskBounds.bottom, m->regions.size());
	}
	BitmapRec *hills = bmp(kBmpHills);
	if (hills && hills->object) {
		hills->object->setDrawOrder(0x1162);
		hills->object->setDestination(_game->display().getPrimary());
		hills->object->setVisibility(true);
		hills->object->setPosition(0, 0x206);
	}
	// 0x48c610 + 0x493390: the videos open only when the name ends in .smk
	_forward.open(_game, 0x3a);
	_backward.open(_game, 0x3b);
	_tileCount = _forward.getFrameCount();
	if (_tileCount <= 0)
		_tileCount = kTiles;
	if (_bkgTop)
		_bkgTop->setPosition(0, 0x1e0);
	if (_bkgLow) {
		_bkgLow->setPosition(0, 0x1e0);
		_bkgLow->setDestination(_game->display().getPrimary());
	}
	_forward.decodeFrame(_tile, _backdrop);
	_e1->init();
	_sub2->initHooks();
	for (const Placement &p : _sst)
		applyPlacement(p);
	_game->media().playMusic(0x7c);
}

// 0x43dbd0: Ned falls into the graveyard while the sky scrolls up, then the keeper rises
void Scene1::arrival() {
	Slot &fall = slotAt(kNedArea, 0x14);
	Slot &keeper = slot(0x8e);
	int16 z = ned()->getZ();
	if (keeper.sprite)
		keeper.sprite->setDrawOrder(z + 1);
	if (slot(0x8f).sprite)
		slot(0x8f).sprite->setDrawOrder(z + 1);
	if (palette(kPaletteGame))
		memcpy(_gamePalette, palette(kPaletteGame), sizeof(_gamePalette));
	ned()->copyPalette(_gamePalette);
	_game->display().applyPalette(_gamePalette, kPalGame);
	BitmapRec *ground = bmp(kBmpGround), *sky = bmp(kBmpSky), *hills = bmp(kBmpHills);
	RdxObject *groundObj = ground ? ground->object : nullptr;
	RdxObject *skyObj = sky ? sky->object : nullptr;
	int groundH = ground ? ground->bitmap.surface.h : 0;
	int skyY = groundH;
	if (groundObj)
		groundObj->setPosition(0, 0);
	if (skyObj)
		skyObj->setPosition(0, skyY);
	drawBackdrop();

	// Ned falls from above the screen, a pixel per frame faster every 150 ms
	fall.x = 0xa6;
	fall.y = -fall.h;
	if (fall.sprite) {
		fall.sprite->setPosition(fall.x, fall.y);
		fall.sprite->setVisibility(true);
	}
	fall.deadline = 0;
	int fallY = 0, speed = 6, groundY = 0;
	uint32 speedTime = _game->now() + 0x96;
	int16 dist = (int16)(groundH - 0x1b8);
	if (dist > 0) {
		uint frames = ((uint)(dist + 5)) / 6;
		for (uint n = 0; n < frames && !_game->getEngine()->shouldQuit(); n++) {
			uint32 now = _game->now();
			int frame = 0, duration = 0;
			seqStep(kNedArea, 0x14, fall.step, frame, duration);
			if (fall.sprite)
				fall.sprite->setCurrentImage(frame);
			int ox = 0, oy = 0;
			frameOffset(kNedArea, 0x14, frame, ox, oy);
			fallY += speed;
			if (fall.sprite)
				fall.sprite->setPosition(fall.x - ox, (int16)fallY - oy + fall.y);
			if (fall.deadline < now) {
				if (++fall.step >= fall.sequenceLength)
					fall.step = 0;
				fall.deadline = now + (int16)duration * kMsPerStep;
			}
			if (skyObj) {
				skyObj->setVisibility(true);
				skyObj->setDrawOrder(0x1388);
			}
			if (groundObj)
				groundObj->setPosition(0, groundY);
			skyY -= 6;
			if (skyObj)
				skyObj->setPosition(0, skyY);
			drawBackdrop();
			frameWait(0x1e, now);
			if (now > speedTime) {
				speed++;
				speedTime = now + 0x96;
			}
			groundY -= 6;
		}
	}

	// The world scrolls up from below: the hills, the backdrop and the keeper rise with it
	attachList(kNedList, kNedCount);
	hillsVisible(true);
	if (_bkgLow)
		_bkgLow->setVisibility(true);
	if (_bkgTop)
		_bkgTop->setVisibility(true);
	int lowY = 0x1e0, topY = 0x2bc;
	Common::Point hillsPos = hills && hills->object ? hills->object->getPosition() : Common::Point(0, 0);
	int hillsX = hillsPos.x, hillsY = hillsPos.y;
	if (fall.sprite)
		fall.sprite->setVisibility(false);
	switchSlot(0x8e, -1);
	keeper.y += 0x1b8;
	for (int n = 0; n < 0x4a && !_game->getEngine()->shouldQuit(); n++) {
		uint32 now = _game->now();
		groundY -= 6;
		lowY = MAX(lowY - 6, 0x28);
		topY = MAX(topY - 6, 0x104);
		hillsY -= 6;
		keeper.y -= 6;
		skyY = MAX(skyY - 6, 0);
		if (groundObj)
			groundObj->setPosition(0, groundY);
		if (skyObj)
			skyObj->setPosition(0, skyY);
		if (hills && hills->object)
			hills->object->setPosition(hillsX, hillsY);
		if (_bkgLow)
			_bkgLow->setPosition(0, lowY);
		if (_bkgTop)
			_bkgTop->setPosition(0, topY);
		if (keeper.deadline < now) {
			if (++keeper.step >= keeper.sequenceLength)
				keeper.step = 0;
			if (keeper.step == 2)
				playSound(0x8e);
			int duration = placeAt(kArea, 0x8e, 0x8d, true, xformWrap, this, nullptr, true);
			keeper.deadline = now + (int16)duration * kMsPerStep;
		} else {
			placeAt(kArea, 0x8e, 0x8d, true, xformWrap, this, nullptr, true);
		}
		drawBackdrop();
		frameWait(0x1e, now);
	}
	if (skyObj)
		skyObj->setPosition(0, 0);
	_keeperSlot = 0x8e;
	_keeperGreets = 1;
	ned()->enable(false);
	keeperPick();
}

// ---- The tick ----

// The hills' view moves a pixel every second tile (the tick, 0x43d8a0 and 0x43da40)
void Scene1::hillsStep() {
	if (_hillsTile == (int16)_tile)
		return;
	_hillsTile = (int16)_tile;
	if ((_tile & 1) == 0) {
		BitmapRec *hills = bmp(kBmpHills);
		if (hills && hills->object)
			hills->object->adjView(ned()->getFacing() ? 1 : -1, 0, 0, 0);
	}
	cloudsScroll(-1, -2);
}

// m7 0x43cb90
int Scene1::tick(uint32 input, uint32 now) {
	// 0x488770: the raw keyboard; Q quits to the quilt, T E A M are remembered for sub-scene 5
	NightmareNedEngine *vm = _game->getEngine();
	if (vm->isKeyDown(Common::KEYCODE_q)) {
		switchTo(0);
		return 0;
	}
	if (vm->isKeyDown(Common::KEYCODE_t))
		_typed[0] = 'T';
	if (vm->isKeyDown(Common::KEYCODE_e))
		_typed[1] = 'E';
	if (vm->isKeyDown(Common::KEYCODE_a))
		_typed[2] = 'A';
	if (vm->isKeyDown(Common::KEYCODE_m))
		_typed[3] = 'M';
	// Test hook: nightmarened_s1_goto=<sub> enters that sub-scene from the graveyard once
	// (after the arrival), the way its region would
	if (ConfMan.hasKey("nightmarened_s1_goto") && !_gotoDone && !_keeperGreets) {
		_gotoDone = true;
		_game->_optionsAllowed = false;
		switchTo(1, ConfMan.getInt("nightmarened_s1_goto"));
		return 0;
	}
	if (_exitTo == 1 || _exitTo == 2) {
		int to = _exitTo == 1 ? 2 : 3;
		_game->_optionsAllowed = false;
		if (switchTo(to)) {
			// Not ported yet: Ned stays and gets his controls back
			_game->_optionsAllowed = true;
			ned()->enable(true);
		}
		_exitTo = 0;
		return 0;
	}
	creaturesTick(input, now);
	nedWrap();
	cloudsDrift(now);
	hillsStep();
	enemyStream();
	diggerTick();
	if (_costumeOn) {
		ned()->setCostume(4);
		_costumeOn = 0;
		_costumeMode = 1;
	}
	if (_costumeOff) {
		ned()->setCostume(1);
		_costumeOff = 0;
		_costumeMode = 0;
	}
	if (!_inputFree)
		input &= kInputButton2;
	if (_grabbed && _grabEnd < now) {
		_grabbed = 0;
		_grabEnd = 0;
		ned()->setCostume(1);
		ned()->playAction(0x18, _nedPos, 0, _yOff);
		_keeperTime = now + 4000;
	}
	int facing = 0;
	ned()->tick(_world, input, facing, _nedPos, now, 0, _yOff, &_hooks);
	camera();
	if (_nextDraw < now) {
		frameForTile();
		_game->display().setCollisionOverlay(overlayMaps(), Common::Point(-_tile * kTileW, _yOff));
		draw();
		_nextDraw = now + kMsPerStep;
	} else if (_nextDraw) {
		g_system->delayMillis(MIN<uint32>(_nextDraw - now, kMsPerStep));
	}
	debugC(3, kDebugGraphics, "Scene 1: input %x Ned %d,%d state %d slot %d tile %d rise %d", input,
		_nedPos.x, _nedPos.y, ned()->getState(), ned()->getCurSlot(), _tile, _yOff);

	Slot &kneel = slotAt(kNedArea, 0xa0);
	if (_costumeMode && ned()->getCurSlot() == 0xa0 && kneel.step == kneel.sequenceLength - 1) {
		_game->_optionsAllowed = false;
		switchTo(1, 0);
	} else if (_toSub7) {
		ned()->show(false);
		_toSub7 = 0;
		_grabbed = 0;
		_grabEnd = 0;
		ned()->setCostume(1);
		_game->_optionsAllowed = false;
		switchTo(1, 7);
	} else if (_toVideo == 4) {
		// The rest of MASB.SMK: Ned goes down into the mausoleum, to scene 5
		ned()->show(false);
		if (_bkgTop)
			_bkgTop->setVisibility(false);
		BitmapRec *frame = bmp(0x4bf8ec);
		uint32 ms = 1000 / MAX<int>(_game->media()._transFps, 1);
		for (int f = (int16)_videoFrame; f < _videoFrames && !vm->shouldQuit(); f++) {
			uint32 start = _game->now();
			if (frame)
				_mausoleum.decodeFrame(f, frame->bitmap.surface);
			drawBackdrop();
			frameWait(ms, start);
		}
		_mausoleum.close();
		_toVideo = 0;
		release(0x4a1c98, 1);
		_game->_optionsAllowed = false;
		if (switchTo(5)) {
			_game->_optionsAllowed = true;
			ned()->show(true);
			layersVisible(true);
		}
		return 0;
	} else if (_toGrave) {
		if (ned()->getCurSlot() == 0x1a2) {
			_toGrave = 0;
		} else {
			_game->_optionsAllowed = false;
			switchTo(1, 3 + _graveNext % 3);
			if (++_graveNext > 2)
				_graveNext = 0;
		}
	} else if (_bellRung) {
		bool noMirror = slot(0xa6).flip == nullptr;
		ned()->enable(false);
		if (_bellSlot == 0x120 && !_grabbed) {
			_keeperTime = _game->now() + 9000;
			_bellSlot = switchSlot(0x4c, _bellSlot);
			place(_bellSlot, 0x4c, noMirror, xformPlain, this);
			_bellRung = 0;
		}
	} else if (_toSub2) {
		_toSub2 = 0;
		_game->_optionsAllowed = false;
		switchTo(1, 2);
	}

	if (_e1->pumpkinsDone() && !_done[5]) {
		_done[5] = 1;
		_visionCheck = 1;
	}
	if (!_visionCheck)
		return 0;
	_visionCheck = 0;
	if (_done[6])
		hint(0);
	if (_done[2])
		hint(3);
	if (_done[3])
		hint(2);
	if (_done[4] && !_sub2->_blockVision)
		hint(4);
	if (_done[7])
		hint(5);
	if (_done[5])
		hint(1);
	return 0;
}

// 0x475180
void Scene1::hint(int which) {
	_game->hints().trigger(which, ned()->getFacing() != 0, _nedPos.x, _nedPos.y, xformWrap, this);
}

// 0x43d4d0: everything that moves, in the original's order; the mausoleum video starts at tile 0x3de
void Scene1::creaturesTick(uint32 input, uint32 now) {
	int16 x = (int16)(_tile * kTileW);
	keeperTick(now, x);
	_e2->statueTick(now, x);
	_e1->coffinTick(now);
	diggerUpdate(now, x);
	_e1->mournersTick(now);
	if (_tile == 0x3de && ned()->getFacing() && _toVideo == 0)
		_toVideo = 1;
	if (_tile != 0x3de && _toVideo == 2)
		_toVideo = 3;
	if ((input & 0xff) == kInputUp && _toVideo == 2 && _e1->pumpkinsDone())
		_toVideo = 4;
	if (_toVideo == 1)
		videoMasa();
	else if (_toVideo == 3)
		videoMasb();
	_e1->pumpkinTick(now);
	_e2->ghoulTick(now);
	ambientTick(now);
	_e2->reaperTick(now);
	_e2->zombieTick(now);
	_e2->record10Tick(now);
	_e2->treeTick(now, x);
	_e2->goblinTick(now);
	_e2->swampTick(now);
}

// 0x43d640: each step of a standing slot scrolls a tile; otherwise Ned is kept between
// 200 and 0x111 facing right (0x16f and 0x1b8 facing left)
void Scene1::camera() {
	int16 sx = (int16)_nedPos.x;
	if (0x4cc < _tile && _nedPos.x <= 0x4ff)
		sx = (int16)((kTiles - _tile) * kTileW + sx);
	else if (_tile < 0x48 && 0x500 < _nedPos.x)
		sx = (int16)((-kTiles - _tile) * kTileW + sx);
	else
		sx = (int16)(sx - _tile * kTileW);
	if (ned()->isStanding()) {
		int16 step = slotAt(kNedArea, ned()->getCurSlot()).step;
		if (step != _standSlot) {
			if (ned()->getFacing() == 0)
				stepLeft(0);
			else
				stepRight(0);
			_standSlot = slotAt(kNedArea, ned()->getCurSlot()).step;
			return;
		}
	}
	if (ned()->getState() == 10 && !ned()->isState10Ending())
		return;
	int hi, lo;
	if (ned()->getFacing()) {
		hi = sx - 0x111;
		lo = 200 - sx;
	} else {
		hi = sx - 0x1b8;
		lo = 0x16f - sx;
	}
	if (0x24 < hi)
		stepRight(0x25);
	else if (0 < hi)
		stepRight(0);
	else if (0x24 < lo)
		stepLeft(0x25);
	else if (0 < lo)
		stepLeft(0);
}

// 0x43d8a0: one tile on, or on to the next multiple of four
void Scene1::stepRight(int snap) {
	_nextDraw = 0;
	int dx;
	if (snap < 1) {
		dx = -kTileW;
		_tile++;
	} else {
		int16 t = (int16)(((_tile + 4) / 4) * 4);
		dx = (_tile - t) * kTileW;
		if (_tile == 0 && t == 0)
			t = 4;
		_tile = t;
	}
	if (_tileCount <= _tile)
		_tile = 0;
	stepped(dx);
}

// 0x43da40: one tile back, or back to one before a multiple of four
void Scene1::stepLeft(int snap) {
	_nextDraw = 0;
	int dx, t;
	if (snap < 1) {
		dx = kTileW;
		t = _tile - 1;
	} else {
		t = (int16)(((_tile - 4) / 4) * 4 - 1);
		dx = (_tile - t) * kTileW;
	}
	if (t < 0)
		t = _tileCount - 1;
	_tile = t;
	stepped(dx);
}

// The end of 0x43d8a0 / 0x43da40: Ned and everything placed in screen space move by dx
void Scene1::stepped(int dx) {
	hillsStep();
	ned()->adjPosition(dx, 0);
	keeperScroll(dx, 0);
	_e2->zombieScroll(dx, 0);
	_e2->reaperScroll(dx, 0);
	_e1->mournersScroll(dx, 0);
	_e2->statueScroll(dx, 0);
	_e1->pumpkinsPlace();
	_e2->ghoulScroll(dx, 0);
	_e1->pumpkinScroll(dx, 0);
	_e2->swampScroll(dx, 0);
	_e2->goblinScroll(dx, 0);
	_e2->record10Scroll(dx, 0);
	_e1->coffinScroll(dx, 0);
	diggerScroll(dx, 0);
}

// 0x43b730: each record loads inside its tile range, shows once loaded and is freed outside
void Scene1::enemyStream() {
	const ExeImage &exe = _game->exe();
	int x = _tile * kTileW;
	for (int k = 0; k < kEnemies; k++) {
		uint32 rec = 0x4a32fc + k * 0x64;
		int32 p0 = exe.readSint32(rec + 0x08);
		int32 p3 = exe.readSint32(rec + 0x14);
		uint32 list = exe.readUint32(rec);
		int count = exe.readSint32(rec + 4);
		// A wrapped range (end < start) only tests the start. The show range (+0x0c..+0x10)
		// only matters while the load is pending, and the loader here is synchronous.
		bool inLoad = p3 < p0 ? p0 <= x : (p0 <= x && x <= p3);
		if (inLoad) {
			if (_enemyShown[k])
				continue;
			if (!_enemyLoading[k]) {
				int scene = exe.readSint32(list + (count - 1) * 0x24 + 4);
				_enemyLoading[k] = 1;
				_enemyLoaded[k] = 0;
				submit(list, count, scene);
				// The record's +0x1c callback, which the original puts on the list's last entry
				_enemyLoaded[k] = 1;
			} else {
				attachList(list, count);
				enemyShow(k);
				_enemyShown[k] = 1;
				_enemyLoading[k] = 0;
			}
		} else if (_enemyLoaded[k]) {
			enemyHide(k);
			release(list, count);
			_enemyLoading[k] = _enemyShown[k] = 0;
			_enemyLoaded[k] = 0;
		}
	}
}

void Scene1::enemyShow(int record) {
	switch (record) {
	case 0: _e2->reaperShow(); break;
	case 1: _e2->treeShow(); break;
	case 2: _e1->pumpkinShow(); break;
	case 3: _e2->zombieShow(); break;
	case 4: _e2->ghoulShow(); break;
	case 5: _e1->mournersShow(); break;
	case 6: _e2->goblinShow(); break;
	case 7: _e1->coffinShow(); break;
	case 8: _e2->swampShow(); break;
	case 9: _e2->statueShow(); break;
	case 10: _e2->record10Show(); break;
	default: break;
	}
}

void Scene1::enemyHide(int record) {
	switch (record) {
	case 0: _e2->reaperHide(); break;
	case 1: _e2->treeHide(); break;
	case 2: _e1->pumpkinHide(); break;
	case 3: _e2->zombieHide(); break;
	case 4: _e2->ghoulHide(); break;
	case 5: _e1->mournersHide(); break;
	case 6: _e2->goblinHide(); break;
	case 7: _e1->coffinHide(); break;
	case 8: _e2->swampHide(); break;
	case 9: _e2->statueHide(); break;
	case 10: _e2->record10Hide(); break;
	default: break;
	}
}

// ---- Hooks (DAT_005245f0) ----

// 0x43bf10
int Scene1::hookUse(void *user, int x, int y, int16 *state) {
	Scene1 *s = (Scene1 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(dir, found, rect);
	s->_e1->pumpkinUse(found);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x43c060: the area goes in a; 0x43ba30 gets the address of that argument as its
// direction, so the direction it finds is dropped
void Scene1::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene1 *s = (Scene1 *)user;
	*a = 2;
	int16 dir = 2;
	s->findTarget(dir, *b, rect);
}

// 0x43bc80: the exit jumps (0x924 facing left to scene 2, 0x929 facing right to scene 3),
// the grave (3000), the bell (3001), the costume regions (4000/4001), sub-scene 2 (11000)
int Scene1::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene1 *s = (Scene1 *)user;
	// DAT_004cbee8 / DAT_004cbeec: area 1 slot 0xc6's length and x step (action 0x20's jump)
	Slot &jump = s->slotAt(kNedArea, 0xc6);
	switch (id) {
	case 0x924:
		if (s->ned()->getFacing() != 0)
			return 0;
		if (jump.sequenceLength)
			jump.offsets[0] = (int16)((s->_nedPos.x - 0x2864) / jump.sequenceLength);
		s->ned()->setAction(0x20);
		regionEnable(s->map(kMapExits), 0x924, false);
		s->_toScene5Bits = 1;
		*state = 0xf;
		return 1;
	case 0x929:
		if (s->ned()->getFacing() == 0)
			return 0;
		if (jump.sequenceLength)
			jump.offsets[0] = (int16)-((s->_nedPos.x - 0x1b71) / jump.sequenceLength);
		s->ned()->setAction(0x20);
		regionEnable(s->map(kMapExits), 0x929, false);
		s->_toScene5Bits = 2;
		*state = 0xf;
		return 1;
	case 3000:
		s->_toGrave = 1;
		return 0;
	case 3001:
		s->_bellRung = 1;
		return 0;
	case 4000:
		if (!s->_costumeMode)
			s->_costumeOn = 1;
		return 0;
	case 4001:
		if (s->_costumeMode)
			s->_costumeOff = 1;
		return 0;
	case 11000:
		s->_toSub2 = 1;
		return 0;
	default:
		if (!s->regionCreature(id, flags, x, y, state)) {
			*state = 0xe;
			return 1;
		}
		return 0;
	}
}

// 0x43be00
bool Scene1::regionCreature(int32 id, uint32 flags, int x, int y, int16 *state) {
	return _e2->statueRegion(id, flags, x, y, state) || _e1->coffinRegion(id, flags, x, y, state) ||
		_e2->treeRegion(id, flags, x, y, state) || _e1->pumpkinRegion(id, flags, x, y, state) ||
		_e2->zombieRegion(id, flags, x, y, state) || _e2->ghoulRegion(id, flags, x, y, state) ||
		_e2->reaperRegion(id, flags, x, y, state) || _e1->mournersRegion(id, flags, x, y, state);
}

// 0x43bf80: the first owner of the zapped slot takes the hit
int Scene1::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene1 *s = (Scene1 *)user;
	int r;
	if ((r = s->_e2->statueHit(x, y, a, b, rect)) != 0)
		return r;
	if ((r = s->hitHands(*b)) != 0)
		return r;
	if ((r = s->_e2->treeHit(x, y, a, b, rect)) != 0)
		return r;
	if ((r = s->_e1->pumpkinHit(x, y, a, b, rect)) != 0)
		return r;
	if ((r = s->_e2->reaperHit(x, y, a, b, rect)) != 0)
		return r;
	if (s->keeperHit(*b, s->_game->now()))
		return 1;
	if ((r = s->_e1->mournersHit(x, y, a, b, rect)) != 0)
		return r;
	if ((r = s->_e2->swampHit(x, y, a, b, rect)) != 0)
		return r;
	return s->_e2->record10Hit(x, y, a, b, rect);
}

// 0x43ba30: the target in front of Ned, as a world rect (its current frame's box)
int Scene1::findTarget(int16 &dir, int16 &found, int32 rect[4]) {
	found = -1;
	int unused = 0;
	if (!ned()->findTarget(ned()->getFacing(), ned()->getCurSlot(), _targets, dir, unused, found))
		return 0;
	int32 pos[2] = { 0, 0 };
	int k = 0;
	int r = _e2->statueRect(found, pos);
	if (!r)
		r = rectHands(found, pos);
	if (!r)
		r = _e2->treeRect(found, pos);
	if (!r) {
		r = _e1->pumpkinRect(found, pos);
		boxRect(kArea, found, currentFrame(kArea, found), isFlipped(kArea, found) ? 3 : 0, rect);
		rect[0] += pos[0];
		rect[1] += pos[1];
		rect[3] += pos[1];
		rect[2] += pos[0];
		if (r)
			return 1;
		r = _e2->reaperRect(found, pos);
		if (!r) {
			int32 noFlip = 1;
			r = _e1->mournersRect(found, pos, &noFlip);
			k = noFlip == 0 ? 3 : 0;
			if (!r && !_e2->swampRect(found, pos) && !_e2->record10Rect(found, pos))
				keeperRect(found, pos);
		}
	}
	boxRect(kArea, found, currentFrame(kArea, found), k, rect);
	rect[0] += pos[0];
	rect[1] += pos[1];
	rect[3] += pos[1];
	rect[2] += pos[0];
	return 1;
}

// 0x43af80: zapping the hands (0xa1..0xa3) that hold Ned frees him in a puff
int Scene1::hitHands(int16 found) {
	if (found <= 0xa0 || 0xa4 <= found)
		return 0;
	targetRemoveSlot(_grabSlot);
	_grabSlot = switchSlot(-1, _grabSlot);
	puffStart(kArea, 0xa0, ned()->getZ() - 1, _game->now(), xformPlain, this);
	_grabSlot = 0xe;
	_inputFree = 1;
	return 1;
}

// 0x43b000
int Scene1::rectHands(int16 found, int32 pos[2]) {
	if (found != _grabSlot)
		return 0;
	int p[2];
	attachPos(found, 0xa0, p, _flipped != 0);
	pos[0] = p[0];
	pos[1] = p[1];
	return 1;
}

// ---- The mausoleum (MASB.SMK) ----

// 0x43f0f0: Ned walks into the mausoleum: the first half of MASB.SMK
void Scene1::videoMasa() {
	ambientEnable(false);
	if (_bkgLow)
		_bkgLow->setVisibility(false);
	if (submit(0x4a1c98, 1, 99) || wait(0x4a1c98))
		return;
	_mausoleum.open(_game, 0x7b);
	_videoFrames = _mausoleum.getFrameCount();
	BitmapRec *frame = bmp(0x4bf8ec);
	if (!frame || !frame->object)
		return;
	frame->object->setDestination(_game->display().getPrimary());
	frame->object->setDrawOrder(99);
	frame->object->setVisibility(true);
	uint32 ms = 1000 / MAX<int>(_game->media()._transFps, 1);
	_videoFrame = _videoFrames / 2;
	for (int f = 0; f < (int16)_videoFrame && !_game->getEngine()->shouldQuit(); f++) {
		uint32 start = _game->now();
		_mausoleum.decodeFrame(f, frame->bitmap.surface);
		drawBackdrop();
		frameWait(ms, start);
	}
	_toVideo = 2;
}

// 0x43f300: he walks out again: the second half of MASBR.SMK, then the graveyard comes back
void Scene1::videoMasb() {
	_mausoleum.close();
	_mausoleum.open(_game, 0x7a);
	_videoFrames = _mausoleum.getFrameCount();
	BitmapRec *frame = bmp(0x4bf8ec);
	if (!frame || !frame->object)
		return;
	frame->object->setDestination(_game->display().getPrimary());
	frame->object->setDrawOrder(99);
	frame->object->setVisibility(true);
	uint32 ms = 1000 / MAX<int>(_game->media()._transFps, 1);
	_videoFrame = _videoFrames / 2;
	for (int f = (int16)_videoFrame; f < _videoFrames - 2 && !_game->getEngine()->shouldQuit(); f++) {
		uint32 start = _game->now();
		_mausoleum.decodeFrame(f, frame->bitmap.surface);
		drawBackdrop();
		frameWait(ms, start);
		_videoFrame = f + 1;
	}
	_mausoleum.close();
	_toVideo = 0;
	release(0x4a1c98, 1);
	layersVisible(true);
	_game->display().applyPalette(_palette524d80, kPalStatic);
}

// ---- The grave-digger (sprite 0x9c or 0x9a, by the grave that opens next) ----

// The freeing 0x43f820 and 0x43fa70 share
void Scene1::diggerRelease() {
	stopAllSounds();
	if (_diggerSlot == -1)
		return;
	if (_diggerList == 0) {
		stopSound(0x9c);
		release(0x4a3900, 2);
	}
	if (_diggerList == 1 || _diggerList == 2) {
		stopSound(0x9a);
		release(0x4a3948, 2);
	}
	_diggerReady = _diggerShown = _diggerLoaded = 0;
	_diggerSlot = -1;
	_diggerList = -1;
}

// 0x43f820: loaded between x 400 and 0x5b4 while a grave is left to open
void Scene1::diggerTick() {
	if (2 < _graveNext)
		return;
	int x = _tile * kTileW;
	if (x < 400 || 0x5b4 < x) {
		if (!_diggerShown || _diggerList == -1)
			return;
		diggerRelease();
		return;
	}
	if (!_diggerLoaded) {
		submit(_graveNext == 0 ? 0x4a3900 : 0x4a3948, 2, 0);
		_diggerList = _graveNext;
		_diggerLoaded = 1;
		return;
	}
	if (!_diggerReady)
		return;
	if (!_diggerShown) {
		diggerStart();
		_diggerShown = 1;
		return;
	}
	if (_graveNext == _diggerList || _diggerList == -1)
		return;
	diggerRelease();
}

// 0x43fb40
void Scene1::diggerStart() {
	_diggerNoMirror = 1;
	int16 z = ned()->getZ() + 1;
	if (_graveNext == 0) {
		if (slot(0x9c).sprite)
			slot(0x9c).sprite->setDrawOrder(z);
		_diggerSlot = switchSlotPre(0x9c, -1);
		playSound(_diggerSlot);
		attachList(0x4a3900, 1);
	} else if (_graveNext == 1 || _graveNext == 2) {
		if (slot(0x9a).sprite)
			slot(0x9a).sprite->setDrawOrder(z);
		ensureFlip(kArea, 0x9a);
		flipActive(kArea, 0x9a, false);
		_diggerSlot = switchSlotPre(0x9a, -1);
		playSound(_diggerSlot);
		attachList(0x4a3948, 1);
	}
}

// 0x43fd50: between x 600 and 0x604 he digs every 3..6 s, facing Ned
void Scene1::diggerUpdate(uint32 now, int16 x) {
	if (x <= 599 || 0x605 <= x || _diggerSlot == -1 || now <= _diggerTime)
		return;
	Slot &s = slot(_diggerSlot);
	if (s.step == 0) {
		visible(_diggerSlot, true);
		playSound(_diggerSlot);
		if (_diggerSlot == 0x9a) {
			bool right = slot(0x9a).x <= _nedPos.x;
			flipActive(kArea, 0x9a, right);
			_diggerNoMirror = right ? 0 : 1;
		} else {
			_diggerNoMirror = 1;
		}
	}
	if (s.deadline < now) {
		s.step++;
		if (s.sequenceLength <= s.step) {
			_diggerTime = (int)_game->crtRandom() % 3000 + 3000 + now;
			visible(_diggerSlot, false);
			s.step = 0;
			return;
		}
		int duration = place(_diggerSlot, _diggerSlot, _diggerNoMirror != 0, xformPlain, this);
		s.deadline = now + (int16)duration * kMsPerStep;
		return;
	}
	place(_diggerSlot, _diggerSlot, _diggerNoMirror != 0, xformPlain, this);
}

// 0x43ff80
void Scene1::diggerScroll(int dx, int dy) {
	if (_diggerSlot != -1)
		adj(_diggerSlot, dx, dy);
}

// ---- The keeper (0x8e against 0x8d), who greets Ned and gives one of seven speeches ----

// 0x42fc40
void Scene1::keeperPick() {
	_game->crtSeed(_game->now());
	int16 k = (int16)((int)_game->crtRandom() % 7);
	if (6 < k)
		k = 6;
	_keeperSpeech = k + 0x144;
	submit(0x4a02c0 + k * 0x70, 3, 0);
}

// 0x430250: he is there within 500 px of his post
void Scene1::keeperTick(uint32 now, int16 x) {
	bool noMirror = slot(0x8d).flip == nullptr;
	int16 post = (int16)slot(0x8d).x;
	if (x < post - 500 || post + 500 < x) {
		if (0 < _keeperSlot)
			_keeperSlot = switchSlot(-1, _keeperSlot);
	} else if (_keeperSlot == -1) {
		_keeperSlot = switchSlot(0x8e, -1);
		playSound(_keeperSlot);
		place(_keeperSlot, 0x8d, noMirror, xformPlain, this);
	}
	if (-1 < _keeperSlot)
		keeperStep(now);
}

// 0x42fd20
void Scene1::keeperStep(uint32 now) {
	bool noMirror = slot(0x8d).flip == nullptr;
	if (_keeperSlot == -1)
		return;
	Slot &s = slot(_keeperSlot);
	if (now <= s.deadline) {
		place(_keeperSlot, 0x8d, noMirror, xformPlain, this);
		return;
	}
	s.step++;
	if (_keeperSlot == 0x8f && slot(0x8f).step == 5)
		playSound(0x8f);
	if (_keeperSlot == 0x8e && slot(0x8e).step == 2)
		playSound(0x8e);
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		switch (_keeperSlot) {
		case 0x8e:
			targetRemoveSlot(_keeperSlot);
			_keeperTurns++;
			if (_keeperGreets && 2 < _keeperTurns)
				_keeperSlot = switchSlot(0x8f, _keeperSlot);
			targetAddSlot(_keeperSlot);
			break;
		case 0x8f:
			_keeperSlot = switchSlot(_keeperSpeech, _keeperSlot);
			playSound(_keeperSlot);
			ned()->enable(true);
			ned()->playAction(0x18, _nedPos, 0, _yOff);
			_keeperGreets = 0;
			ambientSchedule(now + 30000);
			break;
		case 0x91: {
			_keeperSlot = switchSlot(0x8e, _keeperSlot);
			int duration = place(_keeperSlot, 0x8d, noMirror, xformPlain, this);
			slot(_keeperSlot).deadline = now + (int16)duration * kMsPerStep;
			if (ned()->getFacing())
				ned()->playAction(0x43, _nedPos, 0, _yOff);
			break;
		}
		case 0x144:
		case 0x145:
		case 0x146:
		case 0x147:
		case 0x148:
		case 0x149:
		case 0x14a:
			stopSound(_keeperSlot);
			release(0x4a02c0 + (_keeperSlot - 0x144) * 0x70, 3);
			_keeperSlot = switchSlot(0x8e, _keeperSlot);
			break;
		default:
			break;
		}
	}
	int duration = place(_keeperSlot, 0x8d, noMirror, xformPlain, this);
	slot(_keeperSlot).deadline = now + (int16)duration * kMsPerStep;
}

// 0x4301f0
void Scene1::keeperScroll(int dx, int dy) {
	if (_keeperSlot != -1)
		adj(_keeperSlot, dx, dy);
}

// 0x430220
int Scene1::keeperRect(int16 found, int32 pos[2]) {
	if (found != 0x8e)
		return 0;
	int p[2];
	attachPos(0x8e, 0x8d, p, true);
	pos[0] = p[0];
	pos[1] = p[1];
	return 1;
}

// 0x430340: zapped, he ducks (0x91)
bool Scene1::keeperHit(int16 found, uint32 now) {
	if (found != 0x8e)
		return false;
	bool noMirror = slot(0x8d).flip == nullptr;
	_keeperSlot = switchSlot(0x91, _keeperSlot);
	playSound(_keeperSlot);
	int duration = place(_keeperSlot, 0x8d, noMirror, xformPlain, this);
	slot(_keeperSlot).deadline = now + (int16)duration * kMsPerStep;
	return true;
}

// ---- The bird (0x12 swoops down, 0x13 lands, 0x14 flies off; placed against 0x109) ----

// 0x477620
void Scene1::ambientTick(uint32 now) {
	if (_keeperGreets)
		return;
	if (_game->hints().isActive() && _ambientSlot < 0)
		return;
	if ((_grabbed && _ambientSlot < 0) || (enemyLoaded(2) && _ambientSlot < 0)) {
		ambientSchedule(now);
		return;
	}
	if (_ambientTime < now && _ambientSlot < 0) {
		if (_tile < 0x2d) {
			ambientSchedule(now);
			return;
		}
		Slot &ref = slot(0x109);
		ref.x = _nedPos.x - 0x24;
		ref.y = -ref.h;
		_ambientSlot = switchSlot(0x12, _ambientSlot);
		playSound(_ambientSlot);
		place(_ambientSlot, 0x109, ref.flip == nullptr, xformPlain, this);
	}
	ambientPlace(now);
}

// 0x4773a0
void Scene1::ambientPlace(uint32 now) {
	Slot &ref = slot(0x109);
	bool noMirror = ref.flip == nullptr;
	if (_ambientSlot < 1)
		return;
	if (now <= slot(_ambientSlot).deadline) {
		place(_ambientSlot, 0x109, noMirror, xformPlain, this);
		return;
	}
	if (_ambientSlot == 0x12) {
		ref.y += 0x12;
		if (_nedPos.y - 0x92 < ref.y + ref.h) {
			_ambientSlot = switchSlot(0x13, _ambientSlot);
			playSound(_ambientSlot);
		}
	}
	if (_ambientSlot == 0x14) {
		ref.y -= 0xc;
		if (ref.y < -ref.h) {
			_ambientSlot = switchSlot(-1, _ambientSlot);
			ambientSchedule(now);
		}
	}
	if (_ambientSlot == 0x13 && slot(0x13).step == 1) {
		int16 d = (int16)((int16)_nedPos.x - (int16)ref.x);
		bool close = ned()->getFacing() == 0 ? (2 <= d && d <= 0x31) : ABS(d) <= 0x3b;
		if (close)
			ned()->playAction(0x42, _nedPos, 0, _yOff);
	}
	// When the bird has just gone the original steps slot -1, in front of the slot table
	if (_ambientSlot < 0)
		return;
	Slot &s = slot(_ambientSlot);
	s.step++;
	if (s.sequenceLength <= s.step) {
		s.step = 0;
		if (_ambientSlot == 0x13)
			_ambientSlot = switchSlot(0x14, 0x13);
	}
	if (_ambientSlot < 1)
		return;
	int duration = place(_ambientSlot, 0x109, noMirror, xformPlain, this);
	slot(_ambientSlot).deadline = now + (int16)duration * kMsPerStep;
}

} // End of namespace NightmareNed
