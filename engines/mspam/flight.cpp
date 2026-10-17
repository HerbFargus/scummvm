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

#include "mspam/detection.h"
#include "mspam/runtime.h"

// The title's flight path object (MSBSOLAR.DLL FUN_1008_9d5c-b58a). The title
// only uses its moons (CMOON::OnEvent, 1008:b058): SE-Mercury's Mercury and
// Earth go round the Sun, and their candles, cakes and numbers ride along as
// shadow sprites. The bus actions (Set Bus, Bus Thruster, Bonk Sprite, Bus
// Settings and the 'm', 't' and 'x' parameters) are never used.

namespace MSPAM {

static const double kTwoPi = 6.28318530717959;
static const double kRadiansScale = 0.0001; // the actions give radians * 10000

static int32 paramInt(const Action &a, uint i) {
	return i < a.params.size() ? (int32)a.params[i].value : 0;
}

static Common::String paramStr(const Action &a, uint i) {
	return i < a.params.size() ? a.params[i].str : Common::String();
}

// FUN_1008_af4c: a period of n ticks per orbit, negative to go backwards
static void setPeriod(FlightMoon &moon, int32 period) {
	moon.forward = period >= 0;
	moon.step = period ? kTwoPi / period : 0.0;
}

// GENTITLE::DOACTION: every FP action but Open needs an open flight path
void Runtime::flightAction(const Action &a) {
	if (a.id == 0x03) {
		// FP: Open (FUN_1008_9d5c)
		delete _flight;
		_flight = new Flight();
		_flight->layout = getLayoutName();
		return;
	}
	if (!_flight) {
		debugC(1, kDebugActions, "No flight path for %s", getActionName(a, _layout->layout).c_str());
		return;
	}
	FlightMoon *moon = _flight->current >= 0 ? &_flight->moons[_flight->current] : nullptr;
	switch (a.id) {
	case 0x04:
		// FP: Close (FUN_1008_9de4): the moons stop and go
		delete _flight;
		_flight = nullptr;
		break;
	case 0x05:
		// FP: Set Gravity (FUN_1008_9ea4): the constant and frequency only pull the bus
		_flight->gravityX = paramInt(a, 0);
		_flight->gravityY = paramInt(a, 1);
		break;
	case 0x06:
		setMoon(paramStr(a, 0));
		break;
	case 0x07:
		startStopFlight();
		break;
	case 0x08:
		flightParam(a);
		break;
	case 0x11:
		// FP: Set Shadow Sprite (FUN_1008_ae7c): a sprite kept at an offset from the moon
		if (moon) {
			FlightMoon::Shadow shadow;
			shadow.name = paramStr(a, 0);
			shadow.dx = paramInt(a, 1);
			shadow.dy = paramInt(a, 2);
			shadow.dz = paramInt(a, 3);
			moon->shadows.push_back(shadow);
		}
		break;
	case 0x14:
		// FP: Set Radians Sprite (FUN_1008_ad90): an expression for when the moon passes an angle
		if (moon) {
			FlightMoon::RadiansExpr r;
			r.radians = paramInt(a, 0) * kRadiansScale;
			r.expr = paramStr(a, 1);
			moon->radiansExprs.push_back(r);
		}
		break;
	default:
		unimplemented(a, "bus flight");
		break;
	}
}

// FUN_1008_4842: FP: General params on the current moon, by the first letter
// of "OrDrPtClsInSpRsTh"
void Runtime::flightParam(const Action &a) {
	Common::String which = paramStr(a, 0);
	int32 first = paramInt(a, 1), second = paramInt(a, 2);
	FlightMoon *moon = _flight->current >= 0 ? &_flight->moons[_flight->current] : nullptr;
	char c = which.empty() ? 0 : which[0];
	if (c >= 'A' && c <= 'Z')
		c += 'a' - 'A';
	if (c == 'm' || c == 't' || c == 'x') {
		debugC(1, kDebugActions, "FP: General params '%c' only applies to the bus", c);
		return;
	}
	if (!moon) {
		debugC(1, kDebugActions, "FP: General params without a moon");
		return;
	}
	switch (c) {
	case 'c':
		// Stored by the original but never read
		break;
	case 'd':
		moon->delay = first;
		break;
	case 'i':
		moon->angle = first * kRadiansScale;
		break;
	case 'p':
		setPeriod(*moon, first);
		break;
	case 'r':
		moon->skipTo = first * kRadiansScale;
		break;
	case 's':
		moon->skipFrom = first * kRadiansScale;
		break;
	default:
		// 'o' and anything unknown: the orbit's radii
		moon->radiusX = first;
		moon->radiusY = second;
		break;
	}
}

// FUN_1008_a19e: picks the moon with the name, or makes one round the gravity
// point with the defaults (a circle of radius 200, 200 ticks of 50 ms per orbit)
void Runtime::setMoon(const Common::String &name) {
	for (uint i = 0; i < _flight->moons.size(); i++) {
		if (_flight->moons[i].name.equalsIgnoreCase(name)) {
			_flight->current = i;
			return;
		}
	}
	if (findElementState(name) < 0) {
		warning("%s: FP: Set Moon of no element '%s'", getLayoutName().c_str(), name.c_str());
		return;
	}
	FlightMoon moon;
	moon.name = name;
	moon.centerX = _flight->gravityX;
	moon.centerY = _flight->gravityY;
	moon.radiusX = 200.0;
	moon.radiusY = 0.0;
	moon.angle = 0.0;
	setPeriod(moon, 200);
	moon.skipFrom = kTwoPi;
	moon.skipTo = 0.0;
	moon.delay = 50;
	moon.ticking = false;
	moon.lastTick = 0;
	_flight->moons.insert_at(0, moon);
	_flight->current = 0;
}

// FUN_1008_a4a6: starts every moon's time event, or stops them all
void Runtime::startStopFlight() {
	for (FlightMoon &moon : _flight->moons) {
		moon.ticking = !_flight->started;
		moon.lastTick = _now;
	}
	_flight->started = !_flight->started;
}

// Each moon's time event fires every 'd' ms; one that is late fires once and
// starts again from there, as MSPAM reschedules its periodic events
void Runtime::updateFlight() {
	if (!_flight || !_flight->layout.equalsIgnoreCase(getLayoutName()))
		return;
	for (uint i = 0; _flight && i < _flight->moons.size(); i++) {
		FlightMoon &moon = _flight->moons[i];
		uint32 delay = MAX<uint32>(moon.delay, 1);
		if (!moon.ticking || _now - moon.lastTick < delay)
			continue;
		moon.lastTick = _now - moon.lastTick < 2 * delay ? moon.lastTick + delay : _now;
		Common::String name = moon.name;
		moonTick(moon);
		// An expression may have closed the flight path
		if (!_flight || i >= _flight->moons.size() || _flight->moons[i].name != name)
			break;
	}
}

// CMOON::OnEvent (1008:b058): one step round the orbit
void Runtime::moonTick(FlightMoon &moon) {
	moon.angle += moon.step;

	// The expressions of the angles passed in this step
	for (uint i = 0; i < moon.radiansExprs.size(); i++) {
		double r = moon.radiansExprs[i].radians;
		bool passed = moon.forward ? (r <= moon.angle && moon.angle < moon.step + r) :
			(moon.angle <= r && r + moon.step < moon.angle);
		if (passed) {
			Common::String expr = moon.radiansExprs[i].expr;
			debugC(2, kDebugActions, "%s passes %.4f: %s", moon.name.c_str(), r, expr.c_str());
			_eval->execute(expr);
			if (!_flight)
				return;
		}
	}

	// The skipped range, then the wrap (to 0, not by 2 pi)
	if (moon.forward) {
		if (moon.skipFrom <= moon.angle && moon.angle < moon.skipTo)
			moon.angle = moon.skipTo;
		if (!(moon.angle < kTwoPi))
			moon.angle = 0.0;
	} else {
		if (moon.angle <= moon.skipFrom && moon.angle > moon.skipTo)
			moon.angle = moon.skipTo;
		if (!(moon.angle + kTwoPi > 0.0))
			moon.angle = 0.0;
	}

	// x = sin, y = cos: angle 0 is in front of the gravity point, 3 pi / 2 left of it
	double radiusY = moon.radiusY == 0.0 ? moon.radiusX : moon.radiusY;
	int32 x = (int32)(sin(moon.angle) * moon.radiusX + moon.centerX + 0.5);
	int32 y = (int32)(cos(moon.angle) * radiusY + moon.centerY + 0.5);

	int el = findElementState(moon.name);
	if (el < 0)
		return;
	int32 px, py, pz;
	getSpritePosition(el, px, py, pz);
	if (px == x && py == y)
		return;
	for (const FlightMoon::Shadow &s : moon.shadows) {
		int shadow = findElementState(s.name);
		if (shadow >= 0)
			setSpritePosition(shadow, x + s.dx, y + s.dy, pz + s.dz);
	}
	setSpritePosition(el, x, y, pz);
}

// ISprite::GetPosition and SetPosition, the same position as the expressions' .x .y .z
void Runtime::getSpritePosition(int el, int32 &x, int32 &y, int32 &z) const {
	const ElementState &e = _layout->elements[el];
	if (e.def->isVideo()) {
		x = e.x;
		y = e.y;
		z = e.z;
		return;
	}
	getPathPoint(e, x, y, z);
}

void Runtime::setSpritePosition(int el, int32 x, int32 y, int32 z) {
	ElementState &e = _layout->elements[el];
	int32 px, py, pz;
	getSpritePosition(el, px, py, pz);
	e.x += x - px;
	e.y += y - py;
	e.z += z - pz;
}

} // End of namespace MSPAM
