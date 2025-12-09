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

add_subdirectory(${VENDOR_DIR}/unordered-dense EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(                stb-image ${VENDOR_DIR}/stb/stb_image.h)
set_source_files_properties(stb-image PUBLIC ${VENDOR_DIR}/stb/stb_image.h PROPERTIES LANGUAGE C)
target_include_directories( stb-image PUBLIC ${VENDOR_DIR}/stb)
target_compile_definitions( stb-image PRIVATE STB_IMAGE_IMPLEMENTATION=1)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/glm EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_subdirectory(${VENDOR_DIR}/sol2 EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------

add_library(               luajit INTERFACE)
target_link_libraries(     luajit INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/${VENDOR_DIR}/luajit/src/libluajit.a)
target_include_directories(luajit INTERFACE ${VENDOR_DIR}/luajit/src)

# ------------------------------------------------------------------------------

add_executable(            ${PROJECT_NAME})
if(USE_ASAN)
    target_compile_options(${PROJECT_NAME} PUBLIC -fsanitize=address)
    target_link_libraries(${PROJECT_NAME} PUBLIC asan)
endif()
target_precompile_headers( ${PROJECT_NAME} PUBLIC src/pch.hpp)
target_compile_definitions(${PROJECT_NAME} PUBLIC "PROGRAM_NAME=\"${PROJECT_NAME}\"")
target_compile_definitions(${PROJECT_NAME} PUBLIC "DBUS_NAME=\"darianopolis\"")
target_compile_definitions(${PROJECT_NAME} PUBLIC "DBUS_TLD=\"com\"")
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
    -Wno-unused-function
    -Wno-unused-parameter
    -Wimplicit-fallthrough
    -Wshadow
    )
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
    src/process.cpp
    src/client.cpp
    src/script.cpp
    src/bind.cpp
    src/watchdog.cpp
    src/background.cpp
    src/borders.cpp
    src/dbus.cpp
    )
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
    sol2::sol2
    luajit
    stb-image
    unordered_dense::unordered_dense
    )
