
import os
import argparse
import sys
from pathlib import Path
import subprocess
import json
import glob
import asyncio
import logging
import multiprocessing
import struct

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
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
# -----------------------------------------------------------------------------
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
