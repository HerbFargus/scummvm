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
	scene1.o \
	scene1_base.o \
	scene1_enemies1.o \
	scene1_enemies2.o \
	scene1_graves.o \
	scene1_sub0.o \
	scene1_sub2.o \
	scene1_sub3.o \
	scene1_sub4.o \
	scene1_sub5.o \
	scene1_sub7.o \
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
