export ARCHS = arm64

include $(THEOS)/makefiles/common.mk

LIBRARY_NAME = libkernutils
libkernutils_FILES = $(wildcard *.c helpers/*.c offset-cache/*.c)
libkernutils_CFLAGS += -I. -Ihelpers -Ioffset-cache -Wno-deprecated-declarations
libkernutils_PRIVATE_FRAMEWORKS = IOKit
ADDITIONAL_CFLAGS += -DTHEOS_LEAN_AND_MEAN

include $(THEOS_MAKE_PATH)/library.mk
