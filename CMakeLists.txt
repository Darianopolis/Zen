cmake_minimum_required(VERSION 4.0)

project(zen)

# ------------------------------------------------------------------------------

add_subdirectory(3rdparty/wayland EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

set(USE_GIT_WLROOTS OFF)
if (USE_GIT_WLROOTS)
    add_subdirectory(3rdparty/wlroots-install EXCLUDE_FROM_ALL)
else()
    add_library(               wlroots INTERFACE)
    target_compile_definitions(wlroots INTERFACE WLR_USE_UNSTABLE)
    target_link_libraries(     wlroots INTERFACE wlroots-0.19)
    target_include_directories(wlroots INTERFACE /usr/include/wlroots-0.19)
endif()

# ------------------------------------------------------------------------------

add_library(               libevdev INTERFACE)
target_include_directories(libevdev INTERFACE /usr/include/libevdev-1.0)

# ------------------------------------------------------------------------------

add_library(               pixman INTERFACE)
target_link_libraries(     pixman INTERFACE pixman-1)
target_include_directories(pixman INTERFACE /usr/include/pixman-1)

# ------------------------------------------------------------------------------

add_subdirectory(3rdparty/backward-cpp EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(3rdparty/magic-enum EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_executable(            ${PROJECT_NAME})
target_precompile_headers( ${PROJECT_NAME} PUBLIC src/pch.hpp)
target_compile_definitions(${PROJECT_NAME} PUBLIC "PROGRAM_NAME=\"${PROJECT_NAME}\"")
target_compile_options(    ${PROJECT_NAME} PUBLIC
    -std=c++26
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wno-sometimes-uninitialized
    -Wno-missing-designated-field-initializers
    -Wno-missing-field-initializers
    -Wno-unknown-pragmas
    -Wno-gnu-conditional-omitted-operand
    -Wno-zero-length-array
    -Wimplicit-fallthrough
    )
if(USE_ASAN)
    target_compile_options(${PROJECT_NAME} PUBLIC -fsanitize=address)
endif()
target_include_directories(${PROJECT_NAME} PUBLIC src)
target_sources(            ${PROJECT_NAME} PUBLIC
    src/surface.cpp
    src/seat.cpp
    src/log.cpp
    src/main.cpp
    src/output.cpp
    src/util.cpp
    src/zone.cpp
    src/debug.cpp
    src/ipc.cpp
    src/commands.cpp
    )
if(USE_ASAN)
    target_link_libraries(${PROJECT_NAME} PUBLIC asan)
endif()
target_link_libraries(${PROJECT_NAME} PUBLIC
    wayland-server
    wayland-header
    xkbcommon
    wlroots
    pixman
    libevdev
    input
    evdev
    stdc++exp
    Backward::Object
    magic_enum::magic_enum
    )

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin COMPONENT Runtime)
install(FILES resources/portals.conf DESTINATION xdg-desktop-portal RENAME ${PROJECT_NAME}-portals.conf)
