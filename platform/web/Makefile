# Browser build: WebAssembly via Emscripten, with its SDL3 port and WebGL.
ifeq ($(shell command -v emcc),)
$(error emcc not found; use the emscripten/emsdk container)
endif

include ../../core/core.mk

CC      := emcc
BUILD   := build
OUT     := $(ROOT)/build/web
OBJS    := $(BUILD)/main.o $(addprefix $(BUILD)/,$(CORE_OBJ))
CFLAGS  := -O2 -Wall --use-port=sdl3 -I$(CORE) -MMD -MP
LDFLAGS := --use-port=sdl3 -sALLOW_MEMORY_GROWTH --shell-file shell.html

all: $(OUT)/index.html

$(OUT)/index.html: $(OBJS) shell.html | $(OUT)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(CORE)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(GEN)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD) $(OUT):
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(OUT)

.PHONY: all clean

# Header dependencies from -MMD, so changing core/world.h rebuilds everything that uses it.
-include $(OBJS:.o=.d)
