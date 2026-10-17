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

namespace NightmareNed {

const PlainGameDescriptor nightmareNedGames[] = {
	{"nightmarened", "Nightmare Ned"},
	{ 0, 0 }
};

const ADGameDescription gameDescriptions[] = {
	// US Windows CD (September 1997). NITENED.EXE, NED01000.NAF and NED99000.NAF
	// only exist inside the InstallShield 3 archive DATA\DATA32.Z.
	{
		"nightmarened",
		nullptr,
		AD_ENTRY2s("data/data32.z", "b93005807c599410e8751a7812590c47", 59623139,
				   "data/data/NED00000.NAF", "4b8407b2ce05e939e66ed93017566662", 7880490),
		Common::EN_USA,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace NightmareNed
