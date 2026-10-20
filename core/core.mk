# The core sources, and the C generated from assets into build/gen, for every Makefile that
# builds the core (set ROOT first if the repo root isn't ../..). Compile core and generated
# files with $(CORE_INC) and make them depend on $(CORE_HDR). platform/pc/CMakeLists.txt
# does the same itself.
CORE_MK_GOAL := $(.DEFAULT_GOAL)
ROOT     ?= ../..
CORE     := $(ROOT)/core
GEN      := $(ROOT)/build/gen
# python3, or python where python3 doesn't run (on Windows it's often the Store stub).
PYTHON   ?= $(if $(shell python3 -c "print(1)" 2>/dev/null),python3,python)
LUA      := $(ROOT)/third_party/lua
CORE_SRC := world.c wb64.c lua.c
GEN_SRC  := guy_mesh.c builtin_experience.c
CORE_OBJ := $(CORE_SRC:.c=.o) $(GEN_SRC:.c=.o)
CORE_HDR := $(GEN)/lua32.h
CORE_INC := -I$(CORE) -I$(GEN) -I$(LUA)
EXPERIENCE := $(ROOT)/experiences/course

ifeq ($(wildcard $(LUA)/luaconf.h),)
$(error third_party/lua is empty: run git submodule update --init)
endif

# A failed generator must not leave a half-written file that looks up to date.
.DELETE_ON_ERROR:

$(GEN)/guy_mesh.c: $(ROOT)/tools/obj2c.py $(ROOT)/tools/objload.py $(ROOT)/tools/texload.py $(ROOT)/assets/model/guy.obj $(ROOT)/assets/texture/guy_base.png $(ROOT)/assets/texture/guy_coat.png
	@mkdir -p $(dir $@)
	$(PYTHON) $< $(ROOT)/assets/model/guy.obj $@ guy_mesh 255,255,255 $(ROOT)/assets/texture/guy_base.png guy_texture $(ROOT)/assets/texture/guy_coat.png

$(GEN)/builtin_experience.c: $(ROOT)/tools/wb64.py $(ROOT)/tools/objload.py $(ROOT)/tools/texload.py $(wildcard $(EXPERIENCE)/* $(EXPERIENCE)/*/* $(EXPERIENCE)/*/*/*)
	@mkdir -p $(dir $@)
	$(PYTHON) $< $(EXPERIENCE) $@ builtin_experience

$(GEN)/lua32.h: $(ROOT)/tools/lua32.py $(LUA)/luaconf.h
	@mkdir -p $(dir $@)
	$(PYTHON) $< $(LUA)/luaconf.h $@

# Keep these rules from becoming the default target.
.DEFAULT_GOAL := $(CORE_MK_GOAL)
