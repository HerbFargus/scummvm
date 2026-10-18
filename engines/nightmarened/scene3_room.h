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

#ifndef NIGHTMARENED_SCENE3_ROOM_H
#define NIGHTMARENED_SCENE3_ROOM_H

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
 * Scene 3 sub-scene 1, the switchboard off the corridor (NED97000 scene 0xb, area 4;
 * methods 0x461640..0x461d70). One screen: a telephone switchboard with three switches
 * (slots 440..442) and a call button (443) that Ned zaps, and an operator (170) peering
 * over it. The switches route the wire to one of eight lines (the lit path, 444..451);
 * what answers on each line is shuffled on every visit (0x4623d0). The button makes the
 * operator pick up the phone (172). Six lines talk and then send a monster that grabs Ned
 * (183) or a swordsman he jumps from (181, action 0x24). On one, Ned's parents come and
 * walk him off to the left, back to the corridor (scene 3, 0x464040); on another a
 * patchwork man drags him off to the right, out to the quilt (scene 0, 0x463be0). Those
 * are the only exits. Entered from the scene 3 corridor hub.
 */
class Scene3Room : public Scene {
public:
	explicit Scene3Room(Game *game);
	~Scene3Room() override;

	int load(int fromScene, int fromSub) override;   // m2 0x461640
	int enter(int fromScene, int fromSub) override;  // m3 0x461760
	int leave(int toScene, int toSub) override;      // m4 0x461c80
	int unload(int toScene, int toSub) override;     // m5 0x461d20
	int tick(uint32 input, uint32 now) override;     // m7 0x461d70

	const Common::Point &getNedPosition() const { return _nedPos; }

private:
	enum {
		kArea = 4,
		kNaf = 9,                  // ned97000 (DAT_0051f6c8)
		kNafScene = 0xb,
		kNedList = 0xe,
		kList = 0x4b0aa8,          // 0x3b entries
		kListCount = 0x3b,
		kPalette = 0x51dd20,       // the backdrop's palette
		kSstAreas = 0x4afcd8,      // .SST area prefix per area index (i16)
		kSstAreaIndex = 2,
		kChannelTable = 0x4b0a48,  // 8 lines x {u32 &DAT_0051e150[k], u32 lit path, u32 jingle}
		kChannelFns = 0x51e150,

		// Request entries of kList (the slot id is at +0x0c)
		kBackdrop = 0x4b0aa8,      // bitmap 1, z 1000
		kMap = 0x4b0acc,           // cbix 20 (DAT_0051e184)
		kOp = 0x4b0af0,            // 170: the operator, the reference for every operator slot
		kHandset = 0x4b0b14,       // 408: the phone's handset (hidden while he holds it)
		kCards = 0x4b0b38,         // 409
		kSwitch0 = 0x4b0b5c,       // 440
		kSwitch1 = 0x4b0b80,       // 441
		kSwitch2 = 0x4b0ba4,       // 442
		kButton = 0x4b0bc8,        // 443
		kHum = 0x4b0bec,           // sound 156
		kPicture0 = 0x4b0c10,      // 444: the reference for the lit wire paths
		kOpIdle = 0x4b0c34,        // 171: a fidget every 10 s
		kChannelSoundLast = 0x4b0e50, // sound 258 (the line jingles are 250..258)
		kOpStart = 0x4b0e74,       // 172: picks up the phone
		kOpStartSound = 0x4b0e98,  // 259
		kOpTalk = 0x4b0ebc,        // 173: laughs
		kOpTalkSound = 0x4b0ee0,
		kOpParents = 0x4b0f04,     // 174: while the parents come
		kJump = 0x4b0f4c,          // 181: the swordsman
		kJumpSound = 0x4b0f70,
		kGrab = 0x4b0f94,          // 183: the monster
		kGrabSound = 0x4b0fb8,
		kParents0 = 0x4b0fdc,      // 178: the parents arrive
		kDragged0 = 0x4b1000,      // 405: the patchwork man arrives
		kDragged1 = 0x4b1024,      // 176: takes Ned
		kDragged1Sound = 0x4b1048,
		kParents1 = 0x4b106c,      // 403
		kParents1Sound = 0x4b1090,
		kParents2 = 0x4b10b4,      // 179: take Ned
		kParents2Sound = 0x4b10d8,
		kParents3 = 0x4b10fc,      // 404: walk Ned left off the screen
		kDragged2 = 0x4b1120,      // 407: drags Ned right off the screen

		kWalkToX = 0x16c
	};

	// What answers a line, in 0x4623d0's order
	enum Programme {
		kProgNone = -1,
		kProg0 = 0,     // 0x462e00: talk, grab
		kProg1,         // 0x4630c0: talk, jump
		kProg2,         // 0x4632a0: talk, grab
		kProg3,         // 0x463560: talk, jump
		kProgParents,   // 0x464040: back to the corridor
		kProgDragged,   // 0x463be0: off to the quilt
		kProg6,         // 0x463740: talk, grab
		kProg7,         // 0x463a00: talk, jump
		kProgCount
	};

	// End-of-sound callbacks (slot +0x60)
	enum SoundCallback {
		kCbNone = 0,
		kCbTalk1 = 0x464600,  // DAT_0051e1cc = 1
		kCbTalk2 = 0x464610,  // DAT_0051e180 = 1
		kCbStart = 0x464620   // DAT_0051e19c = 1
	};

	typedef Common::HashMap<uint32, Request> EntryMap;

	// Resources
	Slot &slot(int id);
	int16 eid(uint32 entry) const;
	int submit(uint32 va, int count, int scene);
	void release(uint32 va, int count);
	void freeAll();
	bool isLoaded(uint32 va) const { return _entries.contains(va); }
	int wait(uint32 entry) const;
	int showSprite(uint32 entry, int z);
	int showBitmap(uint32 entry, int z);

	// Display (area 4, no scroll)
	int16 switchSlot(int newSlot, int oldSlot);
	int place(int id, int ref);
	void attachPos(int id, int ref, int out[2]);
	void stepNext(int id, int ref, uint32 now);

	// Sound
	void playSound(int id);
	void playSoundNotify(int id, uint32 callback);
	void stopSound(int id);
	void stopAllSounds();
	void checkSoundEnds();

	// Ned
	Ned *ned();
	void nedTick(uint32 input, uint32 now);
	void nedWalkAway();
	int switchTo(int scene);

	// The room
	void shuffle();
	void pickChannel(bool init);
	int programmeAt(int channel) const;
	int runProgramme(int prog, uint32 now, bool reset);
	int talkProgramme(int &state, uint32 voice1, uint32 voice2, bool grab, bool reset);
	int parentsProgramme(uint32 now, bool reset);
	int draggedProgramme(uint32 now, bool reset);
	void placeGrab();
	void operatorTick(uint32 now);
	int grabTick(uint32 now);
	int jumpTick(uint32 now);
	void targetAdd(int id);
	int findTarget(int x, int y, int16 *dir, int16 *found, int32 rect[4]);
	int hitTarget(int16 area, int16 found);
	static int applyPlacement(Scene3Room *s, const Placement &p);

	// Hooks (DAT_0051dd10)
	static int hookUse(void *user, int x, int y, int16 *state);
	static int hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	static void hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	static int hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);

	// ---- Resources and the world ----
	EntryMap _entries;                 // loaded request entries by VA
	BitmapRec *_backdrop;              // DAT_004e1b9c
	CollisionMap *_map;                // DAT_0051e184
	byte _palette[256 * 3];            // DAT_0051dd20
	CollisionWorld _world;             // DAT_0051e1b0
	Common::Array<Placement> _targets; // DAT_0051dd0c: the switches and the button
	Common::Array<Placement> _sst;     // DAT_0051e1d4: 97999002.SST
	NedHooks _hooks;                   // DAT_0051dd10
	Common::Point _nedPos;             // DAT_0051e178 / DAT_0051e17c
	int _scrollX, _scrollY;            // DAT_0051e1e0 / DAT_0051e1e4 (never written)
	int _unportedExit;                 // the last exit to a scene that isn't registered (logged once)
	uint32 _soundCallback[kAreaSlots]; // slot +0x60 for the voices started by 0x455800
	uint32 _lastTick;

	// ---- .data ----
	int16 _channelSound;    // DAT_004b0a40: the line's jingle slot (-1 = none)
	int _nedInput;          // DAT_004b0a44: Ned gets the input
	int16 _draggedSlot;     // DAT_004b12f4
	int16 _parentsSlot;     // DAT_004b12f8

	// ---- .bss ----
	int _parentsState;      // DAT_0051dd08
	int _channelData[8];    // DAT_0051e128: the values 0xc..0x1a shuffled with the programmes (unused)
	int _talkState[6];      // DAT_0051e148, e198, e1a4, e1b8, e1b4, e1c4 (programmes 0-3, 6, 7)
	int _ownNaf;            // DAT_0051e14c: m2 opened ned97000 itself
	int _channelFn[8];      // DAT_0051e150: a programme per table slot
	int _programme;         // DAT_0051e170: the running programme, ticked by m7
	int _talk2Done;         // DAT_0051e180
	int _draggedX;          // DAT_0051e188
	int _channel;           // DAT_0051e18c: the switches as a 3-bit line number
	int16 _jumpSlot;        // DAT_0051e190: 181 while it plays
	int16 _grabY;           // DAT_0051e194
	int _startDone;         // DAT_0051e19c
	int _draggedState;      // DAT_0051e1a0
	int _startHeld;         // DAT_0051e1a8: the pick-up holds at step 4 for the ring
	int _parentsX;          // DAT_0051e1ac
	int _opRun;             // DAT_0051e1bc: the pick-up may run past step 4
	int16 _op;              // DAT_0051e1c0: the operator slot on screen
	uint32 _opIdleTime;     // DAT_0051e1c8
	int _talk1Done;         // DAT_0051e1cc
	int _exitScene;         // DAT_0051e1d0
	int16 _grabSlot;        // DAT_0051e1d8: 183 while it plays
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE3_ROOM_H
