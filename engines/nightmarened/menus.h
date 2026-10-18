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

#ifndef NIGHTMARENED_MENUS_H
#define NIGHTMARENED_MENUS_H

#include "common/array.h"
#include "graphics/surface.h"

#include "nightmarened/game.h"
#include "nightmarened/resources.h"

namespace Video {
class SmackerDecoder;
}

namespace NightmareNed {

class RdxBackground;

/**
 * A Smacker file decoded frame by frame into a 640x480 buffer, the way the game
 * drives it: 0x488130 SmackOpen, 0x4881c0 SmackGoto + SmackToBuffer + SmackDoFrame,
 * 0x488200 SmackClose.
 */
class MenuVideo {
public:
	MenuVideo();
	~MenuVideo();

	/** 0x488130: by file-table index */
	bool open(Game *game, int fileIndex);
	/** 0x488200 */
	void close();
	bool isOpen() const { return _decoder != nullptr; }
	int getFrameCount() const;
	/** 0x4881c0: frame is 0-based (the game passes frame + 1 to SmackGoto) */
	void decodeFrame(int frame, Graphics::Surface &dst);

private:
	Video::SmackerDecoder *_decoder;
	int _current;
};
/**
 * Scene 0, the quilt (registered by 0x4654b0): Ned's bed quilt has five patches, one per
 * dream (the graveyard, scenes 2, 3, the attic, scene 5). Left and right roll the quilt
 * to the next patch (QUILT.SMK / QUILTR.SMK), up zooms into it (X0000.SMK) and plays the
 * patch's picture for that dream's progress, Shift enters the dream, down zooms out
 * (X0000R.SMK). Coming back from a dream plays the next TV clip: the clock loses an hour
 * (clips 0x21..0x29, 8733..8741TV01).
 */
class MenuScene : public Scene {
public:
	explicit MenuScene(Game *game);
	~MenuScene() override;

	int init() override;                             // m0 0x465540
	void shutdown() override;                        // m1 0x465580
	int load(int fromScene, int fromSub) override;   // m2 0x465650
	int enter(int fromScene, int fromSub) override;  // m3 0x4656b0
	int leave(int toScene, int toSub) override;      // m4 0x465be0
	int unload(int toScene, int toSub) override;     // m5 0x465ef0
	void suspend() override;                         // m6 0x4655b0
	int tick(uint32 input, uint32 now) override;     // m7 0x465f70
	int getSaveSize() const override { return 0x18; } // m8 0x4655d0
	void saveState(int32 *out) override;             // m9 0x4655e0
	void restoreState(const int32 *in, int size) override; // m10 0x465600

private:
	enum {
		kArea = 9,
		kNafMenus = 4,             // ned92000
		kNedList = 15,             // Ned in bed (area 1, slot 0x80)
		kNedSlot = 0x80,
		kPatches = 5,
		kFrameMs = 0x53,           // 83 ms per menu frame
		kListHead = 0x4b1348,      // the palette bitmap
		kListClouds = 0x4b1400,    // 101..103 (three entries)
		kListSwoosh = 0x4b1370,    // 100 as sprite, then 100, 162, 163 as sounds (four entries)
		kListPatches = 0x4b1470,   // 0x240 per patch: per progress 0..2 base, 2 sprites, 2 sounds; 3: base only
		kPatchStops = 0x4b1318,    // QUILT.SMK frame per patch
		kPatchStopsBack = 0x4b1330, // QUILTR.SMK frame per patch
		kZoomIn = 0x495428,        // file-table index per patch: X0000.SMK
		kZoomOut = 0x495440,       // X0000R.SMK
		kDiveIn = 0x495458,        // XT0071.SMK
		kQuilt = 0x50,
		kQuiltBack = 0x52,
		kMusic = 0x82,             // 92002208.wav
		kClipFirst = 0x21          // 8733TV01
	};

	enum {
		kEntryPalette = 0,
		kEntryCloud1 = 1,
		kEntryCloud2 = 2,
		kEntryCloud3 = 3,
		kEntrySwoosh = 4,
		kEntrySwooshSound = 5,
		kEntryZoomInSound = 6,
		kEntryZoomOutSound = 7,
		kEntryPatches = 8
	};

	/** DAT_0051eb28 (Hints::_progress): level 0..3 per patch, then TV clips played */
	int32 *progress();
	Slot &slot(int area, int id);
	int entryId(int entry) const { return _list[entry].id; }
	/** 0x459090 res_show_sprite */
	void showSprite(int entry, int z, bool visible);
	/** 0x430410 slot_place_dur (not mirrored, no scroll): returns the step's duration */
	int place(int area, int id, int ref);
	int16 slotSwitch(int area, int newSlot, int oldSlot);
	void draw();
	/** Sleep until start + 83 ms, keeping the window alive */
	void waitFrame(uint32 start);
	void decodeQuilt(MenuVideo &video, int frame);
	bool quitting() const;

	/** 0x466c40: the request list for the current progress of every patch */
	int buildList();
	/** 0x467240: the clouds drift left every 83 ms */
	void updateClouds(uint32 now);
	/** 0x467390: the clouds fly off to the left */
	void cloudsLeave();
	/** 0x4663f0 */
	int zoomIn();
	/** 0x4667c0 */
	int zoomOut();
	/** 0x466e40: the zoomed-in patch's picture */
	void patchTick(uint32 now);
	/** 0x466b50 */
	int diveIn();
	/** The swoosh (slot 100) over the selected patch, as in 0x4663f0 / 0x4667c0 / 0x465be0 */
	void playSwoosh(int base);

	int _position;             // DAT_0051e6dc: the patch the quilt shows
	int _selected;             // DAT_0051eb24: the zoomed-in patch, -1 none
	int _phase;                // DAT_0051e200: 1 zoomed in, 2 picture playing, 3 waiting
	bool _soundDone;           // DAT_0051eb48 (0x467230)
	int _soundSlot;            // the slot whose end sets _soundDone
	int _cheat;                // DAT_0051eb44: ned.ini [Ned Prefs] hatmandirtnap
	int _variant;              // DAT_0051e6e0: alternates the picture on every load
	uint32 _nextDraw;          // DAT_0051eb4c
	uint32 _nextClouds;        // DAT_0051e6e4
	int16 _nedShown;           // DAT_0051e6d8
	int16 _shown;              // DAT_0051eb2c
	Common::Array<Request> _list; // DAT_0051e210 (DAT_0051e1f4 entries)
	bool _listLoaded;
	int _base[kPatches];       // DAT_0051eb30: list entry of each patch's still
	int _anim[kPatches];       // DAT_0051eaf0: its picture (-1 at progress 3)
	int _sound[kPatches];      // DAT_0051eb10: the picture's sound
	byte _palette[256 * 3];    // 0x51e6e8
	MenuVideo _quilt;          // DAT_0051e1f8 (frames DAT_0051e208)
	MenuVideo _quiltBack;      // DAT_0051e1fc (frames DAT_0051e20c)
	Graphics::Surface _frame;  // DAT_0051eb08: the 640x480 quilt bitmap
	RdxBackground *_frameObject; // DAT_0051eb04
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_MENUS_H
