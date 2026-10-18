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

#ifndef NIGHTMARENED_SCENE7_H
#define NIGHTMARENED_SCENE7_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"

#include "nightmarened/data.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/resources.h"
#include "nightmarened/world.h"

namespace NightmareNed {

/**
 * Scene 7, the mouth (registered by 0x42f3e0; NED94000, area 5). An 8480 x 480 level
 * of 53 strips scrolled with clamping: the lower jaw (z 200) and the upper jaw (z 6),
 * which opens for 30 s and then bites. Teeth hide cavities Ned zaps; a bite that catches
 * him calls up one of four monsters, and a fourth hit ends the game (scene 0). Falling
 * through the gaps leads to scenes 3 and 1, the creature at the right end to scene 2,
 * and the two ladders up to the lips play TV clips. Entered from scene 2 (0x44a1c0).
 */
class Scene7 : public Scene {
public:
	explicit Scene7(Game *game);
	~Scene7() override;

	int init() override;                             // m0 0x429b00
	void shutdown() override;                        // m1 0x42f3d0
	int load(int fromScene, int fromSub) override;   // m2 0x428ad0
	int enter(int fromScene, int fromSub) override;  // m3 0x428b40
	int leave(int toScene, int toSub) override;      // m4 0x429960
	int unload(int toScene, int toSub) override;     // m5 0x429b10
	int tick(uint32 input, uint32 now) override;     // m7 0x42cf90
	/** m8 0x42f370 */
	int getSaveSize() const { return 4; }
	/** m9 0x42f380 */
	void saveState(int32 *out);
	/** m10 0x42f3a0 */
	void restoreState(const int32 *in, int size);

	const Common::Point &getNedPosition() const { return _nedPos; }

private:
	enum {
		kArea = 5,
		kNafMouth = 6,             // ned94000
		kNedList = 0x10,
		kMainList = 0x49f190,      // 0x4a entries
		kMainCount = 0x4a,
		kUpperList = 0x49e2a0,     // 53 strips, scene 60, z 6
		kLowerList = 0x49ea18,     // 53 strips, scene 61, z 200
		kStripCount = 0x35,
		kStripW = 0xa0,
		kUpperBitmaps = 0x4bcb40,  // destinations of the strips
		kLowerBitmaps = 0x4bcc14,
		kBugList = 0x49fbf8,       // 9
		kSlot12List = 0x49fd40,    // 6
		kEnemy0List = 0x49fe18,    // 8
		kEnemy1List = 0x49ff38,    // 5
		kEnemy2List = 0x49fff0,    // 5
		kEnemy3List = 0x4a00a8,    // 5
		kSlot8List = 0x4a0160,     // 4
		kSlot35List = 0x4a01f0,    // 2
		kMapFloor = 0x4bcd00,      // cbix 1: the lower jaw
		kMapJaw = 0x4bcd08,        // cbix 3: the upper jaw while it moves
		kMapJawOpen = 0x4bcd0c,    // cbix 2: the upper jaw, open
		kMapGateA = 0x4bd138,      // cbix 4
		kMapGateB = 0x4bd13c,      // cbix 5
		kMapJawShut = 0x4bcaf0,    // cbix 6: the upper jaw, shut
		kPaletteGame = 0x4bd268,   // bitmap 7's palette (with Ned's colours merged in)
		kPaletteFlash = 0x4bcd28,  // bitmap 3's palette
		kSstAreas = 0x4afcd8,      // .SST area prefix per area index (i16)
		kSstAreaIndex = 6,
		kMouthMusic = 0x7e,        // 94002208.wav
		kListCallback = 0x4288d0,  // the callback on the last entry of the staged lists
		kJawOpenY = -0x114,
		kWorldW = 0x2120
	};

	typedef Common::HashMap<uint32, Request> EntryMap;

	// Resources
	Slot &slot(int id);
	void visible(int id, bool on);
	BitmapRec *bmp(uint32 va);
	CollisionMap *map(uint32 va);
	int submit(uint32 va, int count, int scene);
	void release(uint32 va, int count);
	bool isLoaded(uint32 va) const { return _entries.contains(va); }
	void runCallbacks();
	void freeAll();
	uint32 entryFor(uint32 list, int count, int id) const;
	void show(uint32 list, int count, int id, int z, bool on);
	void saveVisibility(uint32 list, int count, bool restore, Common::Array<int> &state);

	// Display (area 5)
	int16 switchSlot(int newSlot, int oldSlot);
	int16 switchSlotPre(int newSlot, int oldSlot);
	int place(int id, int ref, bool noMirror, int *outFrame = nullptr, Common::Point *outPos = nullptr);
	void advance(int id, int ref, bool noMirror, uint32 now);
	void adj(int id, int dx, int dy);
	void attachPos(int id, int ref, int out[2], bool noMirror);
	void boxRect(int id, int frame, int k, int32 out[4]);
	int currentFrame(int id, bool clamp);
	void applyFlip(int id, bool active);
	void ensureFlip(int id);
	static bool intersect(const int32 a[4], const int32 b[4]);

	// Sound
	void playSound(int id, int stop = -1);
	void stopSound(int id);
	void stopAllSounds();

	// Collision
	void addMap(uint32 va);
	void removeMap(uint32 va);
	void moveMapBy(uint32 va, int dy);
	bool solidAt(uint32 va, int x, int y);
	static void regionEnable(CollisionMap *m, int32 id, bool on);
	static void regionSetRect(CollisionMap *m, int32 id, const int32 rect[4]);
	static void regionShift(CollisionMap *m, int dx, int dy);
	static void clearMask(CollisionMap *m);
	int32 addRegion(const int32 rect[4], uint32 type, int32 id);
	void removeRegion(int32 id);
	Common::Array<CollisionMap *> overlayMaps() const;

	// Targets (DAT_004bd15c)
	void targetAdd(int id);
	void targetRemove(int id);

	// Ned
	Ned *ned();
	void nedTick(uint32 input, uint32 now);
	static void xform(void *user, int &x, int &y);

	// Shared modules that are not ported
	void hint(int which);
	bool hintActive() const;
	void playClipNoMusic(int clip);
	int switchTo(int scene);

	// The level (scene7.cpp)
	void clearState();
	void flash(uint32 now, bool init);
	void stripStream(uint32 list, int scene, uint32 dest);
	int stripPlace(uint32 list, uint32 dest, int first, int last, int z, int dy);
	int camera(uint32 now);
	void clipBreak(int clip, bool refloor);

	// The actors (scene7_actors.cpp)
	void creatureTick(uint32 now, bool init);
	void bugTick(uint32 now, bool init);
	void dropTick(uint32 now, bool init);
	void monsterTick(uint32 now);
	void liftTick(uint32 now);
	void popTick(uint32 now);
	void regionTick(bool init);
	void gumTick(uint32 now);
	void tongueTick(uint32 now);
	void loopTick(uint32 now);
	void jawMarksPlace();
	void teethPlace(int dx, int dy);
	void jawTick(uint32 now);
	void spitTick(bool init);
	void anim9Tick(uint32 now);
	void hitChecks();
	int findTarget(int16 &dir, int16 &found, int32 rect[4]);

	// Hooks (DAT_004bd148)
	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);
	int hitTarget(int16 found);

	// ---- Resources and the world ----
	EntryMap _entries;                              // loaded request entries by VA
	Common::Array<uint32> _pendingCallbacks;        // list completion callbacks not run yet
	Common::HashMap<uint32, BitmapRec *> _bitmaps;  // bitmap records by destination VA
	Common::HashMap<uint32, CollisionMap *> _maps;  // collision maps by destination VA
	byte _palette[256 * 3];       // DAT_004bd268
	byte _paletteFlash[256 * 3];  // DAT_004bcd28
	CollisionWorld _world;        // DAT_004bccfc
	CollisionMap _userMap;        // DAT_004bcb38
	Common::Array<Placement> _targets; // DAT_004bd15c: what Ned's zap can hit
	Common::Array<Placement> _sst;     // DAT_004bccec: 93999001.SST
	NedHooks _hooks;              // DAT_004bd148
	Common::Point _nedPos;        // DAT_004bcb20 / DAT_004bcb24
	int _scrollX, _scrollY;       // DAT_004bd130 / DAT_004bd134: world -> screen
	int _unportedExit;            // the last exit to a scene that isn't registered (logged once)

	// ---- .data tables (0x49e230..0x49e29c), copied at start-up ----
	int16 _jawMarks[4];      // DAT_0049e230: 0xc2 0xc3 0xce 0xcf, moved with the upper jaw
	int16 _jawAnchors[8];    // DAT_0049e238: 0xc6..0xcd, the pop anchors on the upper teeth
	int16 _loopSlots[5];     // DAT_0049e24a: 0xf 0x11 0xd0 0xd1 0xd2, always looping
	int16 _loopRefs[5];      // DAT_0049e258
	int16 _jawSlots[7];      // DAT_0049e268: 0xd3..0xd9, the cavities that flash (-1 once spat)
	int16 _stillSlots[4];    // DAT_0049e278: 0x18 0xda 0xdb 0xdc
	int16 _creatureSlot;     // DAT_0049e280: 0x32..0x35, the creature at the right end
	int _jawArmed;           // DAT_0049e284: the bite may catch Ned
	int16 _bugSlot;          // DAT_0049e288: 0x14..0x17, the bug at five holes
	int16 _dropSlot;         // DAT_0049e28c: 0xb, 0xc, 0xd, 0x1a4, 0x1a5, what the cavities spit
	int16 _monsterSlot;      // DAT_0049e290: the monster a bite calls up
	int16 _tongueSlot;       // DAT_0049e294: 0x19
	int16 _gumSlot;          // DAT_0049e298: 0x12
	int _cameraSettled;      // DAT_0049e29c

	// ---- .data flags (0x4a0238..0x4a0260) ----
	int _creatureNew;        // DAT_004a0238
	int _bugNew;             // DAT_004a023c
	int _bugSound;           // DAT_004a0240
	int _dropNew;            // DAT_004a0244
	int _monsterNew;         // DAT_004a0248
	int _liftFirst;          // DAT_004a024c
	int _tongueLoad;         // DAT_004a0250
	int _gumLoad;            // DAT_004a0254
	int _jawOpenMapIn;       // DAT_004a025c
	int _nedActive;          // DAT_004a0260: ned_tick runs (off while Ned climbs down after clip 0x1a)

	// ---- .bss ----
	int _bugRegion;          // DAT_004bcae8: region 0x3fb, on the bug
	uint32 _jawTime;         // DAT_004bcaec: the jaw stays open until
	int16 _teeth[12];        // DAT_004bcaf8: 0x8c..0x95, 0xc0, 0xc1
	int16 _gates[3];         // DAT_004bcb10: 0x96, 0x97, 0xdd
	int16 _gumRef;           // DAT_004bcb18
	int16 _gumNext;          // DAT_004bcb28
	int16 _jawMarkY[4];      // DAT_004bcb30
	int _monsterFlip;        // DAT_004bcb3c: the monster faces right (no flip)
	int _nedHit;             // DAT_004bccf0
	int16 _tongueRef;        // DAT_004bccf8
	int32 _saveCopy;         // DAT_004bcd04
	int16 _jawAnchorY[8];    // DAT_004bcd10
	int16 _popRef;           // DAT_004bcd20: where slots 9 and 0xb9 pop
	int _jawOpen;            // DAT_004bd140
	uint32 _flashTime;       // DAT_004bd158
	uint32 _jawShutTime;     // DAT_004bd160
	int16 _teethY[12];       // DAT_004bd168
	int16 _dropRef;          // DAT_004bd248
	int16 _jawSlotY[7];      // DAT_004bd250
	int _jawY;               // DAT_004bd260: the upper jaw's y offset (kJawOpenY open, 0 shut)
	int _creatureRegion;     // DAT_004bd66c: region 0x419, on the creature
	int16 _monsterRef;       // DAT_004bd670
	int _anim9;              // DAT_004bd678
	int _pop;                // DAT_004bd67c
	int16 _hits;             // DAT_004bd680
	int _lost;               // DAT_004bd684
	int _stageReady;         // DAT_004bd688
	int _toScene3;           // DAT_004bd68c
	int _toScene1;           // DAT_004bd690
	int _toScene2;           // DAT_004bd694
	int _inputLock;          // DAT_004bd698: only button 2 gets through
	int _toRightLip;         // DAT_004bd69c
	int _toLeftLip;          // DAT_004bd6a0
	int _armed;              // DAT_004bd6a4 (saved): Ned has been up the left ladder, so monsters can be zapped
	int _cameraLock;         // DAT_004bd6a8
	int _lift;               // DAT_004bd6ac
	int _flashOn;            // DAT_004bd6b0
	int _monsterGone;        // DAT_004bd6b4
	int _creatureDone;       // DAT_004bd6b8
	int _unusedFlag;         // DAT_004bd6bc (set by the unreferenced 0x4288f0)
	int _dropAfterPop;       // DAT_004bd6c0
	int _leftLipDone;        // DAT_004bd6c4
	int16 _stage;            // DAT_004bd6c8
	int16 _bugIndex;         // DAT_004bd6d4
	int _bugArmed;           // DAT_004bd6d8
	uint32 _bugTime;         // DAT_004bd6dc
	int _dropFloorKnown;     // DAT_004bd6e0
	int16 _dropFloorY;       // DAT_004bd6e4
	int _dropBoxY;           // DAT_004bd6e8
	int _monsterPlaced;      // DAT_004bd6ec
	int _monsterFloorY;      // DAT_004bd6f0
	uint32 _jawStepTime;     // DAT_004bd6f4
	int _jawMoving;          // DAT_004bd6f8
	int _climbDown;          // DAT_004bd6fc
	uint32 _lastDraw;        // DAT_004bd700
	int16 _climbTicks;       // DAT_004bd708
	int _lastScreenX;        // DAT_004bd710
	int _lastScreenY;        // DAT_004bd714
	uint32 _scrollTime;      // DAT_004bd718 (timeGetTime, not the game clock)
	int _spit[4];            // DAT_004bd720..DAT_004bd72c
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE7_H
