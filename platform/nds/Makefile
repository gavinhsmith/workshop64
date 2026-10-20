ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set; use the devkitpro/devkitarm container)
endif

# Read by ds_rules' %.nds rule; must be set before the include.
GAME_TITLE     := workshop64
GAME_SUBTITLE1 := 3D homebrew demo
GAME_SUBTITLE2 := github.com/gavinhsmith/workshop64
GAME_ICON      := $(CURDIR)/../../assets/icon/ds.bmp # made from icon.svg by tools/authoring/icons.py

# Captured by ds_rules' %.elf rule at include time.
LIBPATHS := -L$(DEVKITPRO)/libnds/lib

include $(DEVKITARM)/ds_rules
include ../../core/core.mk

TARGET  := workshop64
BUILD   := build
OBJS    := $(BUILD)/main.o $(addprefix $(BUILD)/,$(CORE_OBJ))
ARCH    := -march=armv5te -mtune=arm946e-s
CFLAGS  := -O2 -Wall $(ARCH) -DARM9 $(_EXTRADEFS) -I$(LIBNDS)/include $(CORE_INC) -MMD -MP
# ds_rules' %.elf rule links these, adding calico's specs and runtime.
LD       := $(CC)
OFILES   := $(OBJS)
LDFLAGS  := -specs=ds_arm9.specs $(ARCH)
LIBS     := -lnds9 -lm

OUT := $(ROOT)/build/nds

all: $(OUT)/$(TARGET).nds

# ds_rules builds the ROM here; it's then moved to $(OUT) (make deletes the intermediate
# copy, and only rebuilds it when the ELF changes).
.INTERMEDIATE: $(TARGET).nds
$(OUT)/$(TARGET).nds: $(TARGET).nds
	@mkdir -p $(dir $@)
	cp $< $@

$(TARGET).nds: $(TARGET).elf

$(TARGET).elf: $(OBJS)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(CORE)/%.c $(CORE_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(GEN)/%.c $(CORE_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) *.elf *.nds *.lst $(OUT)

.PHONY: all clean

# Header dependencies from -MMD, so changing core/world.h rebuilds everything that uses it.
-include $(OBJS:.o=.d)
