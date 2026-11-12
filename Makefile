# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026-present Open Handheld Collective
#   https://github.com/OpenHandheldCollective

# Builds openabl-menu.efi, the program the ABL embeds, with clang and lld-link.
# build.sh runs this in its container; to run it directly, fetch the sources
# first with Scripts/fetch.sh:
#
#   make FEATURES=standard-boot,card-logo
#
# FEATURES (each becomes an OABL_ define):
#   standard-boot     UEFI boot manager, BLS entries, UKIs, extlinux.conf
#   card-logo         boot logo from a logo folder on the card
#   sysfont           scaled system font fallback (needed on SM8750)
#   sdcc-sm4450       SD card fix for SM4450's XBL (also sdcc-sm6450,
#                     sdcc-sm7675)
#   loader-quirks     XBL workarounds needed by OS loaders (SDM845, SM6115)
#   park-display      set the display on before the boot, and off at
#                     ExitBootServices when no devicetree was seen, as
#                     LinuxLoader did on these chips (SM8650, SM8750)
#
# The menu has every language in Ui/Lang/, one file each (listed in
# Ui/Lang/Languages.h). Scripts/mkfont.py makes its font faces at build time
# with just the characters those strings use (needs fontTools: pip install
# fonttools): Montserrat at two weights from Assets/Montserrat-Variable.ttf for
# Latin, Greek and Cyrillic, and a CJK face from
# Assets/SourceHanSansSC-Normal.otf for the rest.
# The ABL image is 512 KiB and the abl partition 1 MiB, so the program is
# kept small.
#
# `make sim` builds a host simulator that renders the menu's screens to PNG
# (needs a host C compiler and zlib).

# Override make's built-in cc and ld defaults. The linker variable is not
# called LINK because lld-link reads LINK from the environment.
ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin LD),default)
LD := lld-link
endif
HOSTCC   ?= cc
PYTHON   ?= python3
MICROUI  ?= build/microui
VERSION  := $(shell cat VERSION)
EDK2     ?= build/edk2
FEATURES ?= standard-boot,sysfont,card-logo,sdcc-sm4450,sdcc-sm6450,sdcc-sm7675,loader-quirks
OUT      ?= build/menu

KNOWN := standard-boot sysfont card-logo sdcc-sm4450 sdcc-sm6450 sdcc-sm7675 \
	loader-quirks park-display
comma := ,
LIST := $(subst $(comma), ,$(FEATURES))
UNKNOWN := $(filter-out $(KNOWN),$(LIST))
ifneq ($(UNKNOWN),)
$(error unknown FEATURES: $(UNKNOWN) (known: $(KNOWN)))
endif
DEFINES := $(shell echo $(sort $(LIST)) | tr a-z- A-Z_ | sed 's/[^ ][^ ]*/-DOABL_&/g')
DEFINES += -DMENU_VERSION=\"$(VERSION)\"
VARIANT := $(shell echo '$(DEFINES)' | cksum | cut -d' ' -f1)

DIRS := Base Ui Gfx View Core Efi Boot Menu Rt
SRCS := $(sort $(foreach d,$(DIRS),$(shell find $(d) -name '*.c')))

# microui's object is shared; the program's are kept per set of FEATURES.
OBJ := build/obj
MICROUI_OBJ := $(OBJ)/microui/microui.o
OWN_OBJS := $(patsubst %.c,$(OBJ)/$(VARIANT)/%.o,$(SRCS)) $(OBJ)/$(VARIANT)/assets.o

# Includes are relative to the repository root ("Core/Gpt.h").
BASE_FLAGS := -target aarch64-unknown-windows -ffreestanding -fno-stack-protector \
	-fshort-wchar -fno-math-errno -Oz -ffunction-sections -fdata-sections \
	-DOABL_UEFI -std=gnu11 -IRt/Include -I. -I$(MICROUI)/src
# edk2 headers from the patched ABL tree, as system headers so their
# warnings don't fail the build.
EDK2_INCLUDES := MdePkg/Include MdePkg/Include/AArch64 MdeModulePkg/Include \
	EmbeddedPkg/Include QcomModulePkg/Include
OWN_FLAGS := $(BASE_FLAGS) $(DEFINES) $(foreach d,$(EDK2_INCLUDES),-isystem $(EDK2)/$(d)) \
	-Wall -Wextra -Wno-unused-parameter -Werror -MMD -MP

FONT := $(OBJ)/montserrat.oablf
CJK_FONT := $(OBJ)/cjk-sc.oablf
LANG_SRCS := $(sort $(wildcard Ui/Lang/*.c))
ASSETS := Font=$(FONT) FontCjk=$(CJK_FONT)

.PHONY: all sim clean test
all: $(OUT)/openabl-menu.efi

# Linked as a boot service driver: an application would be unloaded when it
# returns, but the program has to stay resident for the protocol and the
# ExitBootServices callback it installs.
$(OUT)/openabl-menu.efi: $(MICROUI_OBJ) $(OWN_OBJS)
	@mkdir -p $(@D)
	$(LD) /entry:efi_main /nodefaultlib /machine:arm64 /opt:ref \
		/subsystem:efi_boot_service_driver /out:$@ $^
	@echo "$@ ($$(($$(wc -c < $@) / 1024)) KiB)"

$(MICROUI_OBJ): $(MICROUI)/src/microui.c
	@mkdir -p $(@D)
	@$(CC) $(BASE_FLAGS) -w -c $< -o $@

$(OBJ)/$(VARIANT)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(OWN_FLAGS) -c $< -o $@

# The menu font: ASCII, a few symbols, and the letters the strings use.
$(FONT): Scripts/mkfont.py Assets/Montserrat-Variable.ttf $(LANG_SRCS)
	@mkdir -p $(@D)
	$(PYTHON) Scripts/mkfont.py --primary Assets/Montserrat-Variable.ttf $(LANG_SRCS) > $@.tmp
	@mv $@.tmp $@

# The CJK face: the characters the strings use that the menu font lacks.
$(CJK_FONT): Scripts/mkfont.py Assets/SourceHanSansSC-Normal.otf $(FONT) $(LANG_SRCS)
	@mkdir -p $(@D)
	$(PYTHON) Scripts/mkfont.py Assets/SourceHanSansSC-Normal.otf $(FONT) $(LANG_SRCS) > $@.tmp
	@mv $@.tmp $@

$(OBJ)/$(VARIANT)/assets.c: Scripts/bin2c.py $(FONT) $(CJK_FONT)
	@mkdir -p $(@D)
	$(PYTHON) Scripts/bin2c.py $(ASSETS) > $@

$(OBJ)/$(VARIANT)/assets.o: $(OBJ)/$(VARIANT)/assets.c
	$(CC) $(BASE_FLAGS) -c $< -o $@

-include $(OWN_OBJS:.o=.d)

# Host simulator, using the C library's allocator.
SIM_DIRS := Base Ui Gfx View Sim Host
SIM_SRCS := $(sort $(foreach d,$(SIM_DIRS),$(shell find $(d) -name '*.c'))) Core/Settings.c
SIM_FLAGS := -O1 -g -std=gnu11 -I. -I$(MICROUI)/src \
	$(filter -DMENU_VERSION=%,$(DEFINES))
SIM_OBJS := $(OBJ)/sim-microui/microui.o \
	$(patsubst %.c,$(OBJ)/sim-$(VARIANT)/%.o,$(SIM_SRCS)) $(OBJ)/sim-$(VARIANT)/assets.o

sim: build/sim/openabl-menu-sim
build/sim/openabl-menu-sim: $(SIM_OBJS)
	@mkdir -p $(@D)
	$(HOSTCC) -o $@ $^ -lz -lm

$(OBJ)/sim-microui/microui.o: $(MICROUI)/src/microui.c
	@mkdir -p $(@D)
	@$(HOSTCC) $(SIM_FLAGS) -w -c $< -o $@

$(OBJ)/sim-$(VARIANT)/%.o: %.c
	@mkdir -p $(@D)
	$(HOSTCC) $(SIM_FLAGS) -Wall -Wextra -Wno-unused-parameter -Werror -MMD -MP -c $< -o $@

$(OBJ)/sim-$(VARIANT)/assets.o: $(OBJ)/$(VARIANT)/assets.c
	@mkdir -p $(@D)
	$(HOSTCC) $(SIM_FLAGS) -c $< -o $@

-include $(SIM_OBJS:.o=.d)

# Host tests (need libfdt).
test: build/test/partialgoods-test
	build/test/partialgoods-test

build/test/partialgoods-test: $(sort $(shell find Base -name '*.c')) Host/Host.c \
		Core/PartialGoods.c Test/PartialGoods.c
	@mkdir -p $(@D)
	$(HOSTCC) -O1 -g -std=gnu11 -I. -Wall -Wextra -Wno-unused-parameter -Werror \
		-o $@ $^ -lfdt

clean:
	rm -rf build/obj build/menu build/sim build/test
