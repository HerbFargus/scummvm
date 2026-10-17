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

#ifndef DREAMFORGE_LOGIC_H
#define DREAMFORGE_LOGIC_H

#include "common/hashmap.h"
#include "common/serializer.h"

namespace DreamForge {

class DreamForgeEngine;

// Record types for recState (C.PRG 0x20c10)
enum {
	kRecLayer = 0,
	kRecObject = 1,
	kRecHotspot = 2
};

/**
 * What the room code (the act handlers in C.PRG 0x32270..0x49320) calls. Each
 * call is named after the C.PRG function it stands for, so the handlers read
 * close to the original. Sound and voice numbers are the bank numbers
 * (DAT_000dfb88 + n is voice 10000 + n, DAT_000e131c + n effect 21000 + n...).
 */
class Logic {
public:
	Logic(DreamForgeEngine *vm);

	// Flags (0x10230, 0x10280, 0x10320, 0x10360)
	void set(int flag);
	void clear(int flag);
	bool isSet(int flag) const;
	bool isClear(int flag) const { return !isSet(flag); }

	/** The room code's own globals, by C.PRG address; saved with the game */
	int32 &var(uint32 addr);
	void syncVars(Common::Serializer &s);
	void clearVars();

	// Where we are (0x64828, 0x6482c, 0x64830)
	int act() const;
	int area() const;
	int room() const;
	int screenHeight() const { return 480; }  // 0xe175c
	int sceneWidth() const;                   // 0xe1760
	uint32 ticks() const;                     // 0xe17d8, the 18.2 Hz game timer

	// The puppies. guy() is the current one (0x87320); the others act on it.
	int &guy();
	int &guyY(int g);       // 0x647c4[g]
	int &guyX(int g);       // 0x647cc[g]
	int guyDir(int g);      // 0x647d4[g]
	int &held();            // 0x647c4[0], the held item, -1 none
	void setDir(int dir);                             // 0x169d0
	void oneShot(int slot, int dir);                  // 0x16c40
	void walk(int y, int x, int dir);                 // 0x1b4b0; guy() 3 walks both
	void turn(int from, int to);                      // 0x19ef0
	void say(int voice, int g, bool quick);           // 0x1bd20
	void banter(int set);                             // 0x1c720
	void hide(int g);                                 // 0x167e0; 3 both
	void show(int g);                                 // 0x16810; 3 both
	bool isHidden(int g);                             // 0x166c0
	bool isLying(int g);                              // 0x16690
	bool isSitting(int g);                            // 0x166b0
	void clearLying(int g);                           // 0x166a0
	bool isWalking(int g);                            // 0x16680
	bool isBusy(int g);                               // 0x166e0
	void stopWalking(int g);                          // 0x18940
	bool atPos(int y, int x, int g);                  // 0x17680
	void setPos(int y, int x);                        // 0x1b8d0
	void faceEachOther();                             // 0x1c290
	void guy2FacesGuy1();                             // 0x1c1f0
	int dirTo(int y0, int x0, int y1, int x1);        // 0x341e0

	// The scene
	void sceneAnim(int cine, int top, int left, int transparent, int delay, int depth, int noWait, int hideGuy);  // 0x23af0
	int cutscene(int n, int mode = 10, int a = 0, int b = 0, int c = 0, int d = 0, int e = 1);       // 0x15650; 1 if skipped
	void changeScene(int y1, int x1, int d1, int y2, int x2, int d2, int act, int area, int room);  // 0x1f800
	void redraw(int reload, int present);             // 0x1d700
	int recState(int id, int type);                   // 0x20c10
	// Scene record fields by array index, as the room code reads them directly
	int hotspotIdAt(int index);
	int hotspotTopAt(int index);
	int hotspotHeightAt(int index);
	int layerIdAt(int index);
	int layerDepthAt(int index);
	int objectDepthAt(int index);
	void setObjectIdAt(int index, int id);
	int objectCount();
	void dropHeld();                                   // 0x257a0
	/** The end of the game (0x10180): the closing cutscenes, then back to the launcher */
	void endGame();
	bool objOnPath(int id);                 // 0x223b0: a path object out on its run
	void objPathChance(int id, int chance); // 0x22090
	/** Swaps in another scene file's walk map and waypoints (0x485a0) */
	void loadWalkMap(int act, int area, int room);
	void roomDone();                                  // 0x205c0

	// Scene objects by id (0x21950..0x21fe0)
	void objFrame(int id, int frame);                 // 0x21b40
	void objPos(int id, int top, int left);           // 0x21bf0
	void objHide(int id);                             // 0x21c60
	void objShow(int id);                             // 0x21cd0
	void objLoop(int id);                             // 0x21d40
	void objNoLoop(int id);                           // 0x21dd0
	void objRewind(int id);                           // 0x21e30
	void objStop(int id);                             // 0x21ed0
	void objNow(int id);                              // 0x21f40
	void objTimer(int id, uint32 tick);               // 0x21f90
	bool objDone(int id);                             // 0x219e0
	bool objCond(int id);                             // 0x21950
	int objFrameOf(int id);                           // 0x21a30

	// Sound
	int voice(int id);                                // 0x11b90; returns a handle
	int sfx(int id, int volume, int loop);            // 0x11bc0; returns a handle
	void waitSound(int handle, int keepGoing);        // 0x11c00
	void stopSound(int handle);                       // 0x494a0
	bool soundPlaying(int handle);                    // 0x49940
	void music(int track, int loop);                  // 0x11aa0
	void stopMusic();                                 // 0x11b40

	// Odds and ends
	int rand15();                                     // 0x51035
	void step();                                      // 0x10840, one game step
	void busyOn() {}                                  // 0x12cb0 (the wait cursor)
	void busyOff() {}                                 // 0x12d90
	void fadeOut(int first, int last, int r, int g, int b);  // 0x51160 on the scene palette
	void fadeIn(int first, int last, int r, int g, int b);   // 0x51240
	void unfade();                                           // 0x4cbe0
	/** Something the room code calls that isn't ported yet */
	void todo(const char *what);

private:
	DreamForgeEngine *_vm;
	Common::HashMap<uint32, int32> _vars;
	int32 _dummyDir;
};

} // End of namespace DreamForge

#endif // DREAMFORGE_LOGIC_H
