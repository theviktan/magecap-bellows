"""Editor automation API for MageCap Bellows.

Available in the editor's Python console, in ``--run-script`` automation scripts and through
the agent server (``Tools/bellows_cli.py``). It is not available inside exported games.

Typical agent workflow::

    import bellows
    from bellows import editor

    editor.new_scene("Level1")
    ground = bellows.create_entity("Ground")
    ground.add_mesh_renderer("Builtin/Plane")
    editor.save_scene("Scenes/Level1.bscene")
    editor.play()
    editor.run_frames(120)          # let the game run for two seconds
    editor.screenshot("/tmp/level1.png")
    editor.stop()

Every editor command records one undo step automatically.
"""

import json
import os
import pathlib
import urllib.request

try:
    from _bellows.editor import *  # noqa: F401,F403
    from _bellows import editor as _native
except ImportError as error:  # pragma: no cover - only outside the editor
    raise ImportError("bellows.editor is only available inside MageCap Bellows Editor") from error

POLYHAVEN_API = "https://api.polyhaven.com"


def download_polyhaven_hdri(asset_id: str, resolution: str = "2k", folder: str = "Environments") -> str:
    """Downloads an HDRI from Poly Haven (CC0) into the project and returns its asset path.

    ``asset_id`` is the name in the Poly Haven URL, e.g. ``"kloofendal_48d_partly_cloudy_puresky"``
    for https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky. ``resolution`` is one of
    ``"1k"``, ``"2k"``, ``"4k"`` or ``"8k"``. Assign the result to a SkyLight::

        sky = bellows.find_entity("Sky")
        sky.sky_light.environment = editor.download_polyhaven_hdri("kloofendal_48d_partly_cloudy_puresky")
    """
    request = urllib.request.Request(f"{POLYHAVEN_API}/files/{asset_id}", headers={"User-Agent": "MageCapBellows"})
    with urllib.request.urlopen(request, timeout=30) as response:
        files = json.load(response)
    try:
        url = files["hdri"][resolution]["hdr"]["url"]
    except KeyError as error:
        raise ValueError(f"Poly Haven has no {resolution} .hdr for '{asset_id}'") from error

    target_dir = pathlib.Path(_native.asset_directory()) / folder
    target_dir.mkdir(parents=True, exist_ok=True)
    target = target_dir / f"{asset_id}_{resolution}.hdr"
    if not target.exists():
        temporary = target.with_suffix(".hdr.tmp")
        download = urllib.request.Request(url, headers={"User-Agent": "MageCapBellows"})
        with urllib.request.urlopen(download, timeout=120) as response, open(temporary, "wb") as output:
            while chunk := response.read(1 << 20):
                output.write(chunk)
        os.replace(temporary, target)
    asset_path = f"{folder}/{target.name}" if folder else target.name
    _native.reload_asset(asset_path)
    return asset_path


def wait_seconds(seconds: float) -> None:
    """Runs the editor until at least ``seconds`` of game time have passed (play mode) or frames rendered."""
    import _bellows
    start = _bellows.time() if _native.state() != "edit" else None
    if start is None:
        run_frames(max(1, int(seconds * 60.0)))
        return
    while _bellows.time() - start < seconds and _native.state() != "edit":
        run_frames(1)
