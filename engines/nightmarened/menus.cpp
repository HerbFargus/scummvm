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
#include "nightmarened/game.h"
#include "nightmarened/hints.h"
#include "nightmarened/media.h"
#include "nightmarened/menus.h"
#include "nightmarened/ned.h"
#include "nightmarened/nightmarened.h"
#include "nightmarened/rdx.h"

namespace NightmareNed {

// ---- MenuVideo ----

MenuVideo::MenuVideo() : _decoder(nullptr), _current(-1) {
}

MenuVideo::~MenuVideo() {
	close();
}

bool MenuVideo::open(Game *game, int fileIndex) {
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

void MenuVideo::close() {
	delete _decoder;
	_decoder = nullptr;
	_current = -1;
}

int MenuVideo::getFrameCount() const {
	return _decoder ? _decoder->getFrameCount() : 0;
}

// Smacker only decodes forwards: going back starts again from the first frame
void MenuVideo::decodeFrame(int frame, Graphics::Surface &dst) {
	if (!_decoder || frame < 0 || frame >= (int)_decoder->getFrameCount())
		return;
	if (frame <= _current) {
		_decoder->rewind();
		_current = -1;
	}
	const Graphics::Surface *src = nullptr;
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

// ---- MenuScene ----

MenuScene::MenuScene(Game *game) : Scene(game, 0), _position(0), _selected(-1), _phase(0), _soundDone(false),
	_soundSlot(-1), _cheat(0), _variant(0), _nextDraw(0), _nextClouds(0), _nedShown(-1), _shown(-1),
	_listLoaded(false), _frameObject(nullptr) {
	memset(_palette, 0, sizeof(_palette));
	for (int i = 0; i < kPatches; i++)
		_base[i] = _anim[i] = _sound[i] = -1;
}

MenuScene::~MenuScene() {
	if (_listLoaded)
		_game->freeList(_list);
	delete _frameObject;
	_frame.free();
}

int32 *MenuScene::progress() {
	return _game->hints()._progress;
}

Slot &MenuScene::slot(int area, int id) {
	return _game->res().getSlot(area, id);
}

void MenuScene::showSprite(int entry, int z, bool visible) {
	Slot &s = slot(_list[entry].area, _list[entry].id);
	if (!s.sprite)
		return;
	s.sprite->setDrawOrder(z);
	s.sprite->setDestination(_game->display().getPrimary());
	s.sprite->setVisibility(visible);
}

int MenuScene::place(int area, int id, int ref) {
	int duration = 0;
	_game->display().slotPlace(area, id, ref, nullptr, &duration, true, Common::Point(0, 0));
	return duration;
}

int16 MenuScene::slotSwitch(int area, int newSlot, int oldSlot) {
	return _game->display().slotSwitch(area, newSlot, oldSlot);
}

void MenuScene::draw() {
	_game->display().draw();
}

bool MenuScene::quitting() const {
	return _game->getEngine()->shouldQuit();
}

void MenuScene::waitFrame(uint32 start) {
	NightmareNedEngine *vm = _game->getEngine();
	for (;;) {
		vm->pumpEvents();
		uint32 now = _game->now();
		if (now >= start + kFrameMs || vm->shouldQuit())
			return;
		g_system->delayMillis(MIN<uint32>(start + kFrameMs - now, 10));
	}
}

void MenuScene::decodeQuilt(MenuVideo &video, int frame) {
	video.decodeFrame(frame, _frame);
}

// m0 0x465540: the 0x18-byte progress block (kept by Hints)
int MenuScene::init() {
	memset(progress(), 0, 6 * sizeof(int32));
	return 0;
}

// m1 0x465580
void MenuScene::shutdown() {
	_position = 0;
	_selected = -1;
	memset(progress(), 0, 6 * sizeof(int32));
}

// m6 0x4655b0 frees the progress block; it lives as long as the scene here
void MenuScene::suspend() {
}

// m9 0x4655e0
void MenuScene::saveState(int32 *out) {
	memcpy(out, progress(), 6 * sizeof(int32));
}

// m10 0x465600
void MenuScene::restoreState(const int32 *in, int size) {
	if (size == 0x18 && in)
		memcpy(progress(), in, 6 * sizeof(int32));
	else
		memset(progress(), 0, 6 * sizeof(int32));
}

// m2 0x465650
int MenuScene::load(int fromScene, int fromSub) {
	_cheat = ConfMan.hasKey("hatmandirtnap") ? ConfMan.getInt("hatmandirtnap") : 0;
	if (!_game->getNaf(kNafMenus))
		return 5;
	return buildList();
}

// 0x466c40
int MenuScene::buildList() {
	static const uint32 kHead[] = {
		kListHead, kListClouds, kListClouds + 0x24, kListClouds + 0x48,
		kListSwoosh, kListSwoosh + 0x24, kListSwoosh + 0x48, kListSwoosh + 0x6c
	};
	if (_listLoaded)
		_game->freeList(_list);
	_list.clear();
	for (uint32 va : kHead)
		_list.push_back(_game->readList(va, 1)[0]);
	_variant = (_variant + 1) % 2;
	for (int i = 0; i < kPatches; i++) {
		int level = progress()[i];
		uint32 patch = kListPatches + i * 0x240 + level * 5 * 0x24;
		_list.push_back(_game->readList(patch, 1)[0]);
		_base[i] = _list.size() - 1;
		if (level == 3) {
			_sound[i] = -1;
			_anim[i] = -1;
		} else {
			_list.push_back(_game->readList(patch + (1 + _variant) * 0x24, 1)[0]);
			_anim[i] = _list.size() - 1;
			_list.push_back(_game->readList(patch + (3 + _variant) * 0x24, 1)[0]);
			_sound[i] = _list.size() - 1;
		}
	}
	_game->loadList(kNafMenus, _list, 0);
	_listLoaded = true;
	if (_game->ned())
		_game->ned()->listLoad(kNedList);
	return 0;
}

// m3 0x4656b0: from -1 (start-up, new game, a loaded game) the quilt shows at once;
// from a dream the next TV clip plays first
int MenuScene::enter(int fromScene, int fromSub) {
	Display &display = _game->display();
	RdxSurface *primary = display.getPrimary();
	_position = 0;
	_selected = -1;
	if (fromScene != -1)
		_game->media().stopMusic();
	primary->setClearColor(0);
	draw();
	primary->setClearColor(-1);
	if (fromScene != -1) {
		int clip = progress()[5] + kClipFirst;
		if (_game->media().loadClip(clip))
			return 5;
		_game->media().playClipFull(10, clip);
		primary->setClearColor(0);
		draw();
		primary->setClearColor(-1);
	}

	_frame.free();
	_frame.create(640, 480, Graphics::PixelFormat::createFormatCLUT8());
	delete _frameObject;
	_frameObject = new RdxBackground();
	_frameObject->setBitmap(&_frame, -1);
	_frameObject->setDestination(primary);
	_frameObject->setDrawOrder(10);
	_frameObject->setVisibility(true);
	if (fromScene != -1 && progress()[5] < 8)
		progress()[5]++;

	if (!_listLoaded)
		return 5;
	memcpy(_palette, _list[kEntryPalette].palette, sizeof(_palette));
	if (_game->ned())
		_game->ned()->copyPalette(_palette);
	display.applyPalette(_palette, kPalAll);
	if (!_quilt.open(_game, kQuilt) || !_quiltBack.open(_game, kQuiltBack))
		return 1;
	decodeQuilt(_quilt, 0);

	static const int kCloudX[3] = { 10, 0x244, 0x140 };
	for (int i = 0; i < 3; i++) {
		int id = entryId(kEntryCloud1 + i);
		showSprite(kEntryCloud1 + i, 7 + i, true);
		slot(kArea, id).x = kCloudX[i];
		slot(kArea, id).y = -1;
		slotSwitch(kArea, id, -1);
		place(kArea, id, id);
	}

	Ned *ned = _game->ned();
	if (ned)
		ned->listShow(kNedList);
	_nedShown = slotSwitch(1, kNedSlot, -1);
	Slot &bed = slot(1, kNedSlot);
	bed.x = 0x140 - bed.w / 2;
	bed.y = 0x1df - bed.h / 2;
	place(1, kNedSlot, kNedSlot);
	_game->media().playMusic(kMusic);
	if (ned) {
		ned->show(false);
		ned->enable(false);
	}
	_game->_optionsAllowed = true;
	return 0;
}

// 0x467240
void MenuScene::updateClouds(uint32 now) {
	if (now <= _nextClouds)
		return;
	static const int kWrap[3] = { 0x282, 0x284, 0x283 };
	static const int kSpeed[3] = { 2, 4, 3 };
	for (int i = 0; i < 3; i++) {
		Slot &s = slot(kArea, entryId(kEntryCloud1 + i));
		if (s.x < -s.w)
			s.x = kWrap[i];
	}
	for (int i = 0; i < 3; i++)
		slot(kArea, entryId(kEntryCloud1 + i)).x -= kSpeed[i];
	for (int i = 0; i < 3; i++)
		place(kArea, entryId(kEntryCloud1 + i), entryId(kEntryCloud1 + i));
	_nextClouds = now + kFrameMs;
}

// 0x467390: 20, 40 and 30 pixels a frame until all three are off the left edge
void MenuScene::cloudsLeave() {
	static const int kSpeed[3] = { 0x14, 0x28, 0x1e };
	uint gone = 0;
	for (int i = 0; i < 3; i++) {
		Slot &s = slot(kArea, entryId(kEntryCloud1 + i));
		if (s.x < -s.w)
			gone |= 1 << i;
	}
	while (gone != 7 && !quitting()) {
		uint32 start = _game->now();
		for (int i = 0; i < 3; i++) {
			Slot &s = slot(kArea, entryId(kEntryCloud1 + i));
			if (s.x < -s.w)
				gone |= 1 << i;
			else
				s.x -= kSpeed[i];
		}
		for (int i = 0; i < 3; i++)
			place(kArea, entryId(kEntryCloud1 + i), entryId(kEntryCloud1 + i));
		draw();
		if (gone == 7)
			return;
		waitFrame(start);
	}
}

// The swoosh sprite plays once over the patch (the original busy-waits on its deadlines)
void MenuScene::playSwoosh(int base) {
	int swoosh = entryId(kEntrySwoosh);
	Slot &s = slot(kArea, swoosh);
	int duration = place(kArea, swoosh, base);
	s.deadline = _game->now() + duration * kMsPerStep;
	_game->playSlotSound(slot(kArea, swoosh), nullptr);
	for (;;) {
		uint32 now;
		do {
			draw();
			now = _game->now();
			updateClouds(now);
			_game->getEngine()->pumpEvents();
			if (quitting())
				return;
			if (now <= s.deadline)
				g_system->delayMillis(5);
		} while (now <= s.deadline);
		if (++s.step >= s.sequenceLength)
			break;
		duration = place(kArea, swoosh, base);
		s.deadline = now + duration * kMsPerStep;
	}
	slotSwitch(kArea, -1, swoosh);
}

// 0x4663f0: X0000.SMK while Ned turns over twice, then the patch's still and the swoosh
int MenuScene::zoomIn() {
	_selected = _position;
	MenuVideo video;
	if (!video.open(_game, _game->exe().readSint32(kZoomIn + _position * 4)))
		return 1;
	Slot &bed = slot(1, kNedSlot);
	int frame = 0, turns = 0;
	int count = video.getFrameCount();
	do {
		uint32 start = _game->now();
		video.decodeFrame(frame++, _frame);
		if (turns < 2) {
			if (++bed.step >= bed.sequenceLength) {
				bed.step = 0;
				turns++;
			}
			place(1, kNedSlot, kNedSlot);
		}
		if (frame == 0x16)
			_game->playSlotSound(slot(kArea, entryId(kEntryZoomInSound)), nullptr);
		updateClouds(_game->now());
		draw();
		waitFrame(start);
	} while (frame != count && !quitting());
	video.close();

	_phase = 1;
	showSprite(kEntrySwoosh, 6, true);
	int baseEntry = _base[_selected];
	showSprite(baseEntry, 6, false);
	int base = entryId(baseEntry);
	Slot &b = slot(kArea, base);
	b.step = 0;
	b.x = 0x140 - b.w / 2;
	b.y = 100;
	int swoosh = entryId(kEntrySwoosh);
	slotSwitch(kArea, swoosh, -1);
	slot(kArea, swoosh).step = 0;
	playSwoosh(base);
	draw();
	return 0;
}

// 0x4667c0: the swoosh, then X0000R.SMK with Ned turning back over its last frames
int MenuScene::zoomOut() {
	int base = entryId(_base[_selected]);
	slotSwitch(kArea, -1, base);
	int swoosh = entryId(kEntrySwoosh);
	slotSwitch(kArea, swoosh, -1);
	slot(kArea, swoosh).step = 0;
	playSwoosh(base);
	draw();

	MenuVideo video;
	if (!video.open(_game, _game->exe().readSint32(kZoomOut + _selected * 4)))
		return 1;
	Slot &bed = slot(1, kNedSlot);
	int count = video.getFrameCount();
	int turnFrom = count - bed.sequenceLength * 2;
	bed.step = bed.sequenceLength - 1;
	int frame = 0, turns = 0;
	do {
		uint32 start = _game->now();
		video.decodeFrame(frame++, _frame);
		if (turns < 2 && frame >= turnFrom) {
			place(1, kNedSlot, kNedSlot);
			if (--bed.step < 0) {
				bed.step = bed.sequenceLength - 1;
				turns++;
			}
		}
		if (frame == 1)
			_game->playSlotSound(slot(kArea, entryId(kEntryZoomOutSound)), nullptr);
		updateClouds(_game->now());
		draw();
		waitFrame(start);
	} while (frame != count && !quitting());
	bed.step = 0;
	place(1, kNedSlot, kNedSlot);
	video.close();
	_selected = -1;
	return 0;
}

// 0x466e40: phase 1 starts the picture (or the still at progress 3) with its sound,
// phase 2 loops it until the sound ends (0x467230), phase 3 loops the still
void MenuScene::patchTick(uint32 now) {
	int base = entryId(_base[_selected]);
	if (_phase == 1) {
		int animEntry = _anim[_selected];
		if (animEntry == -1) {
			_shown = slotSwitch(kArea, base, -1);
			slot(kArea, base).step = 0;
			int d = place(kArea, base, base);
			slot(kArea, base).deadline = now + d * kMsPerStep;
			_phase = 3;
			return;
		}
		showSprite(animEntry, 6, true);
		int anim = entryId(animEntry);
		slot(kArea, anim).step = 0;
		slot(kArea, base).step = 0;
		_shown = slotSwitch(kArea, anim, -1);
		int d = place(kArea, anim, base);
		slot(kArea, anim).deadline = now + d * kMsPerStep;
		_phase = 2;
		_soundDone = false;
		_soundSlot = entryId(_sound[_selected]);
		_game->playSlotSound(slot(kArea, _soundSlot), nullptr);
		return;
	}
	if (_phase == 2) {
		int anim = entryId(_anim[_selected]);
		if (!_soundDone && _soundSlot >= 0 && !_game->isVoicePlaying(slot(kArea, _soundSlot).voice))
			_soundDone = true;
		if (_soundDone) {
			_phase = 3;
			_soundSlot = -1;
			_shown = slotSwitch(kArea, base, anim);
			int d = place(kArea, base, base);
			slot(kArea, base).deadline = now + d * kMsPerStep;
			return;
		}
		Slot &a = slot(kArea, anim);
		if (a.deadline >= now)
			return;
		if (++a.step >= a.sequenceLength)
			a.step = 0;
		int d = place(kArea, anim, base);
		a.deadline = now + d * kMsPerStep;
		return;
	}
	if (_phase == 3) {
		Slot &b = slot(kArea, base);
		if (b.deadline >= now)
			return;
		if (++b.step >= b.sequenceLength)
			b.step = 0;
		int d = place(kArea, base, base);
		b.deadline = now + d * kMsPerStep;
	}
}

// m7 0x465f70
int MenuScene::tick(uint32 input, uint32 now) {
	uint32 lo = input & 0xff, hi = input & 0xff00;
	if ((lo & kInputRight) && _selected == -1) {
		int frame = _game->exe().readSint32(kPatchStops + _position * 4);
		_position = (_position + 1) % kPatches;
		int stop = _game->exe().readSint32(kPatchStops + _position * 4);
		do {
			uint32 start = _game->now();
			frame = (frame + 1) % _quilt.getFrameCount();
			decodeQuilt(_quilt, frame);
			updateClouds(_game->now());
			draw();
			waitFrame(start);
		} while (frame != stop && !quitting());
		return 0;
	}
	if ((lo & kInputLeft) && _selected == -1) {
		int frame = _game->exe().readSint32(kPatchStopsBack + _position * 4);
		_position = _position == 0 ? kPatches - 1 : _position - 1;
		int stop = _game->exe().readSint32(kPatchStopsBack + _position * 4);
		do {
			uint32 start = _game->now();
			frame = (frame + 1) % _quiltBack.getFrameCount();
			decodeQuilt(_quiltBack, frame);
			updateClouds(_game->now());
			draw();
			waitFrame(start);
		} while (frame != stop && !quitting());
		return 0;
	}
	if ((lo & kInputUp) && _selected == -1) {
		zoomIn();
		return 0;
	}
	if ((lo & kInputDown) && _selected != -1 && _phase == 3) {
		zoomOut();
		return 0;
	}
	if (hi == kInputButton1 && _selected != -1 && _phase == 3) {
		if (_selected >= 0 && _selected < kPatches)
			_game->switchScene(_selected + 1, -1);
		return 0;
	}

	if (_selected != -1)
		patchTick(now);
	if (_cheat == 0x205) {
		// The scene keys (DirectInput scan codes Z S C D M A K U B P)
		static const struct {
			Common::KeyCode key;
			int scene, sub;
		} kCheats[] = {
			{ Common::KEYCODE_z, 1, -1 }, { Common::KEYCODE_s, 3, -1 }, { Common::KEYCODE_c, 6, -1 },
			{ Common::KEYCODE_d, 7, -1 }, { Common::KEYCODE_m, 2, -1 }, { Common::KEYCODE_a, 4, -1 },
			{ Common::KEYCODE_k, 3, 0 }, { Common::KEYCODE_u, 1, 0 }, { Common::KEYCODE_b, 5, -1 },
			{ Common::KEYCODE_p, 3, 1 }
		};
		for (const auto &c : kCheats) {
			if (_game->getEngine()->isKeyDown(c.key)) {
				_game->switchScene(c.scene, c.sub);
				_selected = -1;
				return 0;
			}
		}
	}
	if (now <= _nextDraw) {
		g_system->delayMillis(MIN<uint32>(_nextDraw - now, 5));
		return 0;
	}
	updateClouds(now);
	draw();
	_nextDraw = now + kFrameMs;
	return 0;
}

// m4 0x465be0: the swoosh over the chosen patch, the clouds fly off, everything is freed
int MenuScene::leave(int toScene, int toSub) {
	_game->_optionsAllowed = false;
	slotSwitch(1, -1, kNedSlot);
	if (_selected != -1 && _base[_selected] != -1 && _listLoaded) {
		int base = entryId(_base[_selected]);
		slotSwitch(kArea, -1, base);
		int swoosh = entryId(kEntrySwoosh);
		slotSwitch(kArea, swoosh, -1);
		slot(kArea, swoosh).step = 0;
		playSwoosh(base);
		draw();
		cloudsLeave();
		for (int i = 0; i < 3; i++)
			slotSwitch(kArea, -1, entryId(kEntryCloud1 + i));
		draw();
	}
	_quilt.close();
	_quiltBack.close();
	if (_listLoaded) {
		slotSwitch(kArea, -1, entryId(kEntrySwoosh));
		for (int i = 0; i < 3; i++)
			slotSwitch(kArea, -1, entryId(kEntryCloud1 + i));
		_game->freeList(_list);
		_list.clear();
		_listLoaded = false;
		if (_game->ned())
			_game->ned()->listFree(kNedList);
	}
	_game->closeNaf(kNafMenus);
	return 0;
}

// 0x466b50: XT0071.SMK, the way into the chosen dream
int MenuScene::diveIn() {
	MenuVideo video;
	if (!video.open(_game, _game->exe().readSint32(kDiveIn + _selected * 4)))
		return 1;
	int count = video.getFrameCount();
	for (int frame = 0; frame != count && !quitting();) {
		uint32 start = _game->now();
		video.decodeFrame(frame++, _frame);
		draw();
		waitFrame(start);
	}
	video.close();
	_selected = -1;
	return 0;
}

// m5 0x465ef0
int MenuScene::unload(int toScene, int toSub) {
	if (_selected != -1)
		diveIn();
	_game->media().stopMusic();
	delete _frameObject;
	_frameObject = nullptr;
	_frame.free();
	RdxSurface *primary = _game->display().getPrimary();
	primary->setClearColor(0);
	draw();
	primary->setClearColor(-1);
	return 0;
}

} // End of namespace NightmareNed
