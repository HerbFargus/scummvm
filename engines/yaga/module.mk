MODULE := engines/yaga

MODULE_OBJS = \
	animation.o \
	audiofile.o \
	console.o \
	installer.o \
	metaengine.o \
	pyhost.o \
	pythontest.o \
	scripthandler.o \
	text.o \
	video.o \
	yaga.o \
	python/builtins.o \
	python/ceval.o \
	python/class.o \
	python/compile.o \
	python/dict.o \
	python/errors.o \
	python/file.o \
	python/float.o \
	python/func.o \
	python/int.o \
	python/long.o \
	python/marshal.o \
	python/object.o \
	python/parser.o \
	python/sequence.o \
	python/sre.o \
	python/stdmodules.o \
	python/str.o \
	python/struct.o \
	python/sysmodule.o \
	python/typeobj.o \
	python/unicode.o \
	python/vm.o \
	python/zlibmodule.o


# This module can be built as a plugin
ifeq ($(ENABLE_YAGA), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk
# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
