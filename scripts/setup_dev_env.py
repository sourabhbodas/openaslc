#!/usr/bin/env python3
"""
OpenASLC Local Development Environment Setup

Installs the toolchain required to build OpenASLC (CMake, a C++20 compiler,
git) if it isn't already on PATH, then runs a one-time configure/build/test
cycle to confirm the environment works. Use build_and_test.py for the
day-to-day configure/build/test loop afterwards.
"""

import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def find(name):
    return shutil.which(name)


def has_cpp_compiler():
    return any(find(compiler) for compiler in ("cl", "g++", "clang++"))


def run(cmd):
    print(f"--> Running: {' '.join(cmd)}")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print(f"Error: command failed with exit code {result.returncode}")
        sys.exit(result.returncode)


def install_windows(dry_run):
    winget = find("winget")
    if not winget:
        print("winget not found. Install manually: CMake, a C++ compiler (e.g. MinGW or "
              "Visual Studio Build Tools), and git.")
        return False

    packages = []
    if not find("cmake"):
        packages.append("Kitware.CMake")
    if not has_cpp_compiler():
        packages.append("BrechtSanders.WinLibs.POSIX.UCRT")  # g++/gcc, UCRT runtime, POSIX threads
    if not find("git"):
        packages.append("Git.Git")

    if not packages:
        print("All required tools already present.")
        return False

    for pkg in packages:
        cmd = [winget, "install", "--id", pkg, "-e",
               "--accept-source-agreements", "--accept-package-agreements"]
        if dry_run:
            print(f"[dry-run] would run: {' '.join(cmd)}")
        else:
            run(cmd)

    if not dry_run:
        print("\nNOTE: restart your shell so PATH updates from winget take effect.")
    return True


def install_linux(dry_run):
    apt = find("apt-get")
    if not apt:
        print("No apt-get found. Install cmake, a C++ compiler (build-essential), "
              "and git manually for your distro.")
        return False

    missing = []
    if not find("cmake"):
        missing.append("cmake")
    if not has_cpp_compiler():
        missing.append("build-essential")
    if not find("git"):
        missing.append("git")

    if not missing:
        print("All required tools already present.")
        return False

    if dry_run:
        print(f"[dry-run] would run: sudo apt-get update && sudo apt-get install -y {' '.join(missing)}")
    else:
        run(["sudo", apt, "update"])
        run(["sudo", apt, "install", "-y", *missing])
    return True


def install_macos(dry_run):
    brew = find("brew")
    if not brew:
        print("Homebrew not found. Install it from https://brew.sh, or install "
              "cmake and git manually.")
        return False

    missing = []
    if not find("cmake"):
        missing.append("cmake")
    if not find("git"):
        missing.append("git")
    if not has_cpp_compiler():
        print("No C++ compiler found. Run 'xcode-select --install' for the Xcode "
              "command line tools.")

    if not missing:
        print("All required tools already present.")
        return False

    cmd = [brew, "install", *missing]
    if dry_run:
        print(f"[dry-run] would run: {' '.join(cmd)}")
    else:
        run(cmd)
    return True


def verify_build(project_root, build_dir):
    cmake = find("cmake")
    if not cmake:
        print("\ncmake still not on PATH - restart your shell and re-run this script.")
        sys.exit(1)

    print("\n--- [1/3] Configuring CMake Project ---")
    run([cmake, "-B", str(build_dir), "-S", str(project_root)])

    print("\n--- [2/3] Building OpenASLC Targets ---")
    run([cmake, "--build", str(build_dir)])

    ctest = find("ctest")
    print("\n--- [3/3] Running Unit Test Suite ---")
    if ctest:
        run([ctest, "--test-dir", str(build_dir), "--output-on-failure"])
    else:
        print("Warning: ctest not found on PATH - skipping test run.")


def main():
    parser = argparse.ArgumentParser(description="Set up a local OpenASLC development environment.")
    parser.add_argument("--dry-run", action="store_true",
                         help="Print what would be installed without installing anything.")
    parser.add_argument("--skip-build", action="store_true",
                         help="Only install/check the toolchain, skip the verification build.")
    parser.add_argument("--build-dir", default="build",
                         help="Build directory used for the verification build (default: build)")
    args = parser.parse_args()

    system = platform.system()
    print(f"Detected platform: {system}")

    if system == "Windows":
        install_windows(args.dry_run)
    elif system == "Linux":
        install_linux(args.dry_run)
    elif system == "Darwin":
        install_macos(args.dry_run)
    else:
        print(f"Unsupported platform '{system}'. Install cmake, a C++20 compiler, "
              "and git manually.")

    if args.dry_run:
        print("\nDry run complete - no packages were installed.")
        return

    if args.skip_build:
        return

    project_root = Path(__file__).parent.resolve()
    build_dir = project_root / args.build_dir
    verify_build(project_root, build_dir)
    print("\nDev environment ready. Use build_and_test.py for subsequent configure/build/test cycles.")


if __name__ == "__main__":
    main()
