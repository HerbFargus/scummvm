MODULE := engines/nightmarened

MODULE_OBJS = \
	metaengine.o \
	naf.o \
	nightmarened.o

# This module can be built as a plugin
ifeq ($(ENABLE_NIGHTMARENED), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
