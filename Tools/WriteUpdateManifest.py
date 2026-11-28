"""Writes update.json, the file the editor's update check reads, for one release.

CI runs it for each release, next to the setup program and the zip it describes:

    python Tools/WriteUpdateManifest.py --version 0.7.0 --installer dist/MageCap-Bellows-0.7.0-Windows-Setup.exe
        --portable dist/MageCap-Bellows-0.7.0-Windows.zip --notes docs/ReleaseNotes/0.7.0.md --output dist/update.json

The file addresses are relative (download/v0.7.0/<file>): the update server serves update.json at
/bellows/update.json and each release file at /bellows/download/<tag>/<file>, so the editor resolves
them against the address it fetched update.json from. The fields are checked by
UpdateLogic::ParseManifest in BellowsEditor/Source/UpdateChecker.cpp.

--self-test checks this script on made-up files and prints SELF-TEST PASSED.
"""

import argparse
import datetime
import hashlib
import json
import pathlib
import re
import sys
import tempfile

VERSION_PATTERN = re.compile(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?$")


def describe(path: pathlib.Path, tag: str) -> dict:
    """The url, size and SHA-256 of one release file."""
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return {"url": f"download/{tag}/{path.name}", "size": path.stat().st_size, "sha256": digest.hexdigest()}


def build_manifest(version: str, installer: pathlib.Path, portable: pathlib.Path | None, notes: str,
                   published: str, release_url: str) -> dict:
    if not VERSION_PATTERN.match(version):
        raise ValueError(f"'{version}' isn't a version like 0.7.0")
    tag = f"v{version}"
    manifest = {
        "name": "MageCap Bellows",
        "version": version,
        "published": published,
        "notes": notes,
        "installer": describe(installer, tag),
    }
    if release_url:
        manifest["release_url"] = release_url
    if portable is not None:
        manifest["portable"] = describe(portable, tag)
    return manifest


def self_test() -> int:
    with tempfile.TemporaryDirectory() as folder:
        root = pathlib.Path(folder)
        setup = root / "MageCap-Bellows-1.2.3-Windows-Setup.exe"
        setup.write_bytes(b"setup")
        archive = root / "MageCap-Bellows-1.2.3-Windows.zip"
        archive.write_bytes(b"zip!")
        manifest = build_manifest("1.2.3", setup, archive, "# 1.2.3\n- New", "2026-12-04", "")
        assert manifest["version"] == "1.2.3"
        assert manifest["installer"] == {
            "url": "download/v1.2.3/MageCap-Bellows-1.2.3-Windows-Setup.exe",
            "size": 5,
            "sha256": hashlib.sha256(b"setup").hexdigest(),
        }
        assert manifest["portable"]["url"] == "download/v1.2.3/MageCap-Bellows-1.2.3-Windows.zip"
        assert "release_url" not in manifest
        assert build_manifest("1.2.3", setup, None, "", "2026-12-04", "https://x.com")["release_url"] == "https://x.com"
        assert "portable" not in build_manifest("1.2.3", setup, None, "", "2026-12-04", "")
        for bad in ("1.2", "v1.2.3", "1.2.3-", "soon"):
            try:
                build_manifest(bad, setup, None, "", "2026-12-04", "")
            except ValueError:
                continue
            raise AssertionError(f"'{bad}' was taken as a version")
        output = root / "update.json"
        assert main(["--version", "1.2.3", "--installer", str(setup), "--notes-text", "Hi", "--output", str(output)]) == 0
        written = json.loads(output.read_text(encoding="utf-8"))
        assert written["notes"] == "Hi" and written["installer"]["size"] == 5
    print("SELF-TEST PASSED")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Write update.json for a MageCap Bellows release.")
    parser.add_argument("--self-test", action="store_true", help="Check this script and exit")
    parser.add_argument("--version", help="The release's version, e.g. 0.7.0")
    parser.add_argument("--installer", type=pathlib.Path, help="The setup program")
    parser.add_argument("--portable", type=pathlib.Path, help="The zip, for copies that weren't installed")
    parser.add_argument("--notes", type=pathlib.Path, help="The release notes (Markdown)")
    parser.add_argument("--notes-text", help="The release notes as text, instead of --notes")
    parser.add_argument("--release-url", default="", help="An https:// page about the release")
    parser.add_argument("--output", type=pathlib.Path, help="Where update.json is written")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.version or not args.installer or not args.output:
        parser.error("--version, --installer and --output are needed")
    for path in (args.installer, args.portable):
        if path is not None and not path.is_file():
            parser.error(f"{path} doesn't exist")
    notes = args.notes.read_text(encoding="utf-8") if args.notes else (args.notes_text or "")
    published = datetime.datetime.now(datetime.timezone.utc).date().isoformat()
    try:
        manifest = build_manifest(args.version, args.installer, args.portable, notes, published, args.release_url)
    except ValueError as error:
        parser.error(str(error))
    args.output.write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    print(f"Wrote {args.output} for {args.version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
