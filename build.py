#!/bin/python

import os
import shutil
from pathlib import Path
import subprocess
import argparse
import filecmp

from scripts.utils import *

parser = argparse.ArgumentParser()
parser.add_argument("-U", "--update",    action="store_true", help="Update")
parser.add_argument("-C", "--configure", action="store_true", help="Force configure")
parser.add_argument("-B", "--build",     action="store_true", help="Build")
parser.add_argument("-R", "--release",   action="store_true", help="Release")
parser.add_argument("-I", "--install",   action="store_true", help="Install")
parser.add_argument("--asan", action="store_true", help="Enable Address Sanitizer")
args = parser.parse_args()

# -----------------------------------------------------------------------------

program_name = "zen"

# -----------------------------------------------------------------------------

cwd = Path(".")
user_dir    = Path(os.path.expanduser("~"))
current_dir = Path(os.curdir).absolute()
build_dir   = ensure_dir(".build")
vendor_dir  = ensure_dir(build_dir / "3rdparty")

# -----------------------------------------------------------------------------

def check_process(res: subprocess.CompletedProcess[bytes]) -> subprocess.CompletedProcess[bytes]:
    if res.returncode != 0:
        raise RuntimeError(f"cmd failed with code: {res.returncode}")

    return res

def run(cmd, cwd: Path|None = None):
    if cwd:
        print(f"{cmd} @ {cwd}")
    else:
        print(f"{cmd}")

    return check_process(subprocess.run(cmd, cwd=cwd))

# -----------------------------------------------------------------------------

import scripts.deps as deps
dep_dirs = deps.fetch_deps(vendor_dir, cwd / "build.json", args.update)

# -----------------------------------------------------------------------------

def build_luajit():
    source_dir = dep_dirs["luajit"]

    if not (source_dir / "src/libluajit.a").exists() or args.update:
        run(["make", "-j"], cwd = source_dir)

build_luajit()

# -----------------------------------------------------------------------------

wlroots_src_dir = dep_dirs["wlroots"]

def build_wlroots():
    version = "0.21"

    build_dir   = vendor_dir.absolute() / "wlroots-build"
    install_dir = vendor_dir.absolute() / "wlroots-install"

    if not build_dir.exists() or not install_dir.exists() or args.update:
        cmd = ["meson", "setup", "--reconfigure", "--default-library", "static", "--prefix", install_dir, build_dir]
        run(cmd, cwd=wlroots_src_dir)

        run(["meson", "compile", "-C", build_dir])

        run(["meson", "install", "-q", "-C", build_dir])

        with open(install_dir / "CMakeLists.txt", "w") as cmakelists:
            cmakelists.write("add_library(               wlroots INTERFACE)\n")

            cmd = ["pkg-config", "--static", "--libs", f"wlroots-{version}"]
            env = os.environ.copy()
            env["PKG_CONFIG_PATH"] = str(install_dir / "lib/pkgconfig")
            res = check_process(subprocess.run(cmd, capture_output=True, text=True, env=env))

            cmakelists.write(f"target_include_directories(wlroots INTERFACE include/wlroots-{version})\n")
            cmakelists.write(f"target_link_options(       wlroots INTERFACE {res.stdout.strip()})\n")
            cmakelists.write( "target_compile_definitions(wlroots INTERFACE -DWLR_USE_UNSTABLE)")

build_wlroots()

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

    wlroots_protocol_dir = wlroots_src_dir / "protocol"
    for name in os.listdir(wlroots_protocol_dir):
        if Path(name).suffix == ".xml":
            wayland_protocols += [(wlroots_protocol_dir.absolute() / name, Path(name).stem)]

    return wayland_protocols

def generate_wayland_protocols():

    wayland_scanner = "wayland-scanner"                   # Wayland scanner executable
    wayland_dir = vendor_dir / "wayland"                  #
    wayland_src = ensure_dir(wayland_dir / "src")         # Directory for generate sources
    wayland_include = ensure_dir(wayland_dir / "include") # Directory for generate headers

    cmake_target_name = "wayland-header"
    cmake_file = wayland_dir / "CMakeLists.txt"

    if cmake_file.exists() and not args.update:
        return

    with open(cmake_file, "w") as cmakelists:
        cmakelists.write(f"add_library({cmake_target_name}\n")

        for xml_path, name in list_wayland_protocols():

            # Generate header
            header_name = f"{name}-protocol.h"
            header_path = wayland_include / header_name
            if not header_path.exists():
                print(f"Generating wayland header: {header_name}")
                run([wayland_scanner, "server-header", xml_path, header_name], cwd = wayland_include)

            # Generate source
            source_name = f"{name}-protocol.c"
            source_path = wayland_src / source_name
            if not source_path.exists():
                print(f"Generating wayland source: {source_name}")
                run([wayland_scanner, "private-code", xml_path, source_name], cwd = wayland_src)

            # Add source to CMakeLists
            cmakelists.write(f"    \"src/{source_name}\"\n")

        cmakelists.write("    )\n")
        cmakelists.write(f"target_include_directories({cmake_target_name} PUBLIC include)\n")

generate_wayland_protocols()

# -----------------------------------------------------------------------------

build_type   = "Debug" if not args.release else "Release"
c_compiler   = "clang"
cxx_compiler = "clang++"
linker_type  = "MOLD"

build_name = build_type.lower()
if args.asan:
    build_name += "-asan"
cmake_dir = build_dir / build_name

if ((args.build or args.install) and not cmake_dir.exists()) or args.configure:
    cmd  = ["cmake", "-B", cmake_dir, "--fresh", "-G", "Ninja", f"-DVENDOR_DIR={vendor_dir}", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    cmd += [f"-DCMAKE_C_COMPILER={c_compiler}", f"-DCMAKE_CXX_COMPILER={cxx_compiler}", f"-DCMAKE_LINKER_TYPE={linker_type}"]
    cmd += [f"-DCMAKE_BUILD_TYPE={build_type}"]
    cmd += [f"-DPROJECT_NAME={program_name}"]
    if args.asan:
        cmd += ["-DUSE_ASAN=1"]

    run(cmd)

if (args.build or args.install):
    run(["cmake", "--build", cmake_dir])

# -----------------------------------------------------------------------------

def install_file(file: Path, target: Path):
    if target.exists():
        if filecmp.cmp(file, target):
            return
        os.remove(target)
    print(f"Installing [{file}] to [{target}]")
    shutil.copy2(file, target)

if args.install:
    local_bin_dir  = ensure_dir(user_dir / ".local/bin")
    xdg_portal_dir = ensure_dir(user_dir / ".config/xdg-desktop-portal")

    install_file(cmake_dir / program_name, local_bin_dir / program_name)
    install_file(current_dir / "resources/portals.conf", xdg_portal_dir / f"{program_name}-portals.conf")
