#!/usr/bin/env python3
"""Packages a MageCap Bellows build as a ready-to-run folder and zip archive.

The package contains the editor, the runtime, the ForgeScript tool, engine data, the sample
project and a bundled Python runtime, so it runs on a computer with nothing else installed
(apart from a graphics driver with Vulkan 1.3).

Run it with the Python the engine was built against (the build embeds that version):

    python Tools/PackageRelease.py --build-dir build/windows --output dist [--msvc-redist <dir>]

--msvc-redist points at Visual Studio's C++ runtime folder (...\\Microsoft.VC143.CRT); its DLLs are
copied next to the executables so the editor starts on PCs without the Visual C++ Redistributable.
"""

import argparse
import fnmatch
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import sysconfig

REPOSITORY = pathlib.Path(__file__).resolve().parent.parent
RUNTIME_FOLDER = "PythonRuntime"    # Must match ScriptEngine::BundledRuntimeFolder

# Standard-library parts no game or editor needs (mirrors the game exporter).
EXCLUDED_PYTHON = {
    "test", "tests", "idlelib", "tkinter", "turtledemo", "ensurepip", "site-packages", "dist-packages",
    "__pycache__", "lib2to3", "pydoc_data", "venv",
}
EXCLUDED_PROJECT = {".bellows", "__pycache__", "Build"}
EXECUTABLES = ("BellowsEditor", "BellowsRuntime", "forgescript")
ENGINE_DATA = ("Shaders", "Resources", "Python", "ThirdPartyNotices.md", "forgescript-api.json")


def ignore_names(excluded, patterns=()):
    def ignore(_directory, names):
        return {n for n in names if n in excluded or n.startswith("config-") or any(fnmatch.fnmatch(n, p) for p in patterns)}
    return ignore


def engine_version() -> str:
    match = re.search(r"project\(MageCapBellows\s+VERSION\s+([0-9.]+)", (REPOSITORY / "CMakeLists.txt").read_text())
    if not match:
        raise SystemExit("Could not read the engine version from CMakeLists.txt")
    return match.group(1)


def build_commit() -> str:
    """The short commit of the checkout, as the build reports it (cmake/BellowsBuildInfo.cmake)."""
    try:
        result = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], cwd=REPOSITORY,
                                capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return "unknown"
    return result.stdout.strip() or "unknown"


def copy_python_runtime(package: pathlib.Path):
    """Copies the running Python's standard library (and shared library) into the package."""
    version = f"{sys.version_info.major}.{sys.version_info.minor}"
    stdlib = pathlib.Path(sysconfig.get_paths()["stdlib"])
    prefix = pathlib.Path(sys.base_prefix)
    home = package / RUNTIME_FOLDER
    patterns = ("*.pyc", "*.a", "*.exe")
    if sys.platform == "win32":
        shutil.copytree(stdlib, home / "Lib", ignore=ignore_names(EXCLUDED_PYTHON, patterns))
        dlls = prefix / "DLLs"
        if dlls.is_dir():
            shutil.copytree(dlls, home / "DLLs", ignore=ignore_names(EXCLUDED_PYTHON, patterns + ("tcl*", "tk*", "_tkinter*")))
        for library in list(prefix.glob("python3*.dll")) + list(prefix.glob("vcruntime*.dll")):
            shutil.copy2(library, package / library.name)
    else:
        shutil.copytree(stdlib, home / "lib" / f"python{version}", ignore=ignore_names(EXCLUDED_PYTHON, patterns))
        name = sysconfig.get_config_var("INSTSONAME") or sysconfig.get_config_var("LDLIBRARY")
        library = pathlib.Path(sysconfig.get_config_var("LIBDIR")) / name
        if not library.exists():
            raise SystemExit(f"The Python shared library {library} was not found")
        shutil.copy2(library, home / "lib" / name)
    return version


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Package a MageCap Bellows build.")
    parser.add_argument("--build-dir", type=pathlib.Path, required=True, help="CMake build folder, e.g. build/windows")
    parser.add_argument("--output", type=pathlib.Path, default=REPOSITORY / "dist", help="Where the package is written")
    parser.add_argument("--msvc-redist", type=pathlib.Path, help="Folder with the Visual C++ runtime DLLs (Windows)")
    parser.add_argument("--no-zip", action="store_true", help="Only create the folder")
    parser.add_argument("--name-with-commit", action="store_true",
                        help="Put the commit in the package name, so builds between releases can be told apart")
    args = parser.parse_args(argv)

    binaries = args.build_dir / "bin"
    extension = ".exe" if sys.platform == "win32" else ""
    if not (binaries / f"BellowsEditor{extension}").is_file():
        raise SystemExit(f"{binaries} has no BellowsEditor{extension}; build the Release configuration first")

    system = "Windows" if sys.platform == "win32" else platform.system()
    commit = build_commit()
    name = f"MageCap-Bellows-{engine_version()}-{commit}-{system}" if args.name_with_commit \
        else f"MageCap-Bellows-{engine_version()}-{system}"
    package = args.output / name
    shutil.rmtree(package, ignore_errors=True)
    package.mkdir(parents=True)

    for executable in EXECUTABLES:
        source = binaries / f"{executable}{extension}"
        if source.is_file():
            shutil.copy2(source, package / source.name)
    for library in binaries.glob("*.dll"):
        shutil.copy2(library, package / library.name)
    for item in ENGINE_DATA:
        source = binaries / item
        if source.is_dir():
            shutil.copytree(source, package / item, ignore=ignore_names({"__pycache__"}))
        elif source.is_file():
            shutil.copy2(source, package / item)
        else:
            raise SystemExit(f"{source} is missing from the build")

    if args.msvc_redist:
        dlls = list(args.msvc_redist.glob("*.dll"))
        if not dlls:
            raise SystemExit(f"No DLLs found in {args.msvc_redist}")
        for library in dlls:
            shutil.copy2(library, package / library.name)

    python_version = copy_python_runtime(package)

    shutil.copytree(REPOSITORY / "Projects" / "FeatureTest", package / "Projects" / "FeatureTest",
                    ignore=ignore_names(EXCLUDED_PROJECT))
    shutil.copytree(REPOSITORY / "docs", package / "Docs", ignore=ignore_names(set(), ("Images",)))
    # ForgeScript's version history lives with the language; the package keeps it with the other docs.
    shutil.copy2(REPOSITORY / "ForgeScript" / "CHANGELOG.md", package / "Docs" / "ForgeScript" / "CHANGELOG.md")
    shutil.copy2(REPOSITORY / "README.md", package / "README.md")

    (package / "START HERE.txt").write_text(
        f"MageCap Bellows {engine_version()} (build {commit})\n\n"
        f"1. Double-click BellowsEditor{extension} to start the editor.\n"
        "2. Click Open Project... and open Projects/FeatureTest/FeatureTest.bproject to see a sample scene,\n"
        "   or click New Project to start your own.\n"
        "3. Press Play (Ctrl+P) to run the game inside the editor.\n\n"
        "Requirements: a graphics driver with Vulkan 1.3 (any recent Intel, AMD or NVIDIA driver).\n"
        f"Python {python_version} is included; nothing else needs to be installed.\n"
        "Documentation: the Docs folder (ScriptingAPI.md, EditorAutomation.md, ForgeScript/).\n"
        f"What's new: Docs/ReleaseNotes/{engine_version()}.md; ForgeScript's changes: Docs/ForgeScript/CHANGELOG.md.\n",
        encoding="utf-8")

    if not args.no_zip:
        archive = shutil.make_archive(str(package), "zip", root_dir=args.output, base_dir=name)
        print(f"Created {archive}")
    print(f"Package folder: {package}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
