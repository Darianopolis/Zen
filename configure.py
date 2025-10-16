
import os
import sys
from pathlib import Path
import subprocess
import shutil
import glob

# -----------------------------------------------------------------------------

color_debug = "\u001B[96m"
color_info = "\u001B[94m"
color_warn = "\u001B[93m"
color_fatal = "\u001B[91m"
color_reset = "\u001B[0m"

clear_line = "\x1b[1K\r"

def log_debug(message):
    print(f" [{color_debug}INFO{color_reset}] {message}")

def log_info(message):
    print(f" [{color_info}INFO{color_reset}] {message}")

def log_info_progress(message):
    print(f"{clear_line} [{color_info}INFO{color_reset}] {message}", end = '')

def log_info_progress_clear():
    print(clear_line, end = '\r')

def log_info_progress_end():
    print()

def log_warn(message):
    print(f" [{color_warn}WARN{color_reset}] {message}")

def log_error(message):
    print(f"[{color_fatal}ERROR{color_reset}] {message}")

def panic(message, code = 1):
    print(f"[{color_fatal}FATAL{color_reset}] {message}")
    sys.exit(code)

# -----------------------------------------------------------------------------

def ensure_dir(path):
    os.makedirs(path, exist_ok=True)
    return Path(path)

vendor_dir = ensure_dir("3rdparty")

# -----------------------------------------------------------------------------

def list_wayland_protocols():

    wayland_protocols = []

    system_protocol_dir = Path("/usr/share/wayland-protocols")
    for category in os.listdir(system_protocol_dir):
        category_path = system_protocol_dir / category
        for subfolder in os.listdir(category_path):
            subfolder_path = category_path / subfolder
            for name in os.listdir(subfolder_path):
                wayland_protocols += [(subfolder_path / name, Path(name).stem)]

    return wayland_protocols

def generate_wayland_protocols():

    wayland_scanner = "wayland-scanner"                   # Wayland scanner executable
    wayland_dir = vendor_dir / "wayland"                  #
    wayland_src = ensure_dir(wayland_dir / "src")         # Directory for generate sources
    wayland_include = ensure_dir(wayland_dir / "include") # Directory for generate headers

    cmake_target_name = "wayland-header"
    with open(wayland_dir / "CMakeLists.txt", "w") as cmakelists:
        cmakelists.write(f"add_library({cmake_target_name}\n")

        for xml_path, name in list_wayland_protocols():

            # Generate header
            header_name = f"{name}-protocol.h"
            header_path = wayland_include / header_name
            if not header_path.exists():
                cmd = [wayland_scanner, "server-header", xml_path, header_name]
                log_debug(f"Generating wayland header: {header_name}")
                subprocess.run(cmd, cwd = wayland_include)

            # Generate source
            source_name = f"{name}-protocol.c"
            source_path = wayland_src / source_name
            if not source_path.exists():
                cmd = [wayland_scanner, "private-code", xml_path, source_name]
                log_debug(f"Generating wayland source: {source_name}")
                subprocess.run(cmd, cwd = wayland_src)

            # Add source to CMakeLists
            cmakelists.write(f"    \"src/{source_name}\"\n")

        cmakelists.write("    )\n")
        cmakelists.write(f"target_include_directories({cmake_target_name} PUBLIC include)\n")

generate_wayland_protocols()

# -----------------------------------------------------------------------------

def git_fetch(dir, repo, branch):
    if dir.exists():
        cmd = ["git", "pull"]
        log_debug(f"{cmd} @ {dir}")
        subprocess.run(cmd, cwd = dir)
    else:
        cmd = ["git", "clone", repo, "--branch", branch, dir]
        log_debug(cmd)
        subprocess.run(cmd)

backward_cpp_src_dir = vendor_dir / "backward-cpp"
wlroots_src_dir      = vendor_dir / "wlroots"

def update_git_deps():
    git_fetch(backward_cpp_src_dir, "https://github.com/bombela/backward-cpp.git",        "master")
    git_fetch(wlroots_src_dir,      "https://gitlab.freedesktop.org/wlroots/wlroots.git", "master")

update_git_deps()

# -----------------------------------------------------------------------------

def build_wlroots():
    version = "0.20"

    build_dir   = vendor_dir.absolute() / "wlroots-build"
    install_dir = vendor_dir.absolute() / "wlroots-install"

    cmd = ["meson", "setup", "--reconfigure", "--default-library", "static", "--prefix", install_dir, build_dir]
    log_debug(cmd)
    subprocess.run(cmd, cwd=wlroots_src_dir)

    cmd = ["meson", "compile", "-C", build_dir]
    log_debug(cmd)
    subprocess.run(cmd)

    cmd = ["meson", "install", "-q", "-C", build_dir]
    log_debug(cmd)
    subprocess.run(cmd)

    with open(install_dir / "CMakeLists.txt", "w") as cmakelists:
        cmakelists.write("add_library(               wlroots INTERFACE)\n")

        cmd = ["pkg-config", "--static", "--libs", f"wlroots-{version}"]
        env = os.environ.copy()
        env["PKG_CONFIG_PATH"] = install_dir / "lib/pkgconfig"
        res = subprocess.run(cmd, capture_output=True, text=True, env=env)

        cmakelists.write(f"target_include_directories(wlroots INTERFACE include/wlroots-{version})\n")
        cmakelists.write(f"target_link_options(       wlroots INTERFACE {res.stdout.strip()})\n")
        cmakelists.write( "target_compile_definitions(wlroots INTERFACE -DWLR_USE_UNSTABLE)")

build_wlroots()
