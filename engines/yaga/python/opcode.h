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

#ifndef YAGA_PYTHON_OPCODE_H
#define YAGA_PYTHON_OPCODE_H

// Python 2.2 bytecode (Include/opcode.h)

namespace Yaga {
namespace Python {

enum Opcode {
	kStopCode = 0,
	kPopTop = 1,
	kRotTwo = 2,
	kRotThree = 3,
	kDupTop = 4,
	kRotFour = 5,

	kUnaryPositive = 10,
	kUnaryNegative = 11,
	kUnaryNot = 12,
	kUnaryConvert = 13,
	kUnaryInvert = 15,

	kBinaryPower = 19,
	kBinaryMultiply = 20,
	kBinaryDivide = 21,
	kBinaryModulo = 22,
	kBinaryAdd = 23,
	kBinarySubtract = 24,
	kBinarySubscr = 25,
	kBinaryFloorDivide = 26,
	kBinaryTrueDivide = 27,
	kInplaceFloorDivide = 28,
	kInplaceTrueDivide = 29,

	kSlice = 30,       // +0..3
	kStoreSlice = 40,  // +0..3
	kDeleteSlice = 50, // +0..3

	kInplaceAdd = 55,
	kInplaceSubtract = 56,
	kInplaceMultiply = 57,
	kInplaceDivide = 58,
	kInplaceModulo = 59,
	kStoreSubscr = 60,
	kDeleteSubscr = 61,
	kBinaryLshift = 62,
	kBinaryRshift = 63,
	kBinaryAnd = 64,
	kBinaryXor = 65,
	kBinaryOr = 66,
	kInplacePower = 67,
	kGetIter = 68,

	kPrintExpr = 70,
	kPrintItem = 71,
	kPrintNewline = 72,
	kPrintItemTo = 73,
	kPrintNewlineTo = 74,
	kInplaceLshift = 75,
	kInplaceRshift = 76,
	kInplaceAnd = 77,
	kInplaceXor = 78,
	kInplaceOr = 79,
	kBreakLoop = 80,

	kLoadLocals = 82,
	kReturnValue = 83,
	kImportStar = 84,
	kExecStmt = 85,
	kYieldValue = 86,
	kPopBlock = 87,
	kEndFinally = 88,
	kBuildClass = 89,

	kHaveArgument = 90, // opcodes from here on have an argument

	kStoreName = 90,
	kDeleteName = 91,
	kUnpackSequence = 92,
	kForIter = 93,

	kStoreAttr = 95,
	kDeleteAttr = 96,
	kStoreGlobal = 97,
	kDeleteGlobal = 98,
	kDupTopX = 99,
	kLoadConst = 100,
	kLoadName = 101,
	kBuildTuple = 102,
	kBuildList = 103,
	kBuildMap = 104,
	kLoadAttr = 105,
	kCompareOp = 106,
	kImportName = 107,
	kImportFrom = 108,

	kJumpForward = 110,
	kJumpIfFalse = 111,
	kJumpIfTrue = 112,
	kJumpAbsolute = 113,
	kForLoop = 114,

	kLoadGlobal = 116,

	kContinueLoop = 119,
	kSetupLoop = 120,
	kSetupExcept = 121,
	kSetupFinally = 122,

	kLoadFast = 124,
	kStoreFast = 125,
	kDeleteFast = 126,
	kSetLineno = 127,

	kRaiseVarargs = 130,
	kCallFunction = 131,
	kMakeFunction = 132,
	kBuildSlice = 133,
	kMakeClosure = 134,
	kLoadClosure = 135,
	kLoadDeref = 136,
	kStoreDeref = 137,

	kCallFunctionVar = 140,
	kCallFunctionKw = 141,
	kCallFunctionVarKw = 142,

	kExtendedArg = 143
};

// COMPARE_OP arguments beyond the rich comparisons
enum {
	kCmpIn = 6,
	kCmpNotIn = 7,
	kCmpIs = 8,
	kCmpIsNot = 9,
	kCmpExcMatch = 10
};

} // End of namespace Python
} // End of namespace Yaga

#endif // YAGA_PYTHON_OPCODE_H
