MODULE := engines/bigtuna

MODULE_OBJS = \
	bigtuna.o \
	bitmap.o \
	console.o \
	metaengine.o \
	movie.o \
	score.o \
	sound.o \
	stage.o

# This module can be built as a plugin
ifeq ($(ENABLE_BIGTUNA), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
