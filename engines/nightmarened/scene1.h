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

#ifndef NIGHTMARENED_SCENE1_H
#define NIGHTMARENED_SCENE1_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/surface.h"

#include "nightmarened/scene1_base.h"

namespace Video {
class SmackerDecoder;
}

namespace NightmareNed {

class RdxBackground;
class RdxGroup;
class Scene1Enemies1;
class Scene1Enemies2;
class Scene1Sub0;
class Scene1Sub2;
class Scene1Sub7;

/**
 * A Smacker file decoded one frame at a time into a buffer (0x488130 SmackOpen,
 * 0x4881c0 SmackGoto + SmackToBuffer + SmackDoFrame, 0x488200 SmackClose). Going back,
 * or more than 30 frames forward, seeks the way ScummVM's Smacker decoder does (from ten
 * frames before the target) instead of decoding from the start.
 */
class GraveVideo {
public:
	GraveVideo();
	~GraveVideo();
	bool open(Game *game, int fileIndex);
	void close();
	bool isOpen() const { return _decoder != nullptr; }
	int getFrameCount() const;
	/** frame is 0-based (the game passes frame + 1 to SmackGoto) */
	void decodeFrame(int frame, Graphics::Surface &dst);

private:
	Video::SmackerDecoder *_decoder;
	int _current;
};

/**
 * Scene 1, the graveyard (registered by 0x43c140; NED99000, area 2). A looping
 * side-scroller 1300 x 9 px wide (Ned's wrap mode 2: 0x514 tiles of 9 px), whose
 * backdrop is a Smacker video (ned.ini [Grave Videos] Forward/Backward Background,
 * GRVA/GRVB) decoded at the frame of Ned's tile, behind parallax layers. Eleven
 * creatures (the records at 0x4a32fc) stream in and out by tile range; zapping them,
 * the open graves (sub-scenes 3, 4, 5 in turn), a crypt (sub-scene 0) and two more
 * places (sub-scenes 2 and 7) lead off it. The world's six vision triggers fire from
 * them. Scenes 2 and 3 are reached through the videos MASA/MASB.
 */
class Scene1 : public GraveyardRoom {
public:
	explicit Scene1(Game *game);
	~Scene1() override;

	int init() override;                             // m0 0x43c3c0
	void shutdown() override;                        // m1 0x43c540
	int load(int fromScene, int fromSub) override;   // m2 0x43c580
	int enter(int fromScene, int fromSub) override;  // m3 0x43eb60
	int leave(int toScene, int toSub) override;      // m4 0x43c770
	int unload(int toScene, int toSub) override;     // m5 0x43c880
	int tick(uint32 input, uint32 now) override;     // m7 0x43cb90
	int getSaveSize() const override { return 0x20; } // m8 0x43eab0
	void saveState(int32 *out) override;             // m9 0x43eac0
	void restoreState(const int32 *in, int size) override; // m10 0x43eae0

	enum {
		kTiles = 0x514,      // tiles per lap
		kTileW = 9,
		// Request lists
		kMainList = 0x4a1cc0,    // 0x19: bitmaps, the maps, the keeper
		kMainCount = 0x19,
		kNedList = 0x4a2048,     // 0x1a: sprites shown with Ned
		kNedCount = 0x1a,
		// Where the original kept what those lists load
		kMapFloor = 0x4bf918,    // cbix 1: the ground
		kMapExits = 0x4bf91c,    // cbix 2: the exit and trigger regions
		kMap3 = 0x5238e4,
		kMap4 = 0x5238dc,
		kMap5 = 0x5238e0,
		kMap7 = 0x5237a4,
		kBmpGround = 0x4deb2c,   // bitmap 1 (palette 0x523f80)
		kBmpFront = 0x4deb30,    // bitmap 2
		kBmpSky = 0x4deb8c,      // bitmap 25
		kBmpHills = 0x4deb80,    // area 10 bitmap 4
		kBmpClouds = 0x4deb60,   // area 10 bitmaps 15..22, 8 pointers
		kPaletteGame = 0x523f80
	};

	// ---- What the creature modules and the sub-scenes share (the original's globals) ----

	/** 0x43cac0: world -> screen across the lap seam (user = Scene1) */
	static void xformWrap(void *user, int &x, int &y);
	/** 0x43cb30: world -> screen, x - tile * 9, y + the rise (user = Scene1) */
	static void xformPlain(void *user, int &x, int &y);
	/** 0x43cb30 on a point */
	void toScreen(int &x, int &y) const { x -= _tile * kTileW; y += _yOff; }

	/** 0x43d3a0: the slot joins Ned's target list (from its slot record) */
	void targetAddSlot(int16 id);
	/** 0x43d440 */
	void targetRemoveSlot(int16 id);
	/** 0x43d460: a region on the scene's region map, which becomes the world's user map */
	void regionAdd(int16 type, int16 id, const int32 rect[4], int32 *outId);
	/** 0x43d4a0 */
	void regionRemove(int32 id);
	/** 0x43e260: the view rises (dy) with the backdrop, the ground and the clouds */
	void riseBy(int dx, int dy);

	/** 0x4770a0: the puff (area 1 slot 0x1a9) centred on slot id of area, at draw order z */
	void puffStart(int area, int id, int z, uint32 now, NedXform xf, void *user);
	/** 0x4771f0: the puff's next step; it hides itself after the last */
	void puffTick(uint32 now, NedXform xf, void *user);
	/** 0x4772e0 */
	void puffScroll(int dx, int dy);
	/** 0x477310: the ambient creature's next appearance (now + 0..34 s) */
	void ambientSchedule(uint32 now);
	/** 0x477340: on = schedule it from now, off = hide it */
	void ambientEnable(bool on);

	/** 0x428330: the cloud group's visibility */
	void cloudsVisible(bool on);
	/** 0x428500: the clouds drift (2 px every 125 ms) */
	void cloudsDrift(uint32 now);
	/** 0x428450: (a, b) as the original names them: a only accumulates, b moves the clouds */
	void cloudsScroll(int a, int b);
	/** 0x43e350: the video backdrop's two layers */
	void backdropVisible(bool on);
	/** 0x43e380 */
	void hillsVisible(bool on);
	/** 0x43e3a0 / 0x43e400: every layer of the graveyard */
	void layersVisible(bool on);
	/** 0x43c0a0: the exit regions of map 2 come back on */
	void exitsEnable();
	/** 0x43cb60: Ned's wrap mode 2 over the lap (slot 4's x offset 9) */
	void nedWrap();
	/** 0x43f720: the backdrop frame for the tile, then srfDraw */
	void drawBackdrop();
	/** 0x43fa70: frees the grave-digger's sound list */
	void diggerFree();
	/** 0x43f7f0: the grave-digger's sprite */
	void diggerVisible(bool on);
	/** 0x43eeb0: the scene's state-15 callback (Ned's scripted actions end) */
	static void actionDone(void *user, int16 action);
	/** 0x430180: frees the keeper's seven speech lists */
	void keeperFree();
	/** 0x45a560(0, 999, 1) result: the scene's placements (DAT_0052356c) */
	Common::Array<Placement> &placements() { return _sst; }
	/** A record (0..10) has loaded and shown (the A callback flags, DAT_00523578 ...) */
	bool enemyLoaded(int record) const { return record >= 0 && record < kEnemies && _enemyLoaded[record]; }

	Scene1Enemies1 *enemies1() { return _e1; }
	Scene1Enemies2 *enemies2() { return _e2; }
	Scene1Sub2 *sub2() { return _sub2; }

	enum {
		kEnemies = 11
	};

	Common::Point _nedPos;   // DAT_005245e8 / DAT_005245ec
	int _tile;               // _DAT_00525e04: Ned's tile in the lap (x / 9)
	int _tileCount;          // DAT_00525e00: the videos' frame count (0x514)
	int _yOff;               // DAT_0052344c: the view's rise
	int _riseLeft;           // DAT_00523450
	int _rise;               // DAT_00523454
	int _inputFree;          // DAT_004a1c94: 0 = only button 2 gets through
	int _grabbed;            // DAT_004bf8f8: Ned is held until _grabEnd
	uint32 _grabEnd;         // DAT_005245e4
	uint32 _keeperTime;      // DAT_00524864
	int _visionCheck;        // DAT_004bf8f4: the vision triggers are tested this tick
	int _toSub7;             // DAT_004bf920
	int _toSub2;             // DAT_004bf904
	int _toGrave;            // DAT_004bf8fc
	int _graveNext;          // DAT_00528098: which grave (sub 3, 4, 5) opens next
	int _toVideo;            // DAT_005238cc: 1 MASA, 2 waiting, 3 MASB, 4 the scene 5 video
	int _exitTo;             // DAT_00523458: 1 = scene 2, 2 = scene 3 (after the TV clip)
	int _costumeMode;        // DAT_004bf90c: the costume region (4000/4001) is on
	int _flipped;            // DAT_00524848
	int16 _grabSlot;         // DAT_004a1c88: the hands (0xa0) that hold Ned, or 0xe
	int16 _bellSlot;         // DAT_004a1c84
	// The save block (DAT_00523a00, 8 dwords): Ned's position and the vision flags
	int32 _done[8];
	/** DAT_00524d80: a palette sub-scene 2 keeps (0x446b20); 0x43f300 restores its static colours */
	byte _palette524d80[256 * 3];

private:
	friend class Scene1Enemies1;
	friend class Scene1Enemies2;
	friend class Scene1Sub0;
	friend class Scene1Sub2;
	friend class Scene1Sub3;
	friend class Scene1Sub4;
	friend class Scene1Sub5;
	friend class Scene1Sub7;

	void listCallback(uint32 cb) override;

	// The enemy records (0x4a32fc, 0x64 bytes): list, count, four tile bounds, flag, callbacks
	void enemyStream();                                   // 0x43b730
	void enemyShow(int record);                           // the record's +0x20 callback
	void enemyHide(int record);                           // the record's +0x24 callback

	// Hooks (DAT_005245f0)
	static int hookUse(void *user, int x, int y, int16 *state);                      // 0x43bf10
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state); // 0x43bc80
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]); // 0x43c060
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]); // 0x43bf80
	int findTarget(int16 &dir, int16 &found, int32 rect[4]);                          // 0x43ba30
	bool regionCreature(int32 id, uint32 flags, int x, int y, int16 *state);           // 0x43be00
	int hitHands(int16 found);                                                         // 0x43af80
	int rectHands(int16 found, int32 pos[2]);                                          // 0x43b000

	// The level
	void setupWorld();             // 0x43e6c0
	void arrival();                // 0x43dbd0
	void cloudsCreate();           // 0x428350
	void cloudsFree();             // 0x428430
	void cloudsRise(int dy);       // 0x4285e0
	void frameForTile();           // the backdrop's video frame for the tile
	void creaturesTick(uint32 input, uint32 now); // 0x43d4d0
	void camera();                 // 0x43d640
	void stepRight(int snap);      // 0x43d8a0
	void stepLeft(int snap);       // 0x43da40
	void stepped(int dx);          // the end of 0x43d8a0 / 0x43da40
	void hillsStep();              // the hills' view moves one pixel every second tile
	void videoMasa();              // 0x43f0f0
	void videoMasb();              // 0x43f300
	void diggerTick();             // 0x43f820
	void diggerStart();            // 0x43fb40
	void diggerUpdate(uint32 now, int16 x); // 0x43fd50
	void diggerScroll(int dx, int dy); // 0x43ff80
	void ambientTick(uint32 now);  // 0x477620
	void ambientPlace(uint32 now); // 0x4773a0
	void keeperPick();             // 0x42fc40
	void keeperStep(uint32 now);   // 0x42fd20
	void keeperTick(uint32 now, int16 x); // 0x430250
	void keeperScroll(int dx, int dy); // 0x4301f0
	int keeperRect(int16 found, int32 pos[2]); // 0x430220
	bool keeperHit(int16 found, uint32 now); // 0x430340
	void diggerRelease();          // the list freeing 0x43f820 and 0x43fa70 share
	void hint(int which);

	Scene1Enemies1 *_e1;
	Scene1Enemies2 *_e2;
	Scene1Sub0 *_sub0;
	Scene1Sub2 *_sub2;
	Scene1Sub7 *_sub7;
	NedHooks _hooks;                 // DAT_005245f0

	// The video backdrop: a 640 x 660 buffer shown by two backgrounds
	Graphics::Surface _backdrop;     // DAT_00524604 (0x67200 bytes)
	Graphics::Surface _backdropTop;  // its first 220 rows (hbmp DAT_00527c50)
	Graphics::Surface _backdropLow;  // rows 220..659 (hbmp DAT_005280a8)
	RdxBackground *_bkgTop;          // DAT_00527c54, z 5
	RdxBackground *_bkgLow;          // DAT_00527c48, z 0x32
	GraveVideo _forward;             // DAT_005245dc: file 0x3a, 99010601.SMK
	GraveVideo _backward;            // DAT_00523684: file 0x3b, 99010602.SMK
	GraveVideo _mausoleum;           // DAT_00523a20: MASB.SMK (0x7b), then MASBR.SMK (0x7a)
	int _videoFrames;                // DAT_005238d4
	int _videoFrame;                 // DAT_005244a8
	int _frameShown;                 // DAT_004a1c90: the tile the backdrop shows
	int _hillsTile;                  // DAT_004a3718
	int _standSlot;                  // DAT_004a3714
	RdxGroup *_clouds;               // DAT_004bcae0
	int _cloudX, _cloudY;            // DAT_004bcad4 / DAT_004bcad8
	uint32 _cloudTime;               // DAT_004bcae4
	Common::Array<Placement> _sst;   // DAT_0052356c: 99999001.SST
	Common::String _videoNames[10];  // DAT_00523340 ... [Grave Videos]

	int _enemyLoaded[kEnemies];      // the records' flags (DAT_00523578, DAT_00524600, ...)
	int _enemyLoading[kEnemies];     // record +0x28
	int _enemyShown[kEnemies];       // record +0x2c

	uint32 _nextDraw;                // DAT_004bf908
	bool _gotoDone;                  // test hook nightmarened_s1_goto
	int _savedTile;                  // DAT_00523a40
	byte _savedPalette[256 * 3];     // DAT_00523a42: the palette when the scene was left
	byte _gamePalette[256 * 3];      // DAT_00523f80
	int _mainLoaded;                 // DAT_005238d0 (callback 0x43b8e0)
	int _nedLoaded;                  // DAT_005238c8 (callback 0x43b8f0)
	int _toScene5Bits;               // DAT_00523a24
	int _costumeOn;                  // DAT_004bf910
	int _costumeOff;                 // DAT_004bf914
	int _bellRung;                   // DAT_004bf900
	int _typed[4];                   // DAT_005245d0..d3: T, E, A, M held
	int _diggerLoaded, _diggerShown, _diggerReady; // DAT_004bf954 / DAT_004bf958 / DAT_004bf960
	int _diggerList;                 // DAT_004a38f4
	int _diggerNoMirror;             // DAT_004a38f0
	uint32 _diggerTime;              // DAT_004bf95c
	int16 _diggerSlot;               // DAT_004a38f8
	int16 _keeperSlot;               // DAT_00524874
	int16 _keeperTurns;              // DAT_00524876
	int _keeperGreets;               // DAT_004a02b8
	int16 _keeperSpeech;             // DAT_004bd734
	int16 _ambientSlot;              // DAT_004b447c
	uint32 _ambientTime;             // DAT_005228cc
	int16 _puffSlot;                 // DAT_004b4478
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE1_H
