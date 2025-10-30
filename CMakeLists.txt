cmake_minimum_required(VERSION 4.0)

project(${PROJECT_NAME})

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/wayland EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/wlroots-install EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(               libevdev INTERFACE)
target_include_directories(libevdev INTERFACE /usr/include/libevdev-1.0)

# ------------------------------------------------------------------------------

add_library(               pixman INTERFACE)
target_link_libraries(     pixman INTERFACE pixman-1)
target_include_directories(pixman INTERFACE /usr/include/pixman-1)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/backward-cpp EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/magic-enum EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/glm EXCLUDE_FROM_ALL)

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
