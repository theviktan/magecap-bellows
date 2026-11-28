"""Checks the feature checklist in docs/Roadmap.md (section 8), so no planned feature is forgotten.

    python Tools/CheckRoadmap.py                  # every row is well formed
    python Tools/CheckRoadmap.py --release 0.7.0  # ...and every row planned for 0.7 is Done
    python Tools/CheckRoadmap.py --self-test      # checks this script on made-up tables

The table sits between <!-- checklist:start --> and <!-- checklist:end -->, with the columns
Feature | What it means | Release | Status | Notes. A release is a version (0.8, 1.0) or Later; a
status is Planned, In progress or Done. CI runs it with --release when a version is tagged, and
refuses to publish the release while any of that version's rows isn't Done. Exit code 0 when all
is well, 1 otherwise, with one line per problem.
"""

import argparse
import pathlib
import re
import sys

START = "<!-- checklist:start -->"
END = "<!-- checklist:end -->"
COLUMNS = ["Feature", "What it means", "Release", "Status", "Notes"]
STATUSES = {"Planned", "In progress", "Done"}
RELEASE_PATTERN = re.compile(r"^(\d+\.\d+|Later)$")


def cells(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def check(text: str, release: str | None) -> list[str]:
    """The problems in the checklist (and, with 'release', the rows of that version not yet Done)."""
    if text.count(START) != 1 or text.count(END) != 1 or text.index(START) > text.index(END):
        return [f"the checklist must sit between one '{START}' and one '{END}'"]
    lines = [line for line in text[text.index(START) + len(START):text.index(END)].splitlines() if line.strip()]
    if len(lines) < 2 or cells(lines[0]) != COLUMNS or not set(lines[1].replace("|", "").strip()) <= set("-: "):
        return ["the checklist's header must be: | " + " | ".join(COLUMNS) + " |, then a separator line"]

    problems = []
    features = set()
    wanted = None
    if release is not None:
        match = re.match(r"^v?(\d+)\.(\d+)(\.\d+)?(-.*)?$", release.strip())
        if not match:
            return [f"'{release}' isn't a version like 0.7.0"]
        wanted = f"{match.group(1)}.{match.group(2)}"
    for number, line in enumerate(lines[2:], 1):
        row = cells(line)
        if len(row) != len(COLUMNS):
            problems.append(f"row {number} has {len(row)} columns, not {len(COLUMNS)}: {line.strip()}")
            continue
        feature, meaning, planned, status, notes = row
        if not feature or not meaning:
            problems.append(f"row {number} needs a feature and what it means")
        if feature in features:
            problems.append(f"'{feature}' is listed twice")
        features.add(feature)
        if not RELEASE_PATTERN.match(planned):
            problems.append(f"'{feature}': the release '{planned}' isn't a version like 0.8 or Later")
        if status not in STATUSES:
            problems.append(f"'{feature}': the status '{status}' isn't one of Planned, In progress, Done")
        if status == "Done" and not notes:
            problems.append(f"'{feature}' is Done but its Notes don't name the commit")
        if wanted is not None and planned == wanted and status != "Done":
            problems.append(f"'{feature}' is planned for {wanted} and is {status}: finish it, or Ron moves it to a later release")
    return problems


def self_test() -> int:
    header = f"{START}\n| " + " | ".join(COLUMNS) + " |\n|---|---|---|---|---|\n"
    good = header + "| Fog | Haze | 0.9 | Planned | |\n| Decals | Images | 0.7 | Done | 1e0354e |\n" + END
    assert check(good, None) == []
    assert check(good, "0.7.0") == []
    assert check(good, "v0.7.3") == []
    assert len(check(good, "0.9.0")) == 1 and "Fog" in check(good, "0.9.0")[0]
    assert check(good, "0.8.0-dev") == []
    assert check(good, "seven") == ["'seven' isn't a version like 0.7.0"]
    assert check("no table here", None)
    assert check(header.replace("Notes", "Comments") + END, None)
    bad = header + "| Fog | Haze | 1.0.1 | Started | |\n| Fog | Haze | Later | Done | |\n| Short | row |\n" + END
    problems = check(bad, None)
    assert any("release '1.0.1'" in p for p in problems)
    assert any("status 'Started'" in p for p in problems)
    assert any("listed twice" in p for p in problems)
    assert any("don't name the commit" in p for p in problems)
    assert any("columns" in p for p in problems)
    print("SELF-TEST PASSED")
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Check docs/Roadmap.md's feature checklist.")
    parser.add_argument("--release", help="A version being released: its rows must all be Done")
    parser.add_argument("--roadmap", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent / "docs" / "Roadmap.md")
    parser.add_argument("--self-test", action="store_true", help="Check this script and exit")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    problems = check(args.roadmap.read_text(encoding="utf-8"), args.release)
    for problem in problems:
        print(f"{args.roadmap.name}: {problem}")
    if problems:
        return 1
    print("ROADMAP CHECKLIST OK" + (f" for {args.release}" if args.release else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
