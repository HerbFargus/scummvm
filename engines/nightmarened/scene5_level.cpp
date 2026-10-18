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

#include "common/debug.h"

#include "nightmarened/detection.h"
#include "nightmarened/display.h"
#include "nightmarened/game.h"
#include "nightmarened/ned.h"
#include "nightmarened/rdx.h"
#include "nightmarened/scene5.h"

namespace NightmareNed {

// 0x41ce80: world -> screen
void Scene5::xform(void *user, int &x, int &y) {
	Scene5 *s = (Scene5 *)user;
	x += s->_scrollX;
	y += s->_scrollY;
}

// 0x41c5e0: the scroll this tick (out), limited to the area Ned is in. Everything the
// scene placed in screen space moves with it: the loaded tiles, Ned, the foreground
// pieces; the two parallax layers move their views by 1/6 and 1/3 (not when Ned has
// just wrapped around the level), and the tiles in view are placed again.
void Scene5::camera(uint32 now, int d[2]) {
	bool jump = _lastNedX - _nedPos.x > 0x900 || _lastNedX - _nedPos.x < -0x900;
	_lastNedY = _nedPos.y;
	_lastNedX = _nedPos.x;
	cameraWant(now, d);
	int32 r[4];
	cameraLimits(r);
	if (-r[0] < d[0] + _scrollX)
		d[0] = -(r[0] + _scrollX);
	else if (d[0] + _scrollX < 0x280 - r[2])
		d[0] = (0x280 - r[2]) - _scrollX;
	if (-r[1] < _scrollY + d[1])
		d[1] = -(r[1] + _scrollY);
	else if (_scrollY + d[1] < 0x1e0 - r[3])
		d[1] = (0x1e0 - r[3]) - _scrollY;
	if (d[0] == 0 && d[1] == 0)
		return;
	int dx = d[0], dy = d[1];
	for (int i = _grid.cols * _grid.rows - 1; i >= 0; i--) {
		BitmapRec *rec = bmp(kTileDest + 4 * i);
		if (rec && rec->object)
			rec->object->adjPosition(dx, dy);
	}
	ned()->adjPosition(dx, dy);
	_scrollX += dx;
	_scrollY += dy;
	int vx = _midRemX - dx, vy = _midRemY - dy;
	_midRemX = vx % 6;
	_midRemY = vy % 6;
	if (!jump && _midLayer && _midLayer->object)
		_midLayer->object->adjView(vx / 6, vy / 6, 0, 0);
	vx = _farRemX - dx;
	vy = _farRemY - dy;
	_farRemX = vx % 3;
	_farRemY = vy % 3;
	if (!jump && _farLayer && _farLayer->object)
		_farLayer->object->adjView(vx / 3, vy / 3, 0, 0);
	streamTiles();
	static const int16 kPieces[] = { 304, 305, 306, 307, 308, 309, 311, 312, 313, 314, 315, 316 };
	for (int16 id : kPieces)
		nudge(id, dx, dy);
	// The pipe 309 sits at the seam: it is kept on Ned's side of the level
	Slot &pipe = slot(309);
	int wx = pipe.x - _scrollX;
	if (_nedPos.x < 0x900) {
		if (wx > 0x900) {
			pipe.x -= kWorldW;
			if (pipe.sprite)
				pipe.sprite->adjPosition(-kWorldW, 0);
		}
	} else if (wx < 0x900) {
		pipe.x += kWorldW;
		if (pipe.sprite)
			pipe.sprite->adjPosition(kWorldW, 0);
	}
	int16 state = ned()->getState();
	order(312, state == 5 || state == 6 ? 0x65 : 2);
}

// 0x419d90: the world rect the view must stay in, by where Ned is. On the ledges above
// y 0x422 (and while he climbs between them) the limits ease in over 0x30 or 0x18 ticks.
void Scene5::cameraLimits(int32 r[4]) {
	int16 state = ned()->getState();
	int climbing = state == 5 || state == 6 ? 1 : 0;
	if (climbing != _climbing) {
		_ledgeEase = 0;
		_climbEase = 0;
	}
	_climbing = climbing;
	r[0] = -99999;
	r[1] = 0;
	r[2] = 99999;
	r[3] = 0x602;
	int x = _nedPos.x, y = _nedPos.y;
	if (y < 0x422) {
		_ledgeEase = 0x18;
		if (x < 0x500) {
			if (y < 0x242) {
				r[0] = 0x1b9;
				r[2] = 0x500;
				r[1] = 0x62;
				int e = _climbEase * 0xf0 / 0x30;
				r[3] = climbing ? e + 0x242 : 0x332 - e;
				if (_climbEase < 0x30)
					_climbEase++;
			} else if (0x281 < x) {
				r[0] = 0x280;
				r[2] = 0x500;
			}
		} else if (0x621 < x && x < 0x8a1) {
			r[0] = 0x621;
			r[2] = 0x8a1;
			r[1] = 9;
		} else if (0xa45 < x && x < 0xe05) {
			if (y < 0x174) {
				r[0] = 0xa45;
				r[2] = 0xe05;
				r[1] = 0;
				r[3] = 0x174;
				return;
			}
			if (0xb40 < x) {
				r[0] = 0xb41;
				r[2] = 0xe05;
			}
			// The original compares the scroll here, not Ned's y
			if (_scrollY < 0x173)
				r[1] = 0x173;
		}
		return;
	}
	if (!climbing) {
		r[1] = 0x422;
		return;
	}
	int left = 0, right = 0;
	if (x < 0x282 || 0x500 < x) {
		if (x < 0x621 || 0x8a1 < x) {
			if (0xb40 < x && x < 0xe06) {
				left = 0xb41;
				right = 0xe05;
			}
		} else {
			left = 0x621;
			right = 0x8a1;
		}
	} else if (_drainState == 2) {
		r[1] = 0x422;
	} else {
		left = 0x280;
		right = 0x500;
	}
	if (left != 0) {
		int e = _ledgeEase * 0x140 / 0x18;
		r[0] = e - 0x140 + left;
		r[2] = (right - e) + 0x140;
		if (_ledgeEase < 0x18)
			_ledgeEase++;
	}
}

// 0x41cea0: the scroll Ned's position asks for. Walking, he is kept at x 200 facing right
// and 440 facing left (a 1-pixel creep every 12 ms on the near side); the farther he is,
// the faster it scrolls, up to a jump past 640. States 7 and 8 scroll diagonally; y is
// kept near 400. A big move (the wrap-around) skips the creep.
void Scene5::cameraWant(uint32 now, int d[2]) {
	d[0] = d[1] = 0;
	if (_cameraFrozen)
		return;
	int16 state = ned()->getState();
	bool left = ned()->getFacing() == 0;
	if (state == 10) {
		if (ned()->isState10Ending())
			d[0] = left ? 0x12 : -0x12;
		return;
	}
	bool jump = 0x900 < _camNedX - _nedPos.x || _camNedX - _nedPos.x < -0x900;
	_camNedX = _nedPos.x;
	_camNedY = _nedPos.y;
	int sy = _nedPos.y + _scrollY;
	int sx = _nedPos.x + _scrollX;
	d[1] = 9999;
	int v = 0;
	bool set = false;
	if (left) {
		bool far = false;
		if (state == 7) {
			if (_scrollTime + 0x12 < now) {
				d[0] = 1;
				d[1] = 1;
			}
		} else if (state == 8) {
			if (_scrollTime + 0xc < now) {
				d[0] = 1;
				d[1] = -1;
			}
		} else if (state == 1 && 0x1b7 < sx && !jump) {
			if (_scrollTime + 0xc < now)
				d[0] = 1;
		} else if (state == 1 && sx <= 0x1b7) {
			far = true;
		} else if (sx < 0x1b9) {
			far = sx <= 0x1b7;
		} else {
			v = sx - 0x1b8;
			if (v < 7)
				v = 1;
			else if (v < 0xd)
				v = 2;
			else if (v < 0x19)
				v = 4;
			else if (v < 0x31)
				v = 8;
			else if (v < 0x141)
				v = 0xc;
			else if (v < 0x281)
				v = 0x12;
			v = -v;
			set = true;
		}
		if (far) {
			v = 0x1b8 - sx;
			if (v < 7)
				v = 1;
			else if (v < 0xd)
				v = 2;
			else if (v < 0x19)
				v = 4;
			else if (v < 0x31)
				v = 8;
			else if (v < 0x141)
				v = 0xc;
			else if (v <= 0x280)
				v = 0x12;
			set = true;
		}
	} else {
		bool right = false;
		if (state == 7) {
			if (_scrollTime + 0x12 < now) {
				d[0] = -1;
				d[1] = 1;
			}
		} else if (state == 8) {
			if (_scrollTime + 0xc < now) {
				d[0] = -1;
				d[1] = -1;
			}
		} else if (state == 1 && sx < 0xc9 && !jump) {
			if (_scrollTime + 0xc < now)
				d[0] = -1;
		} else if (state == 1 && sx >= 0xc9) {
			right = true;
		} else if (sx < 200) {
			v = 200 - sx;
			if (v < 7)
				v = 1;
			else if (v < 0xd)
				v = 2;
			else if (v < 0x19)
				v = 4;
			else if (v < 0x31)
				v = 8;
			else if (v < 0x141)
				v = 0xc;
			else if (v < 0x281)
				v = 0x12;
			set = true;
		} else {
			right = sx >= 0xc9;
		}
		if (right) {
			v = sx - 200;
			if (v < 7)
				v = -1;
			else if (v < 0xd)
				v = -2;
			else if (v < 0x19)
				v = -4;
			else if (v < 0x31)
				v = -8;
			else if (v < 0x141)
				v = -0xc;
			else if (0x280 < v)
				v = -v;
			else
				v = -0x12;
			set = true;
		}
	}
	if (set)
		d[0] = v;
	if (d[1] == 9999) {
		v = sy - 400;
		if (v < 0) {
			if (v < -100)
				d[1] = 0x10;
			else
				d[1] = -8 < v ? 1 : 8;
		} else if (v < 1) {
			d[1] = 0;
		} else if (v < 0x65) {
			d[1] = v < 8 ? -1 : -8;
		} else {
			d[1] = -0x10;
		}
	}
	if (d[0] != 0 || d[1] != 0)
		_scrollTime = _game->now();
	_screenX = sx;
	_screenY = sy;
}

// 0x41d5d0: the tiles from 320 left of the view to 320 right of it (one wrap either way)
// and 240 above to 240 below are loaded and placed
void Scene5::streamTiles() {
	int cw = _grid.cellW;
	int c0 = -(_grid.x + _scrollX) - 0x140;
	if (c0 < 0)
		c0 = -((cw - c0 - 1) / cw);
	else
		c0 /= cw;
	int c1 = (-(_grid.x + _scrollX) + 0x3c0) / cw + 1;
	if (c0 >= c1)
		return;
	int r0 = (-(_grid.y + _scrollY) - 0xf0) / _grid.cellH;
	int r1 = (-(_grid.y + _scrollY) + 0x2d0) / _grid.cellH + 1;
	if (r0 < 0)
		r0 = 0;
	if (_grid.rows < r1)
		r1 = _grid.rows;
	for (int r = r0; r < r1; r++)
		streamRow(c0, c1, r);
}

// 0x41d680: one row; a tile that isn't loaded yet loads now (NAF scene 99) and goes on
// the primary surface at order 0x73 with colour key 10
void Scene5::streamRow(int c, int c1, int row) {
	for (; c < c1; c++) {
		int index = row * _grid.cols + c;
		if (c < 0)
			index += _grid.cols;
		else if (_grid.cols <= c)
			index -= _grid.cols;
		if (index < 0 || index >= _grid.cols * _grid.rows)
			continue;
		uint32 dest = kTileDest + 4 * index;
		BitmapRec *rec = bmp(dest);
		if (!rec || !rec->object) {
			submit(_grid.list + index * 0x24, 1, _grid.scene);
			rec = bmp(dest);
			if (!rec || !rec->object)
				continue;
			rec->object->setDestination(_game->display().getPrimary());
			rec->object->setVisibility(true);
			rec->object->setDrawOrder(0x73);
			rec->object->setBitmap(&rec->bitmap.surface, kKey);
		}
		rec->object->setPosition(c * _grid.cellW + _scrollX + _grid.x, row * _grid.cellH + _scrollY + _grid.y);
	}
}

// 0x41da30: the ladders' ends (regions 100..109 of map 11) while Ned climbs (slot 0x10
// up, 0x14 down). Getting on a ladder takes the floors around it away; its far end is
// the exit: the toilet above (5:0), a drop down a pipe (5, -1), scene 1, the attic.
void Scene5::exitRegions() {
	int16 cur = ned()->getCurSlot();
	if (cur != 0x10 && cur != 0x14)
		return;
	int i = nedInRect(_exitRects, 10);
	if (i < 0)
		return;
	int add = -1, add2 = -1;
	switch (i) {
	case 0:
		if (_exitStage == 0) {
			order(316, 1);
			removeMap(mapN(1));
			removeMap(mapN(2));
			_exitStage = 100;
		}
		break;
	case 1:
		if (_exitStage == 100) {
			_exitScene = 5;
			_exitSub = 0;
		}
		break;
	case 2:
	case 4:
	case 6:
		if (_exitStage == 0) {
			order(312, 2);
			order(315, 2);
			order(311, 3);
			removeMap(mapN(4));
			removeMap(mapN(5));
			_exitStage = i + 100;
		}
		break;
	case 3:
		if (_exitStage == 0x66) {
			_exitScene = 5;
			_exitSub = -1;
			add = 3;
			add2 = 4;
			_cameraFrozen = 1;
		}
		break;
	case 5:
		if (_exitStage == 0x68) {
			_exitScene = 1;
			_exitSub = -1;
		}
		break;
	case 7:
		if (_exitStage == 0x6a) {
			_exitScene = 5;
			_exitSub = -1;
			add = 3;
			add2 = 4;
		}
		break;
	case 8:
		if (_exitStage == 0 && _doorState == 2) {
			order(313, 1);
			removeMap(mapN(8));
			removeMap(mapN(9));
			_exitStage = 0x6c;
		}
		break;
	case 9:
		if (_exitStage == 0x6c) {
			_exitScene = 4;
			_exitSub = -1;
		}
		break;
	default:
		break;
	}
	if (add >= 0) {
		addMap(mapN(add + 1));
		if (add2 >= 0)
			addMap(mapN(add2 + 1));
		_exitStage = 0;
	}
}

// ---- Hooks (DAT_004bad60) ----

// 0x41ccd0 (+0x00): button 2 uses slot 0x2f, or the slope slot towards a target
int Scene5::hookUse(void *user, int x, int y, int16 *state) {
	Scene5 *s = (Scene5 *)user;
	int16 dir = 0x2f, found = -1;
	int32 rect[4] = { 0, 0, 0, 0 };
	s->findTarget(dir, found, rect, s->_scrollX, s->_scrollY);
	s->ned()->setUseSlot(dir);
	return 1;
}

// 0x41cbd0 (+0x04): regions 9999 (clip 0x1f), 9998 and 9997 (visions 1, 3 and 2),
// 0x457 / 0x458 (the camera stops and goes on); Ned's state is never taken
int Scene5::hookRegion(void *user, int32 id, uint32 flags, int x, int y, int16 *state) {
	Scene5 *s = (Scene5 *)user;
	if (id == 9999)
		s->_clipPending = 1;
	if (id == 0x270e) {
		if (s->_hint1Armed) {
			s->_hint1Armed = 0;
			s->hint(1, s->_nedPos.x, s->_nedPos.y, xform);
		}
		if (s->_hint3Armed)
			s->hint(3, s->_nedPos.x, s->_nedPos.y, xform);
	}
	if (id == 0x270d)
		s->hint(2, s->_nedPos.x, s->_nedPos.y, xform);
	if (id == 0x457)
		s->_cameraFrozen = 1;
	if (id == 0x458)
		s->_cameraFrozen = 0;
	return 0;
}

// 0x41ce50 (+0x08): the area is 6; the direction found is dropped
void Scene5::hookFindTarget(void *user, int x, int y, int16 *a, int16 *b, int32 rect[4]) {
	Scene5 *s = (Scene5 *)user;
	*a = 6;
	int16 dir = 6;
	s->findTarget(dir, *b, rect, s->_scrollX, s->_scrollY);
}

// 0x41cd20 (+0x0c): the lever (0x52 down, 0x6c up), the outhouse door from its left
// facing right (0x53 shuts it, 0xb7 opens it), a falling rat (0xdc..0xde)
int Scene5::hookHitTarget(void *user, int x, int y, int16 *a, int16 *b, const int32 rect[4]) {
	Scene5 *s = (Scene5 *)user;
	switch (*b) {
	case 0x52:
		s->_leverWant = 1;
		break;
	case 0x6c:
		s->_leverWant = 3;
		break;
	case 0x53:
		if (s->_nedPos.x < 0x101b && s->ned()->getFacing() != 0)
			s->_doorWant = 3;
		break;
	case 0xb7:
		if (s->_nedPos.x < 0x101b && s->ned()->getFacing() != 0)
			s->_doorWant = 1;
		break;
	case 0xdc:
	case 0xdd:
	case 0xde:
		s->_ratZapped = 1;
		break;
	default:
		break;
	}
	return 1;
}

} // End of namespace NightmareNed
