#!/usr/bin/env python3
"""
OpenASLC Build & Test Automation Script
"""

import sys
import subprocess
import shutil
import argparse
from pathlib import Path

def find_executable(name):
    path = shutil.which(name)
    if path:
        return path
    
    # Common Windows search locations
    if sys.platform == "win32":
        common_paths = [
            Path("C:/Program Files/CMake/bin/cmake.exe"),
            Path("C:/Program Files (x86)/CMake/bin/cmake.exe"),
        ]
        for p in common_paths:
            if p.exists():
                return str(p)
    return None

def run_command(cmd, cwd=None):
    print(f"--> Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        print(f"Error: Command failed with exit code {result.returncode}")
        sys.exit(result.returncode)

def main():
    parser = argparse.ArgumentParser(description="Build and run OpenASLC tests.")
    parser.add_argument("--build-dir", default="build", help="Build directory (default: build)")
    parser.add_argument("--clean", action="store_true", help="Clean build directory before configuring")
    parser.add_argument("--run-app", action="store_true", help="Run openaslc_app executable after build")
    args = parser.parse_args()

    project_root = Path(__file__).parent.resolve()
    build_path = project_root / args.build_dir

    cmake_bin = find_executable("cmake")
    ctest_bin = find_executable("ctest")

    if not cmake_bin:
        print("Error: 'cmake' executable not found in PATH or standard installation locations.")
        print("Please ensure CMake is installed.")
        sys.exit(1)

    if args.clean and build_path.exists():
        print(f"Cleaning build directory: {build_path}")
        shutil.rmtree(build_path)

    # 1. Configure CMake
    print("\n--- [1/3] Configuring CMake Project ---")
    run_command([cmake_bin, "-B", str(build_path), "-S", str(project_root)])

    # 2. Build target
    print("\n--- [2/3] Building OpenASLC Targets ---")
    run_command([cmake_bin, "--build", str(build_path)])

    # 3. Run Tests via CTest
    print("\n--- [3/3] Running Unit Test Suite ---")
    if ctest_bin:
        run_command([ctest_bin, "--test-dir", str(build_path), "--output-on-failure"])
    else:
        # Fallback to direct executable call if ctest isn't explicitly found
        test_exe = build_path / "openaslc_tests"
        if sys.platform == "win32":
            test_exe_win = build_path / "Debug" / "openaslc_tests.exe"
            if test_exe_win.exists():
                test_exe = test_exe_win

        if test_exe.exists():
            run_command([str(test_exe)])
        else:
            print("Warning: ctest not found and test binary not located.")

    if args.run-app:
        print("\n--- Running OpenASLC Application ---")
        app_exe = build_path / "openaslc_app"
        if sys.platform == "win32":
            app_exe_win = build_path / "Debug" / "openaslc_app.exe"
            if app_exe_win.exists():
                app_exe = app_exe_win
        if app_exe.exists():
            run_command([str(app_exe)])

if __name__ == "__main__":
    main()
