ifeq ($(strip $(DEVKITARM)),)
$(error DEVKITARM is not set; use the devkitpro/devkitarm container)
endif

include $(DEVKITARM)/3ds_rules
include ../../core/core.mk

TARGET  := hangout64
BUILD   := build
OBJS    := $(BUILD)/vshader.shbin.o $(BUILD)/main.o $(addprefix $(BUILD)/,$(CORE_OBJ))
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS  := -O2 -Wall $(ARCH) -D__3DS__ -I$(CTRULIB)/include $(CORE_INC) -MMD -MP -I$(BUILD)
LDFLAGS := -specs=3dsx.specs $(ARCH)
LIBS    := -L$(CTRULIB)/lib -lcitro3d -lctru -lm

OUT     := $(ROOT)/build/3ds

all: $(OUT)/$(TARGET).3dsx

$(OUT)/$(TARGET).3dsx: $(BUILD)/$(TARGET).elf
	@mkdir -p $(dir $@)
	3dsxtool $< $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@

# Shader: assemble with picasso, embed with bin2s (also emits vshader_shbin.h).
$(BUILD)/vshader.shbin: vshader.v.pica | $(BUILD)
	picasso -o $@ $<

$(BUILD)/vshader.shbin.o: $(BUILD)/vshader.shbin
	cd $(BUILD) && bin2s -a 4 -H vshader_shbin.h vshader.shbin > vshader_shbin.s
	$(CC) $(ARCH) -c $(BUILD)/vshader_shbin.s -o $@

$(BUILD)/main.o: $(BUILD)/vshader.shbin.o

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(CORE)/%.c $(CORE_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(GEN)/%.c $(CORE_HDR) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) *.3dsx $(OUT)

.PHONY: all clean

# Header dependencies from -MMD, so changing core/world.h rebuilds everything that uses it.
-include $(OBJS:.o=.d)
