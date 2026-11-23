"""Builds Projects/FeatureTest/Scenes/FeatureTest.bscene with the editor automation API.

The FeatureTest scene uses every component type and is the target of the scripting API test
(Assets/Scripts/FeatureTest.py). It is generated rather than hand-edited so it stays reproducible,
and doubles as an example of driving the editor from Python.

Usage (from the repository root, after building):
    <build>/bin/BellowsEditor --project Projects/FeatureTest --run-script Tools/BuildFeatureTest.py --exit [--headless]
"""

import math
import pathlib
import struct
import wave
import zlib

import bellows
from bellows import Vec2, Vec3, Vec4, editor

ASSETS = pathlib.Path(editor.asset_directory())


# -- Generated source assets -------------------------------------------------------------

def write_png(path: pathlib.Path, width: int, height: int, pixel):
    """Writes an 8-bit RGB PNG; pixel(x, y) returns an (r, g, b) tuple of ints."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(pixel(x, y))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    path.parent.mkdir(parents=True, exist_ok=True)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def ground_tiles(x, y):
    """Large stone tiles with dark grout and a little per-tile variation."""
    size = 128
    tx, ty = x // size, y // size
    gx, gy = x % size, y % size
    grout = gx < 3 or gy < 3
    noise = ((tx * 73856093) ^ (ty * 19349663)) & 0xFF
    fine = ((x * 2654435761 + y * 40503) >> 13) & 0x0F
    base = 128 + (noise % 24) - 12 + fine - 8
    if grout:
        base = 70 + fine
    return (max(0, min(255, base)), max(0, min(255, base - 2)), max(0, min(255, base - 6)))


def write_chime(path: pathlib.Path):
    """A short, soft two-tone chime (generated, so it can ship under any license)."""
    rate, duration = 44100, 0.9
    samples = bytearray()
    for i in range(int(rate * duration)):
        t = i / rate
        envelope = math.exp(-5.0 * t) * min(1.0, t * 200.0)
        value = 0.45 * math.sin(2 * math.pi * 880.0 * t) + 0.25 * math.sin(2 * math.pi * 1320.0 * t) + 0.1 * math.sin(2 * math.pi * 1760.0 * t)
        samples += struct.pack("<h", int(max(-1.0, min(1.0, value * envelope)) * 32000))
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(bytes(samples))


write_png(ASSETS / "Textures" / "GroundTiles.png", 512, 512, ground_tiles)
write_chime(ASSETS / "Audio" / "Chime.wav")
editor.reload_asset("Textures/GroundTiles.png")


# -- Materials ---------------------------------------------------------------------------

def material(name, color, metallic=0.0, roughness=0.5, **properties):
    result = bellows.Material(name, color, metallic, roughness)
    for key, value in properties.items():
        setattr(result, key, value)
    result.save(f"Materials/{name}.bmat")
    editor.reload_asset(f"Materials/{name}.bmat")
    return result.path


GROUND = material("Ground", Vec4(0.9, 0.9, 0.9, 1.0), 0.0, 0.85, albedo_map="Textures/GroundTiles.png", uv_tiling=Vec2(10.0, 10.0))
STONE = material("Stone", Vec4(0.12, 0.125, 0.135, 1.0), 0.0, 0.75)
CRATE = material("Crate", Vec4(0.55, 0.33, 0.16, 1.0), 0.0, 0.6)
RED = material("RedPlastic", Vec4(0.75, 0.06, 0.04, 1.0), 0.0, 0.3)
TRIGGER = material("TriggerVolume", Vec4(0.2, 0.6, 1.0, 0.35), 0.0, 0.3, alpha_mode="Blend", emissive=Vec3(0.2, 0.6, 1.0), emissive_intensity=0.5)
IRON = material("Iron", Vec4(0.3, 0.3, 0.32, 1.0), 1.0, 0.4)
SHOWCASE = [
    material("Gold", Vec4(1.0, 0.77, 0.34, 1.0), 1.0, 0.18),
    material("Copper", Vec4(0.95, 0.64, 0.54, 1.0), 1.0, 0.35),
    material("Chrome", Vec4(0.55, 0.56, 0.56, 1.0), 1.0, 0.05),
    material("Ceramic", Vec4(0.85, 0.86, 0.88, 1.0), 0.0, 0.12),
    material("Rubber", Vec4(0.05, 0.05, 0.06, 1.0), 0.0, 0.95),
]
GLOWS = [
    material("GlowOrange", Vec4(1.0, 0.45, 0.1, 1.0), 0.0, 0.5, emissive=Vec3(1.0, 0.45, 0.1), emissive_intensity=8.0),
    material("GlowCyan", Vec4(0.1, 0.7, 1.0, 1.0), 0.0, 0.5, emissive=Vec3(0.1, 0.7, 1.0), emissive_intensity=8.0),
    material("GlowMagenta", Vec4(1.0, 0.15, 0.7, 1.0), 0.0, 0.5, emissive=Vec3(1.0, 0.15, 0.7), emissive_intensity=8.0),
]


# -- Scene -------------------------------------------------------------------------------

editor.new_scene("FeatureTest")
for entity in bellows.get_entities():
    entity.destroy()


def create(name, mesh=None, material_path=None, position=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), parent=None):
    entity = bellows.create_entity(name, parent)
    entity.local_position = Vec3(position)
    entity.rotation = Vec3(rotation)
    entity.scale = Vec3(scale)
    if mesh:
        renderer = entity.add_component(bellows.MeshRenderer)
        renderer.mesh = mesh
        if material_path:
            renderer.material = material_path
    return entity


def add_body(entity, body_type, collider, **settings):
    body = entity.add_component(bellows.RigidBody)
    body.type = body_type
    for key, value in settings.items():
        setattr(body, key, value)
    return entity.add_component(collider)


def add_script(entity, path, **fields):
    script = entity.add_component(bellows.ScriptComponent)
    script.path = path
    for key, value in fields.items():
        script.set_field(key, value)
    return script


# Camera, sun and sky
camera = create("Main Camera", position=(0.0, 4.5, 13.0))
camera.add_component(bellows.Camera).fov = 55.0
camera.add_component(bellows.AudioListener)
camera.look_at(Vec3(0.0, 1.2, 0.0))
add_script(camera, "Scripts/FlyCamera.py")

sun = create("Sun", position=(0.0, 10.0, 0.0), rotation=(-38.0, 55.0, 0.0))
light = sun.add_component(bellows.DirectionalLight)
light.color, light.intensity = Vec3(1.0, 0.93, 0.84), 3.2

sky = create("Sky")
sky_light = sky.add_component(bellows.SkyLight)
sky_light.environment = "Environments/quarry_01_1k.hdr"
sky_light.intensity = 1.0

# Ground (static box collider under a textured plane)
ground = create("Ground", "Builtin/Plane", GROUND, scale=(40.0, 1.0, 40.0))
collider = add_body(ground, "Static", bellows.BoxCollider, friction=0.8)
collider.half_extents, collider.offset = Vec3(0.5, 0.05, 0.5), Vec3(0.0, -0.05, 0.0)

# PBR showcase: five spheres on stone pedestals
for index, path in enumerate(SHOWCASE):
    x = -5.0 + index * 2.5
    pedestal = create(f"Pedestal {index + 1}", "Builtin/Cylinder", STONE, position=(x, 0.3, -3.0), scale=(1.0, 0.6, 1.0))
    add_body(pedestal, "Static", bellows.MeshCollider)
    create(f"Sphere {index + 1}", "Builtin/Sphere", path, position=(x, 1.2, -3.0), scale=(1.2, 1.2, 1.2))

# glTF models
plinth = create("Bottle Plinth", "Builtin/Cube", STONE, position=(-3.5, 0.4, 2.0), scale=(1.2, 0.8, 1.2))
add_body(plinth, "Static", bellows.BoxCollider)
bottle = create("Water Bottle", "Models/WaterBottle.glb", position=(-3.5, 1.9, 2.0), scale=(8.0, 8.0, 8.0))
add_script(bottle, "Scripts/Spinner.py", speed=60.0)

# Skeletal animation: a column that waves (Tools/GenerateTestModels.py)
dancer = create("Dancer", "Models/SkinnedColumn.glb", position=(5.5, 0.0, -0.5))
dancer_animator = dancer.add_component(bellows.Animator)
dancer_animator.start_clip = "Wave"
dancer_animator.speed = 0.5

lantern = create("Lantern", "Models/Lantern.glb", position=(3.0, 0.0, 1.0), rotation=(0.0, -30.0, 0.0), scale=(0.15, 0.15, 0.15))
spot_entity = create("Spot Light", position=(3.0, 6.0, 4.0), rotation=(-60.0, 0.0, 0.0))
spot = spot_entity.add_component(bellows.SpotLight)
spot.color, spot.intensity, spot.range = Vec3(1.0, 0.85, 0.6), 60.0, 14.0
spot.inner_angle, spot.outer_angle = 18.0, 30.0

# Orbiting colored lights with glowing orbs
for index, path in enumerate(GLOWS):
    orb = create(f"Orbiting Light {index + 1}", "Builtin/Sphere", path, scale=(0.18, 0.18, 0.18))
    orb.get_component(bellows.MeshRenderer).cast_shadows = False
    point = orb.add_component(bellows.PointLight)
    point.color = Vec3(*[(0.95, 0.45, 0.1), (0.1, 0.7, 1.0), (1.0, 0.15, 0.7)][index])
    point.intensity, point.range = 12.0, 6.0
    add_script(orb, "Scripts/Orbit.py", radius=4.0, speed=0.5, phase=index * 2.0944, height=1.4, center=Vec3(0.0, 0.0, 0.5))

# Physics playground: a crate pyramid, a cannon, a ramp, a capsule and a trigger zone
crate_size = 0.8
for row in range(3):
    for column in range(3 - row):
        x = 7.0 + (column - (2 - row) / 2.0) * (crate_size + 0.02)
        crate = create(f"Crate {row}-{column}", "Builtin/Cube", CRATE, position=(x, crate_size * (row + 0.5), -3.0),
                       scale=(crate_size, crate_size, crate_size))
        add_body(crate, "Dynamic", bellows.BoxCollider, mass=2.0, friction=0.7)

cannon = create("Cannon", position=(-1.0, 1.0, -7.5), rotation=(8.0, -38.0, 0.0))
add_script(cannon, "Scripts/Cannon.py", interval=1.6, launch_speed=13.0)
create("Barrel", "Builtin/Cylinder", IRON, rotation=(90.0, 0.0, 0.0), scale=(0.5, 1.6, 0.5), parent=cannon)
create("Cannon Base", "Builtin/Cube", IRON, position=(-1.0, 0.35, -7.5), scale=(1.0, 0.7, 1.4))

ramp = create("Ramp", "Builtin/Cube", STONE, position=(-7.5, 0.5, -1.5), rotation=(0.0, 0.0, -18.0), scale=(4.0, 0.3, 2.0))
add_body(ramp, "Static", bellows.MeshCollider)

buddy = create("Capsule Buddy", "Builtin/Capsule", RED, position=(-4.0, 1.0, -0.5))
capsule = add_body(buddy, "Dynamic", bellows.CapsuleCollider, mass=5.0, lock_rotation_x=True, lock_rotation_z=True)
capsule.radius, capsule.half_height = 0.5, 0.5

# Navigation: a guard that walks the navmesh, carrying its kinematic capsule (the feature tests send it on its way)
guard = create("Guard", "Builtin/Capsule", RED, position=(-3.0, 0.9, 7.0), scale=(0.6, 0.9, 0.6))
guard_capsule = add_body(guard, "Kinematic", bellows.CapsuleCollider)
guard_capsule.radius, guard_capsule.half_height = 0.5, 0.5
guard_agent = guard.add_component(bellows.NavAgent)
guard_agent.radius, guard_agent.base_offset = 0.3, 0.9

zone = create("Trigger Zone", "Builtin/Cube", TRIGGER, position=(6.0, 1.0, 4.0), scale=(3.0, 2.0, 3.0))
zone.get_component(bellows.MeshRenderer).cast_shadows = False
add_body(zone, "Static", bellows.BoxCollider, is_trigger=True)
add_script(zone, "Scripts/TriggerZone.py")

speaker = create("Speaker", "Builtin/Cone", IRON, position=(0.0, 0.35, 4.5), scale=(0.5, 0.7, 0.5))
source = speaker.add_component(bellows.AudioSource)
source.clip, source.spatial, source.volume = "Audio/Chime.wav", True, 0.8

# Text: a title over the PBR showcase and a camera-facing label over the speaker
title = create("Title", position=(0.0, 3.2, -3.0))
title_text = title.add_component(bellows.Text)
title_text.text, title_text.size = "MageCap Bellows", 0.7
title_text.color, title_text.outline_width = Vec4(1.0, 0.86, 0.55, 1.0), 0.04
speaker_label = create("Speaker Label", position=(0.0, 1.2, 4.5))
label_text = speaker_label.add_component(bellows.Text)
label_text.text, label_text.size, label_text.face_camera = "Speaker", 0.3, True

# Screen UI: a panel in the top-left corner with a title, over the game
hud = create("HUD")
hud.add_component(bellows.UICanvas)
hud_panel = create("HUD Panel", parent=hud)
panel_rect = hud_panel.add_component(bellows.UIRect)
panel_rect.anchor_min = panel_rect.anchor_max = Vec3(0.0, 0.0, 0.0)
panel_rect.offset_min, panel_rect.offset_max = Vec3(24.0, 24.0, 0.0), Vec3(504.0, 100.0, 0.0)
panel_image = hud_panel.add_component(bellows.UIImage)
panel_image.color, panel_image.corner_radius = Vec4(0.09, 0.09, 0.16, 0.85), 14.0
panel_image.border_width, panel_image.border_color = 2.0, Vec4(0.96, 0.53, 0.23, 1.0)
hud_title = create("HUD Title", parent=hud_panel)
title_rect = hud_title.add_component(bellows.UIRect)
title_rect.anchor_min, title_rect.anchor_max = Vec3(0.0, 0.0, 0.0), Vec3(1.0, 1.0, 0.0)
title_rect.offset_min, title_rect.offset_max = Vec3(22.0, 0.0, 0.0), Vec3(-22.0, 0.0, 0.0)
hud_text = hud_title.add_component(bellows.Text)
hud_text.text, hud_text.size, hud_text.alignment = "FeatureTest: every component", 30.0, "left"
hud_text.color = Vec4(0.94, 0.95, 0.98, 1.0)

# The scripting API tests: FeatureTest.forge runs first, then FeatureTest.py checks its result.
runner = create("Test Runner")
add_script(runner, "Scripts/FeatureTest.py")
forge_runner = create("ForgeScript Test Runner")
add_script(forge_runner, "Scripts/FeatureTest.forge", label="authored")

# Ball prefab (fired by the cannon and used by the test), saved from a template entity
ball = create("Ball", "Builtin/Sphere", RED, scale=(0.6, 0.6, 0.6))
add_body(ball, "Dynamic", bellows.SphereCollider, mass=1.0, restitution=0.35, friction=0.5, continuous=True).radius = 0.5
ball_audio = ball.add_component(bellows.AudioSource)
ball_audio.clip, ball_audio.spatial, ball_audio.volume = "Audio/Chime.wav", True, 0.35
add_script(ball, "Scripts/BounceSound.py")
editor.save_prefab(ball, "Prefabs/Ball.bprefab")
ball.destroy()

# Scene settings live with the scene; the project starts here.
editor.commit("Build FeatureTest")
assert editor.save_scene("Scenes/FeatureTest.bscene")
# Navigation: baked from the static colliders (ground, pedestals, plinth), saved beside the scene.
baked = editor.bake_navigation()
print(f"Baked navigation: {baked['tiles']} tiles, {baked['polygons']} polygons")

editor.set_camera((0.0, 6.0, 15.0), (0.0, 1.0, 0.0))
editor.run_frames(3)
print(f"FeatureTest scene built with {len(bellows.get_entities())} entities")
