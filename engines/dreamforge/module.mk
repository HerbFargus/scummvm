MODULE := engines/dreamforge

MODULE_OBJS = \
	act1.o \
	act1_afterload.o \
	act1_click.o \
	act1_helpers.o \
	act1_hooks.o \
	act1_post.o \
	act1_use.o \
	actor.o \
	banter.o \
	console.o \
	cutscene.o \
	dfa.o \
	dreamforge.o \
	game.o \
	input.o \
	lipsync.o \
	logic.o \
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
