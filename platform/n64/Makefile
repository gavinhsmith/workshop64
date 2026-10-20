BUILD_DIR = build
include $(N64_INST)/include/n64.mk

include ../../core/core.mk
CFLAGS += $(CORE_INC) -MP  # -MP: a removed header must not break old .d files

OUT := $(ROOT)/build/n64

all: $(OUT)/workshop64.z64

# libdragon's %.z64 rule only builds ROMs in this folder, so build it here with a nested make,
# then move it to $(OUT). This target matches %.z64 too, so it gets libdragon's settings for
# the ELF; making the local ROM a prerequisite instead would apply them twice.
$(OUT)/workshop64.z64: $(BUILD_DIR)/workshop64.elf
	$(MAKE) workshop64.z64
	@mkdir -p $(dir $@)
	mv workshop64.z64 $@

$(BUILD_DIR)/core/%.o: $(CORE)/%.c $(CORE_HDR)
	@mkdir -p $(dir $@)
	$(N64_CC) -c $(CFLAGS) -o $@ $<
$(BUILD_DIR)/core/%.o: $(GEN)/%.c $(CORE_HDR)
	@mkdir -p $(dir $@)
	$(N64_CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/workshop64.elf: $(BUILD_DIR)/main.o $(addprefix $(BUILD_DIR)/core/,$(CORE_OBJ))
workshop64.z64: N64_ROM_TITLE = "workshop64"

clean:
	rm -rf $(BUILD_DIR) *.z64 $(OUT)

-include $(wildcard $(BUILD_DIR)/*.d $(BUILD_DIR)/core/*.d)

.PHONY: all clean
