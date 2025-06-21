#[[
    neoclip - Neovim clipboard provider
    Last Change:    2025 Jun 21
    License:        https://unlicense.org
    URL:            https://github.com/matveyt/neoclip
#]]


cmake_minimum_required(VERSION 3.16)
project(neoclip DESCRIPTION "Neovim clipboard provider" LANGUAGES C)


# selectively enable module build
set(w32_target      "ON")
set(mac_target      "ON")
set(x11_target      "ON")
set(x11uv_target    "ON")
set(wl_target       "ON")
set(wluv_target     "ON")


if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
endif()
set(CMAKE_C_EXTENSIONS "OFF")
set(CMAKE_C_STANDARD "99")
set(CMAKE_C_STANDARD_REQUIRED "ON")
set(CMAKE_C_VISIBILITY_PRESET "internal")
set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/..")
set(CMAKE_SHARED_MODULE_PREFIX "")

set(gnu_like_compilers "GNU;Clang;AppleClang")
if(CMAKE_C_COMPILER_ID IN_LIST gnu_like_compilers)
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()


# pkg-config required
find_package(PkgConfig REQUIRED)
# Lua(JIT) is always required
pkg_check_modules(PC_LUAJIT REQUIRED luajit)
find_library(LUA_LIBRARIES PATHS "${PC_LUAJIT_LIBDIR}"
    NAMES luajit libluajit luajit-5.1 libluajit-5.1)
set(LUA_DEFINITIONS "${PC_LUAJIT_CFLAGS_OTHER}")
find_path(LUA_INCLUDE_DIRS luajit.h PATHS "${PC_LUAJIT_INCLUDEDIR}"
    PATH_SUFFIXES luajit luajit-2.0 luajit-2.1)


# create/install target with Lua dependency
function(neo_module name)
    cmake_parse_arguments(NEO "" "" "SOURCES;DEFINITIONS;LIBRARIES;INCLUDE_DIRS" ${ARGN})
    add_library(${name} MODULE ${NEO_SOURCES})
    target_compile_definitions(${name} PRIVATE ${LUA_DEFINITIONS} ${NEO_DEFINITIONS})
    target_link_libraries(${name} ${LUA_LIBRARIES} ${NEO_LIBRARIES})
    target_include_directories(${name} PRIVATE ${LUA_INCLUDE_DIRS} ${NEO_INCLUDE_DIRS})
    install(TARGETS ${name} DESTINATION "lua/neoclip" PERMISSIONS OWNER_READ OWNER_WRITE)
endfunction()


if(WIN32)
    # w32-driver
    set(w32_sources "neoclip_w32.c" "neo_common.c")

elseif(APPLE)
    enable_language(OBJC)
    set(CMAKE_OBJC_VISIBILITY_PRESET "internal")
    find_library(APPKIT_LIBRARIES AppKit REQUIRED)

    # mac-driver
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
        ecm_add_wayland_client_protocol(ext_data_control BASENAME "ext-data-control"
            PROTOCOL "extra/ext-data-control-v1.xml")
        ecm_add_wayland_client_protocol(wlr_data_control BASENAME "wlr-data-control"
            PROTOCOL "extra/wlr-data-control-unstable-v1.xml")
    endif()

    # x11-driver
    if(X11_LIBRARIES AND Threads_FOUND)
        set(x11_sources "neoclip_nix.c" "neo_x11.c" "neo_common.c")
        set(x11_definitions "WITH_THREADS")
        set(x11_libraries "${X11_LIBRARIES}" Threads::Threads)
    endif()

    # x11uv-driver
    if(X11_LIBRARIES)
        set(x11uv_sources "neoclip_nix.c" "neo_x11.c" "neo_common.c")
        set(x11uv_libraries "${X11_LIBRARIES}")
    endif()

    # wl-driver
    if(Wayland_FOUND AND WaylandScanner_FOUND AND Threads_FOUND)
        set(wl_sources "neoclip_nix.c" "neo_wayland.c" "neo_common.c"
            "${ext_data_control}" "${wlr_data_control}")
        set(wl_definitions "WITH_THREADS")
        set(wl_libraries "${Wayland_LIBRARIES}" Threads::Threads)
        set(wl_include_dirs "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    # wluv-driver
    if(Wayland_FOUND AND WaylandScanner_FOUND)
        set(wluv_sources "neoclip_nix.c" "neo_wayland.c" "neo_common.c"
            "${ext_data_control}" "${wlr_data_control}")
        set(wluv_libraries "${Wayland_LIBRARIES}")
        set(wluv_include_dirs "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
endif()

foreach(t w32 mac x11 x11uv wl wluv)
    if(${t}_target AND ${t}_sources)
        message("Building `${t}-driver'")
        neo_module(${t}-driver SOURCES ${${t}_sources} DEFINITIONS ${${t}_definitions}
            LIBRARIES ${${t}_libraries} INCLUDE_DIRS ${${t}_include_dirs})
    endif()
endforeach()
