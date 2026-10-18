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

#ifndef NIGHTMARENED_SCENE1_BASE_H
#define NIGHTMARENED_SCENE1_BASE_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"

#include "nightmarened/data.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/resources.h"
#include "nightmarened/world.h"

namespace NightmareNed {

class Scene1;

/**
 * The resource, display, sound and collision helpers every part of the graveyard uses
 * (scene 1 and its sub-scenes; NED99000, file index 11, area 2). Each room keeps its own
 * loaded entries, collision world, region map and target list, as the original kept
 * separate globals for each.
 */
class GraveyardRoom : public Scene {
public:
	enum {
		kArea = 2,
		kNafGrave = 11,  // ned99000 (DAT_004baf5c)
		kNedArea = 1
	};

	GraveyardRoom(Game *game, int id, Scene1 *top);
	~GraveyardRoom() override;

	Scene1 *top() const { return _top; }
	Ned *ned() { return _game->ned(); }
	Game *game() { return _game; }

	// ---- Resources ----
	Slot &slot(int id) { return slotAt(kArea, id); }
	Slot &slotAt(int area, int id);
	/** objSetVisibility on a slot's sprite (ignored without one) */
	void visible(int id, bool on) { visibleAt(kArea, id, on); }
	void visibleAt(int area, int id, bool on);
	/** A bitmap record by the variable the original stored it in (this room's, then the top's) */
	BitmapRec *bmp(uint32 va);
	/** A collision map by the variable the original stored it in (this room's, then the top's) */
	CollisionMap *map(uint32 va);
	/** A palette stored by a bitmap entry (+0x20), 256 x rgb; nullptr if none loaded */
	const byte *palette(uint32 va);
	/**
	 * 0x4586e0 on NED99000, synchronous: every entry loads now and the completion callbacks
	 * (entry +0x1c) run before it returns, through listCallback()
	 */
	int submit(uint32 va, int count, int scene);
	/** 0x458dc0: frees backwards; maps also leave this room's world */
	void release(uint32 va, int count);
	bool isLoaded(uint32 va) const;
	/** Frees everything this room loaded */
	void freeAll();
	/** 0x459170 res_wait: 0 when the entry is loaded, 5 otherwise */
	int wait(uint32 entryVA) const { return isLoaded(entryVA) ? 0 : 5; }
	/** The list's sprite entry for id, or 0 */
	uint32 entryFor(uint32 list, int count, int id) const;
	/** 0x459090 res_show_sprite: draw order z on the primary surface, then the visibility */
	void showSprite(uint32 entryVA, int z, bool on);
	/** 0x459100 res_show_bitmap */
	void showBitmap(uint32 entryVA, int z, bool on);
	/** 0x459000 res_attach_list: every sprite and bitmap to the primary surface, hidden */
	void attachList(uint32 list, int count);
	/** 0x459240: hides everything of the list that is shown and remembers it, or shows it again */
	void saveVisibility(uint32 list, int count, bool restore, Common::Array<int> &state);

	// ---- Display (area 2 unless an area is given) ----
	/** 0x430890 slot_switch */
	int16 switchSlot(int newSlot, int oldSlot) { return switchSlotAt(kArea, newSlot, oldSlot); }
	int16 switchSlotAt(int area, int newSlot, int oldSlot);
	/** 0x430930 slot_switch_pre (step -1) */
	int16 switchSlotPre(int newSlot, int oldSlot) { return switchSlotPreAt(kArea, newSlot, oldSlot); }
	int16 switchSlotPreAt(int area, int newSlot, int oldSlot);
	/**
	 * 0x430710 slot_place (0x430850 / 0x430530 / 0x430410): shows the slot's current step
	 * and positions it against ref's anchor; xf maps world to screen (nullptr = none, as
	 * 0x430530 and 0x430410). Returns the step's duration; outFrame gets its frame.
	 */
	int place(int id, int ref, bool noMirror, NedXform xf, void *user, int *outFrame = nullptr) {
		return placeAt(kArea, id, ref, noMirror, xf, user, outFrame);
	}
	int placeAt(int area, int id, int ref, bool noMirror, NedXform xf, void *user, int *outFrame = nullptr,
		bool relative = false);
	/** 0x430a30 slot_nudge: objAdjPosition unless the slot is -1 */
	void adj(int id, int dx, int dy) { adjAt(kArea, id, dx, dy); }
	void adjAt(int area, int id, int dx, int dy);
	/** 0x430440 slot_attach_pos: where slot would be placed against ref's anchor (no xform) */
	void attachPos(int id, int ref, int out[2], bool noMirror) { attachPosAt(kArea, id, ref, out, noMirror); }
	void attachPosAt(int area, int id, int ref, int out[2], bool noMirror);
	/** 0x486930 on a slot's sequence; false outside [0, steps) */
	bool seqStep(int area, int id, int step, int &frame, int &duration);
	/** The frame of the slot's current step (0 when out of range) */
	int currentFrame(int area, int id);
	/** 0x4860d0 box_rect on a slot's hit boxes (-0x7fff when absent) */
	void boxRect(int area, int id, int frame, int k, int32 out[4]);
	/** 0x485450 box_frame_off: frame offset, or with alt the mirrored pair plus addX */
	void frameOffset(int area, int id, int frame, int &x, int &y, bool alt = false, int addX = 0);
	/** 0x485700 box_anchor */
	void anchor(int area, int id, int &x, int &y);
	/** objApplyEffect(FX_HORIZ_FLIP) when the slot has none (a new effect is active, as in dmix) */
	RdxEffect *ensureFlip(int area, int id);
	/** ensureFlip, then fxSetActive */
	void setFlip(int area, int id, bool active);
	/** fxSetActive on the slot's flip effect, if it has one */
	void flipActive(int area, int id, bool active);
	/** Whether the slot's flip effect exists and is active (the box index 3 test) */
	bool isFlipped(int area, int id);
	/** IntersectRect on {x0, y0, x1, y1} */
	static bool intersect(const int32 a[4], const int32 b[4]);
	/** PtInRect */
	static bool inRect(const int32 r[4], int x, int y);

	// ---- Sound ----
	/** 0x455880 snd_play */
	void playSound(int id, int stop = -1) { playSoundAt(kArea, id, stop); }
	void playSoundAt(int area, int id, int stop = -1);
	/** 0x455930 */
	void stopSound(int id) { stopSoundAt(kArea, id); }
	void stopSoundAt(int area, int id);
	/** 0x455950 / thunk 0x44d020: every slot voice stops; the streamed music plays on */
	void stopAllSounds();

	// ---- Collision (this room's world) ----
	/** 0x487f50 + 0x4872a0: a map goes into the world with its own bounds */
	void addMap(uint32 va);
	/** 0x4872a0 with given bounds */
	void addMapBounds(uint32 va, int x0, int y0, int x1, int y1);
	/** 0x487320 */
	void removeMap(uint32 va);
	/** 0x487c50 bc_solid_at on one map */
	bool solidAt(uint32 va, int x, int y);
	/** 0x488060: bit 31 of the first region with the id is the disabled flag */
	static void regionEnable(CollisionMap *m, int32 id, bool on);
	/** 0x4877f0 */
	static void regionSetRect(CollisionMap *m, int32 id, const int32 rect[4]);
	/** 0x4878d0 */
	static void regionShift(CollisionMap *m, int dx, int dy);
	/** The first region with the id, or nullptr (the lookup 0x488060 and 0x4877f0 share) */
	static CollisionMap::Region *regionFind(CollisionMap *m, int32 id);
	/** 0x487600 + 0x487400 on this room's region map: returns the new region's id in outId */
	void addRegion(int16 type, int16 id, const int32 rect[4], int32 *outId);
	/** 0x4876d0 + 0x487400 */
	void removeRegion(int32 id);
	CollisionWorld &world() { return _world; }
	CollisionMap &userMap() { return _userMap; }
	/** The maps the collision overlay draws */
	Common::Array<CollisionMap *> overlayMaps() const;

	// ---- Targets (the oO list Ned's zap scans) ----
	/** 0x4309c0 olist_add_sprite */
	void targetAdd(int area, int id, bool noFlip);
	/** 0x486f50 olist_remove (its index loop skips the entry after each removal) */
	void targetRemove(int area, int id);
	Common::Array<Placement> &targets() { return _targets; }

	// ---- Misc ----
	/** 0x446820: sleeps until ms have passed since start (game clock) */
	void frameWait(uint32 ms, uint32 start);
	/** srfDraw(DAT_004c3b54) */
	void draw();
	/** 0x45a560: loads %02d%03d0%02d.sst (area prefix 0x4afcd8[areaIdx], scene, n) */
	bool loadSst(int areaIdx, int scene, int n, Common::Array<Placement> &out);
	/** 0x43e460 (olist_foreach callback): applies a placement to its slot or bitmap record */
	void applyPlacement(const Placement &p);
	/** 0x45a0f0, staying here (logged once) when the target isn't registered yet; 0 = switched */
	int switchTo(int scene, int sub = -1);

protected:
	/** A list's completion callback (the entry's +0x1c), run when submit() finishes */
	virtual void listCallback(uint32 cb) {}

	typedef Common::HashMap<uint32, Request> EntryMap;

	Scene1 *_top;
	EntryMap _entries;                              // loaded request entries by VA
	Common::HashMap<uint32, BitmapRec *> _bitmaps;  // bitmap records by destination VA
	Common::HashMap<uint32, CollisionMap *> _maps;  // collision maps by destination VA
	Common::HashMap<uint32, Common::Array<byte> > _palettes; // palettes by destination VA
	CollisionWorld _world;
	CollisionMap _userMap;
	Common::Array<Placement> _targets;
	int _unportedExit;
};

} // End of namespace NightmareNed

#endif // NIGHTMARENED_SCENE1_BASE_H
