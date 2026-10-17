MODULE := engines/mspam

MODULE_OBJS = \
	compound.o \
	layout.o \
	media.o \
	metaengine.o \
	mspam.o

# This module can be built as a plugin
ifeq ($(ENABLE_MSPAM), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
