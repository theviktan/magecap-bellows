"""Exercises the complete MageCap Bellows scripting API inside the FeatureTest scene.

Attach to one entity of FeatureTest.bscene. The test runs over several frames (physics and
triggers need simulated time), logs every check and finally logs "FEATURE TEST PASSED" or
"FEATURE TEST FAILED". When the environment variable BELLOWS_FEATURE_TEST=1 is set (automated
runs), the game quits afterwards with exit code 0 (pass) or 1 (fail).
"""

import math
import os
import pathlib
import struct
import time
import traceback

import bellows
from bellows import Key, MouseButton, Quat, Vec2, Vec3, Vec4


def raises(action, error):
    """True when action() raises the given exception type."""
    try:
        action()
    except error:
        return True
    return False


def close(a, b, tolerance=1e-4):
    if isinstance(a, (int, float)):
        return abs(a - b) <= tolerance
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


class FeatureTest(bellows.Script):
    """Runs the scripting API test suite while the scene plays."""

    timeout: float = 60.0                    # Seconds before the test gives up

    def on_create(self):
        self.checks = 0
        self.failures = []
        self.finished = False
        self.events = []
        self.steps = self.run()

    def on_event(self, name, *values):
        self.events.append((name, values))

    # -- Helpers -----------------------------------------------------------------------
    def check(self, condition, description):
        self.checks += 1
        if not condition:
            self.failures.append(description)
            bellows.log_warning(f"FeatureTest: FAILED - {description}")

    def wait_frames(self, count):
        for _ in range(count):
            yield

    def wait_seconds(self, seconds):
        end = bellows.time() + seconds
        while bellows.time() < end:
            yield

    def wait_until(self, predicate, seconds):
        end = bellows.time() + seconds
        while not predicate() and bellows.time() < end:
            yield

    # -- Frame loop ----------------------------------------------------------------------
    def on_update(self, dt):
        if self.finished:
            return
        try:
            next(self.steps)
            if bellows.time() > self.timeout:
                raise TimeoutError(f"the test did not finish within {self.timeout} seconds")
        except StopIteration:
            self.finish()
        except Exception:  # noqa: BLE001 - every failure must be reported, then the test ends
            self.failures.append("unexpected exception:\n" + traceback.format_exc())
            self.finish()

    def finish(self):
        self.finished = True
        if self.failures:
            bellows.log_error(f"FEATURE TEST FAILED: {len(self.failures)} of {self.checks} checks failed")
            for failure in self.failures:
                bellows.log_error(f"  - {failure}")
        else:
            bellows.log(f"FEATURE TEST PASSED ({self.checks} checks)")
        if os.environ.get("BELLOWS_FEATURE_TEST") == "1":
            bellows.quit(1 if self.failures else 0)

    # -- The test sequence -------------------------------------------------------------
    def run(self):
        yield from self.test_forgescript()
        self.test_math()
        self.test_time_and_input()
        self.test_scene_queries()
        self.test_entities_and_hierarchy()
        self.test_components()
        self.test_materials()
        self.test_scripts()
        self.test_camera_projection()
        self.test_audio()
        self.test_screenshots()
        self.test_saved_data()
        self.test_events()
        yield from self.test_destruction()
        yield from self.test_prefabs()
        yield from self.test_physics()
        yield from self.test_collisions_and_triggers()
        yield from self.test_raycasts()
        yield from self.test_gameplay_scripts()

    def test_forgescript(self):
        """FeatureTest.forge tests the API from ForgeScript first; it reports by renaming its entity."""
        runner = bellows.find_entity("ForgeScript Test Runner")
        self.check(runner is not None and runner.get_component(bellows.ScriptComponent).path == "Scripts/FeatureTest.forge",
                   "the ForgeScript test runner is in the scene")
        if runner is None:
            return
        yield from self.wait_until(lambda: runner.name.startswith("ForgeScript Test P") or runner.name.startswith("ForgeScript Test F"), 20.0)
        self.check(runner.name.startswith("ForgeScript Test Passed"), f"FeatureTest.forge ({runner.name})")
        self.check(runner.get_component(bellows.ScriptComponent).instance is None, "ForgeScript scripts have no Python instance")

    def test_math(self):
        a, b = Vec3(1.0, 2.0, 3.0), Vec3(4.0, 5.0, 6.0)
        self.check(close(a + b, (5.0, 7.0, 9.0)), "Vec3 addition")
        self.check(close(b - a, (3.0, 3.0, 3.0)), "Vec3 subtraction")
        self.check(close(a * 2.0, (2.0, 4.0, 6.0)) and close(2.0 * a, (2.0, 4.0, 6.0)), "Vec3 scalar multiplication")
        self.check(close(a * b, (4.0, 10.0, 18.0)), "Vec3 component-wise multiplication")
        self.check(close(b / 2.0, (2.0, 2.5, 3.0)), "Vec3 division")
        self.check(close(-a, (-1.0, -2.0, -3.0)), "Vec3 negation")
        self.check(close(a.dot(b), 32.0), "Vec3 dot")
        self.check(close(Vec3(1, 0, 0).cross(Vec3(0, 1, 0)), (0.0, 0.0, 1.0)), "Vec3 cross")
        self.check(close(Vec3(3, 4, 0).length(), 5.0) and close(Vec3(3, 4, 0).length_squared(), 25.0), "Vec3 length")
        self.check(close(Vec3(0, 0, 9).normalized(), (0.0, 0.0, 1.0)), "Vec3 normalized")
        self.check(close(a.distance(b), math.sqrt(27.0)), "Vec3 distance")
        self.check(close(a.lerp(b, 0.5), (2.5, 3.5, 4.5)), "Vec3 lerp")
        c = a.copy()
        c += Vec3(1.0)
        c -= Vec3(0.5)
        c *= 2.0
        self.check(close(c, (3.0, 5.0, 7.0)) and close(a, (1.0, 2.0, 3.0)), "Vec3 in-place operators and copy")
        self.check(list(a) == [1.0, 2.0, 3.0] and len(a) == 3 and a[2] == 3.0, "Vec3 sequence protocol")
        self.check(Vec3((1, 2, 3)) == a and Vec3(7.0) == Vec3(7, 7, 7), "Vec3 constructors")
        self.check("Vec3(" in repr(a), "Vec3 repr")

        v2 = Vec2(3.0, 4.0)
        self.check(close(v2.length(), 5.0) and close(v2.normalized(), (0.6, 0.8)) and close(v2.dot(Vec2(1, 0)), 3.0), "Vec2 operations")
        self.check(close(v2 + Vec2(1.0), (4.0, 5.0)) and list(Vec2((1, 2))) == [1.0, 2.0], "Vec2 arithmetic and construction")

        color = Vec4(0.1, 0.2, 0.3, 1.0)
        self.check(close((color.r, color.g, color.b, color.a), (0.1, 0.2, 0.3, 1.0)), "Vec4 color accessors")
        color.a = 0.5
        self.check(close(color.w, 0.5) and len(color) == 4, "Vec4 write access")
        self.check(close(color * 2.0, (0.2, 0.4, 0.6, 1.0)), "Vec4 arithmetic")

        quarter = Quat.from_axis_angle(Vec3(0, 1, 0), math.pi / 2)
        self.check(close(quarter * Vec3(1, 0, 0), (0.0, 0.0, -1.0)), "Quat rotates vectors")
        self.check(close(Quat.from_euler_degrees(Vec3(10, 45, 0)).to_euler_degrees(), (10.0, 45.0, 0.0), 1e-3), "Quat Euler round trip (degrees)")
        self.check(close(Quat.from_euler(Vec3(0.3, 0.0, 0.0)).to_euler(), (0.3, 0.0, 0.0)), "Quat Euler round trip (radians)")
        identity = quarter * quarter.inverse()
        self.check(close((identity.w, identity.x, identity.y, identity.z), (1.0, 0.0, 0.0, 0.0)), "Quat inverse")
        half = Quat().slerp(quarter, 0.5).normalized()
        self.check(close(half.to_euler_degrees(), (0.0, 45.0, 0.0), 1e-3), "Quat slerp")
        self.check(close(bellows.lerp(2.0, 4.0, 0.25), 2.5) and bellows.clamp(5.0, 0.0, 1.0) == 1.0, "lerp and clamp")
        self.check(isinstance(bellows.VERSION, str) and bellows.VERSION.count(".") == 2, "VERSION string")

    def test_time_and_input(self):
        self.check(bellows.delta_time() >= 0.0 and bellows.time() >= 0.0 and bellows.frame() >= 0, "time functions")
        # No device input is delivered in automated runs, but every query must work.
        self.check(not bellows.is_key_down(Key.Space) and not bellows.is_key_pressed(Key.W) and not bellows.is_key_released(Key.Escape), "keyboard queries")
        self.check(not bellows.is_key_down("A"), "keys by name")
        self.check(bellows.action_names() == ["fire", "jump"], "action_names lists the project's actions")
        self.check(not bellows.is_action_down("jump") and not bellows.is_action_pressed("fire") and not bellows.is_action_released("jump"), "input action queries")
        self.check(raises(lambda: bellows.is_action_down("dance"), ValueError), "unknown input actions are rejected")
        self.check(not bellows.is_gamepad_down("A") and not bellows.is_gamepad_pressed("dpad_up") and not bellows.is_gamepad_released("Start"), "gamepad button queries")
        self.check(raises(lambda: bellows.is_gamepad_down("Z"), ValueError), "unknown gamepad buttons are rejected")
        self.check(isinstance(bellows.gamepad_connected(), bool), "gamepad_connected")
        self.check(not bellows.is_mouse_down(MouseButton.Right) and not bellows.is_mouse_pressed() and not bellows.is_mouse_released(MouseButton.Middle), "mouse button queries")
        self.check(isinstance(bellows.mouse_position(), Vec2) and isinstance(bellows.mouse_delta(), Vec2) and isinstance(bellows.scroll_delta(), Vec2), "mouse position queries")
        size = bellows.viewport_size()
        self.check(size.x > 0 and size.y > 0, f"viewport size ({size})")
        bellows.log("FeatureTest: logging works", 42, Vec3(1.0))
        bellows.log_warning("FeatureTest: warnings are visible in the console (expected)")
        print("FeatureTest: print() goes to the console")

    def test_scene_queries(self):
        self.check(bellows.is_playing(), "is_playing")
        self.check(bellows.scene_name() == "FeatureTest", f"scene_name ({bellows.scene_name()})")
        for name in ("Main Camera", "Sun", "Sky", "Ground", "Water Bottle", "Lantern", "Cannon", "Trigger Zone", "Spot Light"):
            self.check(bellows.find_entity(name) is not None, f"authored entity '{name}' exists")
        self.check(bellows.find_entity("Does Not Exist") is None, "find_entity returns None for missing names")
        cameras = bellows.get_entities(bellows.Camera)
        self.check(len(cameras) == 1, "get_entities filters by component")
        self.check(len(bellows.get_entities()) > len(bellows.get_entities("PointLight")) > 0, "get_entities without a filter")
        camera = bellows.primary_camera()
        self.check(camera is not None and camera == cameras[0], "primary_camera")
        self.check(bellows.get_entity(camera.id) == camera, "get_entity by id")
        gravity = bellows.get_gravity()
        self.check(close(gravity, (0.0, -9.81, 0.0), 1e-3), f"default gravity ({gravity})")

    def test_entities_and_hierarchy(self):
        parent = bellows.create_entity("Test Parent")
        child = bellows.create_entity("Test Child", parent)
        grandchild = bellows.create_entity("Test Grandchild")
        grandchild.parent = child
        self.check(child.parent == parent and grandchild.parent == child, "parenting")
        self.check([c.name for c in parent.children] == ["Test Child"], "children")
        self.check(parent.find_child("Test Grandchild") == grandchild, "find_child searches descendants")
        self.check(hash(parent) == hash(bellows.find_entity("Test Parent")) and parent != child, "entity equality and hashing")
        self.check(bool(parent) and parent.valid and "Test Parent" in repr(parent), "entity validity and repr")

        try:
            parent.parent = grandchild
            self.check(False, "hierarchy cycles are rejected")
        except ValueError:
            self.check(True, "hierarchy cycles are rejected")

        parent.name = "Renamed Parent"
        self.check(parent.name == "Renamed Parent", "renaming")

        parent.position = Vec3(1.0, 2.0, 3.0)
        child.local_position = Vec3(0.0, 1.0, 0.0)
        self.check(close(child.position, (1.0, 3.0, 3.0)), "child world position follows the parent")
        parent.scale = Vec3(2.0)
        self.check(close(child.position, (1.0, 4.0, 3.0)), "parent scale affects children")
        parent.scale = Vec3(1.0)
        parent.rotation = Vec3(0.0, 90.0, 0.0)
        self.check(close(parent.rotation, (0.0, 90.0, 0.0), 1e-3), "entity rotation in degrees")
        self.check(close(parent.forward, (-1.0, 0.0, 0.0), 1e-4), f"forward ({parent.forward})")
        self.check(close(parent.right, (0.0, 0.0, -1.0), 1e-4) and close(parent.up, (0.0, 1.0, 0.0), 1e-4), "right and up")
        parent.rotation = Vec3(0.0)
        parent.rotate(Vec3(0.0, 0.0, 90.0))
        self.check(close(parent.rotation, (0.0, 0.0, 90.0), 1e-3), "rotate")
        parent.translate(Vec3(1.0, 0.0, 0.0))
        self.check(close(parent.position, (2.0, 2.0, 3.0)), "translate")
        parent.look_at(Vec3(2.0, 2.0, -10.0))
        self.check(close(parent.forward, (0.0, 0.0, -1.0), 1e-4), "look_at")

        transform = parent.transform
        transform.position = Vec3(5.0, 0.0, 0.0)
        self.check(close(transform.translation, (5.0, 0.0, 0.0)), "Transform position and translation")
        transform.rotation_degrees = Vec3(0.0, 180.0, 0.0)
        self.check(close(abs(transform.rotation[1]), math.pi, 1e-3) or close(transform.forward, (0.0, 0.0, 1.0), 1e-4), "Transform rotation in radians")
        transform.quaternion = Quat()
        self.check(close(transform.forward, (0.0, 0.0, -1.0)), "Transform quaternion")
        transform.scale = Vec3(1.0, 2.0, 1.0)
        self.check(close(transform.scale, (1.0, 2.0, 1.0)), "Transform scale")
        self.check(close(child.transform.world_position, child.position), "Transform world_position")
        self.check(close(transform.right, (1.0, 0.0, 0.0)) and close(transform.up, (0.0, 1.0, 0.0)), "Transform axes")

        child.parent = None
        self.check(child.parent is None and parent.children == [], "unparenting")
        for entity in (grandchild, child, parent):
            entity.destroy()

    def test_components(self):
        entity = bellows.create_entity("Component Test")
        self.check(entity.components == ["Transform"], "new entities only have a Transform")
        try:
            entity.remove_component("Transform")
            self.check(False, "the Transform cannot be removed")
        except ValueError:
            self.check(True, "the Transform cannot be removed")
        try:
            entity.add_component("NotAComponent")
            self.check(False, "unknown components are rejected")
        except ValueError:
            self.check(True, "unknown components are rejected")

        renderer = entity.add_component(bellows.MeshRenderer)
        renderer.mesh = "Builtin/Sphere"
        renderer.cast_shadows = False
        renderer.visible = True
        self.check(renderer.mesh == "Builtin/Sphere" and not renderer.cast_shadows and renderer.visible, "MeshRenderer properties")
        self.check(entity.has_component("MeshRenderer") and entity.get_component(bellows.MeshRenderer) is not None, "has/get component")

        camera = entity.add_component(bellows.Camera)
        camera.fov = 70.0
        camera.near_clip = 0.2
        camera.far_clip = 500.0
        camera.projection = "Orthographic"
        camera.orthographic_size = 12.0
        camera.primary = False
        self.check(close(camera.fov, 70.0, 1e-3) and close(camera.near_clip, 0.2) and close(camera.far_clip, 500.0), "Camera clip and fov")
        self.check(camera.projection == "Orthographic" and close(camera.orthographic_size, 12.0) and not camera.primary, "Camera projection")

        sun = entity.add_component(bellows.DirectionalLight)
        sun.color, sun.intensity, sun.cast_shadows = Vec3(1.0, 0.9, 0.8), 2.0, False
        self.check(close(sun.color, (1.0, 0.9, 0.8)) and close(sun.intensity, 2.0) and not sun.cast_shadows, "DirectionalLight properties")
        entity.remove_component(bellows.DirectionalLight)

        point = entity.add_component(bellows.PointLight)
        point.color, point.intensity, point.range = Vec3(0.0, 1.0, 0.0), 20.0, 4.0
        self.check(close(point.color, (0.0, 1.0, 0.0)) and close(point.intensity, 20.0) and close(point.range, 4.0), "PointLight properties")

        spot = entity.add_component(bellows.SpotLight)
        spot.color, spot.intensity, spot.range = Vec3(1.0), 30.0, 8.0
        spot.inner_angle, spot.outer_angle = 15.0, 30.0
        self.check(close(spot.inner_angle, 15.0, 1e-3) and close(spot.outer_angle, 30.0, 1e-3) and close(spot.range, 8.0), "SpotLight properties")

        sky_entity = bellows.find_entity("Sky")
        sky = sky_entity.get_component(bellows.SkyLight)
        self.check(sky.environment.endswith(".hdr"), f"SkyLight environment ({sky.environment})")
        old_intensity = sky.intensity
        sky.intensity = old_intensity * 1.1
        sky.rotation = 30.0
        sky.skybox_blur = 0.5
        self.check(close(sky.rotation, 30.0, 1e-3) and close(sky.skybox_blur, 0.5), "SkyLight properties")
        sky.intensity = old_intensity
        sky.rotation = 0.0
        sky.skybox_blur = 0.0

        body = entity.add_component(bellows.RigidBody)
        body.type = "Kinematic"
        body.mass, body.friction, body.restitution = 3.0, 0.4, 0.2
        body.linear_damping, body.angular_damping, body.gravity_scale = 0.1, 0.2, 0.5
        body.is_trigger = False
        body.continuous = True
        self.check(body.continuous, "RigidBody continuous collision")
        body.lock_rotation_x = body.lock_rotation_y = body.lock_rotation_z = True
        self.check(body.type == "Kinematic" and close(body.mass, 3.0) and close(body.gravity_scale, 0.5), "RigidBody properties")
        self.check(body.lock_rotation_x and body.lock_rotation_y and body.lock_rotation_z and not body.is_trigger, "RigidBody locks")

        box = entity.add_component(bellows.BoxCollider)
        box.half_extents, box.offset = Vec3(0.5, 1.0, 0.5), Vec3(0.0, 1.0, 0.0)
        sphere = entity.add_component(bellows.SphereCollider)
        sphere.radius, sphere.offset = 0.75, Vec3(0.0)
        capsule = entity.add_component(bellows.CapsuleCollider)
        capsule.radius, capsule.half_height, capsule.offset = 0.3, 0.6, Vec3(0.0, 0.5, 0.0)
        mesh_collider = entity.add_component(bellows.MeshCollider)
        mesh_collider.use_mesh_renderer = True
        self.check(close(box.half_extents, (0.5, 1.0, 0.5)) and close(sphere.radius, 0.75), "Box and sphere colliders")
        self.check(close(capsule.half_height, 0.6) and close(capsule.offset, (0.0, 0.5, 0.0)) and mesh_collider.use_mesh_renderer, "Capsule and mesh colliders")

        source = entity.add_component(bellows.AudioSource)
        source.clip, source.volume, source.pitch = "Audio/Chime.wav", 0.5, 1.2
        source.loop, source.play_on_start, source.spatial = True, False, True
        source.min_distance, source.max_distance = 2.0, 30.0
        self.check(source.clip == "Audio/Chime.wav" and close(source.pitch, 1.2) and source.loop and source.spatial, "AudioSource properties")
        listener = entity.add_component(bellows.AudioListener)
        listener.active = False
        self.check(not listener.active, "AudioListener properties")

        script = entity.add_component(bellows.ScriptComponent)
        script.path, script.class_name = "Scripts/Spinner.py", "Spinner"
        self.check(script.path == "Scripts/Spinner.py" and script.class_name == "Spinner", "ScriptComponent properties")

        expected = ["Transform", "MeshRenderer", "Camera", "PointLight", "SpotLight", "RigidBody", "BoxCollider", "SphereCollider",
                    "CapsuleCollider", "MeshCollider", "Script", "AudioSource", "AudioListener"]
        self.check(entity.components == expected, f"components list ({entity.components})")
        for name in expected[1:]:
            entity.remove_component(name)
        self.check(entity.components == ["Transform"], "remove_component")
        entity.destroy()

    def test_materials(self):
        material = bellows.Material("Test Gold", Vec4(1.0, 0.8, 0.3, 1.0), 1.0, 0.25)
        self.check(material.name == "Test Gold" and material.path.startswith("Runtime/"), "runtime materials")
        material.name = "Renamed"
        material.color = (0.5, 0.5, 0.5, 1.0)
        material.metallic = 2.0                  # Clamped
        material.roughness = 0.6
        material.emissive = Vec3(1.0, 0.2, 0.0)
        material.emissive_intensity = 3.0
        material.alpha_mode = "Mask"
        material.double_sided = True
        self.check(material.name == "Renamed" and close(material.color, (0.5, 0.5, 0.5, 1.0)), "Material name and color")
        self.check(close(material.metallic, 1.0) and close(material.roughness, 0.6), "Material metallic and roughness")
        self.check(close(material.emissive, (1.0, 0.2, 0.0)) and close(material.emissive_intensity, 3.0), "Material emission")
        self.check(material.alpha_mode == "Mask" and material.double_sided and "Renamed" in repr(material), "Material alpha mode and sidedness")
        material.alpha_cutoff = 0.3
        material.normal_strength = 0.5
        material.uv_tiling = Vec2(4.0, 2.0)
        material.uv_offset = (0.5, 0.0)
        self.check(close(material.alpha_cutoff, 0.3) and close(material.normal_strength, 0.5), "Material cutoff and normal strength")
        self.check(close(material.uv_tiling, (4.0, 2.0)) and close(material.uv_offset, (0.5, 0.0)), "Material UV transform")
        for slot in ("albedo_map", "normal_map", "metallic_roughness_map", "roughness_map", "metallic_map", "occlusion_map", "emissive_map"):
            setattr(material, slot, "Textures/GroundTiles.png")
            self.check(getattr(material, slot) == "Textures/GroundTiles.png", f"Material.{slot}")
            setattr(material, slot, None)
            self.check(getattr(material, slot) == "", f"clearing Material.{slot}")
        try:
            material.albedo_map = "Textures/Missing.png"
            self.check(False, "missing textures are rejected")
        except ValueError:
            self.check(True, "missing textures are rejected")

        entity = bellows.create_entity("Material Test")
        renderer = entity.add_component(bellows.MeshRenderer)
        renderer.mesh = "Builtin/Cube"
        renderer.material = material
        self.check(renderer.material == material.path, "assigning a material")
        renderer.material = "Materials/Gold.bmat"
        self.check(renderer.material == "Materials/Gold.bmat", "assigning a material by path")
        renderer.material = None
        renderer.color = Vec4(0.0, 1.0, 0.0, 1.0)
        self.check(close(renderer.color, (0.0, 1.0, 0.0, 1.0)), "MeshRenderer color creates a per-entity material")

        gold = bellows.Material.load("Materials/Gold.bmat")
        self.check(gold.path == "Materials/Gold.bmat" and gold.metallic > 0.9, "Material.load")
        try:
            bellows.Material.load("Materials/Missing.bmat")
            self.check(False, "loading a missing material raises")
        except ValueError:
            self.check(True, "loading a missing material raises")

        # Saving writes into the project, so the file is removed again afterwards.
        asset_directory = pathlib.Path(__file__).resolve().parent.parent
        saved_path = "Materials/_FeatureTestSaved.bmat"
        material.save(saved_path)
        saved_file = asset_directory / saved_path
        self.check(material.path == saved_path and saved_file.is_file(), "Material.save")
        reloaded = bellows.Material.load(saved_path)
        self.check(close(reloaded.roughness, 0.6) and reloaded.alpha_mode == "Mask", "saved materials load back")
        saved_file.unlink(missing_ok=True)
        entity.destroy()

    def test_scripts(self):
        bottle = bellows.find_entity("Water Bottle")
        script = bottle.get_component(bellows.ScriptComponent)
        self.check(script.path == "Scripts/Spinner.py", "authored ScriptComponent")
        self.check(script.fields.get("speed") == 60.0, f"overridden fields are stored in the scene ({script.fields})")
        spinner = bottle.script
        self.check(spinner is not None and spinner is script.instance, "script instance access")
        self.check(spinner.entity == bottle and spinner.speed == 60.0, "fields are applied to the instance")
        self.check(isinstance(spinner.axis, Vec3), "Vec3 fields")
        script.set_field("speed", 75.0)
        self.check(script.fields["speed"] == 75.0, "set_field")
        try:
            script.set_field("speed", object())
            self.check(False, "invalid field types are rejected")
        except TypeError:
            self.check(True, "invalid field types are rejected")
        self.check(self.entity.script is self, "a script can find itself")

    def test_camera_projection(self):
        size = bellows.viewport_size()
        center = bellows.world_to_screen(Vec3(0.0, 0.5, 0.0))
        self.check(center is not None and 0 <= center.x <= size.x and 0 <= center.y <= size.y, f"world_to_screen ({center})")
        ray = bellows.screen_to_world_ray(Vec2(size.x * 0.5, size.y * 0.5))
        camera = bellows.primary_camera()
        self.check(ray is not None and close(ray[1], camera.forward, 1e-3), "screen_to_world_ray through the center follows the camera")
        behind = bellows.world_to_screen(camera.position - camera.forward * 5.0)
        self.check(behind is None, "points behind the camera are not on screen")

    def test_audio(self):
        self.check(bellows.play_sound("Audio/Chime.wav", 0.2), "play_sound")
        self.check(not bellows.play_sound("Audio/Missing.wav"), "play_sound fails for missing clips")
        old_volume = bellows.get_master_volume()
        bellows.set_master_volume(0.5)
        self.check(close(bellows.get_master_volume(), 0.5), "master volume")
        bellows.set_master_volume(old_volume)

        self.check(bellows.bus_names() == ["Master", "Music", "Effects"], "bus_names")
        bellows.set_bus_volume("Music", 0.25)
        self.check(close(bellows.get_bus_volume("Music"), 0.25), "set_bus_volume")
        bellows.set_bus_muted("Music")
        self.check(bellows.is_bus_muted("Music") and close(bellows.get_bus_volume("Music"), 0.25), "set_bus_muted keeps the volume")
        bellows.set_bus_muted("Music", False)
        bellows.set_bus_volume("Music", 1.0)
        self.check(bellows.play_sound("Audio/Chime.wav", 0.2, "Music"), "play_sound on a bus")
        self.check(raises(lambda: bellows.set_bus_volume("Voices", 1.0), ValueError), "unknown buses are rejected")

        speaker = bellows.find_entity("Speaker")
        source = speaker.get_component(bellows.AudioSource)
        self.check(source.bus == "Effects", "AudioSource.bus defaults to Effects")
        source.bus = "Music"
        self.check(source.bus == "Music", "AudioSource.bus")
        self.check(raises(lambda: setattr(source, "bus", "Voices"), ValueError), "AudioSource.bus rejects unknown buses")
        source.bus = "Effects"
        self.check(source.play() and source.is_playing, "AudioSource.play")
        source.pause()
        self.check(not source.is_playing, "AudioSource.pause")
        source.play()
        source.stop()
        self.check(not source.is_playing, "AudioSource.stop")
        listener = bellows.primary_camera().get_component(bellows.AudioListener)
        self.check(listener is not None and listener.active, "AudioListener on the camera")

    def test_screenshots(self):
        folder = bellows.user_folder()
        self.check(os.path.isabs(folder) and folder.endswith(os.path.join("Games", "Feature Test")), f"user_folder ({folder})")
        path = bellows.save_screenshot("FeatureTest/shot.png", 320)
        self.check(path == os.path.join(folder, "FeatureTest", "shot.png"), f"save_screenshot returns the full path ({path})")
        with open(path, "rb") as file:
            header = file.read(24)
        width, height = struct.unpack(">II", header[16:24])
        self.check(header[:8] == b"\x89PNG\r\n\x1a\n" and width == 320 and 0 < height < width, f"save_screenshot writes a scaled PNG ({width}x{height})")
        jpeg = bellows.save_screenshot("FeatureTest/shot.jpg", 0)
        with open(jpeg, "rb") as file:
            self.check(file.read(2) == b"\xff\xd8", "save_screenshot writes JPEG for .jpg")
        self.check(raises(lambda: bellows.save_screenshot("shot.gif"), ValueError), "save_screenshot rejects other formats")

    def test_saved_data(self):
        bellows.delete_data("Feature Test Python")
        self.check(bellows.load_data("Feature Test Python", 7) == 7, "load_data's default")
        progress = {"credits": 12.5, "ships": ["Comet", "Nova"], "spawn": bellows.Vec3(1, 2, 3), "docked": True, "captain": None}
        self.check(bellows.save_data("Feature Test Python", progress) and bellows.has_data("Feature Test Python"), "save_data and has_data")
        path = os.path.join(bellows.user_folder(), "Saves", "Feature Test Python.json")
        self.check(os.path.isfile(path), f"saves go to user_folder()/Saves ({path})")
        loaded = bellows.load_data("Feature Test Python")
        self.check(loaded == progress and isinstance(loaded["spawn"], bellows.Vec3), f"load_data gives back what was saved ({loaded})")
        self.check(raises(lambda: bellows.save_data("Feature Test Python", [self.entity]), ValueError), "entities can't be saved")
        self.check(raises(lambda: bellows.save_data("../escape", 1), ValueError), "save names stay in the Saves folder")
        self.check(raises(lambda: bellows.save_data("x", object()), ValueError), "values Python can't save are refused")
        self.check(bellows.delete_data("Feature Test Python") and not bellows.has_data("Feature Test Python"), "delete_data")
        self.check(abs(bellows.real_time() - time.time()) < 5, "real_time is seconds since 1970")

    def test_events(self):
        bellows.emit("feature test", 1, "two", [3], {"four": 4.0}, self.entity)
        self.check(self.events and self.events[-1] == ("feature test", (1.0, "two", [3.0], {"four": 4.0}, self.entity)),
                   "emit reaches on_event, the sender's own included")
        self.check(raises(lambda: bellows.emit("bad", object()), ValueError), "emit rejects values other scripts can't read")

    def test_destruction(self):
        temporary = bellows.create_entity("Temporary")
        bellows.destroy(temporary)
        self.check(temporary.valid, "destroy is deferred to the end of the frame during play")
        yield
        self.check(not temporary.valid and not temporary, "destroyed entities become invalid at the end of the frame")
        try:
            temporary.name
            self.check(False, "using a destroyed entity raises")
        except RuntimeError:
            self.check(True, "using a destroyed entity raises")

    def test_prefabs(self):
        anchor = bellows.create_entity("Prefab Anchor")
        anchor.position = Vec3(0.0, 0.0, -30.0)
        ball = bellows.instantiate("Prefabs/Ball.bprefab", Vec3(0.0, 5.0, 0.0), anchor)
        self.check(ball.parent == anchor and ball.has_component("RigidBody") and ball.has_component("SphereCollider"), "instantiate a prefab under a parent")
        self.check(close(ball.local_position, (0.0, 5.0, 0.0)) and close(ball.position, (0.0, 5.0, -30.0)), "prefab position")
        try:
            bellows.instantiate("Prefabs/Missing.bprefab")
            self.check(False, "instantiating a missing prefab raises")
        except ValueError:
            self.check(True, "instantiating a missing prefab raises")
        yield
        anchor.destroy()

    def test_physics(self):
        # A ball dropped onto the ground comes to rest on it.
        ball = bellows.instantiate("Prefabs/Ball.bprefab", Vec3(6.0, 4.0, 6.0))
        ball.name = "Drop Test Ball"
        body = ball.get_component(bellows.RigidBody)
        yield from self.wait_until(lambda: ball.script is not None and "Ground" in ball.script.hits, 4.0)
        self.check("Ground" in ball.script.hits, f"collision callbacks report the ground ({ball.script.hits})")
        yield from self.wait_seconds(1.5)
        radius = ball.get_component(bellows.SphereCollider).radius * ball.scale.x
        self.check(close(ball.position.y, radius, 0.05), f"the ball rests on the ground (y = {ball.position.y:.3f})")

        # Velocity, impulses, forces and torque.
        body.velocity = Vec3(0.0, 5.0, 0.0)
        self.check(close(body.velocity, (0.0, 5.0, 0.0), 1e-3), "RigidBody.velocity")
        yield from self.wait_frames(2)
        self.check(ball.position.y > radius + 0.05, "velocity moves the body")
        yield from self.wait_seconds(1.5)
        start = ball.position
        body.add_impulse(Vec3(2.0 * body.mass, 0.0, 0.0))
        yield from self.wait_frames(3)
        self.check(ball.position.x > start.x + 0.02, "add_impulse pushes the body")
        body.velocity = Vec3(0.0)
        body.angular_velocity = Vec3(0.0, 4.0, 0.0)
        self.check(close(body.angular_velocity, (0.0, 4.0, 0.0), 1e-3), "RigidBody.angular_velocity")
        body.angular_velocity = Vec3(0.0)
        before = body.angular_velocity.length()
        for _ in range(5):
            body.add_torque(Vec3(0.0, 5.0 * body.mass, 0.0))
            yield
        self.check(body.angular_velocity.length() > before, "add_torque spins the body")
        body.gravity_scale = 0.0
        body.velocity = Vec3(0.0)
        body.teleport(Vec3(6.0, 3.0, 6.0))
        yield
        for _ in range(10):
            body.add_force(Vec3(0.0, 0.0, 30.0 * body.mass))
            yield
        self.check(ball.position.z > 6.02 and close(ball.position.y, 3.0, 0.1), f"add_force and teleport ({ball.position})")

        # Gravity can be changed at runtime.
        bellows.set_gravity(Vec3(0.0, -20.0, 0.0))
        self.check(close(bellows.get_gravity(), (0.0, -20.0, 0.0)), "set_gravity")
        bellows.set_gravity(Vec3(0.0, -9.81, 0.0))
        ball.destroy()

    def test_collisions_and_triggers(self):
        zone_entity = bellows.find_entity("Trigger Zone")
        zone = zone_entity.script
        visitor = bellows.instantiate("Prefabs/Ball.bprefab", zone_entity.position + Vec3(0.0, 6.0, 0.0))
        visitor.name = "Trigger Visitor"
        yield from self.wait_until(lambda: "Trigger Visitor" in zone.entered, 4.0)
        self.check("Trigger Visitor" in zone.entered, f"on_trigger_enter ({zone.entered})")
        visitor.get_component(bellows.RigidBody).teleport(zone_entity.position + Vec3(10.0, 2.0, 0.0))
        yield from self.wait_until(lambda: "Trigger Visitor" in zone.exited, 2.0)
        self.check("Trigger Visitor" in zone.exited, f"on_trigger_exit ({zone.exited})")
        visitor.destroy()

    def test_raycasts(self):
        yield
        hit = bellows.raycast(Vec3(-6.0, 10.0, 6.0), Vec3(0.0, -1.0, 0.0), 100.0)
        self.check(hit is not None and hit.entity is not None and hit.entity.name == "Ground", f"raycast hits the ground ({hit})")
        if hit is not None:
            self.check(close(hit.distance, 10.0, 0.01) and close(hit.point, (-6.0, 0.0, 6.0), 0.01), "raycast distance and point")
            self.check(close(hit.normal, (0.0, 1.0, 0.0), 1e-3) and "RaycastHit" in repr(hit), "raycast normal")
        self.check(bellows.raycast(Vec3(0.0, 50.0, 0.0), Vec3(0.0, 1.0, 0.0), 10.0) is None, "raycast misses return None")

    def test_gameplay_scripts(self):
        cannon = bellows.find_entity("Cannon").script
        yield from self.wait_until(lambda: cannon.fired >= 2, 5.0)
        self.check(cannon.fired >= 2, f"the cannon fires prefabs ({cannon.fired})")
        orbiters = [e for e in bellows.get_entities(bellows.PointLight) if e.get_component(bellows.ScriptComponent) is not None]
        start = [e.position for e in orbiters]
        yield from self.wait_frames(5)
        self.check(orbiters and all(not close(a, e.position, 1e-4) for a, e in zip(start, orbiters)), "orbiting lights move")
        bottle = bellows.find_entity("Water Bottle")
        rotation = bottle.rotation
        yield from self.wait_frames(3)
        self.check(not close(rotation, bottle.rotation, 1e-4), "the spinner rotates the bottle")
