# Builds both hangout64_gamecube.dol and hangout64_wii.dol from the same source.
ifeq ($(strip $(DEVKITPPC)),)
$(error DEVKITPPC is not set; use the devkitpro/devkitppc container)
endif

ifeq ($(PLATFORM),)

all:
	$(MAKE) PLATFORM=gamecube
	$(MAKE) PLATFORM=wii

clean:
	rm -rf build *.dol

.PHONY: all clean

else

include $(DEVKITPPC)/$(PLATFORM)_rules
include ../../core/core.mk

TARGET := hangout64_$(PLATFORM)
BUILD  := build/$(PLATFORM)
OBJS   := $(BUILD)/main.o $(addprefix $(BUILD)/,$(CORE_OBJ))
CFLAGS := -O2 -Wall $(MACHDEP) -I$(LIBOGC_INC) $(CORE_INC) -MMD -MP
LIBS   := -L$(LIBOGC_LIB) $(if $(filter wii,$(PLATFORM)),-lwiiuse -lbte) -logc -lm

.DEFAULT_GOAL := $(TARGET).dol

$(TARGET).dol: $(BUILD)/$(TARGET).elf
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
