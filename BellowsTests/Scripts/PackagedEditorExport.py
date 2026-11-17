"""Exports the open project from a packaged editor, for BellowsTests/Scripts/PackageTest.py.

Exporting copies the runtime, shaders, the bellows package and the bundled Python from next to the
editor, so it fails when the package lacks any of them. The exported program's path is written to
the file named by BELLOWS_EXPORT_RESULT, so the package test can start the game.
"""

import os
import pathlib

from bellows import editor

output = pathlib.Path(os.environ["BELLOWS_EXPORT_FOLDER"])
result = editor.export_game(str(output))
assert result["success"], "the export failed:\n" + "\n".join(result["messages"])
pathlib.Path(os.environ["BELLOWS_EXPORT_RESULT"]).write_text(result["executable"], encoding="utf-8")
print("Exported", result["executable"])
