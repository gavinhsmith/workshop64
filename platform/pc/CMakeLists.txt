cmake_minimum_required(VERSION 3.20)
project(workshop64 C)

find_package(SDL3 REQUIRED)
find_package(OpenGL REQUIRED)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The C generated from assets, as in core/core.mk.
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GEN ${CMAKE_BINARY_DIR}/gen)
set(TOOLS ${ROOT}/tools/objload.py ${ROOT}/tools/texload.py)
set(LUA ${ROOT}/third_party/lua)
if(NOT EXISTS ${LUA}/luaconf.h)
    message(FATAL_ERROR "third_party/lua is empty: run git submodule update --init")
endif()
set(EXPERIENCE ${ROOT}/experiences/course)
file(GLOB_RECURSE EXPERIENCE_FILES ${EXPERIENCE}/*)
add_custom_command(OUTPUT ${GEN}/builtin_experience.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
    COMMAND Python3::Interpreter ${ROOT}/tools/ws64.py ${EXPERIENCE} ${GEN}/builtin_experience.c builtin_experience
    DEPENDS ${ROOT}/tools/ws64.py ${TOOLS} ${EXPERIENCE_FILES})
add_custom_command(OUTPUT ${GEN}/lua32.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
    COMMAND Python3::Interpreter ${ROOT}/tools/lua32.py ${LUA}/luaconf.h ${GEN}/lua32.h
    DEPENDS ${ROOT}/tools/lua32.py ${LUA}/luaconf.h)

# The icon (assets/icon, made from icon.svg): pixels for the window, and on Windows the .ico
# for the executable itself.
add_custom_command(OUTPUT ${GEN}/icon.c
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN}
    COMMAND Python3::Interpreter ${ROOT}/tools/icon2c.py ${ROOT}/assets/icon/icon-64.png ${GEN}/icon.c app_icon
    DEPENDS ${ROOT}/tools/icon2c.py ${ROOT}/tools/texload.py ${ROOT}/assets/icon/icon-64.png)
set(ICON_SRC ${GEN}/icon.c)
if(WIN32)
    enable_language(RC)
    file(REAL_PATH ${ROOT}/assets/icon/workshop64.ico ICO)
    file(WRITE ${CMAKE_BINARY_DIR}/workshop64.rc "1 ICON \"${ICO}\"\n")
    list(APPEND ICON_SRC ${CMAKE_BINARY_DIR}/workshop64.rc)
endif()

add_executable(workshop64 main.c ${ROOT}/core/world.c ${ROOT}/core/avatar.c ${ROOT}/core/ws64.c ${ROOT}/core/lua.c
    ${GEN}/builtin_experience.c ${GEN}/lua32.h ${ICON_SRC})
target_include_directories(workshop64 PRIVATE ${ROOT}/core ${GEN} ${LUA})
if(NOT WIN32)
    target_link_libraries(workshop64 PRIVATE m)
endif()
target_link_libraries(workshop64 PRIVATE SDL3::SDL3 OpenGL::GL)
# The finished program goes to build/pc, wherever CMake's own files are, for every generator
# and config ($<1:...> stops multi-config generators adding a Debug/Release folder).
set_target_properties(workshop64 PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${ROOT}/build/pc>)
if(APPLE)
    target_compile_definitions(workshop64 PRIVATE GL_SILENCE_DEPRECATION)
endif()
