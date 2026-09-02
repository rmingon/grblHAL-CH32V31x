# Makefile - grblHAL driver for WCH CH32V317 (PickOMatic pick-and-place controller)
#
# Part of grblHAL
#
# Copyright (c) 2026 Ronan Mingon
#
# grblHAL is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# grblHAL is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with grblHAL. If not, see <http://www.gnu.org/licenses/>.

TARGET ?= grblHAL-CH32V31x
BUILD  ?= build

# ---------------------------------------------------------------------------
# Toolchain
#
# Preference order:
#   1. CROSS=<prefix> given on the command line (e.g. make CROSS=riscv-none-elf-)
#   2. riscv-none-elf-gcc found in PATH (xPack / MRS standalone toolchain)
#   3. riscv-wch-elf-gcc (MounRiver Studio 2, "RISC-V Embedded GCC12")
#   4. riscv32-wch-elf-gcc (MounRiver Studio 2, "RISC-V Embedded GCC15")
#   5. riscv-none-embed-gcc (legacy MounRiver GCC8)
#
# The MRS2 macOS bundle path below is added to PATH so an out-of-the-box
# MounRiver Studio 2 install is found without any configuration.
# ---------------------------------------------------------------------------

MRS2_DARWIN_BIN ?= /Applications/MounRiver Studio 2.app/Contents/Resources/app/resources/darwin/components/WCH/Toolchain/RISC-V Embedded GCC12/bin

export PATH := $(MRS2_DARWIN_BIN):$(PATH)

CROSS ?= $(shell PATH="$(PATH)" sh -c 'for p in riscv-none-elf- riscv-wch-elf- riscv32-wch-elf- riscv-none-embed-; do command -v "$${p}gcc" >/dev/null 2>&1 && { echo "$$p"; break; }; done')

ifeq ($(CROSS),)
$(error No RISC-V toolchain found. Install MounRiver Studio 2 or add riscv-none-elf-gcc to PATH, or pass CROSS=<prefix>)
endif

# Tools are invoked by full, quoted path: the MRS2 bundle path contains
# spaces, and make's fast-path exec does not honour the exported PATH.
TOOLDIR := $(shell PATH="$(PATH)" sh -c 'd=$$(command -v "$(CROSS)gcc") && dirname "$$d"')

ifeq ($(TOOLDIR),)
$(error $(CROSS)gcc not found in PATH)
endif

CC      = "$(TOOLDIR)/$(CROSS)gcc"
OBJCOPY = "$(TOOLDIR)/$(CROSS)objcopy"
OBJDUMP = "$(TOOLDIR)/$(CROSS)objdump"
SIZE    = "$(TOOLDIR)/$(CROSS)size"

# WCH toolchains (GCC12/GCC15 and the patched GCC8) support the QingKe
# custom ISA extension "xw" and the WCH interrupt attributes; a vanilla
# riscv-none-elf-gcc does not know "xw" and (from GCC12 on) needs an
# explicit zicsr extension.
ifneq (,$(findstring wch,$(CROSS)))
MARCH ?= rv32imafcxw
else ifneq (,$(findstring embed,$(CROSS)))
MARCH ?= rv32imafc
else
MARCH ?= rv32imafc_zicsr
endif
MABI ?= ilp32f

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

C_SRCS  = $(wildcard src/*.c)
C_SRCS += lib/Core/core_riscv.c
C_SRCS += lib/Debug/debug.c
C_SRCS += $(wildcard lib/Peripheral/src/*.c)
C_SRCS += $(wildcard grbl/*.c)
C_SRCS += $(wildcard grbl/kinematics/*.c)

S_SRCS  = lib/Startup/startup_ch32v30x_D8C.S

INCLUDES = -I. -Isrc -Ilib/Core -Ilib/Debug -Ilib/Peripheral/inc

# N_AXIS and build options must be visible to BOTH the grbl core and the
# driver: force-include the configuration into every compilation unit.
FORCED_INCLUDE = -include src/my_machine.h

OBJS  = $(addprefix $(BUILD)/,$(C_SRCS:.c=.o))
OBJS += $(addprefix $(BUILD)/,$(S_SRCS:.S=.o))

# ---------------------------------------------------------------------------
# Flags (matching the MounRiver managed-build defaults)
# ---------------------------------------------------------------------------

COMMON_FLAGS = -march=$(MARCH) -mabi=$(MABI) \
               -msmall-data-limit=8 -msave-restore \
               -Os -g \
               -fmessage-length=0 -fsigned-char \
               -ffunction-sections -fdata-sections -fno-common \
               -Wunused -Wuninitialized

CFLAGS  = $(COMMON_FLAGS) -std=gnu11 $(INCLUDES) $(FORCED_INCLUDE)
ASFLAGS = $(COMMON_FLAGS) -x assembler-with-cpp $(INCLUDES)

LDFLAGS = $(COMMON_FLAGS) -T ld/Link.ld -nostartfiles \
          -Xlinker --gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map \
          --specs=nano.specs --specs=nosys.specs

LDLIBS = -lm

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------

.PHONY: all clean size lst info

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin size

$(BUILD)/$(TARGET).elf: $(OBJS) ld/Link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(BUILD)/$(TARGET).elf
	$(SIZE) --format=berkeley $<

lst: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) -d -C $< > $(BUILD)/$(TARGET).lst

info:
	@echo "CROSS   = $(CROSS)"
	@echo "TOOLDIR = $(TOOLDIR)"
	@echo "MARCH   = $(MARCH)"
	@echo "MABI    = $(MABI)"

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
