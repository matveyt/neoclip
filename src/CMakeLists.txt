cmake_minimum_required(VERSION 3.16)
project(neoclip DESCRIPTION "Neovim clipboard provider" LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "release")
endif()
set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/..")
set(CMAKE_SHARED_MODULE_PREFIX "")
set(CMAKE_C_VISIBILITY_PRESET "internal")
if(APPLE)
    enable_language(OBJC)
    set(CMAKE_OBJC_VISIBILITY_PRESET "internal")
endif()

# LuaJIT & ECM
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/extra")
find_package(LuaJit REQUIRED)
find_package(ECM QUIET)
list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")

# create/install target with LuaJIT dependency
function(neo_module target)
    cmake_parse_arguments(NEO "" "PLATFORM" "SOURCES;INCLUDES;LIBRARIES" ${ARGN})
    add_library(${target} MODULE ${NEO_SOURCES})
    set_property(TARGET ${target} PROPERTY C_STANDARD 99)
    target_compile_definitions(${target} PRIVATE "-DPLATFORM=${NEO_PLATFORM}")
    target_include_directories(${target} PRIVATE ${LUAJIT_INCLUDE_DIR} ${NEO_INCLUDES})
    target_link_libraries(${target} ${LUAJIT_LIBRARIES} ${NEO_LIBRARIES})
    install(TARGETS ${target} DESTINATION lua/neoclip PERMISSIONS OWNER_READ OWNER_WRITE)
endfunction()

if(WIN32)
    # neoclip.w32
    set(target w32)
    set(platform WinAPI)
    set(sources neoclip_w32.c neo_common.c)
elseif(APPLE)
    find_library(APPKIT_LIBRARIES AppKit REQUIRED)
    # neoclip.mac
    set(target mac)
    set(platform AppKit)
    set(sources neoclip_mac.m neo_common.c)
    set(libraries ${APPKIT_LIBRARIES})
else(UNIX)
    find_library(X11_LIBRARIES X11 REQUIRED)
    find_package(Threads)
    # neoclip.x11
    set(target x11)
    set(platform X11)
    set(sources neoclip_nix.c neo_x11.c neo_common.c)
    set(libraries ${X11_LIBRARIES} ${CMAKE_THREADS_LIB_INIT})
    # try finding Wayland
    find_package(Wayland QUIET)
    find_package(WaylandScanner QUIET)
    if(Wayland_FOUND AND WaylandScanner_FOUND)
        ecm_add_wayland_client_protocol(wlr_data_control BASENAME wlr-data-control
            PROTOCOL extra/wlr-data-control-unstable-v1.xml)
        # neoclip.wl
        set(target2 wl)
        set(platform2 Wayland)
        set(sources2 neoclip_nix.c neo_wayland.c neo_common.c ${wlr_data_control})
        set(includes2 ${CMAKE_CURRENT_BINARY_DIR})
        set(libraries2 ${Wayland_LIBRARIES} ${CMAKE_THREADS_LIB_INIT})
    endif()
endif()

neo_module(${target} PLATFORM ${platform} SOURCES ${sources} INCLUDES ${includes}
    LIBRARIES ${libraries})
if(target2)
    neo_module(${target2} PLATFORM ${platform2} SOURCES ${sources2} INCLUDES ${includes2}
        LIBRARIES ${libraries2})
endif()
