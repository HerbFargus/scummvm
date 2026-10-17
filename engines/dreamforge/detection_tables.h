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

namespace DreamForge {

const PlainGameDescriptor dreamForgeGames[] = {
	{"devilmanor", "101 Dalmatians: Escape from DeVil Manor"},
	{ 0, 0 }
};

const ADGameDescription gameDescriptions[] = {
	// CD, v1.00: C.PRG is the game, DATA.Z holds PIG0/2/5/6.USR
	{
		"devilmanor",
		nullptr,
		AD_ENTRY2s("101D/C.PRG", "54a1e8749448e08086a1929510ec4b6a", 993517,
				   "DATA.Z", "a2939a73f4aebef645b6a6d535f8d717", 5929229),
		Common::EN_USA,
		Common::kPlatformDOS,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NOMIDI)
	},

	// The same CD after USELOW.BAT swapped in C.AMB (same game code, other sound glue)
	{
		"devilmanor",
		"Low sound",
		AD_ENTRY2s("101D/C.PRG", "54a1e8749448e08086a1929510ec4b6a", 993485,
				   "DATA.Z", "a2939a73f4aebef645b6a6d535f8d717", 5929229),
		Common::EN_USA,
		Common::kPlatformDOS,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace DreamForge
