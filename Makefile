# Local builds use the same toolchain images as CI.
IMAGE_n64 := ghcr.io/dragonminded/libdragon:preview
IMAGE_ogc := devkitpro/devkitppc:latest
IMAGE_nds := devkitpro/devkitarm:latest
IMAGE_3ds := devkitpro/devkitarm:latest
IMAGE_web := emscripten/emsdk:latest

# Built in Docker; web isn't a console but builds the same way.
CONSOLES := n64 ogc nds 3ds web

# Keep the N64 toolchain (with libdragon installed into it) in a named volume, so
# libdragon is only built once. `make n64-reset` drops it, e.g. after an image update.
N64_VOLUME := hangout64-n64-toolchain
DOCKER_ARGS_n64 := -v $(N64_VOLUME):/n64_toolchain
# Emscripten builds its SDL3 port into its cache; keep it so that only happens once.
DOCKER_ARGS_web := -v hangout64-emsdk-cache:/emsdk/upstream/emscripten/cache

# Stop Git Bash on Windows rewriting the container paths (/src/...) into Windows paths.
export MSYS_NO_PATHCONV := 1

all: test pc $(CONSOLES)

test:
	$(MAKE) -C core test

# Finished files go to build/<platform>; CMake keeps its own files in build/cmake. On Windows,
# use MinGW (MSYS2's gcc and SDL3): CMake's default there, Visual Studio, can't build against
# MSYS2's SDL3. Set CMAKE_ARGS to choose otherwise (delete build/cmake when switching).
ifeq ($(OS),Windows_NT)
CMAKE_ARGS ?= -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM="$(MAKE)"
endif
pc:
	cmake -S platform/pc -B build/cmake $(CMAKE_ARGS)
	cmake --build build/cmake

$(CONSOLES):
	docker run --rm $(DOCKER_ARGS_$@) -v "$(CURDIR):/src" -w /src/platform/$@ $(IMAGE_$@) make

clean:
	rm -rf build core/test_core
	$(foreach c,$(CONSOLES),docker run --rm $(DOCKER_ARGS_$(c)) -v "$(CURDIR):/src" -w /src/platform/$(c) $(IMAGE_$(c)) make clean;)

n64-reset:
	docker volume rm $(N64_VOLUME)

.PHONY: all test pc clean n64-reset $(CONSOLES)
