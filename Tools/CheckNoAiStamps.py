"""Checks that nothing we push or share carries an AI label, an AI tool's notice or a content-credentials
stamp (Sync rule 16, from Ron, Nov 25, 2026).

Checks every file git tracks (outside vendor/), and with --commits, a range of commits: their messages, and
the names and email addresses of their authors and committers:

    python Tools/CheckNoAiStamps.py
    python Tools/CheckNoAiStamps.py --commits origin/main..HEAD
    python Tools/CheckNoAiStamps.py --self-test

- Text files: an AI-made label, such as a generated-with or generated-by footer naming Claude, a link to
  Claude's coding tool, or a co-author or session trailer naming Claude.
- PNG files: a caBX chunk (C2PA content credentials), or a text chunk that names C2PA.
- JPEG files: an APP11 segment holding a JUMBF box (where C2PA keeps its manifest).
- Commits: an author or committer named for an AI tool or its maker, or with an address at an AI company
  (Ron's board review 2, Nov 28, 2026). A finding names the commit and the name, never the email address.

Prints each problem and exits 1 when there is one; prints "NO AI STAMPS FOUND" and exits 0 otherwise.
"""

import argparse
import re
import struct
import subprocess
import sys
from pathlib import Path

# Spelled in pieces, so this file doesn't match itself.
_CLAUDE = "Cl" + "aude"
TEXT_PATTERNS = [
    re.compile(r"generated\s+(with|by)\s+\[?" + _CLAUDE, re.IGNORECASE),
    re.compile(r"\b" + _CLAUDE.lower() + r"\.ai/code\b", re.IGNORECASE),
    re.compile(r"co-authored-by:\s*" + _CLAUDE, re.IGNORECASE),
    re.compile(_CLAUDE + r"-session:", re.IGNORECASE),
    re.compile(r"(made|created|generated)\s+(with|by)\s+(an\s+)?A" + r"I\b", re.IGNORECASE),
    re.compile(r"\bA" + r"I-generated\b", re.IGNORECASE),
]
# Who may not appear as a commit's author or committer: names of AI tools and their makers, matched as
# whole words in a name, and their companies' domains in an address.
IDENTITY_NAME_PATTERN = re.compile(
    r"\b(" + _CLAUDE.lower() + r"|anthropic|openai|chatgpt|gpt-\d\w*|copilot|gemini|codex|cursor\s*agent|devin)\b(\[bot\])?",
    re.IGNORECASE)
IDENTITY_EMAIL_PATTERN = re.compile(r"@([\w-]+\.)*(anthropic\.com|openai\.com|" + _CLAUDE.lower() + r"\.ai)\b", re.IGNORECASE)
IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg"}
SKIPPED_DIRECTORIES = ("vendor/",)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def text_problems(text):
    """The labels found in text, one per pattern at most."""
    return [match.group(0) for pattern in TEXT_PATTERNS if (match := pattern.search(text))]


def png_problems(data):
    if not data.startswith(PNG_SIGNATURE):
        return []
    problems = []
    position = len(PNG_SIGNATURE)
    while position + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        if kind == b"caBX":
            problems.append("a caBX chunk (C2PA content credentials)")
        elif kind in (b"tEXt", b"iTXt", b"zTXt") and b"c2pa" in body.lower():
            problems.append(f"a {kind.decode()} chunk that names C2PA")
        if kind == b"IEND":
            break
        position += 12 + length
    return problems


def jpeg_problems(data):
    if not data.startswith(b"\xff\xd8"):
        return []
    problems = []
    position = 2
    while position + 4 <= len(data) and data[position] == 0xFF:
        marker = data[position + 1]
        if marker == 0xDA:  # the image data starts: no more metadata segments
            break
        length = struct.unpack(">H", data[position + 2:position + 4])[0]
        body = data[position + 4:position + 2 + length]
        if marker == 0xEB and (b"jumb" in body or b"c2pa" in body.lower()):
            problems.append("an APP11 segment with a JUMBF box (C2PA content credentials)")
        position += 2 + length
    return problems


def file_problems(path):
    data = path.read_bytes()
    suffix = path.suffix.lower()
    if suffix == ".png":
        return png_problems(data)
    if suffix in (".jpg", ".jpeg"):
        return jpeg_problems(data)
    if b"\0" in data[:8192]:
        return []  # another binary file: nothing readable to check
    return text_problems(data.decode("utf-8", errors="replace"))


def tracked_files(root):
    output = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True, check=True).stdout
    for name in output.decode("utf-8").split("\0"):
        if name and not name.startswith(SKIPPED_DIRECTORIES):
            path = root / name
            if path.is_file():
                yield name, path


def identity_problems(role, name, email):
    """What's wrong with a commit's author or committer ('role'), without repeating the address."""
    problems = []
    if IDENTITY_NAME_PATTERN.search(name):
        problems.append(f"the {role} is named \"{name}\"")
    if IDENTITY_EMAIL_PATTERN.search(email):
        problems.append(f"the {role}'s email address is at an AI company")
    return problems


def commit_problems(root, commits):
    output = subprocess.run(["git", "log", "--format=%H%x00%an%x00%ae%x00%cn%x00%ce%x00%B%x01", commits], cwd=root,
                            capture_output=True, check=True, text=True).stdout
    for entry in output.split("\x01"):
        if entry.count("\0") < 5:
            continue
        sha, author, author_email, committer, committer_email, message = entry.strip().split("\0", 5)
        for problem in identity_problems("author", author, author_email) + identity_problems("committer", committer, committer_email):
            yield sha[:7], problem
        for problem in text_problems(message):
            yield sha[:7], problem


def png(*chunks):
    """A PNG made of the given (type, data) chunks, for the self-test. CRCs aren't checked here."""
    body = b"".join(struct.pack(">I4s", len(data), kind) + data + b"\0\0\0\0" for kind, data in chunks)
    return PNG_SIGNATURE + body


def self_test():
    """Checks the checker on made-up files, so a broken pattern can't pass the repository silently."""
    footer = "Generated with [" + _CLAUDE + " Code](https://example.com)"
    cases = [
        ("a generated-with footer", bool(text_problems(footer)), True),
        ("a generated-by footer", bool(text_problems("_Generated by [" + _CLAUDE + " Code](https://example.com)_")), True),
        ("a bare tool link", bool(text_problems("See https://" + _CLAUDE.lower() + ".ai/code for more")), True),
        ("a co-author trailer", bool(text_problems("Co-Authored-By: " + _CLAUDE + " <x@example.com>")), True),
        ("a session trailer", bool(text_problems(_CLAUDE + "-Session: https://example.com")), True),
        ("a made-with-AI note", bool(text_problems("This page was made with A" + "I.")), True),
        ("a generated-by-AI note", bool(text_problems("An A" + "I-generated picture")), True),
        ("ordinary text", bool(text_problems("Co-Authored-By: Ron\nMade with ForgeScript\nsaid " + _CLAUDE)), False),
        ("a PNG with caBX", bool(png_problems(png((b"IHDR", b"x" * 13), (b"caBX", b"jumb"), (b"IEND", b"")))), True),
        ("a PNG naming C2PA", bool(png_problems(png((b"IHDR", b"x" * 13), (b"iTXt", b"c2pa manifest"), (b"IEND", b"")))), True),
        ("a plain PNG", bool(png_problems(png((b"IHDR", b"x" * 13), (b"IDAT", b"c2pa"), (b"IEND", b"")))), False),
        ("a JPEG with JUMBF", bool(jpeg_problems(b"\xff\xd8\xff\xeb\x00\x08jumbc2\xff\xda")), True),
        ("a plain JPEG", bool(jpeg_problems(b"\xff\xd8\xff\xe0\x00\x06JFIF\xff\xdac2pa")), False),
        ("an AI-named committer", bool(identity_problems("committer", _CLAUDE, "someone@example.com")), True),
        ("an AI bot author", bool(identity_problems("author", "copilot[bot]", "1+Copilot@users.noreply.github.com")), True),
        ("an AI company address", bool(identity_problems("author", "Ron", "noreply@anthropic.com")), True),
        ("an AI company subdomain", bool(identity_problems("author", "Ron", "x@mail.openai.com")), True),
        ("Ron's identity", bool(identity_problems("author", "Ron", "theviktan@users.noreply.github.com")), False),
        ("a name that only contains a tool's letters", bool(identity_problems("author", "Claudette Geminiani", "c@example.com")), False),
        ("the finding hides the address", "anthropic.com" in " ".join(identity_problems("author", "Ron", "noreply@anthropic.com")), False),
    ]
    failed = [name for name, found, expected in cases if found != expected]
    for name in failed:
        print(f"SELF-TEST FAILED: {name}")
    if failed:
        return 1
    print(f"SELF-TEST PASSED ({len(cases)} cases)")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--commits", help="also check these commits' messages, authors and committers, such as origin/main..HEAD")
    parser.add_argument("--self-test", action="store_true", help="check the checker itself on made-up files")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parent.parent), help="the repository")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    root = Path(args.root)

    problems = []
    for name, path in tracked_files(root):
        problems += [f"{name}: {problem}" for problem in file_problems(path)]
    if args.commits:
        problems += [f"commit {sha}: {problem}" for sha, problem in commit_problems(root, args.commits)]

    for problem in problems:
        print(f"AI STAMP: {problem}")
    if problems:
        print(f"{len(problems)} AI stamp(s) found. Strip them before pushing or sharing (AGENTS.md rule 7, Sync rule 16).")
        return 1
    print("NO AI STAMPS FOUND")
    return 0


if __name__ == "__main__":
    sys.exit(main())
