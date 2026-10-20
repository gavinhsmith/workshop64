cmake_minimum_required(VERSION 3.20)
project(hangout64 C)

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The C generated from assets, as in core/core.mk.
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GEN ${CMAKE_BINARY_DIR}/gen)
set(TOOLS ${ROOT}/tools/objload.py ${ROOT}/tools/texload.py)
file(GLOB ASSETS ${ROOT}/assets/model/* ${ROOT}/assets/texture/*)
add_custom_command(OUTPUT ${GEN}/guy_mesh.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
    COMMAND Python3::Interpreter ${ROOT}/tools/obj2c.py ${ROOT}/assets/model/guy.obj ${GEN}/guy_mesh.c guy_mesh 255,255,255
            ${ROOT}/assets/texture/guy_base.png guy_texture ${ROOT}/assets/texture/guy_coat.png
    DEPENDS ${ROOT}/tools/obj2c.py ${TOOLS} ${ASSETS})
set(LUA ${ROOT}/third_party/lua)
if(NOT EXISTS ${LUA}/luaconf.h)
    message(FATAL_ERROR "third_party/lua is empty: run git submodule update --init")
endif()
set(EXPERIENCE ${ROOT}/experiences/course)
file(GLOB_RECURSE EXPERIENCE_FILES ${EXPERIENCE}/*)
add_custom_command(OUTPUT ${GEN}/builtin_experience.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
    COMMAND Python3::Interpreter ${ROOT}/tools/wb64.py ${EXPERIENCE} ${GEN}/builtin_experience.c builtin_experience
    DEPENDS ${ROOT}/tools/wb64.py ${TOOLS} ${EXPERIENCE_FILES})
add_custom_command(OUTPUT ${GEN}/lua32.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
    COMMAND Python3::Interpreter ${ROOT}/tools/lua32.py ${LUA}/luaconf.h ${GEN}/lua32.h
    DEPENDS ${ROOT}/tools/lua32.py ${LUA}/luaconf.h)

add_executable(hangout64 main.c ${ROOT}/core/world.c ${ROOT}/core/wb64.c ${ROOT}/core/lua.c
    ${GEN}/guy_mesh.c ${GEN}/builtin_experience.c ${GEN}/lua32.h)
target_include_directories(hangout64 PRIVATE ${ROOT}/core ${GEN} ${LUA})
if(NOT WIN32)
    target_link_libraries(hangout64 PRIVATE m)
endif()
target_link_libraries(hangout64 PRIVATE SDL3::SDL3 OpenGL::GL)
# The finished program goes to build/pc, wherever CMake's own files are, for every generator
# and config ($<1:...> stops multi-config generators adding a Debug/Release folder).
set_target_properties(hangout64 PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${ROOT}/build/pc>)
if(APPLE)
    target_compile_definitions(hangout64 PRIVATE GL_SILENCE_DEPRECATION)
endif()
