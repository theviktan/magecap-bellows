"""Writes the skinned test model the engine's animation tests read.

BellowsTests/Data/SkinnedColumn.glb is a 2 m column standing on the origin, skinned to two joints
("Base" at the floor and its child "Top" 1 m up), with two clips:

* "Bend" (2 s, LINEAR): Top turns about Z from 0 to 90 degrees at 1 s and back.
* "Wave" (1 s): Base turns about Y a quarter turn and back (LINEAR), Base rises 0.5 m at 0.5 s and drops
  back at 1 s (STEP), and Top grows to 2x at 0.5 s (CUBICSPLINE, flat tangents).

It is written by hand (no third-party modules) so the file is the same everywhere:
    python Tools/GenerateTestModels.py
"""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "BellowsTests" / "Data" / "SkinnedColumn.glb"

FLOAT, UNSIGNED_BYTE, UNSIGNED_SHORT = 5126, 5121, 5123
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


class Builder:
    """Collects buffer views and accessors into one binary buffer."""

    def __init__(self) -> None:
        self.data = bytearray()
        self.views: list[dict] = []
        self.accessors: list[dict] = []

    def add(self, payload: bytes, component: int, count: int, kind: str, target: int | None = None,
            minimum: list[float] | None = None, maximum: list[float] | None = None) -> int:
        while len(self.data) % 4:
            self.data.append(0)
        view = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        self.data += payload
        self.views.append(view)
        accessor = {"bufferView": len(self.views) - 1, "componentType": component, "count": count, "type": kind}
        if minimum is not None:
            accessor["min"] = minimum
            accessor["max"] = maximum
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def floats(self, values: list[float], kind: str, target: int | None = None, bounds: bool = False) -> int:
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": 16}[kind]
        count = len(values) // width
        minimum = maximum = None
        if bounds:
            minimum = [min(values[i::width]) for i in range(width)]
            maximum = [max(values[i::width]) for i in range(width)]
        return self.add(struct.pack(f"<{len(values)}f", *values), FLOAT, count, kind, target, minimum, maximum)


def quat_about(axis: tuple[float, float, float], degrees: float) -> list[float]:
    half = math.radians(degrees) / 2.0
    s = math.sin(half)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half)]


def column() -> tuple[list[float], list[float], list[int], list[float], list[int]]:
    """A square column 0.2 m wide and 2 m tall in 9 rings, with smooth weights around the knee at 1 m."""
    positions: list[float] = []
    normals: list[float] = []
    joints: list[int] = []
    weights: list[float] = []
    corners = [(-0.1, -0.1), (0.1, -0.1), (0.1, 0.1), (-0.1, 0.1)]
    rings = 9
    for ring in range(rings):
        y = 2.0 * ring / (rings - 1)
        top = min(max((y - 0.75) / 0.5, 0.0), 1.0)
        for x, z in corners:
            positions += [x, y, z]
            length = math.hypot(x, z)
            normals += [x / length, 0.0, z / length]
            joints += [0, 1, 0, 0]
            weights += [1.0 - top, top, 0.0, 0.0]
    indices: list[int] = []
    for ring in range(rings - 1):
        for side in range(4):
            a = ring * 4 + side
            b = ring * 4 + (side + 1) % 4
            c = a + 4
            d = b + 4
            indices += [a, b, d, a, d, c]
    return positions, normals, joints, weights, indices


def build() -> bytes:
    builder = Builder()
    positions, normals, joints, weights, indices = column()
    position = builder.floats(positions, "VEC3", ARRAY_BUFFER, bounds=True)
    normal = builder.floats(normals, "VEC3", ARRAY_BUFFER)
    joint = builder.add(bytes(joints), UNSIGNED_BYTE, len(joints) // 4, "VEC4", ARRAY_BUFFER)
    weight = builder.floats(weights, "VEC4", ARRAY_BUFFER)
    index = builder.add(struct.pack(f"<{len(indices)}H", *indices), UNSIGNED_SHORT, len(indices), "SCALAR", ELEMENT_ARRAY_BUFFER)

    # Inverse bind matrices (column-major): Base at the origin, Top 1 m up.
    identity = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]
    top_inverse = identity[:12] + [0.0, -1.0, 0.0, 1.0]
    inverse_binds = builder.floats(identity + top_inverse, "MAT4")

    bend_times = builder.floats([0.0, 1.0, 2.0], "SCALAR", bounds=True)
    bend_rotations = builder.floats(quat_about((0, 0, 1), 0) + quat_about((0, 0, 1), 90) + quat_about((0, 0, 1), 0), "VEC4")
    wave_times = builder.floats([0.0, 0.5, 1.0], "SCALAR", bounds=True)
    wave_rotations = builder.floats(quat_about((0, 1, 0), 0) + quat_about((0, 1, 0), 90) + quat_about((0, 1, 0), 0), "VEC4")
    wave_lift = builder.floats([0.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0], "VEC3")
    # CUBICSPLINE keys are (in-tangent, value, out-tangent); flat tangents ease in and out.
    zero = [0.0, 0.0, 0.0]
    wave_scale = builder.floats(zero + [1.0, 1.0, 1.0] + zero + zero + [2.0, 2.0, 2.0] + zero + zero + [1.0, 1.0, 1.0] + zero, "VEC3")

    document = {
        "asset": {"version": "2.0", "generator": "MageCap Bellows Tools/GenerateTestModels.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "Column", "mesh": 0, "skin": 0},
            {"name": "Base", "children": [2]},
            {"name": "Top", "translation": [0.0, 1.0, 0.0]},
        ],
        "meshes": [{"name": "Column", "primitives": [{
            "attributes": {"POSITION": position, "NORMAL": normal, "JOINTS_0": joint, "WEIGHTS_0": weight},
            "indices": index}]}],
        "skins": [{"name": "ColumnRig", "joints": [1, 2], "inverseBindMatrices": inverse_binds, "skeleton": 1}],
        "animations": [
            {"name": "Bend",
             "samplers": [{"input": bend_times, "output": bend_rotations, "interpolation": "LINEAR"}],
             "channels": [{"sampler": 0, "target": {"node": 2, "path": "rotation"}}]},
            {"name": "Wave",
             "samplers": [
                 {"input": wave_times, "output": wave_rotations, "interpolation": "LINEAR"},
                 {"input": wave_times, "output": wave_lift, "interpolation": "STEP"},
                 {"input": wave_times, "output": wave_scale, "interpolation": "CUBICSPLINE"}],
             "channels": [
                 {"sampler": 0, "target": {"node": 1, "path": "rotation"}},
                 {"sampler": 1, "target": {"node": 1, "path": "translation"}},
                 {"sampler": 2, "target": {"node": 2, "path": "scale"}}]},
        ],
        "accessors": builder.accessors,
        "bufferViews": builder.views,
        "buffers": [{"byteLength": len(builder.data)}],
    }

    text = json.dumps(document, separators=(",", ":")).encode("utf-8")
    text += b" " * (-len(text) % 4)
    binary = bytes(builder.data) + b"\0" * (-len(builder.data) % 4)
    total = 12 + 8 + len(text) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, total)
            + struct.pack("<I4s", len(text), b"JSON") + text
            + struct.pack("<I4s", len(binary), b"BIN\0") + binary)


def main() -> None:
    OUTPUT.write_bytes(build())
    print(f"Wrote {OUTPUT.relative_to(ROOT)} ({OUTPUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
