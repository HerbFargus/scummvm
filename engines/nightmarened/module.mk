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
	puff.o \
	scene3.o \
	scene3_room.o \
	scene3_rooms.o \
	scene3_school.o \
	scene3_school_actors.o \
	scene5.o \
	scene5_actors.o \
	scene5_level.o \
	scene5_subs.o \
	scene6.o \
	scene6_res.o \
	scene6_room0.o \
	scene6_room1.o \
	scene6_room2.o \
	scene6_room3.o \
	scene7.o \
	scene7_actors.o \
	slotanim.o \
	scene2.o \
	scene2_belt.o \
	scene2_board.o \
	scene7.o \
	scene7_actors.o \
	slothelpers.o \
	world.o

# This module can be built as a plugin
ifeq ($(ENABLE_NIGHTMARENED), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
