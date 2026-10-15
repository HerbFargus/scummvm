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

#include "common/config-manager.h"

#include "common/random.h"
#include "common/memstream.h"

#include "mtropolis/coroutines.h"
#include "mtropolis/miniscript.h"

namespace MTropolis {

class MiniscriptInstructionParserFeedback : public IMiniscriptInstructionParserFeedback {
public:
	explicit MiniscriptInstructionParserFeedback(Common::Array<MiniscriptReferences::GlobalRef> *globalRefs);

	uint registerGlobalGUIDIndex(uint32 guid) override;

private:
	Common::Array<MiniscriptReferences::GlobalRef> *_globalRefs;
};

MiniscriptInstructionParserFeedback::MiniscriptInstructionParserFeedback(Common::Array<MiniscriptReferences::GlobalRef> *globalRefs) : _globalRefs(globalRefs) {
}

uint MiniscriptInstructionParserFeedback::registerGlobalGUIDIndex(uint32 guid) {
	for (uint i = 0; i < _globalRefs->size(); i++) {
		if ((*_globalRefs)[i].guid == guid)
			return i;
	}

	uint newIndex = _globalRefs->size();

	MiniscriptReferences::GlobalRef globalRef;
	globalRef.guid = guid;

	_globalRefs->push_back(globalRef);

	return newIndex;
}

bool miniscriptEvaluateTruth(const DynamicValue &value) {
	// NOTE: Comparing equal to "true" only passes for 1 exactly, but for conditions,
	// any non-zero value is true.
	switch (value.getType()) {
	case DynamicValueTypes::kBoolean:
		return value.getBool();
	case DynamicValueTypes::kInteger:
		return (value.getInt() != 0);
	case DynamicValueTypes::kFloat:
		return !(value.getFloat() == 0.0);
	case DynamicValueTypes::kObject:
		return !value.getObject().object.expired();
	default:
		return false;
	}
}

IMiniscriptInstructionParserFeedback::~IMiniscriptInstructionParserFeedback() {
}

MiniscriptInstruction::~MiniscriptInstruction() {
}

MiniscriptReferences::LocalRef::LocalRef() : guid(0) {
}

MiniscriptReferences::GlobalRef::GlobalRef() : guid(0) {
}

MiniscriptReferences::MiniscriptReferences(const Common::Array<LocalRef> &localRefs, const Common::Array<GlobalRef> &globalRefs) : _localRefs(localRefs), _globalRefs(globalRefs) {
}

void MiniscriptReferences::linkInternalReferences(ObjectLinkingScope *scope) {
	// Resolve using name lookups since there are some known cases where the GUID is broken
	// e.g. "bArriveFromCutScene" in "Set bArriveFromCutScene on PE" in Obsidian
	for (Common::Array<LocalRef>::iterator it = _localRefs.begin(), itEnd = _localRefs.end(); it != itEnd; ++it)
		it->resolution = scope->resolve(it->guid, it->name, false);

	for (Common::Array<GlobalRef>::iterator it = _globalRefs.begin(), itEnd = _globalRefs.end(); it != itEnd; ++it)
		it->resolution = scope->resolve(it->guid, "", true);
}

void MiniscriptReferences::visitInternalReferences(IStructuralReferenceVisitor *visitor) {
	for (LocalRef &ref : _localRefs) {
		Common::SharedPtr<RuntimeObject> obj = ref.resolution.lock();
		if (obj) {
			if (obj->isModifier()) {
				Common::WeakPtr<Modifier> mod = obj.staticCast<Modifier>();
				visitor->visitWeakModifierRef(mod);
				ref.resolution = mod;
			} else if (obj->isStructural()) {
				Common::WeakPtr<Structural> struc = obj.staticCast<Structural>();
				visitor->visitWeakStructuralRef(struc);
				ref.resolution = struc;
			}
		}
	}

	for (GlobalRef &ref : _globalRefs) {
		Common::SharedPtr<RuntimeObject> obj = ref.resolution.lock();
		if (obj) {
			if (obj->isModifier()) {
				Common::WeakPtr<Modifier> mod = obj.staticCast<Modifier>();
				visitor->visitWeakModifierRef(mod);
				ref.resolution = mod;
			} else if (obj->isStructural()) {
				Common::WeakPtr<Structural> struc = obj.staticCast<Structural>();
				visitor->visitWeakStructuralRef(struc);
				ref.resolution = struc;
			}
		}
	}
}

Common::WeakPtr<RuntimeObject> MiniscriptReferences::getRefByIndex(uint index) const {
	if (index >= _localRefs.size())
		return Common::WeakPtr<RuntimeObject>();
	return _localRefs[index].resolution;
}

Common::WeakPtr<RuntimeObject> MiniscriptReferences::getGlobalRefByIndex(uint index) const {
	if (index >= _globalRefs.size())
		return Common::WeakPtr<RuntimeObject>();
	return _globalRefs[index].resolution;
	
}

MiniscriptProgram::MiniscriptProgram(const Common::SharedPtr<Common::Array<uint8> > &programData, const Common::Array<MiniscriptInstruction *> &instructions, const Common::Array<Attribute> &attributes)
	: _programData(programData), _instructions(instructions), _attributes(attributes) {
}

MiniscriptProgram::~MiniscriptProgram() {
	// Destruct all instructions
	for (Common::Array<MiniscriptInstruction *>::const_iterator it = _instructions.begin(), itEnd = _instructions.end(); it != itEnd; ++it)
		(*it)->~MiniscriptInstruction();
}

const Common::Array<MiniscriptInstruction *> &MiniscriptProgram::getInstructions() const {
	return _instructions;
}

const Common::Array<MiniscriptProgram::Attribute> &MiniscriptProgram::getAttributes() const {
	return _attributes;
}

template<class T>
struct MiniscriptInstructionLoader {
	static bool loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback);
};

template<class T>
bool MiniscriptInstructionLoader<T>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	// Default loader for simple instructions with no private data
	new (dest) T();
	return true;
}

template<>
bool MiniscriptInstructionLoader<MiniscriptInstructions::Send>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	Data::Event dataEvent;
	if (!dataEvent.load(instrDataReader))
		return false;

	Event evt;
	if (!evt.load(dataEvent))
		return false;

	MessageFlags msgFlags;
	msgFlags.immediate = ((instrFlags & 0x04) == 0);
	msgFlags.cascade = ((instrFlags & 0x08) == 0);
	msgFlags.relay = ((instrFlags & 0x10) == 0);

	new (dest) MiniscriptInstructions::Send(evt, msgFlags);
	return true;
}

template<>
bool MiniscriptInstructionLoader<MiniscriptInstructions::BuiltinFunc>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	uint32 functionID;
	if (!instrDataReader.readU32(functionID))
		return false;

	if (functionID < 1 || functionID > 20)
		return false; // Unknown function

	new (dest) MiniscriptInstructions::BuiltinFunc(static_cast<MiniscriptInstructions::BuiltinFunc::BuiltinFunctionID>(functionID));
	return true;
}

template<>
bool MiniscriptInstructionLoader<MiniscriptInstructions::CallFunction>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	uint32 functionNameAttribute, numArgs;
	if (!instrDataReader.readU32(functionNameAttribute) || !instrDataReader.readU32(numArgs))
		return false;

	new (dest) MiniscriptInstructions::CallFunction(functionNameAttribute, numArgs);
	return true;
}

template<>
bool MiniscriptInstructionLoader<MiniscriptInstructions::GetChild>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	uint32 childAttribute;
	if (!instrDataReader.readU32(childAttribute))
		return false;

	new (dest) MiniscriptInstructions::GetChild(childAttribute, (instrFlags & 1) != 0, (instrFlags & 32) != 0);
	return true;
}

template<>
bool MiniscriptInstructionLoader<MiniscriptInstructions::PushGlobal>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	uint32 globalID;
	if (!instrDataReader.readU32(globalID))
		return false;

	new (dest) MiniscriptInstructions::PushGlobal(globalID, (instrFlags & 1) != 0);
	return true;
}

template<>
bool MiniscriptInstructionLoader<MiniscriptInstructions::Jump>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	uint32 jumpFlags, unknown, instrOffset;
	if (!instrDataReader.readU32(jumpFlags) || !instrDataReader.readU32(unknown) || !instrDataReader.readU32(instrOffset))
		return false;

	bool isConditional = (jumpFlags == 2);
	if (jumpFlags != 1 && jumpFlags != 2)
		return false; // Don't recognize this flag combination

	if (instrOffset == 0)
		return false; // Not valid

	new (dest) MiniscriptInstructions::Jump(instrOffset, isConditional);
	return true;
}

template<>
bool MiniscriptInstructionLoader<MiniscriptInstructions::PushValue>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	uint16 dataType;
	if (!instrDataReader.readU16(dataType))
		return false;

	if (dataType == 0)
		new (dest) MiniscriptInstructions::PushValue(MiniscriptInstructions::PushValue::kDataTypeNull, nullptr, false);
	else if (dataType == 0x15) {
		Common::XPFloat f;
		if (!instrDataReader.readPlatformFloat(f))
			return false;

		double d = f.toDouble();
		new (dest) MiniscriptInstructions::PushValue(MiniscriptInstructions::PushValue::kDataTypeDouble, &d, false);
	} else if (dataType == 0x1a) {
		uint8 boolValue;
		if (!instrDataReader.readU8(boolValue))
			return false;

		bool b = (boolValue != 0);
		new (dest) MiniscriptInstructions::PushValue(MiniscriptInstructions::PushValue::kDataTypeBool, &b, false);
	} else if (dataType == 0x1f9) {
		uint32 refValue;
		if (!instrDataReader.readU32(refValue))
			return false;

		new (dest) MiniscriptInstructions::PushValue(MiniscriptInstructions::PushValue::kDataTypeLocalRef, &refValue, (instrFlags & 1) != 0);
	} else if (dataType == 0x1fa) {
		uint32 refValue;
		if (!instrDataReader.readU32(refValue))
			return false;

		uint32 indexedRef = feedback.registerGlobalGUIDIndex(refValue);

		new (dest) MiniscriptInstructions::PushValue(MiniscriptInstructions::PushValue::kDataTypeGlobalRef, &indexedRef, (instrFlags & 1) != 0);
	} else if (dataType == 0x1d) {
		MiniscriptInstructions::PushValue::Label label;
		if (!instrDataReader.readU32(label.superGroup) || !instrDataReader.readU32(label.id))
			return false;

		new (dest) MiniscriptInstructions::PushValue(MiniscriptInstructions::PushValue::kDataTypeLabel, &label, false);
	} else
		return false;

	return true;
}

template<>
bool MiniscriptInstructionLoader<MiniscriptInstructions::PushString>::loadInstruction(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, IMiniscriptInstructionParserFeedback &feedback) {
	uint16 strLength;
	if (!instrDataReader.readU16(strLength))
		return false;

	// Unlike most cases, in this case the string is null-terminated but the str length doesn't include the terminator
	Common::String str;
	if (!instrDataReader.readTerminatedStr(str, strLength + 1))
		return false;

	new (dest) MiniscriptInstructions::PushString(str);

	return true;
}

struct SIMiniscriptInstructionFactory {
	bool (*create)(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, MiniscriptInstruction *&outMiniscriptInstructionPtr, IMiniscriptInstructionParserFeedback &feedback);
	void (*getSizeAndAlignment)(size_t &outSize, size_t &outAlignment);
};

template<class T>
class MiniscriptInstructionFactory {
public:
	static bool create(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, MiniscriptInstruction *&outMiniscriptInstructionPtr, IMiniscriptInstructionParserFeedback &feedback);
	static void getSizeAndAlignment(size_t &outSize, size_t &outAlignment);

	static SIMiniscriptInstructionFactory *getInstance();

private:
	static SIMiniscriptInstructionFactory _instance;
};

template<class T>
bool MiniscriptInstructionFactory<T>::create(void *dest, uint32 instrFlags, Data::DataReader &instrDataReader, MiniscriptInstruction *&outMiniscriptInstructionPtr, IMiniscriptInstructionParserFeedback &feedback) {
	if (!MiniscriptInstructionLoader<T>::loadInstruction(dest, instrFlags, instrDataReader, feedback))
		return false;

	outMiniscriptInstructionPtr = static_cast<MiniscriptInstruction *>(static_cast<T *>(dest));
	return true;
}

template<class T>
void MiniscriptInstructionFactory<T>::getSizeAndAlignment(size_t &outSize, size_t &outAlignment) {
	outSize = sizeof(T);
	outAlignment = alignof(T);
}

template<class T>
inline SIMiniscriptInstructionFactory *MiniscriptInstructionFactory<T>::getInstance() {
	return &_instance;
}

template<class T>
SIMiniscriptInstructionFactory MiniscriptInstructionFactory<T>::_instance = {
	MiniscriptInstructionFactory<T>::create,
	MiniscriptInstructionFactory<T>::getSizeAndAlignment
};

MiniscriptParser::InstructionData::InstructionData()
	: opcode(0), flags(0), pdPosition(0), instrFactory(nullptr) {
}

bool MiniscriptParser::parse(const Data::MiniscriptProgram &program, Common::SharedPtr<MiniscriptProgram> &outProgram, Common::SharedPtr<MiniscriptReferences> &outReferences) {
	Common::Array<MiniscriptReferences::LocalRef> localRefs;
	Common::Array<MiniscriptReferences::GlobalRef> globalRefs;
	Common::Array<MiniscriptProgram::Attribute> attributes;
	Common::SharedPtr<Common::Array<uint8> > programDataPtr;
	Common::Array<MiniscriptInstruction *> miniscriptInstructions;

	// If the program is empty then just return an empty program
	if (program.bytecode.size() == 0 || program.numOfInstructions == 0) {
		outProgram = Common::SharedPtr<MiniscriptProgram>(new MiniscriptProgram(programDataPtr, miniscriptInstructions, attributes));
		outReferences = Common::SharedPtr<MiniscriptReferences>(new MiniscriptReferences(localRefs, globalRefs));
		return true;
	}

	localRefs.resize(program.localRefs.size());
	for (size_t i = 0; i < program.localRefs.size(); i++) {
		localRefs[i].guid = program.localRefs[i].guid;
		localRefs[i].name = program.localRefs[i].name;
	}

	attributes.resize(program.attributes.size());
	for (size_t i = 0; i < program.attributes.size(); i++) {
		attributes[i].name = program.attributes[i].name;
	}

	Common::MemoryReadStream stream(&program.bytecode[0], program.bytecode.size());
	Data::DataReader reader(0, stream, program.dataFormat, kRuntimeVersion100, false);

	Common::Array<InstructionData> rawInstructions;
	rawInstructions.resize(program.numOfInstructions);

	for (size_t i = 0; i < program.numOfInstructions; i++) {
		InstructionData &rawInstruction = rawInstructions[i];
		uint16 instrSize;
		if (!reader.readU16(rawInstruction.opcode) || !reader.readU16(rawInstruction.flags) || !reader.readU16(instrSize))
			return false;

		if (instrSize < 6)
			return false;

		if (instrSize > 6) {
			rawInstruction.contents.resize(instrSize - 6);
			if (!reader.read(&rawInstruction.contents[0], instrSize - 6))
				return false;
		}
	}

	programDataPtr.reset(new Common::Array<uint8>());
	Common::Array<uint8> &programData = *programDataPtr.get();

	// Find out how much space we need and place instructions
	size_t maxAlignment = 1;
	size_t programDataSize = 0;
	for (size_t i = 0; i < program.numOfInstructions; i++) {
		InstructionData &rawInstruction = rawInstructions[i];

		SIMiniscriptInstructionFactory *factory = resolveOpcode(rawInstruction.opcode);
		rawInstruction.instrFactory = factory;

		if (!factory) {
			warning("Miniscript: Unknown opcode %x", static_cast<uint>(rawInstruction.opcode));
			return false;
		}

		size_t compiledSize = 0;
		size_t compiledAlignment = 0;
		factory->getSizeAndAlignment(compiledSize, compiledAlignment);

		if (programDataSize % compiledAlignment != 0)
			programDataSize += (compiledAlignment - (programDataSize % compiledAlignment));

		rawInstruction.pdPosition = programDataSize;
		programDataSize += compiledSize;

		if (maxAlignment < compiledAlignment)
			maxAlignment = compiledAlignment;
	}

	programData.resize(programDataSize + maxAlignment - 1);
	uintptr programDataAddress = reinterpret_cast<uintptr>(&programData[0]);

	size_t baseOffset = 0;
	if (programDataAddress % maxAlignment != 0)
		baseOffset = (maxAlignment - (programDataSize % maxAlignment));

	miniscriptInstructions.resize(program.numOfInstructions);

	MiniscriptInstructionParserFeedback parserFeedback(&globalRefs);

	// Create instructions
	for (size_t i = 0; i < program.numOfInstructions; i++) {
		const InstructionData &rawInstruction = rawInstructions[i];

		const void *dataLoc = nullptr;
		if (rawInstruction.contents.size() != 0)
			dataLoc = &rawInstruction.contents[0];

		Common::MemoryReadStream instrContentsStream(static_cast<const byte *>(dataLoc), rawInstruction.contents.size());
		Data::DataReader instrContentsReader(0, instrContentsStream, reader.getDataFormat(), reader.getRuntimeVersion(), reader.isVersionAutoDetect());

		if (!rawInstruction.instrFactory->create(&programData[baseOffset + rawInstruction.pdPosition], rawInstruction.flags, instrContentsReader, miniscriptInstructions[i], parserFeedback)) {
			warning("Miniscript: Failed to load instruction with opcode %x, flags %x, %u bytes of data", static_cast<uint>(rawInstruction.opcode), static_cast<uint>(rawInstruction.flags), rawInstruction.contents.size());

			// Destroy any already-created instructions
			for (size_t di = 0; di < i; di++) {
				miniscriptInstructions[i - 1 - di]->~MiniscriptInstruction();
			}

			return false;
		}
	}

	// Done
	outProgram = Common::SharedPtr<MiniscriptProgram>(new MiniscriptProgram(programDataPtr, miniscriptInstructions, attributes));
	outReferences = Common::SharedPtr<MiniscriptReferences>(new MiniscriptReferences(localRefs, globalRefs));

	return true;
}

SIMiniscriptInstructionFactory *MiniscriptParser::resolveOpcode(uint16 opcode) {
	switch (opcode) {
	case 0x834:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Set>::getInstance();
	case 0x898:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Send>::getInstance();
	case 0xc9:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Add>::getInstance();
	case 0xca:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Sub>::getInstance();
	case 0xcb:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Mul>::getInstance();
	case 0xcc:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Div>::getInstance();
	case 0xcd:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Pow>::getInstance();
	case 0xce:
		return MiniscriptInstructionFactory<MiniscriptInstructions::And>::getInstance();
	case 0xcf:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Or>::getInstance();
	case 0xd0:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Neg>::getInstance();
	case 0xd1:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Not>::getInstance();
	case 0xd2:
		return MiniscriptInstructionFactory<MiniscriptInstructions::CmpEqual>::getInstance();
	case 0xd3:
		return MiniscriptInstructionFactory<MiniscriptInstructions::CmpNotEqual>::getInstance();
	case 0xd4:
		return MiniscriptInstructionFactory<MiniscriptInstructions::CmpLessOrEqual>::getInstance();
	case 0xd5:
		return MiniscriptInstructionFactory<MiniscriptInstructions::CmpLess>::getInstance();
	case 0xd6:
		return MiniscriptInstructionFactory<MiniscriptInstructions::CmpGreaterOrEqual>::getInstance();
	case 0xd7:
		return MiniscriptInstructionFactory<MiniscriptInstructions::CmpGreater>::getInstance();
	case 0xd8:
		return MiniscriptInstructionFactory<MiniscriptInstructions::BuiltinFunc>::getInstance();
	case 0xd9:
		return MiniscriptInstructionFactory<MiniscriptInstructions::DivInt>::getInstance();
	case 0xda:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Modulo>::getInstance();
	case 0xdb:
		return MiniscriptInstructionFactory<MiniscriptInstructions::StrConcat>::getInstance();
	case 0x12f:
		return MiniscriptInstructionFactory<MiniscriptInstructions::PointCreate>::getInstance();
	case 0x130:
		return MiniscriptInstructionFactory<MiniscriptInstructions::RangeCreate>::getInstance();
	case 0x131:
		return MiniscriptInstructionFactory<MiniscriptInstructions::VectorCreate>::getInstance();
	case 0x135:
		return MiniscriptInstructionFactory<MiniscriptInstructions::GetChild>::getInstance();
	case 0x136:
		return MiniscriptInstructionFactory<MiniscriptInstructions::ListAppend>::getInstance();
	case 0x137:
		return MiniscriptInstructionFactory<MiniscriptInstructions::ListCreate>::getInstance();
	case 0x138:
		return MiniscriptInstructionFactory<MiniscriptInstructions::ArgSeparator>::getInstance();
	case 0x3ea:
		return MiniscriptInstructionFactory<MiniscriptInstructions::CallFunction>::getInstance();
	case 0x191:
		return MiniscriptInstructionFactory<MiniscriptInstructions::PushValue>::getInstance();
	case 0x192:
		return MiniscriptInstructionFactory<MiniscriptInstructions::PushGlobal>::getInstance();
	case 0x193:
		return MiniscriptInstructionFactory<MiniscriptInstructions::PushString>::getInstance();
	case 0x7d3:
		return MiniscriptInstructionFactory<MiniscriptInstructions::Jump>::getInstance();
	default:
		return nullptr;
	}
}

namespace MiniscriptInstructions {

MiniscriptInstructionOutcome UnimplementedInstruction::execute(MiniscriptThread *thread) const {
	thread->error("Unimplemented instruction");
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome Set::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() != 2) {
		// Sets are only allowed when they would empty the stack
		thread->error("Invalid stack state for set instruction");
		return kMiniscriptInstructionOutcomeFailed;
	}

	// Convert value
	MiniscriptInstructionOutcome outcome = kMiniscriptInstructionOutcomeContinue;

	const MiniscriptStackValue &srcValue = thread->getStackValueFromTop(0);
	MiniscriptStackValue &target = thread->getStackValueFromTop(1);

	if (target.value.getType() == DynamicValueTypes::kWriteProxy) {
		const DynamicValueWriteProxy &proxy = target.value.getWriteProxy();
		uint pushCount = thread->getRuntime()->getVThread().getPushCount();
		outcome = proxy.pod.ifc->write(thread, srcValue.value, proxy.pod.objectRef, proxy.pod.ptrOrOffset);
		if (outcome == kMiniscriptInstructionOutcomeFailed) {
			// mTropolis 2.0 carries on after assigning a list to a variable that can't hold it
			// (Magic School Bus Bugs initializes an integer with a list), leaving it unchanged
			if (srcValue.value.getType() == DynamicValueTypes::kList && thread->getRuntime()->getProject()->getRuntimeVersion() >= kRuntimeVersion200) {
				debug(2, "Miniscript: '%s' assigned a list to a value that can't hold it", thread->getModifier()->getName().c_str());
				thread->popValues(2);
				return kMiniscriptInstructionOutcomeContinue;
			}

			thread->error("Failed to assign value to proxy");
			return outcome;
		}

		// A write that sent messages (such as cancelling a dialogue line, which sends its done
		// message) pushed tasks that have to run before the script carries on
		if (outcome == kMiniscriptInstructionOutcomeContinue && thread->getRuntime()->getVThread().getPushCount() != pushCount)
			outcome = kMiniscriptInstructionOutcomeYieldToVThread;
	} else {
		VariableModifier *var = nullptr;
		if (target.value.getType() == DynamicValueTypes::kObject) {
			Common::SharedPtr<RuntimeObject> obj = target.value.getObject().object.lock();
			if (obj && obj->isModifier() && static_cast<const Modifier *>(obj.get())->isVariable())
				var = static_cast<VariableModifier *>(obj.get());
		}

		if (var != nullptr) {
			(void)var->varSetValue(thread, srcValue.value);
		} else {
			thread->error("Can't assign to rvalue");
			return kMiniscriptInstructionOutcomeFailed;
		}
	}
	thread->popValues(2);

	return outcome;
}

Send::Send(const Event &evt, const MessageFlags &messageFlags) : _evt(evt), _messageFlags(messageFlags) {
}

MiniscriptInstructionOutcome Send::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() != 2) {
		// Send instructions are only allowed if they empty the stack
		thread->error("Invalid stack state for send instruction");
		return kMiniscriptInstructionOutcomeFailed;
	}

	DynamicValue &targetValue = thread->getStackValueFromTop(0).value;
	DynamicValue &payloadValue = thread->getStackValueFromTop(1).value;

	if (targetValue.getType() != DynamicValueTypes::kObject) {
		// Failed sends are non-fatal (Obsidian requires this in the aircraft propulsion room to enter the propulsion puzzle)
		warning("Invalid message destination (target isn't an object reference)");
		thread->popValues(2);
		return kMiniscriptInstructionOutcomeContinue;
	}

	Common::SharedPtr<RuntimeObject> obj = targetValue.getObject().object.lock();

	if (!obj) {
		// Obsidian also triggers NAV_Restart on Project Started, which triggers "<init globals> on NAV_Restart"
		// which sends PRG_Toggle_Status_Display to sharedScene, even though at that point there is no shared scene.
		warning("Invalid message destination (target object is invalid)");
		thread->popValues(2);
		return kMiniscriptInstructionOutcomeContinue;
	}

	debug(3, "Miniscript: '%s' sends %u/%u to '%s'", thread->getModifier()->getName().c_str(), static_cast<uint>(_evt.eventType), _evt.eventInfo,
		obj->isModifier() ? static_cast<Modifier *>(obj.get())->getName().c_str() : (obj->isStructural() ? static_cast<Structural *>(obj.get())->getName().c_str() : "?"));

	Common::SharedPtr<MessageProperties> msgProps(new MessageProperties(_evt, payloadValue, thread->getModifier()->getSelfReference()));
	Common::SharedPtr<MessageDispatch> dispatch;
	if (obj->isModifier())
		dispatch.reset(new MessageDispatch(msgProps, static_cast<Modifier *>(obj.get()), _messageFlags.cascade, _messageFlags.relay, true));
	else if (obj->isStructural())
		dispatch.reset(new MessageDispatch(msgProps, static_cast<Structural *>(obj.get()), _messageFlags.cascade, _messageFlags.relay, true));
	else {
		warning("Invalid message destination (target object is not a modifier or structural object)");
		return kMiniscriptInstructionOutcomeContinue;
	}

	thread->popValues(2);

	if (_messageFlags.immediate) {
		thread->getRuntime()->sendMessageOnVThread(dispatch);
		return kMiniscriptInstructionOutcomeYieldToVThread;
	} else {
		thread->getRuntime()->queueMessage(dispatch);
		return kMiniscriptInstructionOutcomeContinue;
	}
}

MiniscriptInstructionOutcome BinaryArithInstruction::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &rs = thread->getStackValueFromTop(0).value;
	DynamicValue &lsDest = thread->getStackValueFromTop(1).value;

	if (lsDest.getType() == DynamicValueTypes::kPoint && rs.getType() == DynamicValueTypes::kPoint) {
		Common::Point lsPoint = lsDest.getPoint();
		Common::Point rsPoint = rs.getPoint();

		double resultX = 0.0;
		double resultY = 0.0;
		outcome = arithExecute(thread, resultX, lsPoint.x, rsPoint.x);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;

		outcome = arithExecute(thread, resultY, lsPoint.y, rsPoint.y);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;

		lsDest.setPoint(Common::Point(static_cast<int16>(round(resultX)), static_cast<int16>(round(resultY))));
	} else {
		double leftVal = 0.0;
		switch (lsDest.getType()) {
		case DynamicValueTypes::kInteger:
			leftVal = lsDest.getInt();
			break;
		case DynamicValueTypes::kFloat:
			leftVal = lsDest.getFloat();
			break;
		case DynamicValueTypes::kBoolean:
			leftVal = lsDest.getBool() ? 1.0 : 0.0;
			break;
		default:
			thread->error("Invalid left-side type for binary arithmetic operator");
			return kMiniscriptInstructionOutcomeFailed;
		}

		double rightVal = 0.0;
		switch (rs.getType()) {
		case DynamicValueTypes::kInteger:
			rightVal = rs.getInt();
			break;
		case DynamicValueTypes::kFloat:
			rightVal = rs.getFloat();
			break;
		case DynamicValueTypes::kBoolean:
			rightVal = rs.getBool() ? 1.0 : 0.0;
			break;
		default:
			thread->error("Invalid right-side type for binary arithmetic operator");
			return kMiniscriptInstructionOutcomeFailed;
		}

		double result = 0.0;
		outcome = arithExecute(thread, result, leftVal, rightVal);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;

		lsDest.setFloat(result);
	}

	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Add::arithExecute(MiniscriptThread *thread, double &result, double left, double right) const {
	result = left + right;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Sub::arithExecute(MiniscriptThread *thread, double &result, double left, double right) const {
	result = left - right;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Mul::arithExecute(MiniscriptThread *thread, double &result, double left, double right) const {
	result = left * right;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Div::arithExecute(MiniscriptThread *thread, double &result, double left, double right) const {
	if (right == 0.0) {
		thread->error("Arithmetic error: Division by zero");
		return kMiniscriptInstructionOutcomeFailed;
	}
	result = left / right;
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Pow::arithExecute(MiniscriptThread *thread, double &result, double left, double right) const {
	if (left < 0.0 && right != floor(right)) {
		thread->error("Arithmetic error: Left side is negative but right side is not an integer");
		return kMiniscriptInstructionOutcomeFailed;
	}
	result = pow(left, right);
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome DivInt::arithExecute(MiniscriptThread *thread, double &result, double left, double right) const {
	if (right == 0.0) {
		thread->error("Arithmetic error: Integer division by zero");
		return kMiniscriptInstructionOutcomeFailed;
	}
	result = floor(left / right);
	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Modulo::arithExecute(MiniscriptThread *thread, double &result, double left, double right) const {
	if (right == 0.0) {
		thread->error("Arithmetic error: Modulo division by zero");
		return kMiniscriptInstructionOutcomeFailed;
	}

	// fmod keeps the sign from the left operand, but mTropolis modulo keeps the
	// sign of the right operand.
	double r = fmod(left, right);
	if (signbit(left) != signbit(right)) {
		if (r == 0.0)
			r = copysign(0.0, right);
		else
			r += right;
	}

	result = r;

	return kMiniscriptInstructionOutcomeContinue;
}

static bool isNullObjectReference(MiniscriptThread *thread, const DynamicValue &value) {
	if (value.getType() != DynamicValueTypes::kObject)
		return false;

	Common::SharedPtr<RuntimeObject> obj = value.getObject().object.lock();
	if (!obj)
		return true;

	if (obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable()) {
		DynamicValue target;
		if (obj->readAttribute(thread, target, "object"))
			return target.getType() == DynamicValueTypes::kNull || (target.getType() == DynamicValueTypes::kObject && target.getObject().object.expired());
	}

	return false;
}

MiniscriptInstructionOutcome UnorderedCompareInstruction::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &rs = thread->getStackValueFromTop(0).value;
	DynamicValue &lsDest = thread->getStackValueFromTop(1).value;

	bool isEqual = false;
	bool isUndefined = false;

	// An object reference to nothing equals null, including an object reference variable
	// (which compares as itself, so that its attributes can be reached) that refers to nothing
	bool lsIsNullObject = isNullObjectReference(thread, lsDest);
	bool rsIsNullObject = isNullObjectReference(thread, rs);

	switch (lsDest.getType()) {
	case DynamicValueTypes::kNull:
	case DynamicValueTypes::kObject:
		if ((lsIsNullObject || lsDest.getType() == DynamicValueTypes::kNull) && (rsIsNullObject || rs.getType() == DynamicValueTypes::kNull))
			isEqual = true;
		else
			isEqual = (lsDest == rs);
		break;
	case DynamicValueTypes::kString:
		if (rs.getType() == DynamicValueTypes::kString)
			isEqual = caseInsensitiveEqual(lsDest.getString(), rs.getString());
		break;
	case DynamicValueTypes::kBoolean: {
			switch (rs.getType()) {
			case DynamicValueTypes::kInteger:
				isEqual = (rs.getInt() == (lsDest.getBool() ? 1 : 0));
				break;
			case DynamicValueTypes::kFloat:
				isEqual = (rs.getFloat() == (lsDest.getBool() ? 1.0 : 0.0));
				break;
			case DynamicValueTypes::kBoolean:
				isEqual = (rs.getBool() == lsDest.getBool());
				break;
			default:
				isEqual = (lsDest.getBool() == false);
				break;
			}
		} break;
	case DynamicValueTypes::kFloat: {
			if (isnan(lsDest.getFloat()))
				isUndefined = true;
			else {
				switch (rs.getType()) {
				case DynamicValueTypes::kInteger:
					isEqual = (rs.getInt() == lsDest.getFloat());
					break;
				case DynamicValueTypes::kFloat:
					if (isnan(rs.getFloat()))
						isUndefined = true;
					else
						isEqual = (rs.getFloat() == lsDest.getFloat());
					break;
				case DynamicValueTypes::kBoolean:
					isEqual = ((rs.getBool() ? 1.0 : 0.0) == lsDest.getFloat());
					break;
				default:
					isEqual = false;
					break;
				}
			}
		} break;
	case DynamicValueTypes::kInteger: {
			switch (rs.getType()) {
			case DynamicValueTypes::kInteger:
				isEqual = (rs.getInt() == lsDest.getInt());
				break;
			case DynamicValueTypes::kFloat:
				if (isnan(rs.getFloat()))
					isUndefined = true;
				else
					isEqual = (rs.getFloat() == lsDest.getInt());
				break;
			case DynamicValueTypes::kBoolean:
				isEqual = ((rs.getBool() ? 1 : 0) == lsDest.getInt());
				break;
			default:
				isEqual = false;
				break;
			}
		} break;
	case DynamicValueTypes::kLabel: {
			// Really not sure how this works but there are buggy scripts in Obsidian which
			// were probably written as "if loop = false then ..." except Miniscript resolved
			// "loop" as a sound marker (!) because a sound marker with that name exists instead
			// of resolving it as equivalent to element.loop as it usually would.
			// Strict equality checks prevent the "GEN_Streaming_Update on ALC" script from
			// working, which prevents the bqtstreaming and bstreaming flags from being cleared,
			// causing, among other things, the player to get stuck after the Forest->Bureau
			// transition because the stuck streaming flags are blocking the VO.
			if (rs.getType() == DynamicValueTypes::kBoolean)
				isEqual = !rs.getBool();
			else
				isEqual = (lsDest == rs);
		} break;
	default:
		isEqual = (lsDest == rs);
		break;
	}

	lsDest.setBool(isUndefined == false && this->resolve(isEqual));
	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}


MiniscriptInstructionOutcome And::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &rs = thread->getStackValueFromTop(0).value;
	DynamicValue &lsDest = thread->getStackValueFromTop(1).value;
	lsDest.setBool(miniscriptEvaluateTruth(lsDest) && miniscriptEvaluateTruth(rs));

	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Or::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &rs = thread->getStackValueFromTop(0).value;
	DynamicValue &lsDest = thread->getStackValueFromTop(1).value;
	lsDest.setBool(miniscriptEvaluateTruth(lsDest) || miniscriptEvaluateTruth(rs));

	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Neg::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 1) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &value = thread->getStackValueFromTop(0).value;
	switch (value.getType()) {
	case DynamicValueTypes::kFloat:
		value.setFloat(-value.getFloat());
		break;
	case DynamicValueTypes::kInteger: {
		int32 i = value.getInt();
		if (i == (0 - 1 -0x7fffffff))
			value.setFloat(-static_cast<double>(i));
		else
			value.setInt(-i);
		} break;
	default:
		thread->error("Couldn't negate a value of a non-numeric type");
		return kMiniscriptInstructionOutcomeFailed;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome Not::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 1) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &value = thread->getStackValueFromTop(0).value;
	value.setBool(!miniscriptEvaluateTruth(value));

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome OrderedCompareInstruction::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &rs = thread->getStackValueFromTop(0).value;
	DynamicValue &lsDest = thread->getStackValueFromTop(1).value;

	double leftValue = 0.0;
	double rightValue = 0.0;
	if (lsDest.getType() == DynamicValueTypes::kFloat)
		leftValue = lsDest.getFloat();
	else if (lsDest.getType() == DynamicValueTypes::kInteger)
		leftValue = lsDest.getInt();
	else {
		thread->error("Left-side value is invalid for comparison");
		return kMiniscriptInstructionOutcomeFailed;
	}

	if (rs.getType() == DynamicValueTypes::kFloat)
		rightValue = rs.getFloat();
	else if (rs.getType() == DynamicValueTypes::kInteger)
		rightValue = rs.getInt();
	else {
		thread->error("Right-side value is invalid for comparison");
		return kMiniscriptInstructionOutcomeFailed;
	}

	lsDest.setBool(this->compareFloat(leftValue, rightValue));
	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

BuiltinFunc::BuiltinFunc(BuiltinFunctionID bfid) : _funcID(bfid) {
}

MiniscriptInstructionOutcome BuiltinFunc::execute(MiniscriptThread *thread) const {
	size_t stackArgsNeeded = 1;
	bool returnsValue = true;

	if (thread->getStackSize() < stackArgsNeeded) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	for (size_t i = 0; i < stackArgsNeeded; i++) {
		MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(i);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;
	}

	DynamicValue staticDest;
	DynamicValue *dest = nullptr;

	if (returnsValue) {
		if (stackArgsNeeded > 0)
			dest = &thread->getStackValueFromTop(stackArgsNeeded - 1).value;
		else
			dest = &staticDest;
	}

	MiniscriptInstructionOutcome outcome = executeFunction(thread, dest);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	if (stackArgsNeeded > 0) {
		size_t valuesToPop = stackArgsNeeded;
		if (returnsValue)
			valuesToPop--;

		if (valuesToPop > 0) {
			// coverity[dead_error_line]
			thread->popValues(valuesToPop);
		}
	} else {
		// coverity[dead_error_line]
		if (returnsValue)
			thread->pushValue(staticDest);
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BuiltinFunc::executeFunction(MiniscriptThread *thread, DynamicValue *returnValue) const {
	switch (_funcID) {
	case kSin:
	case kCos:
	case kRandom:
	case kSqrt:
	case kTan:
	case kAbs:
	case kSign:
	case kArctangent:
	case kExp:
	case kLn:
	case kLog:
	case kCosH:
	case kSinH:
	case kTanH:
	case kTrunc:
	case kRound:
		return executeSimpleNumericInstruction(thread, returnValue);
	case kRect2Polar:
		return executeRectToPolar(thread, returnValue);
	case kPolar2Rect:
		return executePolarToRect(thread, returnValue);
	case kNum2Str:
		return executeNum2Str(thread, returnValue);
	case kStr2Num:
		return executeStr2Num(thread, returnValue);
	default:
		thread->error("Unimplemented built-in function");
		return kMiniscriptInstructionOutcomeFailed;
	}
}

MiniscriptInstructionOutcome BuiltinFunc::executeSimpleNumericInstruction(MiniscriptThread *thread, DynamicValue *returnValue) const {
	double result = 0.0;

	double input = 0.0;
	const DynamicValue &inputDynamicValue = thread->getStackValueFromTop(0).value;

	switch (inputDynamicValue.getType()) {
	case DynamicValueTypes::kInteger:
		input = inputDynamicValue.getInt();
		break;
	case DynamicValueTypes::kFloat:
		input = inputDynamicValue.getFloat();
		break;
	default:
		thread->error("Invalid numeric function input type");
		return kMiniscriptInstructionOutcomeFailed;
	}

	switch (_funcID) {
	case kSin:
		result = sin(input * (M_PI / 180.0));
		break;
	case kCos:
		result = cos(input * (M_PI / 180.0));
		break;
	case kRandom:
		if (input < 1.5)
			result = 0.0;
		else {
			uint rngMax = static_cast<uint>(floor(input + 0.5)) - 1;
			result = thread->getRuntime()->getRandom()->getRandomNumber(rngMax);
		}
		break;
	case kSqrt:
		result = sqrt(input);
		break;
	case kTan:
		result = tan(input * (M_PI / 180.0));
		break;
	case kAbs:
		result = fabs(input);
		break;
	case kSign:
		if (input < 0.0)
			result = -1;
		else if (input > 0.0)
			result = 1;
		else
			result = 0;
		break;
	case kArctangent:
		result = atan(input) * (180.0 / M_PI);
		break;
	case kExp:
		result = exp(input);
		break;
	case kLn:
		result = log(input);
		break;
	case kLog:
		result = log10(input);
		break;
	case kCosH:
		result = cosh(input * (M_PI / 180.0));
		break;
	case kSinH:
		result = sinh(input * (M_PI / 180.0));
		break;
	case kTanH:
		result = tanh(input * (M_PI / 180.0));
		break;
	case kTrunc:
		result = trunc(input);
		break;
	case kRound:
		result = round(input);
		break;
	default:
		thread->error("Unimplemented numeric function");
		return kMiniscriptInstructionOutcomeFailed;
	}

	returnValue->setFloat(result);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BuiltinFunc::executeRectToPolar(MiniscriptThread *thread, DynamicValue *returnValue) const {
	const DynamicValue &inputDynamicValue = thread->getStackValueFromTop(0).value;

	if (inputDynamicValue.getType() != DynamicValueTypes::kPoint) {
		thread->error("Polar to rect input must be a vector");
		return kMiniscriptInstructionOutcomeFailed;
	}

	Common::Point pt = inputDynamicValue.getPoint();

	double angle = atan2(pt.y, pt.x);
	double magnitude = sqrt(pt.x * pt.x + pt.y * pt.y);

	returnValue->setVector(AngleMagVector::createRadians(angle, magnitude));

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BuiltinFunc::executePolarToRect(MiniscriptThread *thread, DynamicValue *returnValue) const {
	const DynamicValue &inputDynamicValue = thread->getStackValueFromTop(0).value;

	if (inputDynamicValue.getType() != DynamicValueTypes::kVector) {
		thread->error("Polar to rect input must be a vector");
		return kMiniscriptInstructionOutcomeFailed;
	}

	const AngleMagVector &vec = inputDynamicValue.getVector();

	double x = cos(vec.angleDegrees * (M_PI / 180.0)) * vec.magnitude;
	double y = sin(vec.angleDegrees * (M_PI / 180.0)) * vec.magnitude;

	returnValue->setPoint(Common::Point(static_cast<int16>(round(x)), static_cast<int16>(round(y))));

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BuiltinFunc::executeNum2Str(MiniscriptThread *thread, DynamicValue *returnValue) const {
	Common::String result;

	const DynamicValue &inputDynamicValue = thread->getStackValueFromTop(0).value;
	switch (inputDynamicValue.getType()) {
	case DynamicValueTypes::kInteger:
		result = Common::String::format("%i", static_cast<int>(inputDynamicValue.getInt()));
		break;
	case DynamicValueTypes::kFloat:
		result = Common::String::format("%g", static_cast<double>(inputDynamicValue.getFloat()));
		break;
	default:
		thread->error("Invalid input value to num2str");
		return kMiniscriptInstructionOutcomeFailed;
	}

	returnValue->setString(result);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome BuiltinFunc::executeStr2Num(MiniscriptThread *thread, DynamicValue *returnValue) const {
	double result = 0.0;

	const DynamicValue &inputDynamicValue = thread->getStackValueFromTop(0).value;
	if (inputDynamicValue.getType() != DynamicValueTypes::kString) {
		thread->error("Invalid input value to str2num");
		return kMiniscriptInstructionOutcomeFailed;
	}

	const Common::String &str = inputDynamicValue.getString();
	if (str.empty())
		result = 0.0;
	else if (!sscanf(str.c_str(), "%lf", &result)) {
		// NOTE: sscanf will properly handle cases where a number is followed by a non-numeric value,
		// which is consistent with mTropolis' behavior.
		// 
		// If it fails, the result is 0.0 and no script error.
		//
		// This includes a case in Obsidian where it tries to parse in invalid room number "L100"
		// upon entering the sky face for the first time in Bureau.

#ifdef MTROPOLIS_DEBUG_ENABLE
		if (Debugger *debugger = thread->getRuntime()->debugGetDebugger())
			debugger->notify(kDebugSeverityWarning, Common::String::format("Failed to parse '%s' as a number", str.c_str()));
#endif

		result = 0.0;
	}

	returnValue->setFloat(result);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome StrConcat::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &rVal = thread->getStackValueFromTop(0).value;
	DynamicValue &lValDest = thread->getStackValueFromTop(1).value;

	if (rVal.getType() != DynamicValueTypes::kString) {
		thread->error("String concat right side was not a string");
		return kMiniscriptInstructionOutcomeFailed;
	}

	if (lValDest.getType() != DynamicValueTypes::kString) {
		thread->error("String concat left side was not a string");
		return kMiniscriptInstructionOutcomeFailed;
	}

	lValDest.setString(lValDest.getString() + rVal.getString());
	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome PointCreate::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &yVal = thread->getStackValueFromTop(0).value;
	DynamicValue &xValDest = thread->getStackValueFromTop(1).value;

	int16 coords[2];
	DynamicValue *coordInputs[2] = {&xValDest, &yVal};

	for (int i = 0; i < 2; i++) {
		DynamicValue *v = coordInputs[i];
		DynamicValue listContents;

		if (v->getType() == DynamicValueTypes::kList) {
			// Yes this is actually allowed
			const Common::SharedPtr<DynamicList> &list = v->getList();
			if (list->getSize() != 1 || !list->getAtIndex(0, listContents)) {
				thread->error("Can't convert list to integer");
				return kMiniscriptInstructionOutcomeFailed;
			}

			v = &listContents;
		}

		switch (v->getType()) {
		case DynamicValueTypes::kFloat:
			coords[i] = static_cast<int16>(floor(v->getFloat() + 0.5)) & 0xffff;
			break;
		case DynamicValueTypes::kInteger:
			coords[i] = static_cast<int16>(v->getInt());
			break;
		case DynamicValueTypes::kBoolean:
			coords[i] = (v->getBool()) ? 1 : 0;
			break;
		default:
			thread->error("Invalid input for point creation");
			return kMiniscriptInstructionOutcomeFailed;
		}
	}

	xValDest.setPoint(Common::Point(coords[0], coords[1]));

	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

// Makes a vector from an (angle, magnitude) pair, angle in degrees
MiniscriptInstructionOutcome VectorCreate::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &magVal = thread->getStackValueFromTop(0).value;
	DynamicValue &angleValDest = thread->getStackValueFromTop(1).value;

	DynamicValue angleConverted;
	DynamicValue magConverted;
	if (!angleValDest.convertToType(DynamicValueTypes::kFloat, angleConverted) || !magVal.convertToType(DynamicValueTypes::kFloat, magConverted)) {
		thread->error("Invalid input for vector creation");
		return kMiniscriptInstructionOutcomeFailed;
	}

	AngleMagVector vec;
	vec.angleDegrees = angleConverted.getFloat();
	vec.magnitude = magConverted.getFloat();
	angleValDest.setVector(vec);

	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome RangeCreate::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	outcome = thread->dereferenceRValue(1);
	if (outcome != kMiniscriptInstructionOutcomeContinue)
		return outcome;

	DynamicValue &yVal = thread->getStackValueFromTop(0).value;
	DynamicValue &xValDest = thread->getStackValueFromTop(1).value;

	int32 coords[2];
	DynamicValue *coordInputs[2] = {&xValDest, &yVal};

	for (int i = 0; i < 2; i++) {
		DynamicValue *v = coordInputs[i];
		DynamicValue listContents;

		if (v->getType() == DynamicValueTypes::kList) {
			// Yes this is actually allowed
			const Common::SharedPtr<DynamicList> &list = v->getList();
			if (list->getSize() != 1 || !list->getAtIndex(0, listContents)) {
				thread->error("Can't convert list to integer");
				return kMiniscriptInstructionOutcomeFailed;
			}

			v = &listContents;
		}

		switch (v->getType()) {
		case DynamicValueTypes::kFloat:
			coords[i] = static_cast<int32>(floor(v->getFloat() + 0.5));
			break;
		case DynamicValueTypes::kInteger:
			coords[i] = v->getInt();
			break;
		case DynamicValueTypes::kBoolean:
			coords[i] = (v->getBool()) ? 1 : 0;
			break;
		default:
			thread->error("Invalid input for point creation");
			return kMiniscriptInstructionOutcomeFailed;
		}
	}

	xValDest.setIntRange(IntRange(coords[0], coords[1]));

	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

GetChild::GetChild(uint32 attribute, bool isLValue, bool isIndexed)
	: _attribute(attribute), _isLValue(isLValue), _isIndexed(isIndexed) {
}

MiniscriptInstructionOutcome ArgSeparator::execute(MiniscriptThread *thread) const {
	return kMiniscriptInstructionOutcomeContinue;
}

CallFunction::CallFunction(uint32 functionNameAttribute, uint32 numArgs) : _functionNameAttribute(functionNameAttribute), _numArgs(numArgs) {
}

namespace StringFunctions {

static const char *const kWordDelimiters = " \t\n\r";
static const char *const kParagraphDelimiters = "\n\r";

static bool isDelimiter(const char *delimiters, char c) {
	// Matches strchr, which also finds the terminator
	return c == '\0' || strchr(delimiters, c) != nullptr;
}

// Converts a 1-based range (negative values count from the end) to an offset and length
static void clampRange(int32 start, int32 end, int32 length, int32 &outOffset, int32 &outCount) {
	outOffset = 0;
	outCount = 0;

	if ((start == 0 && end == 0) || length < 1)
		return;

	start = CLIP<int32>(start, -length, length + 1);
	end = CLIP<int32>(end, -length, length + 1);

	if (start < 0)
		start += length + 1;
	if (end < 0)
		end += length + 1;

	if (end == 0) {
		outCount = 0;
		outOffset = start - 1;
	} else {
		if (start == 0)
			start = 1;

		int32 lo = MIN(start, end);
		int32 hi = MAX(start, end);
		outOffset = lo - 1;
		outCount = hi - lo + 1;
	}

	if (outOffset + outCount > length)
		outCount = length - outOffset;
}

static int32 countChunks(const Common::String &str, const char *delimiters, bool collapse) {
	int32 count = 0;
	bool prevWasDelimiter = false;

	for (uint i = 0; i < str.size(); i++) {
		bool delim = strchr(delimiters, str[i]) != nullptr;
		if (delim && (!collapse || !prevWasDelimiter))
			count++;
		prevWasDelimiter = delim;
	}

	if (!prevWasDelimiter)
		count++;

	if (isDelimiter(delimiters, str.empty() ? '\0' : str[0]))
		count--;

	return count;
}

// Finds chunk n (1-based, negative counts from the end) as [start, end)
static void findChunk(const Common::String &str, const char *delimiters, int32 n, int32 &outStart, int32 &outEnd, bool collapse) {
	outStart = 0;
	outEnd = 0;

	if (n == 0)
		return;

	if (n < 0) {
		n += countChunks(str, delimiters, collapse) + 1;
		if (n < 1)
			return;
	}

	const char *s = str.c_str();

	if (collapse && isDelimiter(delimiters, s[0]))
		outStart = strspn(s, delimiters);

	outEnd = outStart + strcspn(s + outStart, delimiters);
	n--;

	if (n == 0)
		return;

	while (s[outEnd] != '\0') {
		outStart = outEnd + 1;
		if (collapse)
			outStart += strspn(s + outStart, delimiters);
		outEnd = outStart + strcspn(s + outStart, delimiters);
		n--;
		if (n == 0)
			return;
	}

	outStart = outEnd;
}

static bool getString(const DynamicValue &value, Common::String &outStr) {
	if (value.getType() == DynamicValueTypes::kString) {
		outStr = value.getString();
		return true;
	}

	DynamicValue converted;
	if (!value.convertToType(DynamicValueTypes::kString, converted))
		return false;

	outStr = converted.getString();
	return true;
}

static bool getInt(const DynamicValue &value, int32 &outInt) {
	return value.roundToInt(outInt);
}

static bool getBool(const DynamicValue &value, bool &outBool) {
	DynamicValue converted;
	if (!value.convertToType(DynamicValueTypes::kBoolean, converted))
		return false;

	outBool = converted.getBool();
	return true;
}

// Ranges may also be given as a single index
static bool getRange(const DynamicValue &value, int32 &outStart, int32 &outEnd) {
	if (value.getType() == DynamicValueTypes::kIntegerRange) {
		outStart = value.getIntRange().min;
		outEnd = value.getIntRange().max;
		return true;
	}

	if (!getInt(value, outStart))
		return false;

	outEnd = outStart;
	return true;
}

static void setRangeResult(DynamicValue &result, int32 start, int32 end) {
	result.setIntRange(IntRange(start, end));
}

static int32 compareStrings(const Common::String &a, const Common::String &b, bool caseSensitive) {
	int32 cmp = caseSensitive ? a.compareTo(b) : a.compareToIgnoreCase(b);
	return (cmp < 0) ? -1 : ((cmp > 0) ? 1 : 0);
}

static int32 findString(const char *haystack, const char *needle, bool caseSensitive) {
	size_t needleLength = strlen(needle);
	if (needleLength == 0)
		return -1;

	for (const char *p = haystack; *p; p++) {
		bool match = caseSensitive ? (strncmp(p, needle, needleLength) == 0) : (scumm_strnicmp(p, needle, needleLength) == 0);
		if (match)
			return p - haystack;
	}

	return -1;
}

// Offset of occurrence n (1-based) searching forwards, or -1
static int32 findOccurrence(const Common::String &haystack, const Common::String &needle, bool caseSensitive, int32 n) {
	int32 offset = 0;
	int32 found = -1;

	while (n > 0) {
		n--;
		int32 pos = findString(haystack.c_str() + offset, needle.c_str(), caseSensitive);
		if (pos < 0)
			return -1;

		found = offset + pos;
		offset = found + needle.size();
	}

	return found;
}

// Offset of occurrence n (1-based) searching backwards, or -1
static int32 findOccurrenceReverse(const Common::String &haystack, const Common::String &needle, bool caseSensitive, int32 n) {
	int32 pos = static_cast<int32>(haystack.size()) - static_cast<int32>(needle.size());
	if (needle.empty())
		return -1;

	while (n > 0 && pos >= 0) {
		bool match = caseSensitive ? (strncmp(haystack.c_str() + pos, needle.c_str(), needle.size()) == 0)
								   : (scumm_strnicmp(haystack.c_str() + pos, needle.c_str(), needle.size()) == 0);
		if (match) {
			n--;
			if (n == 0)
				return pos;
			pos -= needle.size();
		} else
			pos--;
	}

	return -1;
}

enum ChunkMode {
	kChunkModeSubstring,
	kChunkModeRange,
};

static bool chunkFunction(const DynamicValue &rangeArg, const Common::String &str, const char *delimiters, ChunkMode mode, bool collapse, DynamicValue &result) {
	int32 first = 0;
	int32 last = 0;
	if (!getRange(rangeArg, first, last))
		return false;

	int32 start = 0;
	int32 end = 0;
	if (first != 0 || last != 0) {
		int32 start1, end1, start2, end2;
		findChunk(str, delimiters, first, start1, end1, collapse);
		findChunk(str, delimiters, last, start2, end2, collapse);
		start = MIN(start1, start2);
		end = MAX(end1, end2);
	}

	if (mode == kChunkModeSubstring) {
		result.setString(str.substr(start, end - start));
	} else {
		if (start != 0 || end != 0) {
			if (start == end)
				start = end = 0;
			else
				start++;
		}
		setRangeResult(result, start, end);
	}

	return true;
}

static bool replaceRange(const DynamicValue &rangeArg, const Common::String &str, const Common::String &replacement, DynamicValue &result) {
	int32 first = 0;
	int32 last = 0;
	if (!getRange(rangeArg, first, last))
		return false;

	int32 offset = 0;
	int32 count = 0;
	clampRange(first, last, str.size(), offset, count);

	result.setString(str.substr(0, offset) + replacement + str.substr(offset + count));
	return true;
}

} // End of namespace StringFunctions

MiniscriptInstructionOutcome CallFunction::execute(MiniscriptThread *thread) const {
	using namespace StringFunctions;

	const Common::Array<MiniscriptProgram::Attribute> &attribs = thread->getProgram()->getAttributes();
	if (_functionNameAttribute >= attribs.size()) {
		thread->error("Invalid function name index");
		return kMiniscriptInstructionOutcomeFailed;
	}

	if (_numArgs == 0 || thread->getStackSize() < _numArgs) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	for (uint i = 0; i < _numArgs; i++) {
		MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(i);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;
	}

	Common::Array<DynamicValue> args;
	args.resize(_numArgs);
	for (uint i = 0; i < _numArgs; i++)
		args[i] = thread->getStackValueFromTop(_numArgs - 1 - i).value;

	const Common::String &func = attribs[_functionNameAttribute].name;
	const uint numArgs = _numArgs;
	DynamicValue result;
	bool ok = false;

	Common::String str, str2;
	int32 n = 0;
	bool flag = false;

	if (func.equalsIgnoreCase("stringlength")) {
		if (numArgs == 1 && getString(args[0], str)) {
			result.setInt(str.size());
			ok = true;
		}
	} else if (func.equalsIgnoreCase("character")) {
		int32 first, last;
		if (numArgs == 2 && getRange(args[0], first, last) && getString(args[1], str)) {
			int32 offset, count;
			clampRange(first, last, str.size(), offset, count);
			result.setString(str.substr(offset, count));
			ok = true;
		}
	} else if (func.equalsIgnoreCase("chunk") || func.equalsIgnoreCase("chunkrange")) {
		if (numArgs == 3 && getString(args[1], str) && getString(args[2], str2))
			ok = chunkFunction(args[0], str, str2.c_str(), func.equalsIgnoreCase("chunk") ? kChunkModeSubstring : kChunkModeRange, false, result);
	} else if (func.equalsIgnoreCase("chunkcount")) {
		if (numArgs == 2 && getString(args[0], str) && getString(args[1], str2)) {
			result.setInt(countChunks(str, str2.c_str(), false));
			ok = true;
		}
	} else if (func.equalsIgnoreCase("word") || func.equalsIgnoreCase("wordrange")) {
		if (numArgs == 2 && getString(args[1], str))
			ok = chunkFunction(args[0], str, kWordDelimiters, func.equalsIgnoreCase("word") ? kChunkModeSubstring : kChunkModeRange, true, result);
	} else if (func.equalsIgnoreCase("wordcount")) {
		if (numArgs == 1 && getString(args[0], str)) {
			result.setInt(countChunks(str, kWordDelimiters, true));
			ok = true;
		}
	} else if (func.equalsIgnoreCase("paragraph") || func.equalsIgnoreCase("paragraphrange")) {
		if (numArgs == 2 && getString(args[1], str))
			ok = chunkFunction(args[0], str, kParagraphDelimiters, func.equalsIgnoreCase("paragraph") ? kChunkModeSubstring : kChunkModeRange, false, result);
	} else if (func.equalsIgnoreCase("paragraphcount")) {
		if (numArgs == 1 && getString(args[0], str)) {
			result.setInt(countChunks(str, kParagraphDelimiters, false));
			ok = true;
		}
	} else if (func.equalsIgnoreCase("comparestrings") || func.equalsIgnoreCase("equalstrings")) {
		if ((numArgs == 2 || numArgs == 3) && getString(args[0], str) && getString(args[1], str2) && (numArgs < 3 || getBool(args[2], flag))) {
			int32 cmp = compareStrings(str, str2, flag);
			if (func.equalsIgnoreCase("comparestrings"))
				result.setInt(cmp);
			else
				result.setBool(cmp == 0);
			ok = true;
		}
	} else if (func.equalsIgnoreCase("deletestring")) {
		if (numArgs == 2 && getString(args[1], str))
			ok = replaceRange(args[0], str, Common::String(), result);
	} else if (func.equalsIgnoreCase("replacestring")) {
		if (numArgs == 3 && getString(args[1], str) && getString(args[2], str2))
			ok = replaceRange(args[0], str, str2, result);
	} else if (func.equalsIgnoreCase("insertstring")) {
		// Inserts the first argument into the second, before the position in the third
		if (numArgs == 3 && getString(args[0], str2) && getString(args[1], str) && getInt(args[2], n)) {
			DynamicValue range;
			range.setIntRange(IntRange(n, 0));
			ok = replaceRange(range, str, str2, result);
		}
	} else if (func.equalsIgnoreCase("findstringrange")) {
		int32 occurrence = 1;
		if (numArgs >= 2 && numArgs <= 4 && getString(args[0], str2) && getString(args[1], str)
			&& (numArgs < 3 || getBool(args[2], flag)) && (numArgs < 4 || getInt(args[3], occurrence))) {
			int32 pos = (occurrence < 1) ? findOccurrenceReverse(str, str2, flag, -occurrence) : findOccurrence(str, str2, flag, occurrence);
			if (pos < 0)
				setRangeResult(result, 0, 0);
			else
				setRangeResult(result, pos + 1, pos + str2.size());
			ok = true;
		}
	} else if (func.equalsIgnoreCase("findstringcount")) {
		if ((numArgs == 2 || numArgs == 3) && getString(args[0], str2) && getString(args[1], str) && (numArgs < 3 || getBool(args[2], flag))) {
			int32 count = 0;
			int32 offset = 0;
			for (;;) {
				int32 pos = findString(str.c_str() + offset, str2.c_str(), flag);
				if (pos < 0)
					break;
				count++;
				offset += pos + str2.size();
			}
			result.setInt(count);
			ok = true;
		}
	} else if (func.equalsIgnoreCase("chartoascii")) {
		if (numArgs == 1 && getString(args[0], str)) {
			result.setInt(str.empty() ? 0 : static_cast<int32>(static_cast<int8>(str[0])));
			ok = true;
		}
	} else if (func.equalsIgnoreCase("asciitochar")) {
		if (numArgs == 1 && getInt(args[0], n)) {
			result.setString(Common::String(static_cast<char>(n)));
			ok = true;
		}
	} else if (func.equalsIgnoreCase("lowercase") || func.equalsIgnoreCase("uppercase")) {
		if (numArgs == 1 && getString(args[0], str)) {
			if (func.equalsIgnoreCase("lowercase"))
				str.toLowercase();
			else
				str.toUppercase();
			result.setString(str);
			ok = true;
		}
	} else if (func.equalsIgnoreCase("canget") || func.equalsIgnoreCase("canset")) {
		bool canAccess = false;
		if (numArgs == 2 && getString(args[1], str)) {
			Common::SharedPtr<RuntimeObject> obj;
			if (args[0].getType() == DynamicValueTypes::kObject)
				obj = args[0].getObject().object.lock();

			if (obj) {
				str.toLowercase();
				if (func.equalsIgnoreCase("canget")) {
					DynamicValue unused;
					canAccess = obj->readAttribute(thread, unused, str);
				} else {
					DynamicValueWriteProxy proxy;
					canAccess = (obj->writeRefAttribute(thread, proxy, str) == kMiniscriptInstructionOutcomeContinue);
				}
			}
			result.setBool(canAccess);
			ok = true;
		}
	} else {
		thread->error("Function '" + func + "' is not implemented");
		return kMiniscriptInstructionOutcomeFailed;
	}

	if (!ok) {
		thread->error(Common::String::format("Invalid arguments to function '%s'", func.c_str()));
		return kMiniscriptInstructionOutcomeFailed;
	}

	thread->popValues(numArgs);
	thread->pushValue(result);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome GetChild::execute(MiniscriptThread *thread) const {
	const Common::Array<MiniscriptProgram::Attribute> &attribs = thread->getProgram()->getAttributes();
	if (_attribute >= attribs.size()) {
		thread->error("Invalid attribute index");
		return kMiniscriptInstructionOutcomeFailed;
	}

	const Common::String &attrib = attribs[_attribute].name;

	MiniscriptInstructionOutcome outcome = kMiniscriptInstructionOutcomeFailed;

	if (_isIndexed) {
		if (thread->getStackSize() < 2) {
			thread->error("Stack underflow");
			return kMiniscriptInstructionOutcomeFailed;
		}

		// Convert index
		outcome = thread->dereferenceRValue(0);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;

		const MiniscriptStackValue &indexSlot = thread->getStackValueFromTop(0);
		MiniscriptStackValue &indexableValueSlot = thread->getStackValueFromTop(1);

		if (_isLValue) {
			if (indexableValueSlot.value.getType() == DynamicValueTypes::kObject) {
				Common::SharedPtr<RuntimeObject> obj = indexableValueSlot.value.getObject().object.lock();
				if (!obj) {
					thread->error("Tried to write '" + attrib + "' to an invalid object reference");
					return kMiniscriptInstructionOutcomeFailed;
				}

				DynamicValueWriteProxy proxy;
				outcome = obj->writeRefAttributeIndexed(thread, proxy, attrib, indexSlot.value);
				if (outcome == kMiniscriptInstructionOutcomeFailed) {
					thread->error("Failed to get a writeable reference to indexed attribute '" + attrib + "'");
					return outcome;
				}

				indexableValueSlot.value.setWriteProxy(proxy);
			} else if (indexableValueSlot.value.getType() == DynamicValueTypes::kWriteProxy) {
				DynamicValueWriteProxy proxy = indexableValueSlot.value.getWriteProxy();

				outcome = proxy.pod.ifc->refAttribIndexed(thread, proxy, proxy.pod.objectRef, proxy.pod.ptrOrOffset, attrib, indexSlot.value);
				if (outcome == kMiniscriptInstructionOutcomeFailed) {
					thread->error("Can't write to indexed attribute '" + attrib + "'");
					return outcome;
				}

				indexableValueSlot.value.setWriteProxy(proxy);
			} else {
				thread->error("Tried to l-value index something that was not writeable");
				return kMiniscriptInstructionOutcomeFailed;
			}
		} else {
			outcome = readRValueAttribIndexed(thread, indexableValueSlot.value, attrib, indexSlot.value);
			if (outcome != kMiniscriptInstructionOutcomeContinue)
				return outcome;
		}

		thread->popValues(1);
	} else {
		if (thread->getStackSize() < 1) {
			thread->error("Stack underflow");
			return kMiniscriptInstructionOutcomeFailed;
		}

		MiniscriptStackValue &indexableValueSlot = thread->getStackValueFromTop(0);

		if (_isLValue) {
			if (indexableValueSlot.value.getType() == DynamicValueTypes::kObject) {
				Common::SharedPtr<RuntimeObject> obj = indexableValueSlot.value.getObject().object.lock();
				if (!obj) {
					thread->error("Tried to indirect '" + attrib + "' using an invalid object reference");
					return kMiniscriptInstructionOutcomeFailed;
				}

				DynamicValueWriteProxy writeProxy;
				outcome = obj->writeRefAttribute(thread, writeProxy, attrib);
				if (outcome == kMiniscriptInstructionOutcomeFailed) {
					thread->error("Failed to get a writeable reference to attribute '" + attrib + "'");
					return outcome;
				}

				indexableValueSlot.value.setWriteProxy(writeProxy);
			} else if (indexableValueSlot.value.getType() == DynamicValueTypes::kWriteProxy) {
				DynamicValueWriteProxy proxy = indexableValueSlot.value.getWriteProxy();
				outcome = proxy.pod.ifc->refAttrib(thread, proxy, proxy.pod.objectRef, proxy.pod.ptrOrOffset, attrib);
				if (outcome == kMiniscriptInstructionOutcomeFailed) {
					thread->error("Can't write to attribute '" + attrib + "'");
					return outcome;
				}
				indexableValueSlot.value.setWriteProxy(proxy);
			} else {
				thread->error("Tried to l-value index something that was not writeable");
				return kMiniscriptInstructionOutcomeFailed;
			}
		} else {
			outcome = readRValueAttrib(thread, indexableValueSlot.value, attrib);
		}
	}

	return outcome;
}

MiniscriptInstructionOutcome GetChild::readRValueAttrib(MiniscriptThread *thread, DynamicValue &valueSrcDest, const Common::String &attrib) const {
	switch (valueSrcDest.getType()) {
	case DynamicValueTypes::kPoint:
		if (attrib == "x")
			valueSrcDest.setInt(valueSrcDest.getPoint().x);
		else if (attrib == "y")
			valueSrcDest.setInt(valueSrcDest.getPoint().y);
		else {
			thread->error("Point has no attribute '" + attrib + "'");
			return kMiniscriptInstructionOutcomeFailed;
		}
		break;
	case DynamicValueTypes::kIntegerRange:
		if (attrib == "start")
			valueSrcDest.setInt(valueSrcDest.getIntRange().min);
		else if (attrib == "end")
			valueSrcDest.setInt(valueSrcDest.getIntRange().max);
		else {
			thread->error("Integer range has no attribute '" + attrib + "'");
			return kMiniscriptInstructionOutcomeFailed;
		}
		break;

	case DynamicValueTypes::kVector:
		if (attrib == "angle")
			valueSrcDest.setFloat(valueSrcDest.getVector().angleDegrees);
		else if (attrib == "magnitude")
			valueSrcDest.setFloat(valueSrcDest.getVector().magnitude);
		else {
			thread->error("Vector has no attribute '" + attrib + "'");
			return kMiniscriptInstructionOutcomeFailed;
		}
		break;
	case DynamicValueTypes::kObject: {
			Common::SharedPtr<RuntimeObject> obj = valueSrcDest.getObject().object.lock();
			if (!obj) {
				thread->error("Unable to read object attribute '" + attrib + "' from invalid object");
				return kMiniscriptInstructionOutcomeFailed;
			} else if (!obj->readAttribute(thread, valueSrcDest, attrib)) {
				thread->error("Unable to read object attribute '" + attrib + "'");
				return kMiniscriptInstructionOutcomeFailed;
			}
		} break;
	case DynamicValueTypes::kList: {
			Common::SharedPtr<DynamicList> list = valueSrcDest.getList();
			if (attrib == "count") {
				valueSrcDest.setInt(list->getSize());
			} else {
				thread->error("Unable to read list attribute '" + attrib + "'");
				return kMiniscriptInstructionOutcomeFailed;
			}
		} break;
	default:
		thread->error("Unable to read attribute '" + attrib + "' from rvalue");
		return kMiniscriptInstructionOutcomeFailed;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome GetChild::readRValueAttribIndexed(MiniscriptThread *thread, DynamicValue &valueSrcDest, const Common::String &attrib, const DynamicValue &index) const {
	switch (valueSrcDest.getType()) {
	case DynamicValueTypes::kList:
		if (attrib == "value") {
			// Hold list ref since it may get released by the read operation
			Common::SharedPtr<DynamicList> list = valueSrcDest.getList();
			size_t realIndex = 0;
			if (!DynamicList::dynamicValueToIndex(realIndex, index)) {
				thread->error("Unable to list value at specified index");
				return kMiniscriptInstructionOutcomeFailed;
			}

			if (!list->getAtIndex(realIndex, valueSrcDest)) {
				thread->error("List read index out of bounds");
				return kMiniscriptInstructionOutcomeFailed;
			}
		} else {
			thread->error("Unable to read list attribute '" + attrib + "'");
			return kMiniscriptInstructionOutcomeFailed;
		}
		break;
	case DynamicValueTypes::kObject: {
			Common::SharedPtr<RuntimeObject> obj = valueSrcDest.getObject().object.lock();
			if (!obj) {
				thread->error("Unable to read object indexed attribute '" + attrib + "' from invalid object");
				return kMiniscriptInstructionOutcomeFailed;
			} else if (!obj->readAttributeIndexed(thread, valueSrcDest, attrib, index)) {
				Common::String objName;
				if (obj->isModifier())
					objName = static_cast<Modifier *>(obj.get())->getName();
				else if (obj->isStructural())
					objName = static_cast<Structural *>(obj.get())->getName();
				thread->error("Unable to read object indexed attribute '" + attrib + "' of '" + objName + "'");
				return kMiniscriptInstructionOutcomeFailed;
			}
		} break;
	default:
		thread->error("Unable to read indexed rvalue attribute '" + attrib + "'");
		return kMiniscriptInstructionOutcomeFailed;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

PushValue::PushValue(DataType dataType, const void *value, bool isLValue)
	: _dataType(dataType)/*, _isLValue(isLValue) */ {

	switch (dataType) {
	case DataType::kDataTypeBool:
		new (static_cast<bool *>(&_value.b)) bool(*static_cast<const bool *>(value));
		break;
	case DataType::kDataTypeDouble:
		new (static_cast<double *>(&_value.f)) double(*static_cast<const double *>(value));
		break;
	case DataType::kDataTypeLocalRef:
	case DataType::kDataTypeGlobalRef:
		new (static_cast<uint32 *>(&_value.ref)) uint32(*static_cast<const uint32 *>(value));
		break;
	case DataType::kDataTypeLabel:
		new (static_cast<Label *>(&_value.lbl)) Label(*static_cast<const Label *>(value));
		break;
	case DataType::kDataTypeNull:
		break;
	default:
		warning("PushValue instruction has an unknown type of value, this will probably malfunction!");
		break;
	}
}

PushValue::ValueUnion::ValueUnion() {
}

PushValue::~PushValue() {
	switch (_dataType) {
	case DataType::kDataTypeLabel:
		_value.lbl.~Label();
		break;
	default:
		break;
	}
}

MiniscriptInstructionOutcome ListCreate::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptStackValue &rs = thread->getStackValueFromTop(0);
	MiniscriptStackValue &lsDest = thread->getStackValueFromTop(1);

	// Lists of variables keep the variables, but a variable listed with numbers goes in as its
	// value (Magic School Bus Bugs lists a variable then numbers for its eye puzzle facets)
	Common::SharedPtr<DynamicList> list(new DynamicList());
	if (!list->setAtIndex(0, lsDest.value) || !list->setAtIndex(1, rs.value)) {
		MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
		if (outcome == kMiniscriptInstructionOutcomeContinue)
			outcome = thread->dereferenceRValue(1);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;

		list.reset(new DynamicList());
		if (!list->setAtIndex(0, lsDest.value)) {
			thread->error("Failed to set value 1 of list");
			return kMiniscriptInstructionOutcomeFailed;
		}
		if (!list->setAtIndex(1, rs.value)) {
			thread->error("Failed to set value 2 of list");
			return kMiniscriptInstructionOutcomeFailed;
		}
	}

	lsDest.value.setList(list);
	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome ListAppend::execute(MiniscriptThread *thread) const {
	if (thread->getStackSize() < 2) {
		thread->error("Stack underflow");
		return kMiniscriptInstructionOutcomeFailed;
	}

	MiniscriptStackValue &rs = thread->getStackValueFromTop(0);
	MiniscriptStackValue &lsDest = thread->getStackValueFromTop(1);

	if (lsDest.value.getType() != DynamicValueTypes::kList) {
		thread->error("Expected list on left side of list_append");
		return kMiniscriptInstructionOutcomeFailed;
	}

	Common::SharedPtr<DynamicList> listRef = lsDest.value.getList();
	if (listRef.refCount() != 2) {
		listRef = listRef->clone();
		lsDest.value.setList(listRef);
	}

	if (!listRef->setAtIndex(listRef->getSize(), rs.value)) {
		// A variable added to a list of values goes in as its value
		MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;

		if (!listRef->setAtIndex(listRef->getSize(), thread->getStackValueFromTop(0).value)) {
			thread->error("Failed to expand list");
			return kMiniscriptInstructionOutcomeFailed;
		}
	}

	thread->popValues(1);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome PushValue::execute(MiniscriptThread *thread) const {
	DynamicValue value;

	switch (_dataType) {
	case DataType::kDataTypeNull:
		value.clear();
		break;
	case DataType::kDataTypeDouble:
		value.setFloat(_value.f);
		break;
	case DataType::kDataTypeBool:
		value.setBool(_value.b);
		break;
	case DataType::kDataTypeLocalRef:
		value.setObject(ObjectReference(thread->getRefs()->getRefByIndex(_value.ref)));
		break;
	case DataType::kDataTypeGlobalRef:
		value.setObject(ObjectReference(thread->getRefs()->getGlobalRefByIndex(_value.ref)));
		break;
	case DataType::kDataTypeLabel: {
		MTropolis::Label label;
		label.id = _value.lbl.id;
		label.superGroupID = _value.lbl.superGroup;
		value.setLabel(label);
	} break;
	default:
		assert(false);
		break;
	}

	thread->pushValue(value);

	return kMiniscriptInstructionOutcomeContinue;
}

PushGlobal::PushGlobal(uint32 globalID, bool isLValue) : _globalID(globalID), _isLValue(isLValue) {
}

MiniscriptInstructionOutcome PushGlobal::execute(MiniscriptThread *thread) const {
	thread->pushValue(DynamicValue());

	DynamicValue &value = thread->getStackValueFromTop(0).value;

	switch (_globalID) {
	case kGlobalRefElement:
	case kGlobalRefSection:
	case kGlobalRefScene:
	case kGlobalRefProject:
		return executeFindFilteredParent(thread, value);
	case kGlobalRefModifier:
		value.setObject(thread->getModifier()->getSelfReference());
		break;
	case kGlobalRefIncomingData:
		if (_isLValue) {
			DynamicValueWriteProxy proxy;
			thread->createWriteIncomingDataProxy(proxy);
			value.setWriteProxy(proxy);
		} else
			value = thread->getMessageProperties()->getValue();
		break;
	case kGlobalRefSource:
		value.setObject(ObjectReference(thread->getMessageProperties()->getSource()));
		break;
	case kGlobalRefMouse:
		value.setPoint(thread->getRuntime()->getCachedMousePosition());
		break;
	case kGlobalRefTicks:
		value.setInt(thread->getRuntime()->getPlayTime() * 60 / 1000);
		break;
	case kGlobalRefSharedScene:
		value.setObject(ObjectReference(thread->getRuntime()->getActiveSharedScene()));
		break;
	case kGlobalRefActiveScene:
		value.setObject(ObjectReference(thread->getRuntime()->getActiveMainScene()));
		break;
	default:
		assert(false);
		thread->error("Unknown global ref type");
		return kMiniscriptInstructionOutcomeFailed;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome PushGlobal::executeFindFilteredParent(MiniscriptThread *thread, DynamicValue &result) const {
	Common::WeakPtr<RuntimeObject> ref = thread->getModifier()->getSelfReference();
	for (;;) {
		Common::SharedPtr<RuntimeObject> obj = ref.lock();
		if (!obj)
			break;

		bool isMatch = false;
		switch (_globalID) {
		case kGlobalRefElement:
			isMatch = obj->isStructural();	// We don't classify the project, sections, and subsections as elements, but mTropolis does
			break;
		case kGlobalRefSection:
			isMatch = obj->isSection();
			break;
		case kGlobalRefScene:
			// FIXME: Need better detection of scenes
			isMatch = obj->isElement() && static_cast<Element *>(obj.get())->getParent()->isSubsection();
			break;
		case kGlobalRefProject:
			isMatch = obj->isProject();
			break;
		default:
			break;
		};

		if (isMatch)
			break;
		else if (obj->isStructural()) {
			Structural *parent = static_cast<Structural *>(obj.get())->getParent();
			if (parent)
				ref = parent->getSelfReference();
			else {
				ref.reset();
				break;
			}
		} else if (obj->isModifier()) {
			ref = static_cast<Modifier *>(obj.get())->getParent();
		} else {
			ref.reset();
			break;
		}
	}

	result.setObject(ref);

	return kMiniscriptInstructionOutcomeContinue;
}

PushString::PushString(const Common::String &str) : _str(str) {
}

MiniscriptInstructionOutcome PushString::execute(MiniscriptThread *thread) const {
	DynamicValue str;
	str.setString(_str);
	thread->pushValue(str);

	return kMiniscriptInstructionOutcomeContinue;
}

Jump::Jump(uint32 instrOffset, bool isConditional) : _instrOffset(instrOffset), _isConditional(isConditional) {
}

MiniscriptInstructionOutcome Jump::execute(MiniscriptThread *thread) const {
	if (_isConditional) {
		if (thread->getStackSize() < 1) {
			thread->error("Stack underflow");
			return kMiniscriptInstructionOutcomeFailed;
		}

		MiniscriptInstructionOutcome outcome = thread->dereferenceRValue(0);
		if (outcome != kMiniscriptInstructionOutcomeContinue)
			return outcome;

		bool isTrue = miniscriptEvaluateTruth(thread->getStackValueFromTop(0).value);

		thread->popValues(1);

		if (!isTrue)
			thread->jumpOffset(static_cast<int32>(this->_instrOffset));
	} else {
		thread->jumpOffset(static_cast<int32>(this->_instrOffset));
	}

	return kMiniscriptInstructionOutcomeContinue;
}

} // End of namespace MiniscriptInstructions

MiniscriptThread::MiniscriptThread(Runtime *runtime, const Common::SharedPtr<MessageProperties> &msgProps, const Common::SharedPtr<MiniscriptProgram> &program, const Common::SharedPtr<MiniscriptReferences> &refs, Modifier *modifier)
	: _runtime(runtime), _msgProps(msgProps), _program(program), _refs(refs), _modifier(modifier), _currentInstruction(0), _failed(false) {
}

void MiniscriptThread::error(const Common::String &message) {
#ifdef MTROPOLIS_DEBUG_ENABLE
	if (_runtime->debugGetDebugger())
		_runtime->debugGetDebugger()->notify(kDebugSeverityError, Common::String("Miniscript error: " + message));
#endif
	warning("Miniscript error in (%x '%s') at instruction %u: %s", _modifier->getStaticGUID(), _modifier->getName().c_str(), static_cast<uint>(_currentInstruction - 1), message.c_str());

	// This should be redundant
	_failed = true;
}

const Common::SharedPtr<MiniscriptProgram> &MiniscriptThread::getProgram() const {
	return _program;
}

const Common::SharedPtr<MiniscriptReferences> &MiniscriptThread::getRefs() const {
	return _refs;
}

Modifier *MiniscriptThread::getModifier() const {
	return _modifier;
}

const Common::SharedPtr<MessageProperties> &MiniscriptThread::getMessageProperties() const {
	return _msgProps;
}

Runtime *MiniscriptThread::getRuntime() const {
	return _runtime;
}

void MiniscriptThread::pushValue(const DynamicValue &value) {
	_stack.push_back(MiniscriptStackValue());

	MiniscriptStackValue &stackValue = _stack.back();
	stackValue.value = value;
}

void MiniscriptThread::popValues(size_t count) {
	while (count--)
		_stack.pop_back();
}

size_t MiniscriptThread::getStackSize() const {
	return _stack.size();
}

MiniscriptStackValue &MiniscriptThread::getStackValueFromTop(size_t offset) {
	assert(offset < _stack.size());
	return _stack[_stack.size() - 1 - offset];
}

MiniscriptInstructionOutcome MiniscriptThread::dereferenceRValue(size_t offset) {
	assert(offset < _stack.size());
	MiniscriptStackValue &stackValue = _stack[_stack.size() - 1 - offset];

	switch (stackValue.value.getType()) {
	case DynamicValueTypes::kObject: {
			Common::SharedPtr<RuntimeObject> obj = stackValue.value.getObject().object.lock();
			if (obj && obj->isModifier()) {
				const Modifier *modifier = static_cast<const Modifier *>(obj.get());
				if (modifier->isVariable()) {
					static_cast<const VariableModifier *>(modifier)->varGetValue(stackValue.value);
				}
			}
		} break;
	case DynamicValueTypes::kWriteProxy:
		this->error("Attempted to dereference an lvalue proxy");
		return kMiniscriptInstructionOutcomeFailed;
	case DynamicValueTypes::kList:
		stackValue.value.setList(stackValue.value.getList()->clone());
		break;
	default:
		break;
	}

	return kMiniscriptInstructionOutcomeContinue;
}

void MiniscriptThread::jumpOffset(int32 offset) {
	if (offset == 0) {
		this->error("Invalid jump offset");
		_failed = true;
		return;
	}

	// Negative offsets jump backwards (loops)
	_currentInstruction = static_cast<size_t>(static_cast<int64>(_currentInstruction) + offset - 1);
}

bool MiniscriptThread::evaluateTruthOfResult(bool &isTrue) {
	if (_stack.size() != 1) {
		this->error("Miniscript program didn't return a result");
		return false;
	}

	MiniscriptInstructionOutcome outcome = dereferenceRValue(0);
	if (outcome != kMiniscriptInstructionOutcomeContinue) {
		this->error("Miniscript program result couldn't be dereferenced");
		return false;
	}

	isTrue = miniscriptEvaluateTruth(_stack[0].value);
	return true;
}

void MiniscriptThread::createWriteIncomingDataProxy(DynamicValueWriteProxy &proxy) {
	proxy.pod.ifc = DynamicValueWriteInterfaceGlue<IncomingDataWriteInterface>::getInstance();
	proxy.pod.objectRef = this;
	proxy.pod.ptrOrOffset = 0;
}

void MiniscriptThread::retryInstruction() {
	_currentInstruction--;
}

MiniscriptInstructionOutcome MiniscriptThread::IncomingDataWriteInterface::write(MiniscriptThread *thread, const DynamicValue &value, void *objectRef, uintptr ptrOrOffset) {
	// In mTropolis 2.0, a message sent with a variable as its data passes the variable by reference,
	// so writing to the incoming data sets the sender's variable.
	const DynamicValue &incoming = thread->_msgProps->getValue();
	if (thread->getRuntime()->getProject()->getRuntimeVersion() >= kRuntimeVersion200 && incoming.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = incoming.getObject().object.lock();
		if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable()) {
			if (!static_cast<VariableModifier *>(obj.get())->varSetValue(thread, value))
				return kMiniscriptInstructionOutcomeFailed;
			return kMiniscriptInstructionOutcomeContinue;
		}
	}

	thread->_msgProps->setValue(value);

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome MiniscriptThread::IncomingDataWriteInterface::refAttrib(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib) {
	// An object passed as the data has its attributes written, as when Magic School Bus Bugs runs a
	// script for each stamp in a list. In mTropolis 2.0 that includes a variable passed by reference,
	// such as the start of a range in Bugs's random number script.
	const DynamicValue &incoming = thread->_msgProps->getValue();
	if (incoming.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = incoming.getObject().object.lock();
		if (obj && (!obj->isModifier() || !static_cast<Modifier *>(obj.get())->isVariable() || thread->getRuntime()->getProject()->getRuntimeVersion() >= kRuntimeVersion200))
			return obj->writeRefAttribute(thread, proxy, attrib);
	}

	// TODO: Generic refAttrib for dynamic values
	return kMiniscriptInstructionOutcomeFailed;
}

MiniscriptInstructionOutcome MiniscriptThread::IncomingDataWriteInterface::refAttribIndexed(MiniscriptThread *thread, DynamicValueWriteProxy &proxy, void *objectRef, uintptr ptrOrOffset, const Common::String &attrib, const DynamicValue &index) {
	// TODO: Generic refAttribIndexed for dynamic values
	return kMiniscriptInstructionOutcomeFailed;
}

CORO_BEGIN_DEFINITION(MiniscriptThread::ResumeThreadCoroutine)
	struct Locals {
		MiniscriptThread *self = nullptr;
		size_t numInstrs = 0;
		size_t instrNum = 0;
	};

	CORO_BEGIN_FUNCTION
		locals->self = params->thread.get();
		locals->numInstrs = locals->self->_program->getInstructions().size();

		CORO_IF (locals->numInstrs == 0)
			CORO_RETURN;
		CORO_END_IF

		CORO_WHILE (locals->self->_currentInstruction < locals->numInstrs && !locals->self->_failed)
			CORO_AWAIT_MINISCRIPT(locals->self->runNextInstruction());
		CORO_END_WHILE
	CORO_END_FUNCTION
CORO_END_DEFINITION

MiniscriptInstructionOutcome MiniscriptThread::runNextInstruction() {
	const MiniscriptInstruction *instr = _program->getInstructions()[_currentInstruction++];

	MiniscriptInstructionOutcome outcome = instr->execute(this);

	if (outcome == kMiniscriptInstructionOutcomeFailed) {
		// Treat this as non-fatal but bail out of the execution loop
		_failed = true;
		return kMiniscriptInstructionOutcomeContinue;
	}

	// Otherwise continue
	return outcome;
}

MiniscriptInstructionOutcome MiniscriptThread::tryLoadVariable(MiniscriptStackValue &stackValue) {
	if (stackValue.value.getType() == DynamicValueTypes::kObject) {
		Common::SharedPtr<RuntimeObject> obj = stackValue.value.getObject().object.lock();
		if (obj && obj->isModifier() && static_cast<Modifier *>(obj.get())->isVariable()) {
			VariableModifier *varMod = static_cast<VariableModifier *>(obj.get());
			varMod->varGetValue(stackValue.value);
		}
	}

	return kMiniscriptInstructionOutcomeContinue;
}

MiniscriptInstructionOutcome miniscriptIgnoreFailure(MiniscriptInstructionOutcome outcome) {
	if (outcome == kMiniscriptInstructionOutcomeFailed)
		return kMiniscriptInstructionOutcomeContinue;

	return outcome;
}

} // End of namespace MTropolis
