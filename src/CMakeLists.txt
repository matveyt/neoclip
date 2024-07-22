#[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 Jul 19
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
#]]


cmake_minimum_required(VERSION 3.16)
project(neoclip DESCRIPTION "Neovim clipboard provider" LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
endif()
set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/..")
set(CMAKE_SHARED_MODULE_PREFIX "")
set(CMAKE_C_VISIBILITY_PRESET "internal")
if(APPLE)
    enable_language(OBJC)
    set(CMAKE_OBJC_VISIBILITY_PRESET "internal")
endif()


# create/install target with Lua dependency
function(neo_module name)
    if(NOT name)
        return()
    endif()
    cmake_parse_arguments(NEO "" "" "SOURCES;INCLUDE_DIRS;LIBRARIES" ${ARGN})
    add_library(${name} MODULE ${NEO_SOURCES})
    set_property(TARGET ${name} PROPERTY C_STANDARD "99")
    target_compile_definitions(${name} PRIVATE ${LUA_DEFINITIONS})
    target_include_directories(${name} PRIVATE ${LUA_INCLUDE_DIRS} ${NEO_INCLUDE_DIRS})
    target_link_libraries(${name} ${LUA_LIBRARIES} ${NEO_LIBRARIES})
    install(TARGETS ${name} DESTINATION "lua/neoclip" PERMISSIONS OWNER_READ OWNER_WRITE)
endfunction()


# pkg-config
find_package(PkgConfig REQUIRED)
# Extra CMake Modules
find_package(ECM QUIET)
list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
# Lua(JIT)
pkg_check_modules(PC_LUAJIT REQUIRED luajit)
set(LUA_DEFINITIONS "${PC_LUAJIT_CFLAGS_OTHER}")
find_path(LUA_INCLUDE_DIRS luajit.h PATHS "${PC_LUAJIT_INCLUDEDIR}"
    PATH_SUFFIXES luajit luajit-2.0 luajit-2.1)
find_library(LUA_LIBRARIES PATHS "${PC_LUAJIT_LIBDIR}"
    NAMES luajit libluajit luajit-5.1 libluajit-5.1)


if(WIN32)
    # w32-driver
    set(target1 "w32-driver")
    set(sources1 "neoclip_w32.c" "neo_common.c")

elseif(APPLE)
    find_library(APPKIT_LIBRARIES AppKit REQUIRED)
    # mac-driver
    set(target1 "mac-driver")
    set(sources1 "neoclip_mac.m" "neo_common.c")
    set(libraries1 ${APPKIT_LIBRARIES})

elseif(UNIX)
    find_library(X11_LIBRARIES X11 REQUIRED)
    find_package(Threads)
    # x11-driver
    set(target1 "x11-driver")
    set(sources1 "neoclip_nix.c" "neo_x11.c" "neo_common.c")
    set(libraries1 ${X11_LIBRARIES} "${CMAKE_THREADS_LIB_INIT}")

    # try finding Wayland
    find_package(Wayland QUIET)
    find_package(WaylandScanner QUIET)
    if(Wayland_FOUND AND WaylandScanner_FOUND)
        ecm_add_wayland_client_protocol(wlr_data_control BASENAME "wlr-data-control"
            PROTOCOL "extra/wlr-data-control-unstable-v1.xml")
        # wl-driver
        set(target2 "wl-driver")
        set(sources2 "neoclip_nix.c" "neo_wayland.c" "neo_common.c" ${wlr_data_control})
        set(include_dirs2 "${CMAKE_CURRENT_BINARY_DIR}")
        set(libraries2 ${Wayland_LIBRARIES} "${CMAKE_THREADS_LIB_INIT}")
    endif()
endif()

neo_module("${target1}" SOURCES ${sources1} INCLUDE_DIRS ${include_dirs1}
    LIBRARIES ${libraries1})

neo_module("${target2}" SOURCES ${sources2} INCLUDE_DIRS ${include_dirs2}
    LIBRARIES ${libraries2})
