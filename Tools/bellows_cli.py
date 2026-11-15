#!/usr/bin/env python3
"""Command-line client for a running MageCap Bellows Editor (the agent server).

The editor listens on 127.0.0.1 (port 7420 by default) and writes its port and a session token to
AgentSession.json in the editor's config folder:

    Windows: %APPDATA%\\MageCapBellows\\AgentSession.json
    Linux:   ~/.config/MageCapBellows/AgentSession.json
    (or $BELLOWS_CONFIG_DIR/AgentSession.json when that variable is set, as for the editor)

Examples:

    python Tools/bellows_cli.py ping
    python Tools/bellows_cli.py exec "bellows.create_entity('Player')"
    python Tools/bellows_cli.py exec-file build_level.py
    python Tools/bellows_cli.py screenshot shot.png --width 1280 --height 720
    python Tools/bellows_cli.py window-screenshot editor.png
    python Tools/bellows_cli.py logs --since 0
    python Tools/bellows_cli.py wait 60

Each request runs on the editor's main thread between frames; Python code has the full
``bellows`` and ``bellows.editor`` API (see docs/ScriptingAPI.md). Exit code 0 = success.
"""

import argparse
import itertools
import json
import os
import pathlib
import socket
import sys

_REQUEST_IDS = itertools.count(1)


def config_directory() -> pathlib.Path:
    override = os.environ.get("BELLOWS_CONFIG_DIR")
    if override:
        return pathlib.Path(override)
    if sys.platform == "win32":
        base = os.environ.get("APPDATA")
        return pathlib.Path(base) / "MageCapBellows" if base else pathlib.Path.cwd()
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config")
    return pathlib.Path(base) / "MageCapBellows"


def load_session(path: pathlib.Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise SystemExit(f"No running editor found ({path} does not exist). Start BellowsEditor first.")
    except json.JSONDecodeError as error:
        raise SystemExit(f"The session file {path} is invalid: {error}")


class EditorClient:
    """A persistent connection to the editor's agent server."""

    def __init__(self, port: int, token: str, timeout: float = 600.0):
        self._token = token
        self._socket = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        self._reader = self._socket.makefile("r", encoding="utf-8", newline="\n")

    def close(self):
        self._reader.close()
        self._socket.close()

    def request(self, method: str, **params):
        message = {"id": next(_REQUEST_IDS), "token": self._token, "method": method, "params": params}
        self._socket.sendall((json.dumps(message) + "\n").encode("utf-8"))
        line = self._reader.readline()
        if not line:
            raise ConnectionError("The editor closed the connection")
        response = json.loads(line)
        if not response.get("ok"):
            raise RuntimeError(response.get("error", "request failed"))
        return response.get("result")

    def exec(self, code: str) -> dict:
        return self.request("exec", code=code)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Control a running MageCap Bellows Editor.")
    parser.add_argument("--session", type=pathlib.Path, help="Path to AgentSession.json (default: the editor config folder)")
    parser.add_argument("--port", type=int, help="Override the port from the session file")
    parser.add_argument("--json", action="store_true", help="Print raw JSON results")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("ping", help="Check the connection and show editor state")
    exec_parser = sub.add_parser("exec", help="Run Python code in the editor")
    exec_parser.add_argument("code")
    file_parser = sub.add_parser("exec-file", help="Run a Python file in the editor")
    file_parser.add_argument("path", type=pathlib.Path)
    logs_parser = sub.add_parser("logs", help="Print log messages")
    logs_parser.add_argument("--since", type=int, default=0, help="Only messages after this sequence number")
    shot = sub.add_parser("screenshot", help="Save a PNG of the viewport")
    shot.add_argument("path", type=pathlib.Path)
    shot.add_argument("--width", type=int, default=0)
    shot.add_argument("--height", type=int, default=0)
    window = sub.add_parser("window-screenshot", help="Save a PNG of the whole editor window")
    window.add_argument("path", type=pathlib.Path)
    wait = sub.add_parser("wait", help="Wait for a number of editor frames")
    wait.add_argument("frames", type=int)

    args = parser.parse_args(argv)
    session = load_session(args.session or config_directory() / "AgentSession.json")
    port = args.port or int(session["port"])

    try:
        client = EditorClient(port, session["token"])
    except OSError as error:
        print(f"Could not connect to the editor on port {port}: {error}", file=sys.stderr)
        return 1

    try:
        if args.command == "ping":
            result = client.request("ping")
        elif args.command == "exec":
            result = client.exec(args.code)
        elif args.command == "exec-file":
            result = client.exec(args.path.read_text(encoding="utf-8"))
        elif args.command == "logs":
            result = client.request("logs", since=args.since)
        elif args.command == "screenshot":
            path = str(args.path.resolve()).replace("\\", "/")
            result = client.exec(f"bellows.editor.screenshot({path!r}, {args.width}, {args.height})")
        elif args.command == "window-screenshot":
            path = str(args.path.resolve()).replace("\\", "/")
            result = client.exec(f"bellows.editor.capture_window({path!r})")
        else:
            result = client.request("wait_frames", frames=args.frames)
    except (RuntimeError, ConnectionError, OSError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    finally:
        client.close()

    if args.json:
        print(json.dumps(result, indent=2))
    elif isinstance(result, dict) and "output" in result:
        if result["output"]:
            print(result["output"], end="" if result["output"].endswith("\n") else "\n")
        return 0 if result.get("success") else 1
    elif args.command == "logs":
        for entry in result:
            print(f"[{entry['sequence']}] {entry['level']:>8} {entry['source']:>6}: {entry['text']}")
    elif result is not None:
        print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
