"""Plays and exports the open project from a packaged editor, for BellowsTests/Scripts/PackageTest.py.

Play in a game window starts the packaged BellowsRuntime next to the editor. Exporting copies the
runtime, shaders, the bellows package and the bundled Python from next to the editor, so it fails
when the package lacks any of them. The exported program's path is written to the file named by
BELLOWS_EXPORT_RESULT, so the package test can start the game.
"""

import os
import pathlib
import time

from bellows import editor

assert editor.play_in_window(headless=True, frames=30), "the packaged editor could not start a game window"
deadline = time.monotonic() + 300
while editor.is_game_running() and time.monotonic() < deadline:
    editor.run_frames(5)
editor.run_frames(1)
assert editor.game_exit_code() == 0, f"the game window from the packaged editor failed ({editor.game_exit_code()})"

output = pathlib.Path(os.environ["BELLOWS_EXPORT_FOLDER"])
result = editor.export_game(str(output))
assert result["success"], "the export failed:\n" + "\n".join(result["messages"])
pathlib.Path(os.environ["BELLOWS_EXPORT_RESULT"]).write_text(result["executable"], encoding="utf-8")
print("Exported", result["executable"])
