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

#ifndef NIGHTMARENED_NED_H
#define NIGHTMARENED_NED_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"

#include "nightmarened/data.h"
#include "nightmarened/resources.h"

namespace NightmareNed {

class CollisionWorld;
class Game;
struct Slot;

/**
 * The scene's 4-entry callback table that ned_tick's last argument points at
 * (the attic's is DAT_00525980). Every entry may be null; so may the table.
 */
struct NedHooks {
	void *user;
	/** +0x00: entering state 0 (button 2), at Ned's position (0x450dc0) */
	int (*use)(void *user, int x, int y, int16 *state);
	/** +0x04: Ned walked into a collision region; nonzero takes the state it wrote */
	int (*region)(void *user, int32 id, uint32 flags, int x, int y, int16 *state);
	/** +0x08: in state 0, the target rect near (x, y); b = -1 means none (0x452350) */
	void (*findTarget)(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]);
	/** +0x0c: Ned's box overlapped that rect; the result is kept until state 0 is re-entered */
	int (*hitTarget)(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]);
};

/** DAT_004c5738: world -> screen, applied to Ned's sprite position instead of the scroll arguments */
typedef void (*NedXform)(void *user, int &x, int &y);
/** DAT_004c52b4 / DAT_004c57bc: called with the action number when state 15 ends (negated if cut short) */
typedef void (*NedActionCallback)(void *user, int16 action);
/** The completion callback ned_list_load puts on a list's last entry */
typedef void (*NedListCallback)(void *user);

/**
 * Ned: the player controller (NITENED.EXE 0x44e9c0-0x455930). Ned's sprites are the
 * area-1 slots (NED00000); his actions are 23 state records whose phases (start, loop,
 * hold, end, tail) each name slots, stepped on a 33 ms unit (41 ms in wrap mode 2).
 */
class Ned {
public:
	enum {
		kStateCount = 23,
		kListCount = 23,
		kSlotCount = 426
	};

	explicit Ned(Game *game);
	~Ned();

	/** 0x44f710 + 0x44fbc0 (WinMain start-up): opens NED00000, loads the palette, list 0, a fidget, costume 1 */
	bool init();

	/**
	 * 0x454920 ned_tick: one controller step. facing (DAT_004c5750 copy, 1 = right) is
	 * written before the input is read; pos is Ned's world position; scroll is added to
	 * the sprite position unless an xform is set; hooks may be null. Returns nonzero when
	 * a state change was taken.
	 */
	int tick(CollisionWorld &world, uint32 input, int &facing, Common::Point &pos, uint32 now,
		int scrollX, int scrollY, const NedHooks *hooks);

	/** 0x44ee30 ned_play_action: plays action record 0x4aeb68[action] as state 15 and places Ned */
	void playAction(int action, Common::Point &pos, int scrollX, int scrollY);
	/** 0x44ee00: selects the action record without playing it */
	void setAction(int action);
	/** 0x44eb00 ned_show */
	void show(bool visible);
	/** 0x44ea50 ned_cur_slot: the area-1 slot on screen */
	int16 getCurSlot() const { return _curSlot; }
	/** 0x44e9b0 */
	void setCurSlot(int16 slot) { _curSlot = slot; }
	/** 0x44ea10: the sprite position of slot (0, 0 when nothing is shown) */
	Common::Point getSpritePosition(int16 slot) const;
	/** 0x44eb30: moves the shown sprite (used by scene cameras after ned_tick) */
	void adjPosition(int dx, int dy);
	/** 0x44e9c0 ned_enable / 0x44e9d0 */
	void enable(bool on) { _enabled = on; }
	bool isEnabled() const { return _enabled; }
	/** 0x44ea00: 1 = facing right */
	int getFacing() const { return _facing; }
	void setFacing(int facing) { _facing = facing; }
	/** DAT_004a5548: the current state */
	int16 getState() const { return _state; }
	/** 0x44e9a0: hanging mode (DAT_004c601c) */
	bool isHanging() const { return _hanging; }
	/** 0x44e9e0: a slot's sequence step */
	int16 getSlotStep(int16 slot);
	/** 0x44ea60: true when slot's next-step deadline is before now */
	bool isSlotDue(int16 slot, uint32 now);
	/** 0x44eab0: true when the shown slot is one of the standing ones (4, 0x4d, 0x17b) */
	bool isStanding() const;
	/** 0x44eae0: state 10 is playing slot 0x13 */
	bool isState10Ending() const;

	/** 0x4506f0 ned_reset_idle: finishes a pending action callback, then idle (state 4, slot 1) */
	void resetIdle();
	/** 0x450760 ned_state_reset: rewinds a state to its first phase, every phase slot at step -1 */
	void stateReset(int16 state);
	/** 0x44ebd0: releases the current state's loop or hold */
	void releaseState();
	/** 0x44eb60: false while the current state defers its exit (0x40000000) */
	bool canRelease() const;
	/** 0x44f1d0: drops into the fall state (0x12) */
	void startFall();
	/** 0x450660: walking from the given loop slot, keeping its step */
	void resumeWalk(int16 slot);
	/** 0x44ecf0: leaves hanging, restores the idle state record and its matrix row, then idle */
	void restoreIdle();
	/** 0x44f6f0: puts slot 1's sprite at (x, y - its height) */
	void setBasePosition(int x, int y);
	/** 0x455600: moves pos down (dirs 2) or up (dirs 1) to the floor within maxDist; true if found */
	bool dropToFloor(CollisionWorld &world, Common::Point &pos, int maxDist, uint dirs);

	/** 0x44f650 ned_set_wrap1: 33 ms unit; wrap mode 1 makes x wrap modulo width */
	void setWrap1(bool wrap, int width, NedXform xform, void *user);
	/** 0x44f5f0 ned_set_wrap2: on = mode 2 (x wraps by tileW * tiles past 0x302a) at 41 ms; off = mode 0 at 33 ms */
	void setWrap2(bool on, int unused, int tileW, int16 tiles, NedXform xform, void *user);
	/** 0x44f590: x wrapped by the current mode */
	int wrapX(int x) const;

	/** 0x44fd00 ned_list_free */
	bool listFree(int list);
	/** 0x44fdf0 ned_list_load: cb runs once the list is loaded; done is cleared first */
	int listLoad(int list, NedListCallback cb = nullptr, void *user = nullptr, int *done = nullptr);
	/** 0x44fd30 ned_list_show: attaches each sprite (z 8, hidden) and gives it an inactive flip */
	bool listShow(int list);
	/** 0x44ea90: frees list 0 */
	void freeMainList();

	/**
	 * 0x4500c0 ned_find_target: the nearest visible kind-2 sprite of objects in front of
	 * Ned (by facing). dir = 0x2d..0x30 by the slope to it (0x2f when nothing is found).
	 */
	bool findTarget(int facing, int16 nedSlot, const Common::Array<Placement> &objects,
		int16 &dir, int &unused, int16 &found);
	/** 0x450330: Ned's box 0 (3 when facing left) for slot at pos, in world coordinates */
	void getBox(int16 slot, const Common::Point &pos, int32 out[4]);

	/** 0x450210 ned_copy_cfg: puts Ned's colours 11..41 into a 256 x rgb palette */
	void copyPalette(byte *palette) const;
	const byte *getPalette() const { return _palette; }

	/** 0x44f250: loads costume mode's state records, transition matrix and hooks (1 = normal) */
	void setCostume(int mode);
	/** 0x44ebc0 */
	int getCostume() const { return _costume; }

	/** 0x44ec00: on every scene switch, ends a scripted action and returns Ned to idle */
	void sceneSwitchBegin();
	/** 0x44ebf0: restarts the idle timer (DAT_004a554c) */
	void markTime();
	/** 0x44eb90 / 0x44ebb0: the scene's state-15 callback (DAT_004c52b4) */
	void setFinishCallback(NedActionCallback cb, void *user) { _onFinish = cb; _onFinishUser = user; }
	NedActionCallback getFinishCallback() const { return _onFinish; }
	/** 0x44eba0: a one-shot state-15 callback (DAT_004c57bc) */
	void setActionCallback(NedActionCallback cb, void *user) { _onAction = cb; _onActionUser = user; }
	/** 0x44e970: lets the idle fidget change after each play */
	void enableFidgetRotation() { _fidgetRotate = true; }
	/** 0x44f950: state 10's slot speeds raised by percent (scene 5, on a tub) */
	void setRunSpeeds(int percent);
	/** 0x44f920: state 10's default slot speeds */
	void resetRunSpeeds();
	/** 0x44eaa0: state 0's first start slot */
	void setUseSlot(int16 slot);
	/** 0x44f6b0 / 0x44f680: the region Ned last touched */
	void getRegion(uint32 &flags, int32 &id, int32 rect[4]) const;
	void clearRegion();

private:
	enum {
		// int16 field indexes of a state record after its u32 flags (record offset 4 + 2 * index)
		kSlot = 0,      // +0x04
		kStartIdx = 4,  // +0x0c
		kStart = 5,     // +0x0e, 5 slots
		kLoopIdx = 10,  // +0x18
		kLoop = 11,     // +0x1a, 2 slots
		kHoldStep = 13, // +0x1e
		kHold = 14,     // +0x20
		kEndIdx = 15,   // +0x22
		kEnd = 16,      // +0x24, 5 slots
		kTailIdx = 21,  // +0x2e
		kTail = 22      // +0x30, 5 slots
	};

	/** A 0x3c-byte state record (DAT_004c4d50[]) */
	struct State {
		uint32 flags;
		int16 h[28];
	};

	Slot &slot(int id);
	void readState(State &st, uint32 va);
	void readMatrixRow(int row, uint32 va);
	void clearRegionCache();
	void storeRegion(const int32 rect[4], uint32 flags, int32 id);
	void hideSlot(int16 id);
	uint32 nextRandom();

	// 0x486930 / 0x485700 / 0x485450 / 0x4860d0 on a slot
	bool seqStep(Slot &s, int step, int &frame, int &duration);
	void boxAnchor(Slot &s, int &x, int &y);
	void boxFrameOffset(Slot &s, int frame, int &x, int &y);
	void boxRect(Slot &s, int frame, int k, int32 out[4]);

	bool advanceSlot(Slot &s);
	void rewindSlot(Slot &s);
	bool atLastStep(Slot &s);
	int stepState(int16 state, uint32 now, int &finished);
	void frameEvent(int16 slotId, int16 step, int16 state);
	void playSound(int16 slotId);
	void placement(int16 slotId, const Common::Point &pos, int &x, int &y);
	void placeSprite(int16 slotId, const Common::Point &pos, int scrollX, int scrollY);
	void enterState(int16 &state, int16 next);
	void fall(int16 &state);
	void clearSlotOffsets(int16 state);
	void applyFlip(Slot &s);

	bool canLeave(int16 next, int16 cur);
	int tryState(CollisionWorld &world, int16 &next, Common::Point &pos, int scrollX, int scrollY,
		const NedHooks *hooks, int flag);
	int walk(uint32 input, int16 &next, CollisionWorld &world, Common::Point &pos, int scrollX,
		int scrollY, const NedHooks *hooks, bool turn);
	void attachOffset(CollisionWorld &world, int16 state, Common::Point &pos);
	void targetCheck(Common::Point &pos, const NedHooks *hooks);

	bool regionAt(CollisionWorld &world, int x, int y, int32 rect[4], uint32 &flags, int32 &id);
	bool userRegionAt(CollisionWorld &world, const int32 box[4], int32 rect[4], uint32 &flags, int32 &id);
	bool probe(CollisionWorld &world, int x, int y, int maxDist, int start, int step, int &out, uint dirs);
	bool probeRegion(CollisionWorld &world, int x, int y, int maxDist, int start, int step, int &out,
		uint dirs, uint32 need);
	int enterRegion(CollisionWorld &world, int16 &state, int16 slotId, int step, int px, int py,
		int x, int y, int scrollX, int scrollY, const NedHooks *hooks);
	int moveCheck(CollisionWorld &world, int16 &state, int16 slotId, int frame, Common::Point &pos,
		int px, int py, int nx, int ny, int scrollX, int scrollY, const NedHooks *hooks, bool force);
	int regionCheck(CollisionWorld &world, int16 slotId, int frame, int px, int py, int x, int y, bool force);
	int landCheck(CollisionWorld &world, int16 &state, Common::Point &pos);

	// DAT_004c5758[state]: may Ned enter state? (0x450c80 and friends)
	int enterHook(uint32 fn, CollisionWorld &world, int16 &state, int16 facing, Common::Point &pos,
		const NedHooks *hooks, int flag);
	// DAT_004c56d8[state]: per-tick movement (0x453780 and friends)
	int tickHook(uint32 fn, CollisionWorld &world, int16 &state, Common::Point &pos, int scrollX,
		int scrollY, const NedHooks *hooks);
	int hookMove(CollisionWorld &world, int16 &state, Common::Point &pos, int scrollX, int scrollY,
		const NedHooks *hooks);
	int hookWalk(CollisionWorld &world, int16 &state, Common::Point &pos, int scrollX, int scrollY,
		const NedHooks *hooks);
	int hookFall(CollisionWorld &world, int16 &state, Common::Point &pos);
	int hookAction(CollisionWorld &world, int16 &state, Common::Point &pos);
	int hookButton1(CollisionWorld &world, int16 &state, Common::Point &pos);
	int hookButton3(CollisionWorld &world, int16 &state, Common::Point &pos, int scrollX, int scrollY,
		const NedHooks *hooks);
	int enterSideStep(CollisionWorld &world, int16 &state, int16 facing, Common::Point &pos);
	int enterClimb(CollisionWorld &world, int16 state, Common::Point &pos, uint32 bit);
	int enterStairs(CollisionWorld &world, int16 facing, Common::Point &pos, int16 stairs,
		uint32 bitLeft, uint32 bitRight, int flag);
	int enterHang(CollisionWorld &world, int16 &state, Common::Point &pos);
	int enterHangWalk(CollisionWorld &world, int16 &state, int16 facing, Common::Point &pos, int dx, bool left);
	void restoreButton3();
	void defaultSlotSetup();
	void costume7SlotSetup();

	Game *_game;
	byte _palette[256 * 3];            // DAT_004c52b8 (from list 0x4a5c20)
	Common::Array<Request> _paletteList;
	Common::Array<Request> _lists[kListCount]; // PTR_DAT_004afb98[] / DAT_004afbf8[]
	Common::Array<Request> _fidgetList;        // 2 entries at 0x4a63e0 + fidget * 0x24
	bool _fidgetLoaded;
	uint32 _frameEvents[kSlotCount];   // 0x4a5578

	int16 _state;          // DAT_004a5548
	uint32 _idleTime;      // DAT_004a554c
	int16 _fidgetFixed;    // DAT_004a5550
	int _gravity;          // DAT_004a5554
	int16 _saved3[5];      // DAT_004a5558..68: state 10's slot speeds, restored by 0x4505e0
	int _costume;          // DAT_004a556c
	int _sideStep;         // DAT_004a5570
	bool _enabled;         // DAT_004a5574

	State _states[kStateCount];        // DAT_004c4d50
	uint32 _matrix[kStateCount * kStateCount]; // DAT_004c57c8: [cur * 23 + next] allowed
	uint32 _enterHooks[kStateCount];   // DAT_004c5758
	uint32 _tickHooks[kStateCount];    // DAT_004c56d8

	uint32 _regionFlags;   // DAT_004c4d44
	int32 _regionId;       // DAT_004c56d0
	int32 _regionRect[4];  // DAT_004c5740..574c
	int _tickUnit;         // DAT_004c4d48
	int _wrapMode;         // DAT_004c56bc
	int _wrapWidth;        // DAT_004c4d40
	int _tileW;            // DAT_004c4d3c
	int16 _tiles;          // DAT_004c56c8
	int _wrapUnused;       // DAT_004c573c
	NedXform _xform;       // DAT_004c5738
	void *_xformUser;
	int16 _anchorX, _anchorY; // DAT_004c4d34 / DAT_004c4d38: the base slot's anchor + w/2, + h
	int16 _halfW;          // DAT_004c57c4
	int16 _baseW, _baseH;  // DAT_004c56c0 / DAT_004c4d30
	uint32 _holdStart;     // DAT_004c56cc
	int16 _z;              // DAT_004c56d4
	int16 _lastSound;      // DAT_004c5734
	int _facing;           // DAT_004c5750
	int _fidget;           // DAT_004c5754: entry index of the loaded fidget pair
	NedActionCallback _onFinish;  // DAT_004c52b4
	void *_onFinishUser;
	NedActionCallback _onAction;  // DAT_004c57bc
	void *_onActionUser;
	int16 _action;         // DAT_004c57b8
	int _hitTarget;        // DAT_004c57c0
	bool _pending;         // DAT_004c57b4: re-check the region when the state ends
	bool _landed;          // DAT_004c6010: play the landing sound
	bool _fidgetShown;     // DAT_004c6014
	int16 _curSlot;        // DAT_004c6018
	bool _hanging;         // DAT_004c601c
	bool _fidgetRotate;    // DAT_004c6024
	uint32 _soundTime;     // DAT_004c6028
	bool _upHeld;          // DAT_004c602c
	bool _downHeld;        // DAT_004c6030
	uint32 _randomSeed;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_NED_H
