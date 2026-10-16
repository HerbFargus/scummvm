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
#include "common/memstream.h"
#include "common/savefile.h"
#include "common/stream.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "yaga/detection.h"
#include "yaga/installer.h"
#include "yaga/pyhost.h"
#include "yaga/python/object.h"
#include "yaga/python/internal.h"

namespace Yaga {

using namespace Python;

// ---- Files -----------------------------------------------------------------------

GameFileSystem::GameFileSystem(const Common::FSNode &atariRoot, const Common::String &gameDir, const Common::String &target)
	: _root(atariRoot), _rootPath("C:\\Program Files\\Atari"), _target(target) {
	_home = _rootPath + "\\" + gameDir;
}

Common::String GameFileSystem::virtualPath(const Common::String &relative) const {
	return _home + "\\" + relative;
}

bool GameFileSystem::relativeParts(const Common::String &path, Common::Array<Common::String> &parts) const {
	Common::String p = path;
	for (uint i = 0; i < p.size(); i++)
		if (p[i] == '/')
			p.setChar('\\', i);
	// Relative paths start in the game's folder, the working directory
	bool absolute = p.size() >= 2 && p[1] == ':';
	if (!absolute && !p.hasPrefix("\\"))
		p = _home + "\\" + p;
	Common::Array<Common::String> all;
	Common::String part;
	for (uint i = 0; i <= p.size(); i++) {
		if (i == p.size() || p[i] == '\\') {
			if (part == "..") {
				if (!all.empty())
					all.pop_back();
			} else if (!part.empty() && part != ".") {
				all.push_back(part);
			}
			part.clear();
		} else {
			part += p[i];
		}
	}
	// Match the install root, ignoring case as Windows does
	Common::Array<Common::String> root;
	root.push_back("C:");
	root.push_back("Program Files");
	root.push_back("Atari");
	if (all.size() < root.size())
		return false;
	for (uint i = 0; i < root.size(); i++)
		if (!all[i].equalsIgnoreCase(root[i]))
			return false;
	parts.clear();
	for (uint i = root.size(); i < all.size(); i++)
		parts.push_back(all[i]);
	return true;
}

Common::FSNode GameFileSystem::findNode(const Common::Array<Common::String> &parts) const {
	Common::FSNode node = _root;
	for (uint i = 0; i < parts.size(); i++) {
		Common::FSList children;
		if (!node.isDirectory() || !node.getChildren(children, Common::FSNode::kListAll))
			return Common::FSNode();
		bool found = false;
		for (uint k = 0; k < children.size(); k++) {
			if (children[k].getName().equalsIgnoreCase(parts[i])) {
				node = children[k];
				found = true;
				break;
			}
		}
		if (!found)
			return Common::FSNode();
	}
	return node;
}

Common::String GameFileSystem::saveName(const Common::Array<Common::String> &parts) const {
	Common::String name = _target;
	for (uint i = 0; i < parts.size(); i++) {
		Common::String p = parts[i];
		p.toLowercase();
		name += "-" + p;
	}
	return name;
}

Common::SeekableReadStream *GameFileSystem::openForReading(const Common::String &path) {
	Common::Array<Common::String> parts;
	if (!relativeParts(path, parts))
		return nullptr;
	Common::InSaveFile *save = g_system->getSavefileManager()->openForLoading(saveName(parts));
	if (save)
		return save;
	Common::FSNode node = findNode(parts);
	if (!node.exists() || node.isDirectory())
		return nullptr;
	return node.createReadStream();
}

// Writes to a save file, keeping what was there when appending
class AppendingSaveStream : public Common::WriteStream {
public:
	AppendingSaveStream(Common::OutSaveFile *out) : _out(out) {}
	~AppendingSaveStream() override {
		_out->finalize();
		delete _out;
	}
	uint32 write(const void *dataPtr, uint32 dataSize) override { return _out->write(dataPtr, dataSize); }
	bool flush() override { return _out->flush(); }
	void finalize() override { _out->finalize(); }
	int64 pos() const override { return _out->pos(); }
private:
	Common::OutSaveFile *_out;
};

Common::WriteStream *GameFileSystem::openForWriting(const Common::String &path, bool append) {
	Common::Array<Common::String> parts;
	if (!relativeParts(path, parts))
		return nullptr;
	Common::String name = saveName(parts);
	Common::String existing;
	if (append) {
		Common::InSaveFile *in = g_system->getSavefileManager()->openForLoading(name);
		if (in) {
			char buf[4096];
			uint32 n;
			while ((n = in->read(buf, sizeof(buf))) > 0)
				existing += Common::String(buf, n);
			delete in;
		}
	}
	Common::OutSaveFile *out = g_system->getSavefileManager()->openForSaving(name, false);
	if (!out)
		return nullptr;
	if (!existing.empty())
		out->write(existing.c_str(), existing.size());
	return new AppendingSaveStream(out);
}

int GameFileSystem::stat(const Common::String &path, uint32 *size, uint32 *mtime) {
	*size = 0;
	*mtime = 0;
	Common::Array<Common::String> parts;
	if (!relativeParts(path, parts))
		return 0;
	Common::InSaveFile *save = g_system->getSavefileManager()->openForLoading(saveName(parts));
	if (save) {
		*size = save->size();
		delete save;
		return 1;
	}
	Common::FSNode node = findNode(parts);
	if (!node.exists())
		return 0;
	if (node.isDirectory())
		return 2;
	Common::SeekableReadStream *s = node.createReadStream();
	if (s) {
		*size = s->size();
		delete s;
	}
	return 1;
}

bool GameFileSystem::remove(const Common::String &path) {
	Common::Array<Common::String> parts;
	if (!relativeParts(path, parts))
		return false;
	return g_system->getSavefileManager()->removeSavefile(saveName(parts));
}

bool GameFileSystem::listDirectory(const Common::String &path, Common::Array<Common::String> &names) {
	Common::Array<Common::String> parts;
	if (!relativeParts(path, parts))
		return false;
	Common::FSNode node = findNode(parts);
	Common::FSList children;
	if (!node.isDirectory() || !node.getChildren(children, Common::FSNode::kListAll))
		return false;
	for (uint i = 0; i < children.size(); i++)
		names.push_back(children[i].getName());
	return true;
}

// ---- Output ------------------------------------------------------------------------

void LogSink::write(const char *s, uint32 len, Common::String &pending) {
	for (uint32 i = 0; i < len; i++) {
		if (s[i] == '\n') {
			debug(1, "%s", pending.c_str());
			pending.clear();
		} else {
			pending += s[i];
		}
	}
}

void LogSink::writeStdout(const char *s, uint32 len) {
	write(s, len, _out);
}

void LogSink::writeStderr(const char *s, uint32 len) {
	write(s, len, _err);
}

// ---- Tracing stand-ins for the engine modules ------------------------------------------

static Type g_stubType("yaga stub", &g_objectType);
static Type g_stubModuleType("module", &g_moduleType);
static Type g_stubMetaType("yaga stub class", &g_typeType); // the classes' metaclass

// One stand-in: any attribute, call or operation gives back another
struct Stub : public Object {
	Common::String _name;
	Ref<Dict> _dict;
	Stub(Type *t, const Common::String &name) : Object(t), _name(name) {}
	Dict *getDict() override { return _dict.get(); }
	void setDict(Dict *d) override { _dict = d; }
};

static int g_stubEvents = 0;

static void trace(const char *kind, const Common::String &target, const Common::String &detail = Common::String()) {
	g_stubEvents++;
	debugC(1, kDebugScript, "stub %05d %-6s %s %s", g_stubEvents, kind, target.c_str(), detail.c_str());
}

static Common::String stubName(Object *o) {
	if (isInstanceOf(o, &g_stubType))
		return ((Stub *)o)->_name;
	if (isInstanceOf(o, &g_typeType))
		return ((Type *)o)->_name;
	return typeName(o);
}

static ObjRef newStub(const Common::String &name) {
	return ObjRef(new Stub(&g_stubType, name));
}

static Common::String describeArgs(Tuple *args) {
	Common::String out;
	for (uint32 i = 0; i < args->_size; i++) {
		if (i)
			out += ", ";
		Object *a = args->get(i);
		if (isInstanceOf(a, &g_stubType)) {
			out += "<" + ((Stub *)a)->_name + ">";
			continue;
		}
		ObjRef r = repr(a);
		if (!r) {
			clearError();
			out += "?";
			continue;
		}
		Common::String s = ((Str *)r.get())->toString();
		if (s.size() > 60)
			s = Common::String(s.c_str(), 57) + "...";
		out += s;
	}
	return out;
}

static ObjRef stubGetAttr(Object *o, Str *name) {
	ObjRef r = genericGetAttr(o, name);
	if (r)
		return r;
	if (!errorMatches(g_excAttributeError))
		return ObjRef();
	clearError();
	// Python's own special methods stay missing, so that the VM's protocols behave
	if (name->_data[0] == '_' && name->_data[1] == '_')
		return genericGetAttr(o, name);
	Common::String full = stubName(o) + "." + name->toString();
	trace("get", full);
	ObjRef child = newStub(full);
	Dict *d = o->getDict();
	if (!d) {
		Ref<Dict> nd = newDict();
		o->setDict(nd.get());
		d = nd.get();
	}
	d->setItem(name, child.get());
	return child;
}

static int stubSetAttr(Object *o, Str *name, Object *value) {
	trace("set", stubName(o) + "." + name->toString(), value ? describeArgs(tuplePack(1, value).get()) : "(del)");
	if (!o->getDict()) {
		Ref<Dict> nd = newDict();
		o->setDict(nd.get());
	}
	return genericSetAttr(o, name, value);
}

static ObjRef stubCall(Object *o, Tuple *args, Dict *) {
	Common::String name = stubName(o);
	trace("call", name, "(" + describeArgs(args) + ")");
	if (name.hasSuffix("StartEventLoop"))
		debug(1, "yaga: the scripts reached the event loop (%s)", name.c_str());
	return newStub(name + "()");
}

static ObjRef stubRepr(Object *o) {
	return newStr("<stub " + stubName(o) + ">");
}

static ObjRef stubIter(Object *o) {
	trace("iter", stubName(o));
	Ref<Tuple> empty = newTuple(0);
	return getIter(empty.get());
}

static int32 stubLength(Object *o) {
	trace("len", stubName(o));
	return 0;
}

static ObjRef stubItem(Object *o, Object *key) {
	trace("item", stubName(o), describeArgs(tuplePack(1, key).get()));
	return newStub(stubName(o) + "[]");
}

static int stubSetItem(Object *o, Object *key, Object *value) {
	trace("setitem", stubName(o), describeArgs(tuplePack(1, key).get()));
	return 0;
}

static int stubContains(Object *o, Object *) {
	trace("in", stubName(o));
	return 0;
}

// Arithmetic on a stand-in gives 0, which keeps formatting and comparisons going
static ObjRef stubNumber(Object *, Object *) {
	return newInt(0);
}

static ObjRef stubNumber3(Object *, Object *, Object *) {
	return newInt(0);
}

static ObjRef stubUnary(Object *) {
	return newInt(0);
}

static ObjRef stubFloat(Object *) {
	return newFloat(0.0);
}

static int stubNonzero(Object *) {
	return 1;
}

static ObjRef stubNew(Type *type, Tuple *args, Dict *) {
	trace("new", type->_name, "(" + describeArgs(args) + ")");
	return ObjRef(new Stub(type, type->_name + "()"));
}

static int stubInit(Object *, Tuple *, Dict *) {
	return 0;
}

// Engine classes answer for any class attribute too (constants such as
// PixelFormat.PXL_R5G6B5); the game's own subclasses do not
static ObjRef stubTypeGetAttr(Object *o, Str *name) {
	ObjRef r = g_typeType.getAttr(o, name);
	if (r || !errorMatches(g_excAttributeError))
		return r;
	Type *t = (Type *)o;
	if (!t->_dict->getItemString("__yaga_stub__") || (name->_data[0] == '_' && name->_data[1] == '_'))
		return r;
	clearError();
	Common::String full = t->_name + "." + name->toString();
	trace("get", full);
	ObjRef value = newStub(full);
	t->_dict->setItem(name, value.get());
	return value;
}

static NumberMethods g_stubAsNumber;
static SequenceMethods g_stubAsSequence;
static MappingMethods g_stubAsMapping;

static void initStubTypes() {
	if (g_stubType._flags & kTypeReady)
		return;
	NumberMethods &n = g_stubAsNumber;
	n.add = n.subtract = n.multiply = n.divide = n.remainder = n.lshift = n.rshift = n.and_ = n.xor_ = n.or_ = stubNumber;
	n.floorDivide = n.trueDivide = stubNumber;
	n.power = stubNumber3;
	n.negative = n.positive = n.absolute = n.invert = n.toInt = n.toLong = stubUnary;
	n.toFloat = stubFloat;
	n.nonzero = stubNonzero;
	g_stubAsSequence.length = stubLength;
	g_stubAsSequence.contains = stubContains;
	g_stubAsMapping.length = stubLength;
	g_stubAsMapping.subscript = stubItem;
	g_stubAsMapping.assSubscript = stubSetItem;
	Type &t = g_stubType;
	t._flags |= kTypeBaseType | kTypeHasDict | kTypeCheckTypes;
	t.asNumber = &n;
	t.asSequence = &g_stubAsSequence;
	t.asMapping = &g_stubAsMapping;
	t.getAttr = stubGetAttr;
	t.setAttr = stubSetAttr;
	t.call = stubCall;
	t.repr = stubRepr;
	t.iter = stubIter;
	t.newObj = stubNew;
	t.init = stubInit;
	typeReady(&t);
	Type &mt = g_stubMetaType;
	mt._flags |= kTypeBaseType;
	mt.getAttr = stubTypeGetAttr;
	typeReady(&mt);
}

// A module of stand-ins: names starting with a capital letter are classes the
// scripts may subclass; the rest are stand-in objects and functions
struct StubModule : public Module {
	explicit StubModule(const char *name) : Module(name) {
		_type = &g_stubModuleType;
	}
};

static ObjRef stubModuleGetAttr(Object *o, Str *name) {
	Module *m = (Module *)o;
	Object *v = m->_dict->getItem(name);
	if (v)
		return ObjRef(v);
	if (name->_data[0] == '_' && name->_data[1] == '_') {
		setErrorFormat(g_excAttributeError, "'module' object has no attribute '%.400s'", name->_data);
		return ObjRef();
	}
	Object *modName = m->_dict->getItemString("__name__");
	Common::String full = (modName ? ((Str *)modName)->toString() : Common::String("?")) + "." + name->toString();
	ObjRef value;
	char c = name->_data[0];
	if (c >= 'A' && c <= 'Z') {
		// A class: a new type deriving from the stand-in base
		Ref<Dict> dict = newDict();
		dict->setItemString("__module__", modName ? modName : g_none);
		dict->setItemString("__yaga_stub__", g_none);
		Ref<Tuple> bases = tuplePack(1, &g_stubType);
		Ref<Tuple> targs = tuplePack(3, newStr(name->toString()).get(), bases.get(), dict.get());
		value = typeNew(&g_stubMetaType, targs.get(), nullptr);

		if (!value)
			return ObjRef();
		((Type *)value.get())->_name = full;
		trace("class", full);
	} else {
		value = newStub(full);
		trace("get", full);
	}
	m->_dict->setItem(name, value.get());
	return value;
}

static void initStubModule(Module *placeholder) {
	initStubTypes();
	Object *nameObj = placeholder->_dict->getItemString("__name__");
	Common::String name = nameObj ? ((Str *)nameObj)->toString() : Common::String("yaga");
	// Replace the plain module in sys.modules with one that makes stand-ins
	Ref<Module> m(new StubModule(name.c_str()));
	getModuleDict()->setItemString(name.c_str(), m.get());
	trace("import", name);
}

void registerEngineStubs(Interpreter *vm) {
	if (!(g_stubModuleType._flags & kTypeReady)) {
		g_stubModuleType.getAttr = stubModuleGetAttr;
		typeReady(&g_stubModuleType);
	}
	static const char *const modules[] = {
		"yaga", "yagacollisionlib", "yagaevents", "yagafont", "yagagraphics",
		"yagares", "yagascene", "yagasound", "yagasprite", "yagaxml"
	};
	for (uint i = 0; i < ARRAYSIZE(modules); i++)
		vm->addNativeModule(modules[i], initStubModule, true);
}

// ---- The launcher -------------------------------------------------------------------

// The entries are sometimes zlib-compressed whatever their flag says: inflate if that works
static Common::String maybeInflate(const Common::String &raw) {
	ObjRef zlib = importModule("zlib");
	if (!zlib) {
		clearError();
		return raw;
	}
	ObjRef out = callMethod(zlib.get(), "decompress", 1, newStr(raw).get());
	if (!out) {
		clearError();
		return raw;
	}
	return ((Str *)out.get())->toString();
}

// A check of the VM against all the game's code: once the game's importer is
// installed, import every module in the archive (instead of running boot)
static const char *const kImportAll =
	"import sys, __builtin__\n"
	"__builtin__.False = 0\n"
	"__builtin__.True = 1\n"
	"__builtin__.false = 0\n"
	"__builtin__.true = 1\n"
	"import archive\n"
	"names = archive.ZlibArchive(sys.path[-1]).contents()\n"
	"names.sort()\n"
	"ok = 0\n"
	"for name in names:\n"
	"    try:\n"
	"        __import__(name)\n"
	"        ok = ok + 1\n"
	"    except:\n"
	"        print 'import failed:', name, sys.exc_info()[0], sys.exc_info()[1]\n"
	"print 'imported', ok, 'of', len(names), 'modules'\n";

bool runLauncher(Interpreter *vm, GameFileSystem *fs, const Common::String &exeName, bool importAll) {
	Installer installer;
	if (!installer.open(exeName)) {
		warning("yaga: cannot read the Python archive in %s", exeName.c_str());
		return false;
	}
	Common::String exePath = fs->virtualPath(exeName);
	Common::Array<Common::String> argv;
	argv.push_back(exePath);
	vm->setArgv(argv);
	vm->setExecutable(exePath);
	Common::Array<Common::String> path;
	path.push_back(fs->homePath());
	vm->setPath(path);

	const Common::Array<InstallerEntry> &entries = installer.getEntries();
	// The bootstrap modules (archive, iu), already compiled
	for (uint i = 0; i < entries.size(); i++) {
		if (entries[i].type != kInstallerEntryModule)
			continue;
		Common::String data = maybeInflate(installer.extractRaw(entries[i]));
		if (data.size() < 8) {
			warning("yaga: bootstrap module %s is empty", entries[i].name.c_str());
			return false;
		}
		ObjRef code = marshalLoads(data.c_str() + 8, data.size() - 8);
		if (!code || !isType(code.get(), &g_codeType)) {
			warning("yaga: bootstrap module %s is not bytecode", entries[i].name.c_str());
			clearError();
			return false;
		}
		debug(1, "yaga: importing bootstrap module %s", entries[i].name.c_str());
		if (!vm->execCodeModule(entries[i].name.c_str(), code.get()))
			return false;
	}
	// The bytecode archives go on the path, as "<exe>?<offset>"
	ObjRef sysPath = sysGetObject("path");
	for (uint i = 0; i < entries.size(); i++) {
		if (entries[i].type != kInstallerEntryZipPath)
			continue;
		Common::String entry = Common::String::format("%s?%u", exePath.c_str(), installer.getArchiveOffset() + entries[i].dataOffset);
		debug(1, "yaga: adding %s to sys.path", entry.c_str());
		if (sysPath)
			((List *)sysPath.get())->append(newStr(entry).get());
	}
	// Then each script, as source, in __main__
	for (uint i = 0; i < entries.size(); i++) {
		if (entries[i].type != kInstallerEntryScript)
			continue;
		Common::String source = maybeInflate(installer.extractRaw(entries[i]));
		// Scripts are stored NUL-terminated
		uint32 len = strlen(source.c_str());
		source = Common::String(source.c_str(), len);
		if (importAll && entries[i].name == "boot") {
			debug(1, "yaga: importing every module instead of running boot");
			return vm->runString(kImportAll, "<import all>");
		}

		debug(1, "yaga: running script %s", entries[i].name.c_str());
		if (!vm->runString(source.c_str(), entries[i].name.c_str())) {
			if (vm->exited()) {
				debug(1, "yaga: the scripts exited with code %d", vm->exitCode());
				return true;
			}
			return false;
		}
	}
	return true;
}

} // End of namespace Yaga
