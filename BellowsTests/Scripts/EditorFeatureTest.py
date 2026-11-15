"""Plays the FeatureTest scene inside the editor and checks that the scripting API test passes.

This covers the editor's play-mode path (scene copy, play/stop) with the complete scripting API.
Run by ctest with BELLOWS_FEATURE_TEST=1, so the test script quits (stops play mode) when done.
"""

from bellows import editor

assert editor.project_name() == "Feature Test", "run with --project Projects/FeatureTest"
assert editor.open_scene("Scenes/FeatureTest.bscene")
start = editor.latest_log_sequence()

editor.play()
for _ in range(400):
    if editor.state() == "edit":
        break
    editor.run_frames(10)
else:
    editor.stop()
    raise AssertionError("the feature test did not finish in play mode")

messages = [entry["text"] for entry in editor.logs(start)]
if not any(text.startswith("FEATURE TEST PASSED") for text in messages):
    failures = [text for text in messages if "FAILED" in text or text.startswith("  - ")]
    raise AssertionError("the feature test failed in the editor:\n" + "\n".join(failures))
assert not editor.is_dirty(), "playing must not modify the edited scene"
print("Feature test passed in editor play mode")
