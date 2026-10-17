MODULE := engines/dreamforge

MODULE_OBJS = \
	actor.o \
	console.o \
	cutscene.o \
	dfa.o \
	dreamforge.o \
	lipsync.o \
	metaengine.o \
	resource.o \
	scene.o \
	script.o \
	sound.o \
	walkmap.o

# This module can be built as a plugin
ifeq ($(ENABLE_DREAMFORGE), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
