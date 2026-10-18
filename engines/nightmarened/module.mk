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
	placements.o \
	resources.o \
	scene6.o \
	scene6_res.o \
	scene6_room0.o \
	scene6_room1.o \
	scene6_room2.o \
	scene6_room3.o \
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
