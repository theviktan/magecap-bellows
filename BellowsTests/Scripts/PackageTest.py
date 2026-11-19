#!/usr/bin/env python3
"""Checks that a release package holds everything its programs load, by building one and running it.

The other suites run the programs from the build folder, where every file is present. A file the
programs load but Tools/PackageRelease.py does not copy would only show up as a broken download,
so this test packages the build into a scratch folder and checks it in three ways:

1. Everything the build folder ships (all but test programs, logs and build leftovers) is in the
   package, including every file under Resources, Shaders and Python.
2. The files the programs load by name (fonts, icons, the script API description, docs) are there.
3. The packaged programs work on their own: forgescript and the editor's check run, the runtime
   passes FeatureTest, and the packaged editor exports a game that starts.

Run by ctest as BellowsPackageTest with the Python the engine was built against:

    python BellowsTests/Scripts/PackageTest.py --build-dir build/linux-debug

The scratch folder is deleted only after every program started from it has exited, because
Windows refuses to delete files a running program still has open.
"""

import argparse
import fnmatch
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
EXTENSION = ".exe" if sys.platform == "win32" else ""

# Build-folder items that are not part of a release: test programs, logs and compiler leftovers.
NOT_SHIPPED = ("BellowsTests*", "ForgeScriptTests*", "ForgeScriptEngineExample*", "TestOutput", "*.log",
               "*.pdb", "*.ilk", "*.lib", "*.exp", "__pycache__")
# Folders the package mirrors from the build folder file for file.
MIRRORED_FOLDERS = ("Resources", "Shaders", "Python")
# Files the programs open by name (EditorTheme, EditorLayer, EditorMain, RuntimeMain, ScriptEditorPanel,
# forgescript --lsp) and the documentation START HERE.txt points to.
LOADED_FILES = (
    "Resources/Fonts/Inter/Inter-Medium.ttf",
    "Resources/Fonts/Inter/Inter-Regular.ttf",
    "Resources/Fonts/Inter/Inter-SemiBold.ttf",
    "Resources/Fonts/Inter/Inter-Bold.ttf",
    "Resources/Fonts/JetBrainsMono/JetBrainsMono-Regular.ttf",
    "Resources/Fonts/Lucide/lucide.ttf",
    "Resources/Icons/Bellows.png",
    "Resources/Icons/ForgeScript.png",
    "forgescript-api.json",
    "Python/bellows/__init__.py",
    "Python/bellows/editor.py",
    "Python/_bellows.pyi",
    "ThirdPartyNotices.md",
    "README.md",
    "START HERE.txt",
    "Docs/ScriptingAPI.md",
    "Docs/EditorAutomation.md",
    "Docs/ForgeScript/Reference.md",
    "Docs/ForgeScript/CHANGELOG.md",
    "Projects/FeatureTest/FeatureTest.bproject",
)


class Failures:
    """Collects every problem, so one run reports all missing files rather than the first."""

    def __init__(self):
        self.messages = []

    def check(self, condition: bool, message: str):
        if not condition:
            self.messages.append(message)
            print(f"FAIL: {message}", flush=True)


def shipped(name: str) -> bool:
    return not any(fnmatch.fnmatch(name, pattern) for pattern in NOT_SHIPPED)


def relative_files(folder: pathlib.Path) -> set:
    return {path.relative_to(folder).as_posix() for path in folder.rglob("*")
            if path.is_file() and all(shipped(part) for part in path.relative_to(folder).parts)}


def engine_version() -> str:
    match = re.search(r"project\(MageCapBellows\s+VERSION\s+([0-9.]+)", (REPOSITORY / "CMakeLists.txt").read_text())
    assert match, "could not read the engine version from CMakeLists.txt"
    return match.group(1)


def run(arguments, cwd: pathlib.Path, environment: dict, timeout: int = 600) -> subprocess.CompletedProcess:
    print("Running:", " ".join(str(argument) for argument in arguments), flush=True)
    result = subprocess.run([str(argument) for argument in arguments], cwd=cwd, env=environment, timeout=timeout,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    output = (result.stdout + result.stderr).strip()
    if output:
        print("\n".join(output.splitlines()[-15:]), flush=True)
    return result


def check_contents(build: pathlib.Path, package: pathlib.Path, failures: Failures):
    binaries = build / "bin"
    for item in sorted(binaries.iterdir()):
        if shipped(item.name) and not item.name.startswith("config-"):
            failures.check((package / item.name).exists(), f"{item.name} is in the build folder but not in the package")

    for folder in MIRRORED_FOLDERS:
        missing = relative_files(binaries / folder) - relative_files(package / folder)
        for name in sorted(missing):
            failures.check(False, f"{folder}/{name} is in the build folder but not in the package")

    for name in LOADED_FILES + (f"Docs/ReleaseNotes/{engine_version()}.md",):
        failures.check((package / name).is_file(), f"{name} is missing from the package")
    failures.check((package / "PythonRuntime").is_dir(), "the bundled Python runtime (PythonRuntime) is missing")
    failures.check(any((package / "Shaders").rglob("*.spv")), "the package has no compiled shaders")


def check_programs(package: pathlib.Path, scratch: pathlib.Path, failures: Failures):
    environment = dict(os.environ)
    # The package must run on its own: no Python from the build machine, isolated settings and user data.
    for variable in ("PYTHONHOME", "PYTHONPATH"):
        environment.pop(variable, None)
    environment["BELLOWS_CONFIG_DIR"] = str(scratch / "Config")
    environment["BELLOWS_USER_DATA_DIR"] = str(scratch / "UserData")
    scripts = package / "Projects" / "FeatureTest" / "Assets" / "Scripts"

    result = run([package / f"forgescript{EXTENSION}", "--version"], package, environment)
    failures.check(result.returncode == 0 and re.match(r"ForgeScript \d+\.\d+\.\d+", result.stdout) is not None,
                   "the packaged forgescript --version did not print its version")

    for script in ("FeatureTest.forge", "FeatureTest.py"):
        result = run([package / f"BellowsEditor{EXTENSION}", "--check-script", scripts / script], package, environment)
        failures.check(result.returncode == 0, f"the packaged editor's --check-script failed on {script}")

    result = run([package / f"BellowsRuntime{EXTENSION}", "--project", package / "Projects" / "FeatureTest",
                  "--headless", "--width", "320", "--height", "180", "--frames", "5000"],
                 package, dict(environment, BELLOWS_FEATURE_TEST="1"), timeout=900)
    failures.check(result.returncode == 0 and "FEATURE TEST PASSED" in result.stdout + result.stderr,
                   "the packaged runtime did not pass FeatureTest")

    export_folder = scratch / "ExportedGame"
    export_result = scratch / "ExportedGame.txt"
    result = run([package / f"BellowsEditor{EXTENSION}", "--headless", "--no-agent-server",
                  "--project", package / "Projects" / "FeatureTest",
                  "--run-script", REPOSITORY / "BellowsTests" / "Scripts" / "PackagedEditorExport.py", "--exit"],
                 package, dict(environment, BELLOWS_EXPORT_FOLDER=str(export_folder),
                               BELLOWS_EXPORT_RESULT=str(export_result)))
    failures.check(result.returncode == 0 and export_result.is_file(), "the packaged editor could not export a game")
    if result.returncode == 0 and export_result.is_file():
        game = pathlib.Path(export_result.read_text(encoding="utf-8"))
        for name in ("Inter-Regular.ttf", "OFL.txt"):
            failures.check((export_folder / "Resources" / "Fonts" / "Inter" / name).is_file(),
                           f"the exported game is missing the built-in font file {name}")
        result = run([game, "--headless", "--width", "320", "--height", "180", "--frames", "30"], export_folder, environment)
        failures.check(result.returncode == 0, "the game exported by the packaged editor did not run")

        # Games log to user_folder()/Logs, keeping the last run's log (the packaged FeatureTest run above
        # is the same game), and write nothing next to the program.
        logs = list((scratch / "UserData" / "MageCapBellows" / "Games").glob("*/Logs"))
        failures.check(len(logs) == 1, f"expected one game log folder in the user data, found {len(logs)}")
        if len(logs) == 1:
            game_log = logs[0] / "Game.log"
            failures.check(game_log.is_file() and "Runtime: playing" in game_log.read_text(encoding="utf-8"),
                           "Game.log is missing, or lacks the lines logged before it was opened")
            failures.check((logs[0] / "Game.previous.log").is_file(), "the last run's log was not kept as Game.previous.log")
        failures.check(not (export_folder / "Game.log").exists(), "the exported game wrote Game.log next to the program")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Package a build and check the package.")
    parser.add_argument("--build-dir", type=pathlib.Path, required=True, help="CMake build folder, e.g. build/linux-debug")
    args = parser.parse_args(argv)

    scratch = pathlib.Path(tempfile.mkdtemp(prefix="BellowsPackageTest-"))
    try:
        result = run([sys.executable, REPOSITORY / "Tools" / "PackageRelease.py", "--build-dir", args.build_dir,
                      "--output", scratch / "dist", "--no-zip"], REPOSITORY, dict(os.environ))
        if result.returncode != 0:
            print("FAIL: Tools/PackageRelease.py failed", flush=True)
            return 1
        packages = [path for path in (scratch / "dist").iterdir() if path.is_dir()]
        if len(packages) != 1:
            print(f"FAIL: expected one package folder, found {len(packages)}", flush=True)
            return 1

        failures = Failures()
        check_contents(args.build_dir.resolve(), packages[0], failures)
        check_programs(packages[0], scratch, failures)
        if failures.messages:
            print(f"PACKAGE TEST FAILED ({len(failures.messages)} problems)", flush=True)
            return 1
        print("PACKAGE TEST PASSED", flush=True)
        return 0
    finally:
        # Every program above was waited for, so nothing in the scratch folder is still open.
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
