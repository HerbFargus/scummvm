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

#include "common/util.h"

#include "yaga/python/object.h"
#include "yaga/python/internal.h"

// The _sre module: the matching engine behind Python 2.2's re. The pure
// Python sre_compile turns a pattern into opcodes and this runs them.
// The engine follows SRE 2.2.1's recursive matcher step for step, including
// its quirks (lastindex is not restored on backtracking, marks are not reset
// between search positions), but keeps its frames on an explicit stack.

namespace Yaga {
namespace Python {

enum {
	kOpFailure, kOpSuccess, kOpAny, kOpAnyAll, kOpAssert, kOpAssertNot, kOpAt, kOpBranch,
	kOpCall, kOpCategory, kOpCharset, kOpBigCharset, kOpGroupref, kOpGrouprefIgnore,
	kOpIn, kOpInIgnore, kOpInfo, kOpJump, kOpLiteral, kOpLiteralIgnore, kOpMark,
	kOpMaxUntil, kOpMinUntil, kOpNotLiteral, kOpNotLiteralIgnore, kOpNegate, kOpRange,
	kOpRepeat, kOpRepeatOne, kOpSubpattern
};

enum {
	kAtBeginning, kAtBeginningLine, kAtBeginningString, kAtBoundary, kAtNonBoundary,
	kAtEnd, kAtEndLine, kAtEndString, kAtLocBoundary, kAtLocNonBoundary,
	kAtUniBoundary, kAtUniNonBoundary
};

enum {
	kCatDigit, kCatNotDigit, kCatSpace, kCatNotSpace, kCatWord, kCatNotWord,
	kCatLinebreak, kCatNotLinebreak, kCatLocWord, kCatLocNotWord, kCatUniDigit,
	kCatUniNotDigit, kCatUniSpace, kCatUniNotSpace, kCatUniWord, kCatUniNotWord,
	kCatUniLinebreak, kCatUniNotLinebreak
};

enum {
	kFlagIgnoreCase = 2,
	kFlagLocale = 4,
	kFlagUnicode = 32
};

enum {
	kInfoPrefix = 1,
	kInfoLiteral = 2,
	kInfoCharset = 4
};

enum {
	kErrIllegal = -1,
	kErrState = -2,
	kErrRecursion = -3
};

static const int32 kMagic = 20010701;
static const uint32 kMaxRepeat = 65535;
static const int kMarkSize = 200;
static const int32 kIntMax = 0x7fffffff;

// python22.dll checks the C stack every 10 levels; with the default stack
// this is the first level at which it gives up
static const int32 kStackLevels = 16160;

// ---- Character classes ------------------------------------------------------------

static bool isLinebreak(uint32 c) {
	return c == '\n';
}

static bool isDigit(uint32 c) {
	return c >= '0' && c <= '9';
}

static bool isSpace(uint32 c) {
	return c == ' ' || (c >= 9 && c <= 13);
}

static bool isAlnum(uint32 c) {
	return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool isWord(uint32 c) {
	return isAlnum(c) || c == '_';
}

static uint32 lowerAscii(uint32 c) {
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

// The parts of Python 2.2's Unicode database that SRE uses

struct CharRange {
	uint32 first, last;
};

static bool inRanges(const CharRange *r, uint32 c) {
	for (; r->first; r++)
		if (c >= r->first && c <= r->last)
			return true;
	return false;
}

static const CharRange g_uniDigits[] = {
	{ 0xb2, 0xb3 }, { 0xb9, 0xb9 }, { 0x660, 0x669 }, { 0x6f0, 0x6f9 }, { 0x966, 0x96f },
	{ 0x9e6, 0x9ef }, { 0xa66, 0xa6f }, { 0xae6, 0xaef }, { 0xb66, 0xb6f }, { 0xbe7, 0xbef },
	{ 0xc66, 0xc6f }, { 0xce6, 0xcef }, { 0xd66, 0xd6f }, { 0xe50, 0xe59 }, { 0xed0, 0xed9 },
	{ 0xf20, 0xf29 }, { 0x1040, 0x1049 }, { 0x1369, 0x1371 }, { 0x17e0, 0x17e9 }, { 0x1810, 0x1819 },
	{ 0x2070, 0x2070 }, { 0x2074, 0x2079 }, { 0x2080, 0x2089 }, { 0x2460, 0x2468 }, { 0x2474, 0x247c },
	{ 0x2488, 0x2490 }, { 0x24ea, 0x24ea }, { 0x2776, 0x277e }, { 0x2780, 0x2788 }, { 0x278a, 0x2792 },
	{ 0xff10, 0xff19 }, { 0, 0 }
};

static const CharRange g_uniSpaces[] = {
	{ 0x09, 0x0d }, { 0x1c, 0x20 }, { 0x85, 0x85 }, { 0xa0, 0xa0 }, { 0x1680, 0x1680 },
	{ 0x2000, 0x200b }, { 0x2028, 0x2029 }, { 0x202f, 0x202f }, { 0x3000, 0x3000 }, { 0, 0 }
};

struct LowerRun {
	uint32 first, last;
	int32 delta;
	uint32 step;
};

// Py_UNICODE_ISALNUM and Py_UNICODE_TOLOWER above ASCII
static const CharRange g_uniAlnum[] = {
	{ 0xaa, 0xaa }, { 0xb2, 0xb3 }, { 0xb5, 0xb5 }, { 0xb9, 0xba }, { 0xbc, 0xbe }, { 0xc0, 0xd6 },
	{ 0xd8, 0xf6 }, { 0xf8, 0x21f }, { 0x222, 0x233 }, { 0x250, 0x2ad }, { 0x2b0, 0x2b8 }, { 0x2bb, 0x2c1 },
	{ 0x2d0, 0x2d1 }, { 0x2e0, 0x2e4 }, { 0x2ee, 0x2ee }, { 0x37a, 0x37a }, { 0x386, 0x386 }, { 0x388, 0x38a },
	{ 0x38c, 0x38c }, { 0x38e, 0x3a1 }, { 0x3a3, 0x3ce }, { 0x3d0, 0x3d7 }, { 0x3da, 0x3f3 }, { 0x400, 0x481 },
	{ 0x48c, 0x4c4 }, { 0x4c7, 0x4c8 }, { 0x4cb, 0x4cc }, { 0x4d0, 0x4f5 }, { 0x4f8, 0x4f9 }, { 0x531, 0x556 },
	{ 0x559, 0x559 }, { 0x561, 0x587 }, { 0x5d0, 0x5ea }, { 0x5f0, 0x5f2 }, { 0x621, 0x63a }, { 0x640, 0x64a },
	{ 0x660, 0x669 }, { 0x671, 0x6d3 }, { 0x6d5, 0x6d5 }, { 0x6e5, 0x6e6 }, { 0x6f0, 0x6fc }, { 0x710, 0x710 },
	{ 0x712, 0x72c }, { 0x780, 0x7a5 }, { 0x905, 0x939 }, { 0x93d, 0x93d }, { 0x950, 0x950 }, { 0x958, 0x961 },
	{ 0x966, 0x96f }, { 0x985, 0x98c }, { 0x98f, 0x990 }, { 0x993, 0x9a8 }, { 0x9aa, 0x9b0 }, { 0x9b2, 0x9b2 },
	{ 0x9b6, 0x9b9 }, { 0x9dc, 0x9dd }, { 0x9df, 0x9e1 }, { 0x9e6, 0x9f1 }, { 0x9f4, 0x9f7 }, { 0x9f9, 0x9f9 },
	{ 0xa05, 0xa0a }, { 0xa0f, 0xa10 }, { 0xa13, 0xa28 }, { 0xa2a, 0xa30 }, { 0xa32, 0xa33 }, { 0xa35, 0xa36 },
	{ 0xa38, 0xa39 }, { 0xa59, 0xa5c }, { 0xa5e, 0xa5e }, { 0xa66, 0xa6f }, { 0xa72, 0xa74 }, { 0xa85, 0xa8b },
	{ 0xa8d, 0xa8d }, { 0xa8f, 0xa91 }, { 0xa93, 0xaa8 }, { 0xaaa, 0xab0 }, { 0xab2, 0xab3 }, { 0xab5, 0xab9 },
	{ 0xabd, 0xabd }, { 0xad0, 0xad0 }, { 0xae0, 0xae0 }, { 0xae6, 0xaef }, { 0xb05, 0xb0c }, { 0xb0f, 0xb10 },
	{ 0xb13, 0xb28 }, { 0xb2a, 0xb30 }, { 0xb32, 0xb33 }, { 0xb36, 0xb39 }, { 0xb3d, 0xb3d }, { 0xb5c, 0xb5d },
	{ 0xb5f, 0xb61 }, { 0xb66, 0xb6f }, { 0xb85, 0xb8a }, { 0xb8e, 0xb90 }, { 0xb92, 0xb95 }, { 0xb99, 0xb9a },
	{ 0xb9c, 0xb9c }, { 0xb9e, 0xb9f }, { 0xba3, 0xba4 }, { 0xba8, 0xbaa }, { 0xbae, 0xbb5 }, { 0xbb7, 0xbb9 },
	{ 0xbe7, 0xbf2 }, { 0xc05, 0xc0c }, { 0xc0e, 0xc10 }, { 0xc12, 0xc28 }, { 0xc2a, 0xc33 }, { 0xc35, 0xc39 },
	{ 0xc60, 0xc61 }, { 0xc66, 0xc6f }, { 0xc85, 0xc8c }, { 0xc8e, 0xc90 }, { 0xc92, 0xca8 }, { 0xcaa, 0xcb3 },
	{ 0xcb5, 0xcb9 }, { 0xcde, 0xcde }, { 0xce0, 0xce1 }, { 0xce6, 0xcef }, { 0xd05, 0xd0c }, { 0xd0e, 0xd10 },
	{ 0xd12, 0xd28 }, { 0xd2a, 0xd39 }, { 0xd60, 0xd61 }, { 0xd66, 0xd6f }, { 0xd85, 0xd96 }, { 0xd9a, 0xdb1 },
	{ 0xdb3, 0xdbb }, { 0xdbd, 0xdbd }, { 0xdc0, 0xdc6 }, { 0xe01, 0xe30 }, { 0xe32, 0xe33 }, { 0xe40, 0xe46 },
	{ 0xe50, 0xe59 }, { 0xe81, 0xe82 }, { 0xe84, 0xe84 }, { 0xe87, 0xe88 }, { 0xe8a, 0xe8a }, { 0xe8d, 0xe8d },
	{ 0xe94, 0xe97 }, { 0xe99, 0xe9f }, { 0xea1, 0xea3 }, { 0xea5, 0xea5 }, { 0xea7, 0xea7 }, { 0xeaa, 0xeab },
	{ 0xead, 0xeb0 }, { 0xeb2, 0xeb3 }, { 0xebd, 0xebd }, { 0xec0, 0xec4 }, { 0xec6, 0xec6 }, { 0xed0, 0xed9 },
	{ 0xedc, 0xedd }, { 0xf00, 0xf00 }, { 0xf20, 0xf29 }, { 0xf40, 0xf47 }, { 0xf49, 0xf6a }, { 0xf88, 0xf8b },
	{ 0x1000, 0x1021 }, { 0x1023, 0x1027 }, { 0x1029, 0x102a }, { 0x1040, 0x1049 }, { 0x1050, 0x1055 }, { 0x10a0, 0x10c5 },
	{ 0x10d0, 0x10f6 }, { 0x1100, 0x1159 }, { 0x115f, 0x11a2 }, { 0x11a8, 0x11f9 }, { 0x1200, 0x1206 }, { 0x1208, 0x1246 },
	{ 0x1248, 0x1248 }, { 0x124a, 0x124d }, { 0x1250, 0x1256 }, { 0x1258, 0x1258 }, { 0x125a, 0x125d }, { 0x1260, 0x1286 },
	{ 0x1288, 0x1288 }, { 0x128a, 0x128d }, { 0x1290, 0x12ae }, { 0x12b0, 0x12b0 }, { 0x12b2, 0x12b5 }, { 0x12b8, 0x12be },
	{ 0x12c0, 0x12c0 }, { 0x12c2, 0x12c5 }, { 0x12c8, 0x12ce }, { 0x12d0, 0x12d6 }, { 0x12d8, 0x12ee }, { 0x12f0, 0x130e },
	{ 0x1310, 0x1310 }, { 0x1312, 0x1315 }, { 0x1318, 0x131e }, { 0x1320, 0x1346 }, { 0x1348, 0x135a }, { 0x1369, 0x137c },
	{ 0x13a0, 0x13f4 }, { 0x1401, 0x166c }, { 0x166f, 0x1676 }, { 0x1681, 0x169a }, { 0x16a0, 0x16ea }, { 0x16ee, 0x16f0 },
	{ 0x1780, 0x17b3 }, { 0x17e0, 0x17e9 }, { 0x1810, 0x1819 }, { 0x1820, 0x1877 }, { 0x1880, 0x18a8 }, { 0x1e00, 0x1e9b },
	{ 0x1ea0, 0x1ef9 }, { 0x1f00, 0x1f15 }, { 0x1f18, 0x1f1d }, { 0x1f20, 0x1f45 }, { 0x1f48, 0x1f4d }, { 0x1f50, 0x1f57 },
	{ 0x1f59, 0x1f59 }, { 0x1f5b, 0x1f5b }, { 0x1f5d, 0x1f5d }, { 0x1f5f, 0x1f7d }, { 0x1f80, 0x1fb4 }, { 0x1fb6, 0x1fbc },
	{ 0x1fbe, 0x1fbe }, { 0x1fc2, 0x1fc4 }, { 0x1fc6, 0x1fcc }, { 0x1fd0, 0x1fd3 }, { 0x1fd6, 0x1fdb }, { 0x1fe0, 0x1fec },
	{ 0x1ff2, 0x1ff4 }, { 0x1ff6, 0x1ffc }, { 0x2070, 0x2070 }, { 0x2074, 0x2079 }, { 0x207f, 0x2089 }, { 0x2102, 0x2102 },
	{ 0x2107, 0x2107 }, { 0x210a, 0x2113 }, { 0x2115, 0x2115 }, { 0x2119, 0x211d }, { 0x2124, 0x2124 }, { 0x2126, 0x2126 },
	{ 0x2128, 0x2128 }, { 0x212a, 0x212d }, { 0x212f, 0x2131 }, { 0x2133, 0x2139 }, { 0x2153, 0x2182 }, { 0x2460, 0x249b },
	{ 0x24ea, 0x24ea }, { 0x2776, 0x2793 }, { 0x3005, 0x3007 }, { 0x3021, 0x3029 }, { 0x3031, 0x3035 }, { 0x3038, 0x303a },
	{ 0x3041, 0x3094 }, { 0x309d, 0x309e }, { 0x30a1, 0x30fa }, { 0x30fc, 0x30fe }, { 0x3105, 0x312c }, { 0x3131, 0x318e },
	{ 0x31a0, 0x31b7 }, { 0x3280, 0x3289 }, { 0x3400, 0x4db5 }, { 0x4e00, 0x9fa5 }, { 0xa000, 0xa48c }, { 0xac00, 0xd7a3 },
	{ 0xf900, 0xfa2d }, { 0xfb00, 0xfb06 }, { 0xfb13, 0xfb17 }, { 0xfb1d, 0xfb1d }, { 0xfb1f, 0xfb28 }, { 0xfb2a, 0xfb36 },
	{ 0xfb38, 0xfb3c }, { 0xfb3e, 0xfb3e }, { 0xfb40, 0xfb41 }, { 0xfb43, 0xfb44 }, { 0xfb46, 0xfbb1 }, { 0xfbd3, 0xfd3d },
	{ 0xfd50, 0xfd8f }, { 0xfd92, 0xfdc7 }, { 0xfdf0, 0xfdfb }, { 0xfe70, 0xfe72 }, { 0xfe74, 0xfe74 }, { 0xfe76, 0xfefc },
	{ 0xff10, 0xff19 }, { 0xff21, 0xff3a }, { 0xff41, 0xff5a }, { 0xff66, 0xffbe }, { 0xffc2, 0xffc7 }, { 0xffca, 0xffcf },
	{ 0xffd2, 0xffd7 }, { 0xffda, 0xffdc },
	{ 0, 0 }
};

static const LowerRun g_uniLower[] = {
	{ 0xc0, 0xd6, 32, 1 }, { 0xd8, 0xde, 32, 1 }, { 0x100, 0x12e, 1, 2 }, { 0x130, 0x130, -199, 1 },
	{ 0x132, 0x136, 1, 2 }, { 0x139, 0x147, 1, 2 }, { 0x14a, 0x176, 1, 2 }, { 0x178, 0x178, -121, 1 },
	{ 0x179, 0x17d, 1, 2 }, { 0x181, 0x181, 210, 1 }, { 0x182, 0x184, 1, 2 }, { 0x186, 0x186, 206, 1 },
	{ 0x187, 0x187, 1, 1 }, { 0x189, 0x18a, 205, 1 }, { 0x18b, 0x18b, 1, 1 }, { 0x18e, 0x18e, 79, 1 },
	{ 0x18f, 0x18f, 202, 1 }, { 0x190, 0x190, 203, 1 }, { 0x191, 0x191, 1, 1 }, { 0x193, 0x193, 205, 1 },
	{ 0x194, 0x194, 207, 1 }, { 0x196, 0x196, 211, 1 }, { 0x197, 0x197, 209, 1 }, { 0x198, 0x198, 1, 1 },
	{ 0x19c, 0x19c, 211, 1 }, { 0x19d, 0x19d, 213, 1 }, { 0x19f, 0x19f, 214, 1 }, { 0x1a0, 0x1a4, 1, 2 },
	{ 0x1a6, 0x1a6, 218, 1 }, { 0x1a7, 0x1a7, 1, 1 }, { 0x1a9, 0x1a9, 218, 1 }, { 0x1ac, 0x1ac, 1, 1 },
	{ 0x1ae, 0x1ae, 218, 1 }, { 0x1af, 0x1af, 1, 1 }, { 0x1b1, 0x1b2, 217, 1 }, { 0x1b3, 0x1b5, 1, 2 },
	{ 0x1b7, 0x1b7, 219, 1 }, { 0x1b8, 0x1b8, 1, 1 }, { 0x1bc, 0x1bc, 1, 1 }, { 0x1c4, 0x1c4, 2, 1 },
	{ 0x1c5, 0x1c5, 1, 1 }, { 0x1c7, 0x1c7, 2, 1 }, { 0x1c8, 0x1c8, 1, 1 }, { 0x1ca, 0x1ca, 2, 1 },
	{ 0x1cb, 0x1db, 1, 2 }, { 0x1de, 0x1ee, 1, 2 }, { 0x1f1, 0x1f1, 2, 1 }, { 0x1f2, 0x1f4, 1, 2 },
	{ 0x1f6, 0x1f6, -97, 1 }, { 0x1f7, 0x1f7, -56, 1 }, { 0x1f8, 0x21e, 1, 2 }, { 0x222, 0x232, 1, 2 },
	{ 0x386, 0x386, 38, 1 }, { 0x388, 0x38a, 37, 1 }, { 0x38c, 0x38c, 64, 1 }, { 0x38e, 0x38f, 63, 1 },
	{ 0x391, 0x3a1, 32, 1 }, { 0x3a3, 0x3ab, 32, 1 }, { 0x3da, 0x3ee, 1, 2 }, { 0x400, 0x40f, 80, 1 },
	{ 0x410, 0x42f, 32, 1 }, { 0x460, 0x480, 1, 2 }, { 0x48c, 0x4be, 1, 2 }, { 0x4c1, 0x4c3, 1, 2 },
	{ 0x4c7, 0x4c7, 1, 1 }, { 0x4cb, 0x4cb, 1, 1 }, { 0x4d0, 0x4f4, 1, 2 }, { 0x4f8, 0x4f8, 1, 1 },
	{ 0x531, 0x556, 48, 1 }, { 0x1e00, 0x1e94, 1, 2 }, { 0x1ea0, 0x1ef8, 1, 2 }, { 0x1f08, 0x1f0f, -8, 1 },
	{ 0x1f18, 0x1f1d, -8, 1 }, { 0x1f28, 0x1f2f, -8, 1 }, { 0x1f38, 0x1f3f, -8, 1 }, { 0x1f48, 0x1f4d, -8, 1 },
	{ 0x1f59, 0x1f5f, -8, 2 }, { 0x1f68, 0x1f6f, -8, 1 }, { 0x1f88, 0x1f8f, -8, 1 }, { 0x1f98, 0x1f9f, -8, 1 },
	{ 0x1fa8, 0x1faf, -8, 1 }, { 0x1fb8, 0x1fb9, -8, 1 }, { 0x1fba, 0x1fbb, -74, 1 }, { 0x1fbc, 0x1fbc, -9, 1 },
	{ 0x1fc8, 0x1fcb, -86, 1 }, { 0x1fcc, 0x1fcc, -9, 1 }, { 0x1fd8, 0x1fd9, -8, 1 }, { 0x1fda, 0x1fdb, -100, 1 },
	{ 0x1fe8, 0x1fe9, -8, 1 }, { 0x1fea, 0x1feb, -112, 1 }, { 0x1fec, 0x1fec, -7, 1 }, { 0x1ff8, 0x1ff9, -128, 1 },
	{ 0x1ffa, 0x1ffb, -126, 1 }, { 0x1ffc, 0x1ffc, -9, 1 }, { 0x2126, 0x2126, -7517, 1 }, { 0x212a, 0x212a, -8383, 1 },
	{ 0x212b, 0x212b, -8262, 1 }, { 0x2160, 0x216f, 16, 1 }, { 0x24b6, 0x24cf, 26, 1 }, { 0xff21, 0xff3a, 32, 1 },
	{ 0, 0, 0, 0 }
};

static bool uniIsDigit(uint32 c) {
	return isDigit(c) || (c >= 0xb2 && inRanges(g_uniDigits, c));
}

static bool uniIsSpace(uint32 c) {
	return inRanges(g_uniSpaces, c);
}

static bool uniIsLinebreak(uint32 c) {
	return c == 0x0a || c == 0x0d || (c >= 0x1c && c <= 0x1e) || c == 0x85 || c == 0x2028 || c == 0x2029;
}

static bool uniIsAlnum(uint32 c) {
	if (c < 128)
		return isAlnum(c);
	return inRanges(g_uniAlnum, c);
}

static bool uniIsWord(uint32 c) {
	return uniIsAlnum(c) || c == '_';
}

static uint32 lowerUnicode(uint32 c) {
	if (c < 128)
		return lowerAscii(c);
	for (const LowerRun *r = g_uniLower; r->first && r->first <= c; r++)
		if (c <= r->last && (c - r->first) % r->step == 0)
			return c + r->delta;
	return c;
}

static bool category(uint32 code, uint32 c) {
	switch (code) {
	case kCatDigit:
		return isDigit(c);
	case kCatNotDigit:
		return !isDigit(c);
	case kCatSpace:
		return isSpace(c);
	case kCatNotSpace:
		return !isSpace(c);
	case kCatWord:
		return isWord(c);
	case kCatNotWord:
		return !isWord(c);
	case kCatLinebreak:
		return isLinebreak(c);
	case kCatNotLinebreak:
		return !isLinebreak(c);
	case kCatLocWord:
		return c < 256 && isWord(c);
	case kCatLocNotWord:
		return !(c < 256 && isWord(c));
	case kCatUniDigit:
		return uniIsDigit(c);
	case kCatUniNotDigit:
		return !uniIsDigit(c);
	case kCatUniSpace:
		return uniIsSpace(c);
	case kCatUniNotSpace:
		return !uniIsSpace(c);
	case kCatUniWord:
		return uniIsWord(c);
	case kCatUniNotWord:
		return !uniIsWord(c);
	case kCatUniLinebreak:
		return uniIsLinebreak(c);
	case kCatUniNotLinebreak:
		return !uniIsLinebreak(c);
	default:
		return false;
	}
}

// ---- Objects --------------------------------------------------------------------------

static Type g_patternType("_sre.SRE_Pattern", &g_objectType);
static Type g_matchType("_sre.SRE_Match", &g_objectType);
static Type g_scannerType("_sre.SRE_Scanner", &g_objectType);

struct SrePattern : public Object {
	ObjRef _pattern;
	int32 _flags;
	Common::Array<uint32> _code;
	int32 _groups;
	ObjRef _groupindex; // may be null
	ObjRef _indexgroup; // may be null
	SrePattern() : Object(&g_patternType), _flags(0), _groups(0) {}
};

struct SreMatch : public Object {
	Ref<SrePattern> _pattern;
	ObjRef _string;
	ObjRef _regs;
	int32 _groups;
	int32 _pos, _endpos, _lastindex;
	Common::Array<int32> _mark;
	SreMatch() : Object(&g_matchType), _groups(0), _pos(0), _endpos(0), _lastindex(-1) {}
};

// ---- The engine -------------------------------------------------------------------------

struct SreRepeat {
	int32 count;
	uint32 pattern; // the REPEAT operands
	int32 prev;
};

// What a frame does with the result of the frame it called
enum {
	kResEntry, kResAssert, kResAssertNot, kResBranch, kResCount, kResRepOneLiteral,
	kResRepOneGeneral, kResRepeat, kResMaxUntil1, kResMaxUntil2, kResMaxUntil3,
	kResMinUntil1, kResMinUntil2, kResMinUntil3
};

struct SreFrame {
	uint32 pc;
	int32 ptr;
	int32 level;
	int resume;
	int32 count;
	int32 lastmark;
	int32 rp;
	int32 ownRep;
	int32 limit;
	uint32 chr;
};

class SreState {
public:
	ObjRef _string;
	int32 _start, _end, _pos, _endpos, _ptr;
	int32 _lastmark, _lastindex;
	int32 _mark[kMarkSize];

	SreState() : _start(0), _end(0), _pos(0), _endpos(0), _ptr(0), _lastmark(-1), _lastindex(-1),
		_bytes(nullptr), _wide(nullptr), _length(0), _code(nullptr), _codeSize(0),
		_markStackBase(0), _repeat(-1), _lowerMode(0) {
		for (int i = 0; i < kMarkSize; i++)
			_mark[i] = -1;
	}

	bool init(SrePattern *pattern, Object *string, int32 start, int32 end);
	void reset();
	int match(uint32 pc, int32 level);
	int search();

private:
	const uint8 *_bytes;
	const uint32 *_wide;
	int32 _length;
	const uint32 *_code;
	uint32 _codeSize;
	Common::Array<int32> _markStack;
	uint32 _markStackBase;
	int32 _repeat;
	int _lowerMode; // 0 ASCII, 1 locale, 2 Unicode
	Common::Array<SreRepeat> _reps;
	Common::Array<SreFrame> _frames;

	uint32 code(uint32 i) const { return i < _codeSize ? _code[i] : 0; }

	// Reads outside the string give 0, where C would read past the buffer
	uint32 ch(int32 i) const {
		if (i < 0 || i >= _length)
			return 0;
		return _bytes ? _bytes[i] : _wide[i];
	}

	uint32 lower(uint32 c) const {
		if (_lowerMode == 2)
			return lowerUnicode(c);
		return c < 128 ? lowerAscii(c) : c;
	}

	bool at(int32 ptr, uint32 atCode) const;
	bool inCharset(uint32 set, uint32 c) const;
	void clearMarks(int32 lastmark);
	void markSave(int32 lo, int32 hi);
	void markRestore(int32 lo, int32 hi);
	void setMark(uint32 i, int32 ptr) {
		if (i < (uint32)kMarkSize)
			_mark[i] = ptr;
	}
	void pushFrame(uint32 pc, int32 level);
};

bool SreState::init(SrePattern *pattern, Object *string, int32 start, int32 end) {
	if (isInstanceOf(string, &g_strType)) {
		Str *s = (Str *)string;
		_bytes = (const uint8 *)s->_data;
		_wide = nullptr;
		_length = s->_len;
	} else if (isInstanceOf(string, &g_unicodeType)) {
		Unicode *u = (Unicode *)string;
		_bytes = nullptr;
		_wide = u->_chars.data();
		_length = u->_chars.size();
	} else {
		setErrorString(g_excTypeError, "expected string or buffer");
		return false;
	}
	if (start < 0)
		start = 0;
	else if (start > _length)
		start = _length;
	if (end < 0)
		end = 0;
	else if (end > _length)
		end = _length;
	_string = string;
	_code = pattern->_code.data();
	_codeSize = pattern->_code.size();
	_start = _ptr = start;
	_end = end;
	_pos = start;
	_endpos = end;
	if (pattern->_flags & kFlagLocale)
		_lowerMode = 1;
	else if (pattern->_flags & kFlagUnicode)
		_lowerMode = 2;
	else
		_lowerMode = 0;
	reset();
	return true;
}

void SreState::reset() {
	_lastmark = -1;
	_lastindex = -1;
	for (int i = 0; i < kMarkSize; i++)
		_mark[i] = -1;
	_repeat = -1;
	_markStackBase = 0;
	_reps.clear();
	_frames.clear();
}

bool SreState::at(int32 ptr, uint32 atCode) const {
	bool thisWord, thatWord;
	switch (atCode) {
	case kAtBeginning:
	case kAtBeginningString:
		return ptr == 0;
	case kAtBeginningLine:
		return ptr == 0 || isLinebreak(ch(ptr - 1));
	case kAtEnd:
		return (ptr + 1 == _end && isLinebreak(ch(ptr))) || ptr == _end;
	case kAtEndLine:
		return ptr == _end || isLinebreak(ch(ptr));
	case kAtEndString:
		return ptr == _end;
	case kAtBoundary:
	case kAtNonBoundary:
		if (_end == 0)
			return false;
		thatWord = ptr > 0 ? isWord(ch(ptr - 1)) : false;
		thisWord = ptr < _end ? isWord(ch(ptr)) : false;
		return atCode == kAtBoundary ? thisWord != thatWord : thisWord == thatWord;
	case kAtLocBoundary:
	case kAtLocNonBoundary:
		if (_end == 0)
			return false;
		thatWord = ptr > 0 ? category(kCatLocWord, ch(ptr - 1)) : false;
		thisWord = ptr < _end ? category(kCatLocWord, ch(ptr)) : false;
		return atCode == kAtLocBoundary ? thisWord != thatWord : thisWord == thatWord;
	case kAtUniBoundary:
	case kAtUniNonBoundary:
		if (_end == 0)
			return false;
		thatWord = ptr > 0 ? uniIsWord(ch(ptr - 1)) : false;
		thisWord = ptr < _end ? uniIsWord(ch(ptr)) : false;
		return atCode == kAtUniBoundary ? thisWord != thatWord : thisWord == thatWord;
	default:
		return false;
	}
}

bool SreState::inCharset(uint32 set, uint32 c) const {
	bool ok = true;
	for (;;) {
		switch (code(set++)) {
		case kOpLiteral:
			if (c == code(set))
				return ok;
			set++;
			break;
		case kOpRange:
			if (code(set) <= c && c <= code(set + 1))
				return ok;
			set += 2;
			break;
		case kOpCharset:
			// 16 bits per code word
			if (c < 256 && (code(set + (c >> 4)) & (1 << (c & 15))))
				return ok;
			set += 16;
			break;
		case kOpBigCharset: {
			// <count> <256 block indices, two per word> <blocks>
			uint32 count = code(set++);
			uint32 word = code(set + ((c >> 8) & 0xff) / 2);
			uint32 block = (((c >> 8) & 1) ? word >> 8 : word) & 0xff;
			set += 128;
			if (c < 65536 && (code(set + block * 16 + ((c & 255) >> 4)) & (1 << (c & 15))))
				return ok;
			set += count * 16;
			break;
		}
		case kOpCategory:
			if (category(code(set), c))
				return ok;
			set++;
			break;
		case kOpNegate:
			ok = !ok;
			break;
		case kOpFailure:
			return !ok;
		default:
			return false;
		}
	}
}

void SreState::clearMarks(int32 lastmark) {
	if (_lastmark > lastmark) {
		for (int32 i = lastmark + 1; i <= _lastmark && i < kMarkSize; i++)
			_mark[i] = -1;
		_lastmark = lastmark;
	}
}

void SreState::markSave(int32 lo, int32 hi) {
	if (hi <= lo)
		return;
	uint32 size = hi - lo + 1;
	if (_markStack.size() < _markStackBase + size)
		_markStack.resize(_markStackBase + size);
	for (uint32 i = 0; i < size; i++)
		_markStack[_markStackBase + i] = (lo + (int32)i) < kMarkSize ? _mark[lo + i] : -1;
	_markStackBase += size;
}

void SreState::markRestore(int32 lo, int32 hi) {
	if (hi <= lo)
		return;
	uint32 size = hi - lo + 1;
	if (_markStackBase < size)
		return;
	_markStackBase -= size;
	for (uint32 i = 0; i < size; i++)
		if (lo + (int32)i < kMarkSize)
			_mark[lo + i] = _markStack[_markStackBase + i];
}

void SreState::pushFrame(uint32 pc, int32 level) {
	SreFrame f;
	f.pc = pc;
	f.ptr = _ptr;
	f.level = level;
	f.resume = kResEntry;
	f.count = 0;
	f.lastmark = -1;
	f.rp = -1;
	f.ownRep = -1;
	f.limit = 0;
	f.chr = 0;
	_frames.push_back(f);
}

// SRE_MATCH: matches the code at pc against the string at _ptr. Returns 1 on
// a match (with _ptr at its end), 0 on failure or a negative error. Each
// recursive call of the C version is a frame here; a frame that called
// another resumes at its 'resume' point with the result in ret.
int SreState::match(uint32 startPc, int32 startLevel) {
	uint32 base = _frames.size();
	SreFrame *f;
	int ret = 0;
	uint32 op, i, tail;
	int32 p, e, cptr, cend, min;

	pushFrame(startPc, startLevel);

enter:
	f = &_frames.back();
	if (f->level >= kStackLevels) {
		ret = kErrRecursion;
		goto leave;
	}
	if (code(f->pc) == kOpInfo) {
		// <INFO> <skip> <flags> <min> ...
		if (code(f->pc + 3) && _end - f->ptr < (int32)code(f->pc + 3)) {
			ret = 0;
			goto leave;
		}
		f->pc += code(f->pc + 1) + 1;
	}

dispatch:
	for (;;) {
		op = code(f->pc++);
		switch (op) {
		case kOpFailure:
			ret = 0;
			goto leave;

		case kOpSuccess:
			_ptr = f->ptr;
			ret = 1;
			goto leave;

		case kOpAt:
			if (!at(f->ptr, code(f->pc))) {
				ret = 0;
				goto leave;
			}
			f->pc++;
			break;

		case kOpCategory:
			if (f->ptr >= _end || !category(code(f->pc), ch(f->ptr))) {
				ret = 0;
				goto leave;
			}
			f->pc++;
			f->ptr++;
			break;

		case kOpLiteral:
			if (f->ptr >= _end || ch(f->ptr) != code(f->pc)) {
				ret = 0;
				goto leave;
			}
			f->pc++;
			f->ptr++;
			break;

		case kOpNotLiteral:
			if (f->ptr >= _end || ch(f->ptr) == code(f->pc)) {
				ret = 0;
				goto leave;
			}
			f->pc++;
			f->ptr++;
			break;

		case kOpAny:
			if (f->ptr >= _end || isLinebreak(ch(f->ptr))) {
				ret = 0;
				goto leave;
			}
			f->ptr++;
			break;

		case kOpAnyAll:
			if (f->ptr >= _end) {
				ret = 0;
				goto leave;
			}
			f->ptr++;
			break;

		case kOpIn:
			if (f->ptr >= _end || !inCharset(f->pc + 1, ch(f->ptr))) {
				ret = 0;
				goto leave;
			}
			f->pc += code(f->pc);
			f->ptr++;
			break;

		case kOpGroupref:
		case kOpGrouprefIgnore:
			i = code(f->pc);
			p = i + i < (uint32)kMarkSize ? _mark[i + i] : -1;
			e = i + i + 1 < (uint32)kMarkSize ? _mark[i + i + 1] : -1;
			if (p < 0 || e < 0 || e < p) {
				ret = 0;
				goto leave;
			}
			while (p < e) {
				if (f->ptr >= _end || (op == kOpGroupref ? ch(f->ptr) != ch(p) : lower(ch(f->ptr)) != lower(ch(p)))) {
					ret = 0;
					goto leave;
				}
				p++;
				f->ptr++;
			}
			f->pc++;
			break;

		case kOpLiteralIgnore:
			if (f->ptr >= _end || lower(ch(f->ptr)) != lower(code(f->pc))) {
				ret = 0;
				goto leave;
			}
			f->pc++;
			f->ptr++;
			break;

		case kOpNotLiteralIgnore:
			if (f->ptr >= _end || lower(ch(f->ptr)) == lower(code(f->pc))) {
				ret = 0;
				goto leave;
			}
			f->pc++;
			f->ptr++;
			break;

		case kOpInIgnore:
			if (f->ptr >= _end || !inCharset(f->pc + 1, lower(ch(f->ptr)))) {
				ret = 0;
				goto leave;
			}
			f->pc += code(f->pc);
			f->ptr++;
			break;

		case kOpMark:
			i = code(f->pc);
			if (i & 1)
				_lastindex = i / 2 + 1;
			if ((int32)i > _lastmark)
				_lastmark = i;
			setMark(i, f->ptr);
			f->pc++;
			break;

		case kOpJump:
		case kOpInfo:
			f->pc += code(f->pc);
			break;

		case kOpAssert:
			// <ASSERT> <skip> <back> <pattern>
			_ptr = f->ptr - code(f->pc + 1);
			if (_ptr < 0) {
				ret = 0;
				goto leave;
			}
			f->resume = kResAssert;
			pushFrame(f->pc + 2, f->level + 1);
			goto enter;

		case kOpAssertNot:
			_ptr = f->ptr - code(f->pc + 1);
			if (_ptr >= 0) {
				f->resume = kResAssertNot;
				pushFrame(f->pc + 2, f->level + 1);
				goto enter;
			}
			f->pc += code(f->pc);
			break;

		case kOpBranch:
			// <BRANCH> <skip> code <JUMP> ... <0>
			f->lastmark = _lastmark;
			goto branchLoop;

		case kOpRepeatOne:
			// <REPEAT_ONE> <skip> <min> <max> item <SUCCESS> tail
			if (f->ptr + (int32)code(f->pc + 1) > _end) {
				ret = 0;
				goto leave;
			}
			_ptr = f->ptr;
			// SRE_COUNT
			cptr = f->ptr;
			cend = _end;
			if ((int32)code(f->pc + 2) < cend - cptr && code(f->pc + 2) != kMaxRepeat)
				cend = cptr + code(f->pc + 2);
			i = code(f->pc + 4);
			switch (code(f->pc + 3)) {
			case kOpAny:
				while (cptr < cend && !isLinebreak(ch(cptr)))
					cptr++;
				break;
			case kOpAnyAll:
				cptr = cend;
				break;
			case kOpLiteral:
				while (cptr < cend && ch(cptr) == i)
					cptr++;
				break;
			case kOpLiteralIgnore:
				while (cptr < cend && lower(ch(cptr)) == i)
					cptr++;
				break;
			case kOpNotLiteral:
				while (cptr < cend && ch(cptr) != i)
					cptr++;
				break;
			case kOpNotLiteralIgnore:
				while (cptr < cend && lower(ch(cptr)) != i)
					cptr++;
				break;
			case kOpIn:
				while (cptr < cend && inCharset(f->pc + 5, ch(cptr)))
					cptr++;
				break;
			default:
				// Any other single character item runs as a match of its own
				f->limit = cend;
				goto countLoop;
			}
			f->count = cptr - _ptr;
			goto repeatOneCounted;

		case kOpRepeat: {
			// <REPEAT> <skip> <min> <max> item <UNTIL> tail
			SreRepeat rep;
			rep.count = -1;
			rep.pattern = f->pc;
			rep.prev = _repeat;
			f->ownRep = _reps.size();
			_reps.push_back(rep);
			_repeat = f->ownRep;
			_ptr = f->ptr;
			f->resume = kResRepeat;
			pushFrame(f->pc + code(f->pc), f->level + 1);
			goto enter;
		}

		case kOpMaxUntil:
			if (_repeat < 0) {
				ret = kErrState;
				goto leave;
			}
			f->rp = _repeat;
			_ptr = f->ptr;
			f->count = _reps[f->rp].count + 1;
			i = _reps[f->rp].pattern;
			if (f->count < (int32)code(i + 1)) {
				// Not enough matches yet
				_reps[f->rp].count = f->count;
				f->resume = kResMaxUntil1;
				pushFrame(i + 3, f->level + 1);
				goto enter;
			}
			if (f->count < (int32)code(i + 2) || code(i + 2) == kMaxRepeat) {
				// Try one more item
				_reps[f->rp].count = f->count;
				f->lastmark = _lastmark;
				markSave(0, f->lastmark);
				f->resume = kResMaxUntil2;
				pushFrame(i + 3, f->level + 1);
				goto enter;
			}
			goto maxUntilTail;

		case kOpMinUntil:
			if (_repeat < 0) {
				ret = kErrState;
				goto leave;
			}
			f->rp = _repeat;
			f->count = _reps[f->rp].count + 1;
			_ptr = f->ptr;
			i = _reps[f->rp].pattern;
			if (f->count < (int32)code(i + 1)) {
				_reps[f->rp].count = f->count;
				f->resume = kResMinUntil1;
				pushFrame(i + 3, f->level + 1);
				goto enter;
			}
			// Try the tail first
			_repeat = _reps[f->rp].prev;
			f->resume = kResMinUntil2;
			pushFrame(f->pc, f->level + 1);
			goto enter;

		default:
			ret = kErrIllegal;
			goto leave;
		}
	}

branchLoop:
	while (code(f->pc)) {
		i = f->pc + 1;
		if ((code(i) == kOpLiteral && (f->ptr >= _end || ch(f->ptr) != code(i + 1))) ||
			(code(i) == kOpIn && (f->ptr >= _end || !inCharset(i + 2, ch(f->ptr))))) {
			f->pc += code(f->pc);
			continue;
		}
		_ptr = f->ptr;
		f->resume = kResBranch;
		pushFrame(i, f->level + 1);
		goto enter;
	}
	ret = 0;
	goto leave;

countLoop:
	if (_ptr < f->limit) {
		f->resume = kResCount;
		pushFrame(f->pc + 3, f->level + 1);
		goto enter;
	}
	f->count = _ptr - f->ptr;

repeatOneCounted:
	f->ptr += f->count;
	if (f->count < (int32)code(f->pc + 1)) {
		ret = 0;
		goto leave;
	}
	tail = f->pc + code(f->pc);
	if (code(tail) == kOpSuccess) {
		// Nothing follows
		_ptr = f->ptr;
		ret = 1;
		goto leave;
	}
	if (code(tail) == kOpLiteral) {
		// Skip positions where the literal that follows cannot match
		f->chr = code(tail + 1);
		goto repeatOneLiteral;
	}
	f->lastmark = _lastmark;
	goto repeatOneGeneral;

repeatOneLiteral:
	min = code(f->pc + 1);
	while (f->count >= min && (f->ptr >= _end || ch(f->ptr) != f->chr)) {
		f->ptr--;
		f->count--;
	}
	if (f->count < min) {
		ret = 0;
		goto leave;
	}
	_ptr = f->ptr;
	f->resume = kResRepOneLiteral;
	pushFrame(f->pc + code(f->pc), f->level + 1);
	goto enter;

repeatOneGeneral:
	if (f->count >= (int32)code(f->pc + 1)) {
		_ptr = f->ptr;
		f->resume = kResRepOneGeneral;
		pushFrame(f->pc + code(f->pc), f->level + 1);
		goto enter;
	}
	ret = 0;
	goto leave;

maxUntilTail:
	// No more items here: the rest of the pattern must match
	_repeat = _reps[f->rp].prev;
	f->resume = kResMaxUntil3;
	pushFrame(f->pc, f->level + 1);
	goto enter;

leave:
	if (_frames.back().ownRep >= 0)
		_reps.resize(_frames.back().ownRep);
	_frames.pop_back();
	if (_frames.size() == base)
		return ret;
	f = &_frames.back();
	switch (f->resume) {
	case kResAssert:
		if (ret <= 0)
			goto leave;
		f->pc += code(f->pc);
		goto dispatch;

	case kResAssertNot:
		if (ret < 0)
			goto leave;
		if (ret) {
			ret = 0;
			goto leave;
		}
		f->pc += code(f->pc);
		goto dispatch;

	case kResBranch:
		if (ret)
			goto leave;
		clearMarks(f->lastmark);
		f->pc += code(f->pc);
		goto branchLoop;

	case kResCount:
		if (ret < 0)
			goto leave;
		if (!ret) {
			f->count = _ptr - f->ptr;
			goto repeatOneCounted;
		}
		goto countLoop;

	case kResRepOneLiteral:
		if (ret)
			goto leave;
		f->ptr--;
		f->count--;
		goto repeatOneLiteral;

	case kResRepOneGeneral:
		if (ret)
			goto leave;
		f->ptr--;
		f->count--;
		clearMarks(f->lastmark);
		goto repeatOneGeneral;

	case kResRepeat:
		_repeat = _reps[f->ownRep].prev;
		goto leave;

	case kResMaxUntil1:
	case kResMinUntil1:
	case kResMinUntil3:
		if (ret)
			goto leave;
		_reps[f->rp].count = f->count - 1;
		_ptr = f->ptr;
		goto leave;

	case kResMaxUntil2:
		if (ret)
			goto leave;
		markRestore(0, f->lastmark);
		_lastmark = f->lastmark;
		_reps[f->rp].count = f->count - 1;
		_ptr = f->ptr;
		goto maxUntilTail;

	case kResMaxUntil3:
		if (ret)
			goto leave;
		_repeat = f->rp;
		_ptr = f->ptr;
		goto leave;

	case kResMinUntil2:
		if (ret)
			goto leave;
		_ptr = f->ptr;
		_repeat = f->rp;
		i = _reps[f->rp].pattern;
		if (f->count >= (int32)code(i + 2) && code(i + 2) != kMaxRepeat)
			goto leave;
		_reps[f->rp].count = f->count;
		f->resume = kResMinUntil3;
		pushFrame(i + 3, f->level + 1);
		goto enter;

	default:
		ret = kErrIllegal;
		goto leave;
	}
}

// SRE_SEARCH: finds the first match from _start on
int SreState::search() {
	int32 ptr = _start;
	int32 end = _end;
	int status = 0;
	int32 prefixLen = 0, prefixSkip = 0;
	uint32 prefix = 0, overlap = 0, charset = 0;
	bool haveCharset = false;
	uint32 flags = 0;
	uint32 pc = 0;

	if (code(0) == kOpInfo) {
		// <INFO> <skip> <flags> <min> <max> <prefix info>
		flags = code(2);
		if (code(3) > 0) {
			// Leave at least one character so the literal search works
			end -= code(3) - 1;
			if (end <= ptr)
				end = ptr + 1;
		}
		if (flags & kInfoPrefix) {
			// <length> <skip> <prefix data> <overlap data>
			prefixLen = code(5);
			prefixSkip = code(6);
			prefix = 7;
			overlap = prefix + prefixLen - 1;
		} else if (flags & kInfoCharset) {
			charset = 5;
			haveCharset = true;
		}
		pc = 1 + code(1);
	}

	if (prefixLen > 1) {
		// Use the overlap table to skip forward through the prefix
		int32 i = 0;
		end = _end;
		while (ptr < end) {
			for (;;) {
				if (ch(ptr) != code(prefix + i)) {
					if (!i)
						break;
					i = code(overlap + i);
				} else {
					if (++i == prefixLen) {
						_start = ptr + 1 - prefixLen;
						_ptr = ptr + 1 - prefixLen + prefixSkip;
						if (flags & kInfoLiteral)
							return 1;
						status = match(pc + 2 * prefixSkip, 1);
						if (status != 0)
							return status;
						i = code(overlap + i);
					}
					break;
				}
			}
			ptr++;
		}
		return 0;
	}

	if (code(pc) == kOpLiteral) {
		uint32 chr = code(pc + 1);
		end = _end;
		for (;;) {
			while (ptr < end && ch(ptr) != chr)
				ptr++;
			// C would run off the string when pos > endpos
			if (ptr == end || ptr > _length)
				return 0;
			_start = ptr;
			_ptr = ++ptr;
			status = match(pc + 2, 1);
			if (status != 0)
				break;
		}
	} else if (haveCharset) {
		end = _end;
		for (;;) {
			while (ptr < end && !inCharset(charset, ch(ptr)))
				ptr++;
			if (ptr == end || ptr > _length)
				return 0;
			_start = ptr;
			_ptr = ptr;
			status = match(pc, 1);
			if (status != 0)
				break;
			ptr++;
		}
	} else {
		while (ptr <= end) {
			_start = _ptr = ptr++;
			status = match(pc, 1);
			if (status != 0)
				break;
		}
	}
	return status;
}

// ---- Helpers ------------------------------------------------------------------------------

static void patternError(int status) {
	if (status == kErrRecursion)
		setErrorString(g_excRuntimeError, "maximum recursion limit exceeded");
	else
		setErrorString(g_excRuntimeError, "internal error in regular expression engine");
}

// PyArg_ParseTupleAndKeywords for object arguments; missing optional ones are null
static bool parseArgs(const char *fname, Tuple *args, Dict *kw, const char *const *names, uint32 count, uint32 required, Object **out) {
	uint32 n = args->_size;
	uint32 given = n + (kw ? kw->_used : 0);
	if (given > count) {
		setErrorFormat(g_excTypeError, "%s() takes at most %u argument%s (%u given)", fname, count, count == 1 ? "" : "s", given);
		return false;
	}
	for (uint32 i = 0; i < count; i++)
		out[i] = i < n ? args->get(i) : nullptr;
	if (kw) {
		int32 pos = 0;
		Object *k, *v;
		while (kw->next(&pos, &k, &v)) {
			uint32 i = count;
			if (isInstanceOf(k, &g_strType))
				for (i = 0; i < count; i++)
					if (((Str *)k)->equals(names[i]))
						break;
			if (i == count) {
				setErrorFormat(g_excTypeError, "'%s' is an invalid keyword argument for this function",
				               isInstanceOf(k, &g_strType) ? ((Str *)k)->_data : "?");
				return false;
			}
			if (i < n) {
				setErrorFormat(g_excTypeError, "keyword parameter '%s' redefined", names[i]);
				return false;
			}
			out[i] = v;
		}
	}
	for (uint32 i = 0; i < required; i++) {
		if (!out[i]) {
			setErrorFormat(g_excTypeError, "%s() takes at least %u argument%s (%u given)", fname, required, required == 1 ? "" : "s", given);
			return false;
		}
	}
	return true;
}

static bool intArg(Object *o, int32 def, int32 *out) {
	if (!o) {
		*out = def;
		return true;
	}
	return asInt(o, out);
}

// state_getslice: a group of the current match, or empty/None when unset.
// Unlike match objects, this ignores lastmark, so stale marks show.
static ObjRef stateGetSlice(SreState &st, int32 index, Object *string, bool empty) {
	int32 i, j;
	index = (index - 1) * 2;
	if (string == g_none || index + 1 >= kMarkSize || st._mark[index] < 0 || st._mark[index + 1] < 0) {
		if (!empty)
			return ObjRef(g_none);
		i = j = 0;
	} else {
		i = st._mark[index];
		j = st._mark[index + 1];
	}
	return sequenceGetSlice(string, i, j);
}

static ObjRef newMatch(SrePattern *pattern, SreState &st, int status) {
	if (status < 0) {
		patternError(status);
		return ObjRef();
	}
	if (status == 0)
		return ObjRef(g_none);
	Ref<SreMatch> m(new SreMatch());
	m->_pattern = pattern;
	m->_string = st._string;
	m->_groups = pattern->_groups + 1;
	m->_mark.resize(2 * m->_groups);
	m->_mark[0] = st._start;
	m->_mark[1] = st._ptr;
	for (int32 i = 0, j = 0; i < pattern->_groups; i++, j += 2) {
		if (j + 1 <= st._lastmark && j + 1 < kMarkSize && st._mark[j] >= 0 && st._mark[j + 1] >= 0) {
			m->_mark[j + 2] = st._mark[j];
			m->_mark[j + 3] = st._mark[j + 1];
		} else {
			m->_mark[j + 2] = m->_mark[j + 3] = -1;
		}
	}
	m->_pos = st._pos;
	m->_endpos = st._endpos;
	m->_lastindex = st._lastindex;
	return ObjRef(m.get());
}

static ObjRef callSre(const char *function, Object *a, Object *b, Object *c) {
	ObjRef mod = importModule("sre");
	if (!mod)
		return ObjRef();
	ObjRef func = getAttrString(mod.get(), function);
	if (!func)
		return ObjRef();
	if (c)
		return callFunction(func.get(), 3, a, b, c);
	return callFunction(func.get(), 2, a, b);
}

// Looks up a native method as Py_FindMethod does, including __methods__
static ObjRef findMethod(const MethodDef *methods, Object *self, Str *name) {
	if (name->equals("__methods__")) {
		Ref<List> l = newList();
		for (const MethodDef *d = methods; d->name; d++)
			l->append(newStr(d->name).get());
		return ObjRef(l.get());
	}
	for (const MethodDef *d = methods; d->name; d++)
		if (name->equals(d->name))
			return ObjRef(new BuiltinFunction(d, self));
	return ObjRef();
}

static int readOnlySetAttr(Object *o, Str *name, Object *value) {
	setErrorFormat(g_excTypeError, "'%.100s' object has only read-only attributes (%s .%.100s)",
	               typeName(o), value ? "assign to" : "del", name->_data);
	return -1;
}

// ---- Pattern objects ----------------------------------------------------------------------

static ObjRef patternMatchOrSearch(Object *self, Tuple *args, Dict *kw, bool search) {
	static const char *const names[] = { "pattern", "pos", "endpos" };
	SrePattern *pat = (SrePattern *)self;
	Object *a[3];
	int32 start, end;
	if (!parseArgs(search ? "search" : "match", args, kw, names, 3, 1, a))
		return ObjRef();
	if (!intArg(a[1], 0, &start) || !intArg(a[2], kIntMax, &end))
		return ObjRef();
	SreState st;
	if (!st.init(pat, a[0], start, end))
		return ObjRef();
	st._ptr = st._start;
	int status = search ? st.search() : st.match(0, 1);
	return newMatch(pat, st, status);
}

static ObjRef patternMatch(Object *self, Tuple *args, Dict *kw) {
	return patternMatchOrSearch(self, args, kw, false);
}

static ObjRef patternSearch(Object *self, Tuple *args, Dict *kw) {
	return patternMatchOrSearch(self, args, kw, true);
}

static bool isLiteralTemplate(Object *t) {
	if (isInstanceOf(t, &g_strType)) {
		Str *s = (Str *)t;
		for (uint32 i = 0; i < s->_len; i++)
			if (s->_data[i] == '\\')
				return false;
		return true;
	}
	if (isInstanceOf(t, &g_unicodeType)) {
		Unicode *u = (Unicode *)t;
		for (uint32 i = 0; i < u->_chars.size(); i++)
			if (u->_chars[i] == '\\')
				return false;
		return true;
	}
	return false;
}

static ObjRef joinList(List *list, Object *pattern) {
	if (list->size() == 0)
		return sequenceGetSlice(pattern, 0, 0);
	if (list->size() == 1)
		return ObjRef(list->get(0));
	ObjRef joiner = sequenceGetSlice(pattern, 0, 0);
	if (!joiner)
		return ObjRef();
	return callMethod(joiner.get(), "join", 1, (Object *)list);
}

static ObjRef patternSubx(SrePattern *pat, Object *templ, Object *string, int32 count, bool subn) {
	ObjRef filter;
	bool filterIsCallable;
	if (callable(templ)) {
		filter = templ;
		filterIsCallable = true;
	} else if (isLiteralTemplate(templ)) {
		filter = templ;
		filterIsCallable = false;
	} else {
		// Not a literal: let sre compile the template
		filter = callSre("_subx", pat, templ, nullptr);
		if (!filter)
			return ObjRef();
		filterIsCallable = callable(filter.get());
	}

	SreState st;
	if (!st.init(pat, string, 0, kIntMax))
		return ObjRef();
	Ref<List> list = newList();
	int32 n = 0, i = 0;
	while (!count || n < count) {
		st.reset();
		st._ptr = st._start;
		int status = st.search();
		if (status <= 0) {
			if (status == 0)
				break;
			patternError(status);
			return ObjRef();
		}
		int32 b = st._start;
		int32 e = st._ptr;
		if (i < b) {
			// The text before this match
			ObjRef item = sequenceGetSlice(string, i, b);
			if (!item)
				return ObjRef();
			list->append(item.get());
		} else if (i == b && i == e && n > 0) {
			// Ignore an empty match right after the previous one
			goto next;
		}
		{
			ObjRef item;
			if (filterIsCallable) {
				ObjRef m = newMatch(pat, st, 1);
				if (!m)
					return ObjRef();
				item = callFunction(filter.get(), 1, m.get());
				if (!item)
					return ObjRef();
			} else {
				item = filter;
			}
			if (item.get() != g_none)
				list->append(item.get());
		}
		i = e;
		n++;
next:
		if (st._ptr == st._start)
			st._start = st._ptr + 1;
		else
			st._start = st._ptr;
	}
	if (i < st._endpos) {
		ObjRef item = sequenceGetSlice(string, i, st._endpos);
		if (!item)
			return ObjRef();
		list->append(item.get());
	}
	ObjRef result = joinList(list.get(), pat->_pattern.get());
	if (!result)
		return ObjRef();
	if (subn)
		return ObjRef(tuplePack(2, result.get(), newInt(n).get()).get());
	return result;
}

static ObjRef patternSubOrSubn(Object *self, Tuple *args, Dict *kw, bool subn) {
	static const char *const names[] = { "repl", "string", "count" };
	Object *a[3];
	int32 count;
	if (!parseArgs(subn ? "subn" : "sub", args, kw, names, 3, 2, a))
		return ObjRef();
	if (!intArg(a[2], 0, &count))
		return ObjRef();
	return patternSubx((SrePattern *)self, a[0], a[1], count, subn);
}

static ObjRef patternSub(Object *self, Tuple *args, Dict *kw) {
	return patternSubOrSubn(self, args, kw, false);
}

static ObjRef patternSubn(Object *self, Tuple *args, Dict *kw) {
	return patternSubOrSubn(self, args, kw, true);
}

static ObjRef patternSplit(Object *self, Tuple *args, Dict *kw) {
	static const char *const names[] = { "source", "maxsplit" };
	SrePattern *pat = (SrePattern *)self;
	Object *a[2];
	int32 maxsplit;
	if (!parseArgs("split", args, kw, names, 2, 1, a) || !intArg(a[1], 0, &maxsplit))
		return ObjRef();
	Object *string = a[0];
	SreState st;
	if (!st.init(pat, string, 0, kIntMax))
		return ObjRef();
	Ref<List> list = newList();
	int32 n = 0;
	int32 last = st._start;
	while (!maxsplit || n < maxsplit) {
		st.reset();
		st._ptr = st._start;
		int status = st.search();
		if (status <= 0) {
			if (status == 0)
				break;
			patternError(status);
			return ObjRef();
		}
		if (st._start == st._ptr) {
			if (last == st._end)
				break;
			// Skip one character
			st._start = st._ptr + 1;
			continue;
		}
		ObjRef item = sequenceGetSlice(string, last, st._start);
		if (!item)
			return ObjRef();
		list->append(item.get());
		for (int32 i = 0; i < pat->_groups; i++) {
			item = stateGetSlice(st, i + 1, string, false);
			if (!item)
				return ObjRef();
			list->append(item.get());
		}
		n++;
		last = st._start = st._ptr;
	}
	ObjRef item = sequenceGetSlice(string, last, st._endpos);
	if (!item)
		return ObjRef();
	list->append(item.get());
	return ObjRef(list.get());
}

static ObjRef patternFindall(Object *self, Tuple *args, Dict *kw) {
	static const char *const names[] = { "source", "pos", "endpos" };
	SrePattern *pat = (SrePattern *)self;
	Object *a[3];
	int32 start, end;
	if (!parseArgs("findall", args, kw, names, 3, 1, a) || !intArg(a[1], 0, &start) || !intArg(a[2], kIntMax, &end))
		return ObjRef();
	Object *string = a[0];
	SreState st;
	if (!st.init(pat, string, start, end))
		return ObjRef();
	Ref<List> list = newList();
	while (st._start <= st._end) {
		st.reset();
		st._ptr = st._start;
		int status = st.search();
		if (status <= 0) {
			if (status == 0)
				break;
			patternError(status);
			return ObjRef();
		}
		ObjRef item;
		if (pat->_groups == 0) {
			item = sequenceGetSlice(string, st._start, st._ptr);
		} else if (pat->_groups == 1) {
			item = stateGetSlice(st, 1, string, true);
		} else {
			Ref<Tuple> t = newTuple(pat->_groups);
			for (int32 i = 0; i < pat->_groups; i++) {
				ObjRef o = stateGetSlice(st, i + 1, string, true);
				if (!o)
					return ObjRef();
				t->set(i, o.get());
			}
			item = t.get();
		}
		if (!item)
			return ObjRef();
		list->append(item.get());
		if (st._ptr == st._start)
			st._start = st._ptr + 1;
		else
			st._start = st._ptr;
	}
	return ObjRef(list.get());
}

struct SreScanner : public Object {
	Ref<SrePattern> _pattern;
	SreState _state;
	SreScanner() : Object(&g_scannerType) {}
};

static ObjRef patternScanner(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("scanner", args, 1, 3))
		return ObjRef();
	int32 start, end;
	if (!intArg(args->_size > 1 ? args->get(1) : nullptr, 0, &start) || !intArg(args->_size > 2 ? args->get(2) : nullptr, kIntMax, &end))
		return ObjRef();
	Ref<SreScanner> s(new SreScanner());
	if (!s->_state.init((SrePattern *)self, args->get(0), start, end))
		return ObjRef();
	s->_pattern = (SrePattern *)self;
	return ObjRef(s.get());
}

static ObjRef patternFinditer(Object *self, Tuple *args, Dict *) {
	ObjRef scanner = patternScanner(self, args, nullptr);
	if (!scanner)
		return ObjRef();
	ObjRef search = getAttrString(scanner.get(), "search");
	if (!search)
		return ObjRef();
	return ObjRef(new CallIter(search.get(), g_none));
}

static ObjRef patternCopy(Object *, Tuple *, Dict *) {
	setErrorString(g_excTypeError, "cannot copy this pattern object");
	return ObjRef();
}

static ObjRef patternDeepcopy(Object *, Tuple *, Dict *) {
	setErrorString(g_excTypeError, "cannot deepcopy this pattern object");
	return ObjRef();
}

static const MethodDef g_patternMethods[] = {
	{ "match", patternMatch, kMethKeywords, nullptr },
	{ "search", patternSearch, kMethKeywords, nullptr },
	{ "sub", patternSub, kMethKeywords, nullptr },
	{ "subn", patternSubn, kMethKeywords, nullptr },
	{ "split", patternSplit, kMethKeywords, nullptr },
	{ "findall", patternFindall, kMethKeywords, nullptr },
	{ "finditer", patternFinditer, kMethVarArgs, nullptr },
	{ "scanner", patternScanner, kMethVarArgs, nullptr },
	{ "__copy__", patternCopy, kMethVarArgs, nullptr },
	{ "__deepcopy__", patternDeepcopy, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef patternGetAttr(Object *o, Str *name) {
	SrePattern *pat = (SrePattern *)o;
	ObjRef r = findMethod(g_patternMethods, o, name);
	if (r)
		return r;
	if (name->equals("pattern"))
		return pat->_pattern;
	if (name->equals("flags"))
		return newInt(pat->_flags);
	if (name->equals("groups"))
		return newInt(pat->_groups);
	if (name->equals("groupindex") && pat->_groupindex)
		return pat->_groupindex;
	setErrorString(g_excAttributeError, name->_data);
	return ObjRef();
}

// ---- Match objects ---------------------------------------------------------------------

static int32 matchGetIndex(SreMatch *m, Object *index) {
	if (isInstanceOf(index, &g_intType))
		return ((Int *)index)->_value;
	int32 i = -1;
	if (m->_pattern->_groupindex) {
		ObjRef v = getItem(m->_pattern->_groupindex.get(), index);
		if (v) {
			if (isInstanceOf(v.get(), &g_intType))
				i = ((Int *)v.get())->_value;
		} else {
			clearError();
		}
	}
	return i;
}

static ObjRef matchGetSliceIndex(SreMatch *m, int32 index, Object *def) {
	if (index < 0 || index >= m->_groups) {
		setErrorString(g_excIndexError, "no such group");
		return ObjRef();
	}
	index *= 2;
	if (m->_string.get() == g_none || m->_mark[index] < 0)
		return ObjRef(def);
	return sequenceGetSlice(m->_string.get(), m->_mark[index], m->_mark[index + 1]);
}

static ObjRef matchGetSlice(SreMatch *m, Object *index, Object *def) {
	return matchGetSliceIndex(m, matchGetIndex(m, index), def);
}

static ObjRef matchGroup(Object *self, Tuple *args, Dict *) {
	SreMatch *m = (SreMatch *)self;
	if (args->_size == 0)
		return matchGetSliceIndex(m, 0, g_none);
	if (args->_size == 1)
		return matchGetSlice(m, args->get(0), g_none);
	Ref<Tuple> result = newTuple(args->_size);
	for (uint32 i = 0; i < args->_size; i++) {
		ObjRef item = matchGetSlice(m, args->get(i), g_none);
		if (!item)
			return ObjRef();
		result->set(i, item.get());
	}
	return ObjRef(result.get());
}

static ObjRef matchGroups(Object *self, Tuple *args, Dict *kw) {
	static const char *const names[] = { "default" };
	SreMatch *m = (SreMatch *)self;
	Object *def;
	if (!parseArgs("groups", args, kw, names, 1, 0, &def))
		return ObjRef();
	if (!def)
		def = g_none;
	Ref<Tuple> result = newTuple(m->_groups - 1);
	for (int32 index = 1; index < m->_groups; index++) {
		ObjRef item = matchGetSliceIndex(m, index, def);
		if (!item)
			return ObjRef();
		result->set(index - 1, item.get());
	}
	return ObjRef(result.get());
}

static ObjRef matchGroupdict(Object *self, Tuple *args, Dict *kw) {
	static const char *const names[] = { "default" };
	SreMatch *m = (SreMatch *)self;
	Object *def;
	if (!parseArgs("groupdict", args, kw, names, 1, 0, &def))
		return ObjRef();
	if (!def)
		def = g_none;
	Ref<Dict> result = newDict();
	if (!m->_pattern->_groupindex)
		return ObjRef(result.get());
	ObjRef keys = callMethod(m->_pattern->_groupindex.get(), "keys", 0);
	if (!keys)
		return ObjRef();
	ObjRef list = sequenceList(keys.get());
	if (!list)
		return ObjRef();
	for (uint32 i = 0; i < ((List *)list.get())->size(); i++) {
		Object *key = ((List *)list.get())->get(i);
		ObjRef item = matchGetSlice(m, key, def);
		if (!item || result->setItem(key, item.get()) < 0)
			return ObjRef();
	}
	return ObjRef(result.get());
}

static bool matchArgIndex(SreMatch *m, const char *fname, Tuple *args, int32 *index) {
	if (!checkArgs(fname, args, 0, 1))
		return false;
	*index = args->_size ? matchGetIndex(m, args->get(0)) : 0;
	if (*index < 0 || *index >= m->_groups) {
		setErrorString(g_excIndexError, "no such group");
		return false;
	}
	return true;
}

static ObjRef matchStart(Object *self, Tuple *args, Dict *) {
	SreMatch *m = (SreMatch *)self;
	int32 index;
	if (!matchArgIndex(m, "start", args, &index))
		return ObjRef();
	return newInt(m->_mark[index * 2]);
}

static ObjRef matchEnd(Object *self, Tuple *args, Dict *) {
	SreMatch *m = (SreMatch *)self;
	int32 index;
	if (!matchArgIndex(m, "end", args, &index))
		return ObjRef();
	return newInt(m->_mark[index * 2 + 1]);
}

static ObjRef pair(int32 a, int32 b) {
	return ObjRef(tuplePack(2, newInt(a).get(), newInt(b).get()).get());
}

static ObjRef matchSpan(Object *self, Tuple *args, Dict *) {
	SreMatch *m = (SreMatch *)self;
	int32 index;
	if (!matchArgIndex(m, "span", args, &index))
		return ObjRef();
	return pair(m->_mark[index * 2], m->_mark[index * 2 + 1]);
}

static ObjRef matchExpand(Object *self, Tuple *args, Dict *) {
	if (!checkArgs("expand", args, 1, 1))
		return ObjRef();
	SreMatch *m = (SreMatch *)self;
	return callSre("_expand", m->_pattern.get(), self, args->get(0));
}

static ObjRef matchCopy(Object *, Tuple *, Dict *) {
	setErrorString(g_excTypeError, "cannot copy this match object");
	return ObjRef();
}

static ObjRef matchDeepcopy(Object *, Tuple *, Dict *) {
	setErrorString(g_excTypeError, "cannot deepcopy this match object");
	return ObjRef();
}

static const MethodDef g_matchMethods[] = {
	{ "group", matchGroup, kMethVarArgs, nullptr },
	{ "start", matchStart, kMethVarArgs, nullptr },
	{ "end", matchEnd, kMethVarArgs, nullptr },
	{ "span", matchSpan, kMethVarArgs, nullptr },
	{ "groups", matchGroups, kMethKeywords, nullptr },
	{ "groupdict", matchGroupdict, kMethKeywords, nullptr },
	{ "expand", matchExpand, kMethVarArgs, nullptr },
	{ "__copy__", matchCopy, kMethVarArgs, nullptr },
	{ "__deepcopy__", matchDeepcopy, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef matchGetAttr(Object *o, Str *name) {
	SreMatch *m = (SreMatch *)o;
	ObjRef r = findMethod(g_matchMethods, o, name);
	if (r)
		return r;
	if (name->equals("lastindex")) {
		if (m->_lastindex >= 0)
			return newInt(m->_lastindex);
		return ObjRef(g_none);
	}
	if (name->equals("lastgroup")) {
		if (m->_pattern->_indexgroup && m->_lastindex >= 0) {
			ObjRef result = sequenceGetItem(m->_pattern->_indexgroup.get(), m->_lastindex);
			if (result)
				return result;
			clearError();
		}
		return ObjRef(g_none);
	}
	if (name->equals("string"))
		return m->_string ? m->_string : ObjRef(g_none);
	if (name->equals("regs")) {
		if (!m->_regs) {
			Ref<Tuple> regs = newTuple(m->_groups);
			for (int32 i = 0; i < m->_groups; i++)
				regs->set(i, pair(m->_mark[i * 2], m->_mark[i * 2 + 1]).get());
			m->_regs = regs.get();
		}
		return m->_regs;
	}
	if (name->equals("re"))
		return ObjRef(m->_pattern.get());
	if (name->equals("pos"))
		return newInt(m->_pos);
	if (name->equals("endpos"))
		return newInt(m->_endpos);
	setErrorString(g_excAttributeError, name->_data);
	return ObjRef();
}

// ---- Scanner objects ------------------------------------------------------------------

static ObjRef scannerStep(Object *self, bool search) {
	SreScanner *s = (SreScanner *)self;
	SreState &st = s->_state;
	st.reset();
	st._ptr = st._start;
	int status = search ? st.search() : st.match(0, 1);
	ObjRef m = newMatch(s->_pattern.get(), st, status);
	if (status == 0 || st._ptr == st._start)
		st._start = st._ptr + 1;
	else
		st._start = st._ptr;
	return m;
}

static ObjRef scannerMatch(Object *self, Tuple *, Dict *) {
	return scannerStep(self, false);
}

static ObjRef scannerSearch(Object *self, Tuple *, Dict *) {
	return scannerStep(self, true);
}

static const MethodDef g_scannerMethods[] = {
	{ "match", scannerMatch, kMethVarArgs, nullptr },
	{ "search", scannerSearch, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

static ObjRef scannerGetAttr(Object *o, Str *name) {
	ObjRef r = findMethod(g_scannerMethods, o, name);
	if (r)
		return r;
	if (name->equals("pattern"))
		return ObjRef(((SreScanner *)o)->_pattern.get());
	setErrorString(g_excAttributeError, name->_data);
	return ObjRef();
}

// ---- The module ----------------------------------------------------------------------------

static ObjRef sreCompile(Object *, Tuple *args, Dict *) {
	if (!checkArgs("compile", args, 3, 6))
		return ObjRef();
	int32 flags, groups = 0;
	if (!asInt(args->get(1), &flags))
		return ObjRef();
	Object *code = args->get(2);
	if (!isInstanceOf(code, &g_listType)) {
		setErrorFormat(g_excTypeError, "argument 3 must be list, not %.50s", typeName(code));
		return ObjRef();
	}
	if (args->_size > 3 && !asInt(args->get(3), &groups))
		return ObjRef();
	Ref<SrePattern> pat(new SrePattern());
	List *l = (List *)code;
	pat->_code.resize(l->size());
	for (uint32 i = 0; i < l->size(); i++) {
		Object *o = l->get(i);
		int32 v;
		if (isInstanceOf(o, &g_longType)) {
			int64 v64;
			if (!longAsInt64(o, &v64))
				return ObjRef();
			v = (int32)v64;
		} else if (!asInt(o, &v)) {
			return ObjRef();
		}
		pat->_code[i] = (uint32)v;
	}
	pat->_pattern = args->get(0);
	pat->_flags = flags;
	pat->_groups = groups;
	if (args->_size > 4)
		pat->_groupindex = args->get(4);
	if (args->_size > 5)
		pat->_indexgroup = args->get(5);
	return ObjRef(pat.get());
}

static ObjRef sreGetcodesize(Object *, Tuple *args, Dict *) {
	if (!checkArgs("getcodesize", args, 0, 0))
		return ObjRef();
	return newInt(2);
}

static ObjRef sreGetlower(Object *, Tuple *args, Dict *) {
	if (!checkArgs("getlower", args, 2, 2))
		return ObjRef();
	int32 c, flags;
	if (!asInt(args->get(0), &c) || !asInt(args->get(1), &flags))
		return ObjRef();
	if (flags & kFlagLocale)
		return newInt(c >= 0 && c < 256 ? lowerAscii(c) : c);
	if (flags & kFlagUnicode)
		return newInt(c >= 0 ? lowerUnicode(c) : c);
	return newInt(c >= 0 && c < 128 ? lowerAscii(c) : c);
}

static const MethodDef g_sreMethods[] = {
	{ "compile", sreCompile, kMethVarArgs, nullptr },
	{ "getcodesize", sreGetcodesize, kMethVarArgs, nullptr },
	{ "getlower", sreGetlower, kMethVarArgs, nullptr },
	{ nullptr, nullptr, 0, nullptr }
};

void initSreModule(Module *m) {
	g_patternType.getAttr = patternGetAttr;
	g_patternType.setAttr = readOnlySetAttr;
	typeReady(&g_patternType);
	g_matchType.getAttr = matchGetAttr;
	g_matchType.setAttr = readOnlySetAttr;
	typeReady(&g_matchType);
	g_scannerType.getAttr = scannerGetAttr;
	g_scannerType.setAttr = readOnlySetAttr;
	typeReady(&g_scannerType);
	for (const MethodDef *d = g_sreMethods; d->name; d++) {
		ObjRef f(new BuiltinFunction(d, nullptr));
		moduleAddObject(m, d->name, f.get());
	}
	moduleAddInt(m, "MAGIC", kMagic);
	moduleAddString(m, "copyright", " SRE 2.2.1 Copyright (c) 1997-2001 by Secret Labs AB ");
}

} // End of namespace Python
} // End of namespace Yaga
