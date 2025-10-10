cmake_minimum_required(VERSION 4.0)
project(quartz)

# wayland

add_subdirectory(3rdparty/wayland)

# wlroots

add_library(               wlroots INTERFACE)
target_compile_definitions(wlroots INTERFACE WLR_USE_UNSTABLE)
target_link_libraries(     wlroots INTERFACE wlroots-0.19)
target_include_directories(wlroots INTERFACE /usr/include/wlroots-0.19)

# pixman

add_library(               pixman INTERFACE)
target_link_libraries(     pixman INTERFACE pixman-1)
target_include_directories(pixman INTERFACE /usr/include/pixman-1)

# quartz

add_executable(        quartz)
target_compile_options(quartz PUBLIC
    -std=c23
    -D_POSIX_C_SOURCE=200809L
    )
target_include_directories(quartz PUBLIC src)
target_sources(            quartz PUBLIC src/quartz.c)
target_link_libraries(     quartz PUBLIC
    wayland-server
    wayland-header
    xkbcommon
    wlroots
    pixman
    )
