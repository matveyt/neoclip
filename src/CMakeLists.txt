cmake_minimum_required(VERSION 3.16)
project(neoclip)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release")
endif()
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
set(CMAKE_SHARED_MODULE_PREFIX "")
set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/..")

if(WIN32)
    set(neoclip ${CMAKE_PROJECT_NAME}_w32)
    set(sources ${neoclip}.c neo_common.c)
elseif(APPLE)
    enable_language(OBJC)
    set(neoclip ${CMAKE_PROJECT_NAME}_mac)
    set(sources ${neoclip}.m neo_common.c)
else(UNIX)
    set(neoclip ${CMAKE_PROJECT_NAME}_x11)
    set(sources ${neoclip}.c neo_x.c neo_common.c)
endif()
add_library(${neoclip} MODULE ${sources})
install(TARGETS ${neoclip} DESTINATION lua PERMISSIONS OWNER_READ OWNER_WRITE)

find_package(LuaJit REQUIRED)
target_include_directories(${neoclip} PRIVATE ${LUAJIT_INCLUDE_DIR})
target_link_libraries(${neoclip} ${LUAJIT_LIBRARIES})

if(APPLE)
    find_library(APPKIT AppKit REQUIRED)
    target_link_libraries(${neoclip} ${APPKIT})
elseif(UNIX)
    find_library(X11 X11 REQUIRED)
    find_package(Threads)
    target_link_libraries(${neoclip} ${X11} ${CMAKE_THREADS_LIB_INIT})

    # Wayland support (WIP)
    find_package(ECM QUIET)
    list(APPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
    find_package(Wayland QUIET)
    find_package(WaylandProtocols QUIET)
    find_package(WaylandScanner QUIET)
    find_library(RT rt)
    if(FALSE) # (Wayland_FOUND AND WaylandProtocols_FOUND AND WaylandScanner_FOUND)
        set(neoclip2 ${CMAKE_PROJECT_NAME}_wl)
        add_library(${neoclip2} MODULE ${neoclip2}.c neo_w.c neo_common.c)
        install(TARGETS ${neoclip2} DESTINATION lua PERMISSIONS OWNER_READ OWNER_WRITE)

        target_include_directories(${neoclip2} PRIVATE ${LUAJIT_INCLUDE_DIR})
        target_link_libraries(${neoclip2} ${LUAJIT_LIBRARIES} ${CMAKE_THREADS_LIB_INIT})

        ecm_add_wayland_client_protocol(xdg_shell BASENAME xdg-shell
            PROTOCOL "${WaylandProtocols_DATADIR}/stable/xdg-shell/xdg-shell.xml")
        target_sources(${neoclip2} PRIVATE ${xdg_shell})
        target_include_directories(${neoclip2} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(${neoclip2} ${Wayland_LIBRARIES} ${RT})
    endif()
endif()