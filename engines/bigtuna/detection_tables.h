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

namespace BigTuna {

const PlainGameDescriptor bigTunaGames[] = {
	{"jmmmom", "Just Me and My Mom"},
	{"smellymystery", "Little Monster: The Smelly Mystery"},
	{"greatrace", "Little Critter and the Great Race"},
	{"mummymystery", "Little Monster: The Mummy Mystery"},
	{ 0, 0 }
};

const ADGameDescription gameDescriptions[] = {
	{
		"jmmmom",
		nullptr,
		AD_ENTRY2s("MOM.EXE", "39ea4bd8e17dfbaf7e26b914e20d9c76", 127172,
				   "INTRO.IM2", "5044194aeba6751fb34ffc759b4f5b1f", 371564),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE | GF_IM2_V1,
		GUIO1(GUIO_NOMIDI)
	},

	{
		"smellymystery",
		nullptr,
		AD_ENTRY2s("SMELLY.EXE", "7209e2f69a889d94ded5b6c5dc686f58", 166408,
				   "INTRO.IM2", "6b3311513dbe05e723bfdd21894cd3ff", 1498250),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NOMIDI)
	},

	{
		"greatrace",
		nullptr,
		AD_ENTRY2s("TGR.EXE", "bc6263d64c2ffbdad694ccaa103c39e2", 87552,
				   "INTRO.IM2", "a5c695c45da7f20a6a9ec62a37959cbc", 4970494),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NOMIDI)
	},

	{
		"mummymystery",
		nullptr,
		AD_ENTRY2s("LMMM.EXE", "c269fac1ed9c696389d4ed83fe0deea4", 87552,
				   "INTRO.IM2", "e0bb0b21072731be6fe3432cc448945b", 6221900),
		Common::EN_ANY,
		Common::kPlatformWindows,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NOMIDI)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace BigTuna
