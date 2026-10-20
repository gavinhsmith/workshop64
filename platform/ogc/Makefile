# Builds both workshop64_gamecube.dol and workshop64_wii.dol from the same source.
ifeq ($(strip $(DEVKITPPC)),)
$(error DEVKITPPC is not set; use the devkitpro/devkitppc container)
endif

ifeq ($(PLATFORM),)

all:
	$(MAKE) PLATFORM=gamecube
	$(MAKE) PLATFORM=wii

clean:
	rm -rf build *.dol ../../build/ogc

.PHONY: all clean

else

include $(DEVKITPPC)/$(PLATFORM)_rules
include ../../core/core.mk

TARGET := workshop64_$(PLATFORM)
OUT    := $(ROOT)/build/ogc
BUILD  := build/$(PLATFORM)
OBJS   := $(BUILD)/main.o $(addprefix $(BUILD)/,$(CORE_OBJ))
CFLAGS := -O2 -Wall $(MACHDEP) -I$(LIBOGC_INC) $(CORE_INC) -MMD -MP
LIBS   := -L$(LIBOGC_LIB) $(if $(filter wii,$(PLATFORM)),-lwiiuse -lbte) -logc -lm

GOALS := $(OUT)/$(TARGET).dol
ifeq ($(PLATFORM),wii)
# For the Homebrew Channel: copy build/ogc/apps/ to the SD card.
HBC := $(OUT)/apps/workshop64
GOALS += $(HBC)/boot.dol $(HBC)/icon.png $(HBC)/meta.xml
$(HBC)/boot.dol: $(OUT)/$(TARGET).dol
	@mkdir -p $(dir $@)
	cp $< $@
$(HBC)/icon.png: $(ROOT)/assets/icon/wii.png
	@mkdir -p $(dir $@)
	cp $< $@
$(HBC)/meta.xml: meta.xml
	@mkdir -p $(dir $@)
	cp $< $@
endif

.DEFAULT_GOAL := goals
goals: $(GOALS)
.PHONY: goals

$(OUT)/$(TARGET).dol: $(BUILD)/$(TARGET).elf
	@mkdir -p $(dir $@)
	elf2dol $< $@
	$(PYTHON) $(ROOT)/tools/dolpad.py $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(MACHDEP) $^ $(LIBS) -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(CORE)/%.c $(CORE_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(GEN)/%.c $(CORE_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

# Header dependencies from -MMD, so changing core/world.h rebuilds everything that uses it.
-include $(OBJS:.o=.d)

endif
