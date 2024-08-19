#[[
    neoclip - Neovim clipboard provider
    Last Change:    2024 Aug 19
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
#]]


cmake_minimum_required(VERSION 3.16)
project(neoclip DESCRIPTION "Neovim clipboard provider" LANGUAGES C)


# selectively enable module build
set(w32_build   "ON")
set(mac_build   "ON")
set(x11_build   "ON")
set(x11uv_build "ON")
set(wl_build    "ON")
set(wluv_build  "OFF")


if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
endif()
set(CMAKE_C_STANDARD "99")
set(CMAKE_C_STANDARD_REQUIRED "ON")
set(CMAKE_C_VISIBILITY_PRESET "internal")
set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/..")
set(CMAKE_SHARED_MODULE_PREFIX "")

set(gnu_like "GNU;Clang;AppleClang")
if(CMAKE_C_COMPILER_ID IN_LIST gnu_like)
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()
unset(gnu_like)


# pkg-config
find_package(PkgConfig REQUIRED)
# Lua(JIT) is always required
pkg_check_modules(PC_LUAJIT REQUIRED luajit)
set(LUA_DEFINITIONS "${PC_LUAJIT_CFLAGS_OTHER}")
find_path(LUA_INCLUDE_DIRS luajit.h PATHS "${PC_LUAJIT_INCLUDEDIR}"
    PATH_SUFFIXES luajit luajit-2.0 luajit-2.1)
find_library(LUA_LIBRARIES PATHS "${PC_LUAJIT_LIBDIR}"
    NAMES luajit libluajit luajit-5.1 libluajit-5.1)


# create/install target with Lua dependency
function(neo_module name)
    cmake_parse_arguments(NEO "" "" "SOURCES;INCLUDE_DIRS;LIBRARIES" ${ARGN})
    add_library(${name} MODULE ${NEO_SOURCES})
    target_compile_definitions(${name} PRIVATE ${LUA_DEFINITIONS})
    target_include_directories(${name} PRIVATE ${LUA_INCLUDE_DIRS} ${NEO_INCLUDE_DIRS})
    target_link_libraries(${name} ${LUA_LIBRARIES} ${NEO_LIBRARIES})
    install(TARGETS ${name} DESTINATION "lua/neoclip" PERMISSIONS OWNER_READ OWNER_WRITE)
endfunction()


if(WIN32)
    # w32-driver
    set(w32_target "w32-driver")
    set(w32_sources "neoclip_w32.c" "neo_common.c")

elseif(APPLE)
    enable_language(OBJC)
    set(CMAKE_OBJC_VISIBILITY_PRESET "internal")
    find_library(APPKIT_LIBRARIES AppKit REQUIRED)

    # mac-driver
    set(mac_target "mac-driver")
    set(mac_sources "neoclip_mac.m" "neo_common.c")
    set(mac_libraries "${APPKIT_LIBRARIES}")

elseif(UNIX)
    find_library(X11_LIBRARIES X11)
    find_package(Threads)
    # Extra CMake Modules
    find_package(ECM)
    if(ECM_FOUND)
        list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
        find_package(Wayland)
        find_package(WaylandScanner)
    endif()
    if(WaylandScanner_FOUND)
        ecm_add_wayland_client_protocol(wlr_data_control BASENAME "wlr-data-control"
            PROTOCOL "extra/wlr-data-control-unstable-v1.xml")
    endif()

    # x11-driver
    if(X11_LIBRARIES AND Threads_FOUND)
        set(x11_target "x11-driver")
        set(x11_sources "neoclip_nix.c" "neo_x11.c" "neo_common.c")
        set(x11_libraries "${X11_LIBRARIES}" Threads::Threads)
    endif()

    # x11uv-driver
    if(X11_LIBRARIES)
        set(x11uv_target "x11uv-driver")
        set(x11uv_sources "neoclip_nix.c" "neo_x11_uv.c" "neo_common.c")
        set(x11uv_libraries "${X11_LIBRARIES}")
    endif()

    # wl-driver
    if(Wayland_FOUND AND wlr_data_control AND Threads_FOUND)
        set(wl_target "wl-driver")
        set(wl_sources "neoclip_nix.c" "neo_wayland.c" "neo_common.c"
            ${wlr_data_control})
        set(wl_include_dirs "${CMAKE_CURRENT_BINARY_DIR}")
        set(wl_libraries "${Wayland_LIBRARIES}" Threads::Threads)
    endif()

    # wluv-driver
    if(Wayland_FOUND AND wlr_data_control)
        set(wluv_target "wluv-driver")
        set(wluv_sources "neoclip_nix.c" "neo_wayland_uv.c" "neo_common.c"
            ${wlr_data_control})
        set(wluv_include_dirs "${CMAKE_CURRENT_BINARY_DIR}")
        set(wluv_libraries "${Wayland_LIBRARIES}")
    endif()
endif()

foreach(t w32 mac x11 x11uv wl wluv)
    if(${t}_target AND ${t}_build)
        message("Building `${t}' target")
        neo_module(${${t}_target} SOURCES ${${t}_sources}
            INCLUDE_DIRS ${${t}_include_dirs} LIBRARIES ${${t}_libraries})
    endif()
endforeach()
