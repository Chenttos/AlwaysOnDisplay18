# AlwaysOnDisplay16
#
# Default scheme is rootless (Dopamine, palera1n rootless).
# For a rootful jailbreak (palera1n rootful, checkra1n, unc0ver) build with:
#   make package FINALPACKAGE=1 THEOS_PACKAGE_SCHEME=
THEOS_PACKAGE_SCHEME ?= rootless

TARGET := iphone:clang:latest:15.0
ARCHS := arm64 arm64e
INSTALL_TARGET_PROCESSES = SpringBoard Preferences

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = AlwaysOnDisplay16

AlwaysOnDisplay16_FILES = Tweak.c
AlwaysOnDisplay16_CFLAGS = -Wall
AlwaysOnDisplay16_FRAMEWORKS = CoreFoundation

include $(THEOS_MAKE_PATH)/tweak.mk
