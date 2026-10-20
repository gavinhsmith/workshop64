# Local builds use the same toolchain images as CI. The N64 one is built here from
# platform/n64/Dockerfile (libdragon no longer publishes a public image).
IMAGE_n64 := workshop64-n64
IMAGE_ogc := devkitpro/devkitppc:latest
IMAGE_nds := devkitpro/devkitarm:latest
IMAGE_3ds := devkitpro/devkitarm:latest
IMAGE_web := emscripten/emsdk:latest

# Built in Docker; web isn't a console but builds the same way.
CONSOLES := n64 ogc nds 3ds web

# Emscripten builds its SDL3 port into its cache; keep it so that only happens once.
DOCKER_ARGS_web := -v workshop64-emsdk-cache:/emsdk/upstream/emscripten/cache

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
# CMake refuses a cache made for another folder (the repo moved or was renamed): start fresh.
pc:
	@if [ -f build/cmake/CMakeCache.txt ] && ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$(CURDIR)/platform/pc$$" build/cmake/CMakeCache.txt; then \
		echo "build/cmake was made for another folder; starting it again"; rm -rf build/cmake; fi
	cmake -S platform/pc -B build/cmake $(CMAKE_ARGS)
	cmake --build build/cmake

# Docker caches the image, so this is quick after the first time.
n64-image:
	docker build -t $(IMAGE_n64) platform/n64
n64 clean: n64-image

$(CONSOLES):
	docker run --rm $(DOCKER_ARGS_$@) -v "$(CURDIR):/src" -w /src/platform/$@ $(IMAGE_$@) make

clean:
	rm -rf build core/test_core
	$(foreach c,$(CONSOLES),docker run --rm $(DOCKER_ARGS_$(c)) -v "$(CURDIR):/src" -w /src/platform/$(c) $(IMAGE_$(c)) make clean;)

# Rebuild the N64 image from scratch, picking up the latest toolchain and libdragon.
n64-reset:
	docker build --no-cache -t $(IMAGE_n64) platform/n64

.PHONY: all test pc clean n64-image n64-reset $(CONSOLES)
