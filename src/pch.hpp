#pragma once

#include <vector>
#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <typeinfo>
#include <cstring>
#include <span>
#include <string_view>
#include <optional>
#include <filesystem>
#include <variant>
#include <bit>
#include <algorithm>
#include <source_location>

#include <csignal>

#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>
#include <libevdev/libevdev.h>
#include "wlroots.hpp"

#include <unistd.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#include <magic_enum/magic_enum.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

using namespace std::literals;
