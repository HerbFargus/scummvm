MODULE := engines/nightmarened

MODULE_OBJS = \
	attic.o \
	attic_rooms.o \
	data.o \
	demo.o \
	display.o \
	game.o \
	hints.o \
	media.o \
	menus.o \
	metaengine.o \
	naf.o \
	ned.o \
	nightmarened.o \
	options.o \
	rdx.o \
	resources.o \
	puff.o \
	scene3.o \
	scene3_room.o \
	scene3_rooms.o \
	scene3_school.o \
	scene3_school_actors.o \
	scene7.o \
	scene7_actors.o \
	world.o

# This module can be built as a plugin
ifeq ($(ENABLE_NIGHTMARENED), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
