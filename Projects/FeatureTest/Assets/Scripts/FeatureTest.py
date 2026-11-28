"""Exercises the complete MageCap Bellows scripting API inside the FeatureTest scene.

Attach to one entity of FeatureTest.bscene. The test runs over several frames (physics and
triggers need simulated time), logs every check and finally logs "FEATURE TEST PASSED" or
"FEATURE TEST FAILED". When the environment variable BELLOWS_FEATURE_TEST=1 is set (automated
runs), the game quits afterwards with exit code 0 (pass) or 1 (fail).
"""

import importlib
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
        self.test_groups()
        self.test_components()
        self.test_materials()
        self.test_scripts()
        self.test_camera_projection()
        self.test_audio()
        self.test_screenshots()
        self.test_saved_data()
        self.test_events()
        self.test_ui_focus()
        self.test_navigation()
        yield from self.test_nav_agents()
        yield from self.test_field_changed()
        yield from self.test_destruction()
        yield from self.test_prefabs()
        yield from self.test_physics()
        yield from self.test_collisions_and_triggers()
        yield from self.test_raycasts()
        yield from self.test_gameplay_scripts()
        yield from self.test_tweens()
        yield from self.test_animation()

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
        # Resumed right away, so the rest of the test keeps running.
        self.check(not bellows.is_paused(), "the game starts unpaused")
        bellows.set_paused(True)
        paused = bellows.is_paused()
        bellows.set_paused(False)
        self.check(paused and not bellows.is_paused(), "set_paused and is_paused")
        # The editor's runs have a 10-second limit and a shipped game none; a game can set its own.
        limit = bellows.python_time_limit()
        self.check(limit in (0.0, 10.0), f"python_time_limit ({limit})")
        bellows.set_python_time_limit(30)
        raised = bellows.python_time_limit()
        bellows.set_python_time_limit(limit)
        self.check(raised == 30.0 and bellows.python_time_limit() == limit, "set_python_time_limit")
        self.check(raises(lambda: bellows.set_python_time_limit(-1), ValueError), "a negative time limit is refused")
        camera = bellows.primary_camera()
        before = camera.position if camera else None
        bellows.shake_camera(0.5, 0.2)
        self.check(camera is None or camera.position == before, "shake_camera moves the picture, not the camera's Transform")
        self.check(raises(lambda: bellows.shake_camera(11), ValueError) and raises(lambda: bellows.shake_camera(1, -1), ValueError), "camera shake limits")
        # No device input is delivered in automated runs, but every query must work.
        self.check(not bellows.is_key_down(Key.Space) and not bellows.is_key_pressed(Key.W) and not bellows.is_key_released(Key.Escape), "keyboard queries")
        self.check(not bellows.is_key_down("A"), "keys by name")
        ui_actions = ["ui_accept", "ui_cancel", "ui_down", "ui_left", "ui_next", "ui_previous", "ui_right", "ui_up"]
        self.check(bellows.action_names() == ["fire", "jump"] + ui_actions,
                   f"action_names lists the project's actions and the UI's ({bellows.action_names()})")
        self.check(not bellows.ui_wants_pointer() and not bellows.ui_wants_keyboard(), "with no UI under the pointer, input is the game's")
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

        self.check(parent.keep_across_scenes is False, "entities aren't kept across scenes unless marked")
        parent.keep_across_scenes = True
        self.check(parent.keep_across_scenes, "keep_across_scenes")
        parent.keep_across_scenes = False

        self.check(parent.tags == [] and not parent.has_tag("enemy"), "entities start with no tags")
        self.check(parent.add_tag(" enemy ") and not parent.add_tag("enemy") and child.add_tag("enemy"), "add_tag trims and adds each tag once")
        self.check(parent.add_tag("boss") and parent.tags == ["enemy", "boss"] and parent.has_tag("boss"), "tags keep their order")
        tagged = bellows.find_tagged("enemy")
        self.check([e.name for e in tagged] == ["Renamed Parent", parent.children[0].name], f"find_tagged in hierarchy order ({[e.name for e in tagged]})")
        self.check(parent.remove_tag("boss") and not parent.remove_tag("boss") and parent.tags == ["enemy"], "remove_tag")
        self.check(raises(lambda: parent.add_tag("  "), ValueError) and raises(lambda: parent.add_tag("x" * 65), ValueError), "bad tags are refused")
        self.check(bellows.find_tagged("nobody") == [], "find_tagged with no matches")
        parent.remove_tag("enemy")
        child.remove_tag("enemy")

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

    def test_groups(self):
        """Hierarchy groups: top-level entities filed under named folders; children follow them."""
        self.check(bellows.group("Test Bays") is None, "group() finds nothing before it exists")
        bays = bellows.create_group(" Test Bays ", "anchor")
        self.check(bays.name == "Test Bays" and bays.icon == "anchor" and bays.valid, "create_group trims the name and sets the icon")
        self.check(bellows.group("test bays") == bays and bays in bellows.groups(), "groups are found in any letter case")
        self.check(raises(lambda: bellows.create_group("TEST BAYS"), ValueError), "a name another group has is refused")
        self.check(raises(lambda: bellows.create_group("  "), ValueError) and raises(lambda: bellows.create_group("Odd", "no-icon"), ValueError),
                   "bad names and icons are refused")

        bay = bellows.create_entity("Test Bay")
        arm = bellows.create_entity("Test Bay Arm", bay)
        self.check(bay.group is None and not bay.is_in_group("Test Bays"), "entities start in no group")
        self.check(bays.add_entity(bay) and not bays.add_entity(bay), "add_entity files a top-level entity once")
        self.check(bay.group == bays and arm.group == bays and arm.is_in_group("TEST BAYS") and bays.has_entity(arm), "a child follows its parent's group")
        self.check(bays.entities() == [bay] and bays.all_entities() == [bay, arm], "entities and all_entities")
        self.check(raises(lambda: bays.add_entity(arm), ValueError), "a child can't be filed by itself")

        bays.note = "Where ships dock"
        bays.color = Vec3(2.0, 0.5, -1.0)
        bays.name = "Test Docks"
        self.check(bays.note == "Where ships dock" and close(bays.color, (1.0, 0.5, 0.0)) and bellows.group("Test Docks") == bays,
                   "note, color (kept from 0 to 1) and renaming")
        self.check(raises(lambda: setattr(bays, "icon", "nope"), ValueError) and raises(lambda: setattr(bays, "note", "n" * 201), ValueError),
                   "bad icons and long notes are refused")

        self.check(not bays.hidden_in_editor and not bays.locked_in_editor, "groups start shown and unlocked in the editor")
        bays.hidden_in_editor = True
        bays.locked_in_editor = True
        self.check(bays.hidden_in_editor and bays.locked_in_editor and bay.valid, "hidden_in_editor and locked_in_editor (the game still has the entities)")
        bays.hidden_in_editor = False
        bays.locked_in_editor = False

        spare = bellows.create_group("Test Spare")
        bay.group = spare
        self.check(spare.entities() == [bay] and bays.entities() == [], "setting entity.group moves it between groups")
        self.check(not bays.remove_entity(bay) and spare.remove_entity(bay) and bay.group is None, "remove_entity")

        spare.delete()
        self.check(not spare.valid and raises(lambda: spare.name, RuntimeError), "a deleted group's handle says so")
        bays.add_entity(bay)
        bays.delete()
        self.check(bay.valid and bay.group is None and bellows.group("Test Docks") is None, "deleting a group leaves its entities in none")
        bay.destroy()

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
        animator = entity.add_component(bellows.Animator)
        self.check(animator.start_clip == "" and animator.play_on_start and animator.loop and close(animator.speed, 1.0), "Animator defaults")
        animator.start_clip, animator.play_on_start, animator.loop, animator.speed = "Run", False, False, -2.0
        self.check(animator.start_clip == "Run" and not animator.play_on_start and not animator.loop and close(animator.speed, -2.0),
                   "Animator properties")
        self.check(raises(lambda: setattr(animator, "speed", 11.0), ValueError) and raises(lambda: setattr(animator, "speed", float("nan")), ValueError),
                   "Animator speed limits")
        # The test entity's sphere has no skeleton: nothing to play, and no clips or bones.
        self.check(animator.clips == [] and animator.bones == [] and animator.clip == "" and not animator.playing, "an Animator without a skeleton")
        self.check(raises(lambda: animator.play("Run"), ValueError), "an Animator without a skeleton can't play")
        agent = entity.add_component(bellows.NavAgent)
        self.check(close(agent.speed, 3.5) and close(agent.acceleration, 8.0) and close(agent.radius, 0.4) and close(agent.height, 1.8)
                   and close(agent.base_offset, 0.0) and close(agent.stopping_distance, 0.1) and close(agent.turn_speed, 720.0) and agent.avoid_others,
                   "NavAgent defaults")
        agent.speed, agent.acceleration, agent.radius, agent.height = 2.0, 20.0, 0.3, 1.2
        agent.base_offset, agent.stopping_distance, agent.turn_speed, agent.avoid_others = 0.6, 0.5, 0.0, False
        self.check(close(agent.speed, 2.0) and close(agent.acceleration, 20.0) and close(agent.radius, 0.3) and close(agent.height, 1.2)
                   and close(agent.base_offset, 0.6) and close(agent.stopping_distance, 0.5) and close(agent.turn_speed, 0.0) and not agent.avoid_others,
                   "NavAgent properties")
        self.check(raises(lambda: setattr(agent, "speed", 101.0), ValueError) and raises(lambda: setattr(agent, "radius", 0.0), ValueError)
                   and raises(lambda: setattr(agent, "base_offset", -21.0), ValueError) and raises(lambda: setattr(agent, "height", float("nan")), ValueError),
                   "NavAgent limits")
        self.check(close(agent.velocity, (0.0, 0.0, 0.0)), "a NavAgent with nowhere to go stands still")
        listener = entity.add_component(bellows.AudioListener)
        listener.active = False
        self.check(not listener.active, "AudioListener properties")
        emitter = entity.add_component(bellows.ParticleEmitter)
        self.check(emitter.play_on_start and emitter.looping and close(emitter.rate, 20.0) and emitter.burst == 0 and emitter.max_particles == 1000
                   and emitter.shape == "point" and emitter.blend == "additive" and emitter.texture == "" and emitter.world_space,
                   "ParticleEmitter defaults")
        self.check(emitter.is_playing, "a ParticleEmitter that plays on start counts as playing before its first frame")
        emitter.duration, emitter.rate, emitter.burst, emitter.max_particles = 2.0, 50.0, 12, 300
        emitter.lifetime, emitter.lifetime_variation, emitter.speed, emitter.speed_variation = 1.5, 0.1, 4.0, 0.5
        emitter.spread_angle, emitter.shape, emitter.shape_size = 90.0, "box", Vec3(1.0, 0.5, 2.0)
        emitter.gravity_scale, emitter.drag, emitter.start_size, emitter.end_size = -0.5, 0.25, 0.4, 0.1
        emitter.start_color, emitter.end_color = Vec4(0.2, 0.4, 0.6, 1.0), Vec4(0.1, 0.1, 0.1, 0.0)
        emitter.intensity, emitter.blend, emitter.texture, emitter.world_space = 3.0, "alpha", "Textures/GroundTiles.png", False
        emitter.play_on_start, emitter.looping = False, False
        self.check(close(emitter.duration, 2.0) and close(emitter.rate, 50.0) and emitter.burst == 12 and emitter.max_particles == 300
                   and close(emitter.lifetime, 1.5) and close(emitter.lifetime_variation, 0.1) and close(emitter.speed, 4.0)
                   and close(emitter.speed_variation, 0.5) and close(emitter.spread_angle, 90.0) and emitter.shape == "box"
                   and close(emitter.shape_size, (1.0, 0.5, 2.0)) and close(emitter.gravity_scale, -0.5) and close(emitter.drag, 0.25)
                   and close(emitter.start_size, 0.4) and close(emitter.end_size, 0.1) and close(emitter.start_color, (0.2, 0.4, 0.6, 1.0))
                   and close(emitter.end_color, (0.1, 0.1, 0.1, 0.0)) and close(emitter.intensity, 3.0) and emitter.blend == "alpha"
                   and emitter.texture == "Textures/GroundTiles.png" and not emitter.world_space and not emitter.play_on_start and not emitter.looping,
                   "ParticleEmitter properties")
        self.check(raises(lambda: setattr(emitter, "rate", -1.0), ValueError) and raises(lambda: setattr(emitter, "spread_angle", 181.0), ValueError)
                   and raises(lambda: setattr(emitter, "max_particles", 10001), ValueError) and raises(lambda: setattr(emitter, "shape", "donut"), ValueError)
                   and raises(lambda: setattr(emitter, "blend", "glow"), ValueError) and raises(lambda: setattr(emitter, "lifetime", float("nan")), ValueError)
                   and raises(lambda: emitter.emit(-1), ValueError),
                   "ParticleEmitter limits")
        self.check(not emitter.is_playing and emitter.particle_count == 0, "a ParticleEmitter that doesn't play on start waits")
        emitter.play()
        self.check(emitter.is_playing, "ParticleEmitter.play")
        emitter.emit(5)
        emitter.stop(clear=True)
        self.check(not emitter.is_playing and emitter.particle_count == 0, "ParticleEmitter.stop")

        text = entity.add_component(bellows.Text)
        self.check(text.text == "Text" and text.font == "" and text.alignment == "center", "Text defaults")
        text.text, text.size, text.color = "Score: 10\nLives: 3", 0.25, (1.0, 0.8, 0.2, 1.0)
        text.alignment, text.vertical_alignment, text.overflow = "right", "bottom", "ellipsis"
        text.box_size, text.wrap, text.line_spacing = Vec3(2.0, 1.0, 0.0), False, 1.5
        text.outline_width, text.outline_color, text.face_camera = 0.5, (0.0, 0.0, 0.0, 1.0), True
        self.check(text.text == "Score: 10\nLives: 3" and close(text.size, 0.25) and close(text.color, (1.0, 0.8, 0.2, 1.0)), "Text properties")
        self.check(text.alignment == "right" and text.vertical_alignment == "bottom" and text.overflow == "ellipsis", "Text alignment and overflow")
        self.check(close(text.box_size, (2.0, 1.0, 0.0)) and not text.wrap and close(text.line_spacing, 1.5), "Text box")
        self.check(close(text.outline_width, 0.1) and text.face_camera and text.visible, "Text outline (clamped to 0.1) and facing")
        self.check(not text.theme_color, "Text.theme_color is off by default")
        text.theme_color = True
        self.check(text.theme_color, "Text.theme_color")
        text.theme_color = False
        measured = text.measured_size
        self.check(measured.x > 0.5 and close(measured.y, text.size * 1.5 * 2.0, 0.2), f"Text.measured_size ({measured})")
        self.check(raises(lambda: setattr(text, "alignment", "justify"), ValueError), "Text rejects unknown alignments")
        self.check(raises(lambda: setattr(text, "overflow", "scroll"), ValueError), "Text rejects unknown overflow")

        canvas = entity.add_component(bellows.UICanvas)
        self.check(close(canvas.reference_size, (1920.0, 1080.0, 0.0)) and canvas.scale_mode == "expand" and not canvas.modal, "UICanvas defaults")
        canvas.reference_size, canvas.scale_mode, canvas.sort_order = Vec3(1280.0, 720.0, 0.0), "constant_pixels", 4
        self.check(close(canvas.reference_size, (1280.0, 720.0, 0.0)) and canvas.sort_order == 4 and canvas.visible, "UICanvas properties")
        self.check(close(canvas.scale, 1.0), f"UICanvas.scale with constant pixels ({canvas.scale})")
        canvas.scale_mode = "fit_height"
        self.check(canvas.scale > 0.0, f"UICanvas.scale fitting the height ({canvas.scale})")
        self.check(raises(lambda: setattr(canvas, "scale_mode", "stretch"), ValueError), "UICanvas rejects unknown scale modes")

        rect = entity.add_component(bellows.UIRect)
        self.check(close(rect.anchor_min, (0.5, 0.5, 0.0)) and close(rect.size, (200.0, 50.0, 0.0)) and rect.interactable and not rect.focusable, "UIRect defaults")
        rect.anchor_min, rect.anchor_max, rect.pivot = Vec3(0.0, 1.0, 0.0), Vec3(2.0, 1.0, 0.0), Vec3(0.0, 1.0, 0.0)
        self.check(close(rect.anchor_max, (1.0, 1.0, 0.0)), "UIRect anchors are clamped to 0-1")
        rect.offset_min, rect.offset_max = Vec3(10.0, -60.0, 0.0), Vec3(-10.0, -10.0, 0.0)
        self.check(close(rect.position, (10.0, -10.0, 0.0)) and close(rect.size, (-20.0, 50.0, 0.0)), "UIRect position and size from offsets")
        rect.size = Vec3(0.0, 80.0, 0.0)
        self.check(close(rect.offset_min, (10.0, -90.0, 0.0)) and close(rect.offset_max, (10.0, -10.0, 0.0)), "UIRect.size keeps the pivot in place")
        rect.position = Vec3(20.0, -20.0, 0.0)
        self.check(close(rect.offset_min, (20.0, -100.0, 0.0)) and close(rect.offset_max, (20.0, -20.0, 0.0)), "UIRect.position moves both offsets")
        rect.visible, rect.interactable = False, False
        self.check(not rect.visible and not rect.interactable, "UIRect visibility")
        self.check(canvas.theme == "" and rect.style_class == "", "UICanvas.theme and UIRect.style_class default to the plain built-in look")
        canvas.theme, rect.style_class = "Builtin/MageCap.btheme", "primary"
        self.check(canvas.theme == "Builtin/MageCap.btheme" and rect.style_class == "primary", "UICanvas.theme and UIRect.style_class")
        canvas.theme, rect.style_class = "", ""

        image = entity.add_component(bellows.UIImage)
        image.texture, image.color, image.corner_radius = "Textures/GroundTiles.png", (0.1, 0.2, 0.3, 0.5), -4.0
        image.border_width, image.border_color = 3.0, (1.0, 0.5, 0.0, 1.0)
        self.check(image.texture == "Textures/GroundTiles.png" and close(image.color, (0.1, 0.2, 0.3, 0.5)), "UIImage properties")
        self.check(close(image.corner_radius, 0.0) and close(image.border_width, 3.0) and close(image.border_color, (1.0, 0.5, 0.0, 1.0)),
                   "UIImage corners (no negative radius) and border")
        self.check(image.slice_border == [0.0, 0.0, 0.0, 0.0], "UIImage slice_border default")
        image.slice_border = [4, 6, 4, -2]
        self.check(close(image.slice_border, (4.0, 6.0, 4.0, 0.0)), "UIImage slice_border (no negative edges)")
        self.check(raises(lambda: setattr(image, "slice_border", [1, 2, 3]), ValueError), "slice_border needs 4 numbers")
        self.check(rect.width_sizing == "fixed" and rect.height_sizing == "fixed", "UIRect sizing defaults")
        rect.width_sizing = "fill"
        self.check(rect.width_sizing == "fill", "UIRect.width_sizing")
        self.check(raises(lambda: setattr(rect, "height_sizing", "stretch"), ValueError), "UIRect rejects unknown sizings")

        layout = entity.add_component(bellows.UILayout)
        self.check(layout.direction == "vertical" and close(layout.spacing, 8.0) and layout.columns == 2, "UILayout defaults")
        layout.direction, layout.spacing, layout.padding = "grid", 12.0, [10, 20, 10, 20]
        layout.main_alignment, layout.cross_alignment, layout.columns = "center", "end", 0
        self.check(layout.direction == "grid" and close(layout.padding, (10.0, 20.0, 10.0, 20.0)), "UILayout properties")
        self.check(layout.main_alignment == "center" and layout.cross_alignment == "end" and layout.columns == 1,
                   "UILayout alignments and columns (at least 1)")
        self.check(raises(lambda: setattr(layout, "direction", "diagonal"), ValueError), "UILayout rejects unknown directions")
        self.check(raises(lambda: setattr(layout, "main_alignment", "middle"), ValueError), "UILayout rejects unknown alignments")

        scroll = entity.add_component(bellows.UIScroll)
        self.check(scroll.vertical and not scroll.horizontal and scroll.show_scrollbars and close(scroll.wheel_speed, 60.0), "UIScroll defaults")
        scroll.horizontal, scroll.scroll_position, scroll.wheel_speed = True, Vec3(-5.0, 40.0, 0.0), 30.0
        self.check(scroll.horizontal and close(scroll.scroll_position, (0.0, 40.0, 0.0)) and close(scroll.wheel_speed, 30.0), "UIScroll properties")
        self.check(isinstance(scroll.content_size, Vec3) and isinstance(scroll.viewport_size, Vec3), "UIScroll content and viewport sizes")

        button = entity.add_component(bellows.UIButton)
        self.check(isinstance(button.normal_color, Vec4) and button.disabled_color.w < 1.0, "UIButton defaults come from the theme")
        themed_hover = button.hover_color
        button.hover_color = Vec4(1.0, 0.0, 0.0, 1.0)
        self.check(close(button.hover_color, (1.0, 0.0, 0.0, 1.0)), "UIButton properties")
        button.clear_colors()
        self.check(close(button.hover_color, (themed_hover.x, themed_hover.y, themed_hover.z, themed_hover.w)), "UIButton.clear_colors goes back to the theme")

        toggle = entity.add_component(bellows.UIToggle)
        self.check(not toggle.on, "UIToggle defaults")
        toggle.on = True
        self.check(toggle.on and entity.value is True, "UIToggle properties and Entity.value")

        slider = entity.add_component(bellows.UISlider)
        self.check(close(slider.min, 0.0) and close(slider.max, 1.0) and close(slider.step, 0.0) and close(slider.value, 0.0), "UISlider defaults")
        slider.max, slider.step, slider.value = 10.0, 2.0, 5.2
        self.check(close(slider.value, 6.0), f"UISlider snaps to its step ({slider.value})")
        slider.value = 50.0
        self.check(close(slider.value, 10.0), "UISlider keeps between min and max")
        slider.step = -1.0
        self.check(close(slider.step, 0.0), "UISlider steps are never negative")

        dropdown = entity.add_component(bellows.UIDropdown)
        self.check(dropdown.options == [] and dropdown.selected == 0 and dropdown.selected_text == "", "UIDropdown defaults")
        dropdown.options = ["Low", "Medium", "High"]
        dropdown.selected = 2
        self.check(dropdown.selected_text == "High", "UIDropdown properties")
        self.check(raises(lambda: setattr(dropdown, "selected", 3), ValueError), "UIDropdown rejects an index past its options")
        dropdown.options = ["Only"]
        self.check(dropdown.selected == 0, "UIDropdown keeps its selection inside new options")

        text_input = entity.add_component(bellows.UITextInput)
        self.check(text_input.placeholder == "" and not text_input.multi_line and not text_input.password
                   and text_input.max_length == 0, "UITextInput defaults")
        text_input.placeholder, text_input.multi_line, text_input.password, text_input.max_length = "Name", True, True, 8
        self.check(text_input.placeholder == "Name" and text_input.multi_line and text_input.password
                   and text_input.max_length == 8, "UITextInput properties")
        self.check(raises(lambda: setattr(text_input, "max_length", -1), ValueError), "UITextInput rejects a negative max_length")

        script = entity.add_component(bellows.ScriptComponent)
        script.path, script.class_name = "Scripts/Spinner.py", "Spinner"
        self.check(not script.run_while_paused, "scripts hold while the game is paused unless marked")
        script.run_while_paused = True
        self.check(script.path == "Scripts/Spinner.py" and script.class_name == "Spinner" and script.run_while_paused, "ScriptComponent properties")

        expected = ["Transform", "MeshRenderer", "Camera", "PointLight", "SpotLight", "RigidBody", "BoxCollider", "SphereCollider",
                    "CapsuleCollider", "MeshCollider", "Script", "AudioSource", "Animator", "NavAgent", "AudioListener", "Text", "ParticleEmitter", "UICanvas", "UIRect", "UIImage",
                    "UILayout", "UIScroll", "UIButton", "UIToggle", "UISlider", "UIDropdown", "UITextInput"]
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
        self.check(material.texture_filter == "Linear" and material.wrap_u == "Repeat" and material.wrap_v == "Repeat", "Material sampling defaults")
        material.texture_filter, material.wrap_u, material.wrap_v = "Nearest", "Clamp", "Mirror"
        self.check(material.texture_filter == "Nearest" and material.wrap_u == "Clamp" and material.wrap_v == "Mirror", "Material texture filter and wraps")
        self.check(raises(lambda: setattr(material, "texture_filter", "Bicubic"), ValueError)
                   and raises(lambda: setattr(material, "wrap_u", "Tile"), ValueError), "unknown texture filters and wraps are refused")
        self.check(not material.unlit, "Material is lit by default")
        material.unlit = True
        self.check(material.unlit, "Material unlit")
        material.unlit = False
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
        self.check(bellows.log_folder() == os.path.join(folder, "Logs"), f"log_folder ({bellows.log_folder()})")
        try:
            importlib.import_module("bellows.editor")  # Only the editor has it
            in_editor = True
        except ImportError:
            in_editor = False
        self.check(bellows.started_from_editor() == in_editor, f"started_from_editor ({bellows.started_from_editor()}, in the editor: {in_editor})")
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
        data = bellows.load_json("Data/FeatureData.json")
        self.check(data["name"] == "FeatureTest data" and data["hard_mode"] is False and data["secret"] is None, "load_json reads a data file")
        self.check([level["title"] for level in data["levels"]] == ["Meadow", "Caves"] and data["levels"][1]["enemies"] == 7, "load_json's lists and dicts")
        self.check(bellows.load_text("Data\\FeatureData.json").startswith("{"), "load_text, with either slash")
        self.check(raises(lambda: bellows.load_json("../FeatureTest.bproject"), ValueError), "data files can't be read from outside Assets")
        self.check(raises(lambda: bellows.load_json("Data/Missing.json"), ValueError), "a missing data file raises ValueError")
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
        bellows.emit("feature test vectors", Vec2(1, 2), Vec4(0.5, 0.25, 0, 1))
        self.check(self.events[-1] == ("feature test vectors", ([1.0, 2.0], [0.5, 0.25, 0.0, 1.0])),
                   "emit sends Vec2 and Vec4 as lists of numbers")

    def test_ui_focus(self):
        # A small menu under this test's own entity: UI events climb through non-UI parents to its script.
        menu = bellows.create_entity("Focus Test Menu", parent=self.entity)
        canvas = menu.add_component(bellows.UICanvas)
        button = bellows.create_entity("Focus Test Button", parent=menu)
        rect = button.add_component(bellows.UIRect)
        self.check(not rect.focus(), "a rect that is not focusable cannot take focus")
        rect.focusable = True
        self.check(rect.focus() and rect.focused and bellows.ui_focused() == button, "UIRect.focus gives it focus")
        self.check(self.events and self.events[-1] == ("focused", (button,)), f"focused reaches the scripts above the button ({self.events[-1:]})")
        self.check(not rect.hovered and not rect.pressed, "UIRect hovered and pressed")
        canvas.modal = True
        self.check(canvas.modal, "UICanvas.modal")
        canvas.visible = False
        self.check(not rect.focus(), "hidden UI cannot take focus")
        bellows.ui_clear_focus()
        self.check(bellows.ui_focused() is None and self.events[-1] == ("unfocused", (button,)), "ui_clear_focus")
        self.check(bellows.ui_hovered() is None, "ui_hovered")

        # The everyday UI properties on Entity, and the error that names an entity lacking the component.
        canvas.visible, canvas.modal = True, False
        self.check(menu.visible, "Entity.visible reads a canvas")
        button.visible = False
        self.check(not rect.visible and not button.visible, "Entity.visible sets the UIRect")
        button.visible, button.interactable = True, False
        self.check(not rect.interactable and not button.interactable, "Entity.interactable")
        self.check(not button.focus(), "Entity.focus refuses UI that isn't interactable")
        button.interactable = True
        self.check(button.focus() and bellows.ui_focused() == button, "Entity.focus")
        button.add_component("Text")
        button.text = "Play"
        self.check(button.text == "Play" and button.get_component("Text").text == "Play", "Entity.text")
        try:
            self.entity.text
            self.check(False, "Entity.text needs a Text component")
        except RuntimeError as error:
            self.check(self.entity.name in str(error) and "Text" in str(error), f"Entity.text names the entity ({error})")
        self.check(raises(lambda: setattr(self.entity, "visible", False), RuntimeError), "Entity.visible needs UI")
        self.check(raises(lambda: self.entity.focus(), RuntimeError), "Entity.focus needs a UIRect")

        # Entity.value reads and sets a widget's value: a toggle's on, a slider's value, a dropdown's index.
        slider = button.add_component("UISlider")
        slider.max, slider.step = 100.0, 10.0
        button.value = 47.0
        self.check(close(button.value, 50.0) and close(slider.value, 50.0), f"Entity.value sets a slider ({button.value})")
        self.check(raises(lambda: setattr(button, "value", "loud"), ValueError), "a slider's value is a number")
        button.remove_component("UISlider")
        button.add_component("UIDropdown").options = ["Easy", "Hard"]
        button.value = 1
        self.check(button.value == 1.0 and button.get_component("UIDropdown").selected_text == "Hard", "Entity.value sets a dropdown")
        self.check(raises(lambda: setattr(button, "value", 1.5), ValueError), "a dropdown's value is a whole index")
        button.remove_component("UIDropdown")
        button.add_component("UIToggle")
        button.value = True
        self.check(button.value is True, "Entity.value sets a toggle")
        self.check(raises(lambda: setattr(button, "value", 1), ValueError), "a toggle's value is true or false")
        button.remove_component("UIToggle")
        field = button.add_component("UITextInput")
        field.max_length = 5
        button.value = "Hello\nWorld"
        self.check(button.value == "Hello" and button.text == "Hello", f"Entity.value sets a text input, kept to what typing would keep ({button.value!r})")
        self.check(raises(lambda: setattr(button, "value", 3), ValueError), "a text input's value is text")
        button.remove_component("UITextInput")
        try:
            self.entity.value
            self.check(False, "Entity.value needs a widget")
        except RuntimeError as error:
            self.check(self.entity.name in str(error) and "UISlider" in str(error), f"Entity.value names the entity ({error})")
        bellows.ui_clear_focus()
        menu.destroy()

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

    def test_tweens(self):
        box = bellows.create_entity("Tween Box")
        box.position = Vec3(0, 0, 0)
        move = bellows.tween(box, "position", Vec3(0, 3, 0), 0.5, "linear")
        self.check(isinstance(move, bellows.Tween) and move.property == "position" and move.entity == box, "tween returns a Tween handle")
        self.check(move.running and not move.finished, "a new tween is running")
        yield from self.wait_until(lambda: move.finished, 2.0)
        self.check(move.finished and not move.running and move.progress == 1.0, "a tween finishes")
        self.check(box.position.y == 3.0, "a tween lands exactly on its target")
        self.check(any(name == "tween finished" and values == (box, "position", float(move.id))
                       for name, values in self.events), "'tween finished' reaches on_event with the entity, property and id")

        spin = bellows.tween_by(box, "rotation", Vec3(0, 90, 0), 10.0, "out", 0.25)
        spin.repeat = 2
        spin.yoyo = True
        spin.while_paused = True
        self.check(spin.repeat == 2 and spin.yoyo and spin.while_paused, "a tween's repeat, yoyo and while_paused can be set")
        self.check(spin.pause() and spin.running and spin.resume(), "a tween pauses and resumes")
        self.check(spin.complete() and spin.finished and abs(box.rotation.y - 90.0) < 1e-3, "complete jumps to the end")

        image = bellows.create_entity("Tween Image")
        image.add_component("UIRect")
        image.add_component("UIImage")
        fade = bellows.tween(image, "UIImage.color", Vec4(1, 1, 1, 0), 5.0)
        self.check(fade.running, "colors tween from a Vec4")
        self.check(bellows.stop_tweens(image) == 1 and not fade.running and not fade.finished, "stop_tweens stops an entity's tweens")
        self.check(raises(lambda: bellows.tween(box, "name", 1, 1), ValueError), "only number and vector properties tween")
        self.check(raises(lambda: bellows.tween(box, "scale", Vec3(2), 1, "wobbly"), ValueError), "unknown easing styles are refused")
        self.check(raises(lambda: bellows.tween(box, "scale", Vec3(2), float("inf")), ValueError), "a tween's length must be finite")
        bellows.destroy(image)
        bellows.destroy(box)
        yield

    def test_animation(self):
        """The Dancer plays SkinnedColumn.glb's clips: "Bend" (2 s) turns its Top bone about Z to 90 degrees
        at 1 s and back; "Wave" (1 s) turns its Base about Y."""
        dancer = bellows.find_entity("Dancer")
        self.check(dancer is not None and dancer.has_component("Animator"), "the Dancer is in the scene")
        if dancer is None:
            return
        animator = dancer.get_component(bellows.Animator)
        self.check(animator.clips == ["Bend", "Wave"] and animator.bones == ["Base", "Top"], "Animator clips and bones")
        self.check(animator.clip == "Wave" and animator.playing and close(animator.length, 1.0), "the Dancer plays Wave (its start clip; FeatureTest.forge plays it again at its end)")
        self.check(raises(lambda: animator.play("Dance"), ValueError), "an unknown clip is refused")
        try:
            animator.play("Dance")
        except ValueError as error:
            self.check("Bend, Wave" in str(error), "the refusal lists the clips")
        self.check(raises(lambda: animator.bone_position("Hand"), ValueError), "an unknown bone is refused")
        self.check(raises(lambda: animator.play("Bend", -1.0), ValueError), "a negative fade is refused")

        # Bend, cut in, and held at 1 s: the Top bone has turned 90 degrees, and stays at the knee.
        animator.speed = 1.0
        animator.play("Bend", 0.0)
        self.check(animator.clip == "Bend" and close(animator.time, 0.0) and close(animator.length, 2.0), "play switches clip")
        animator.time = 1.0
        animator.pause()
        self.check(not animator.playing, "pause holds the clip")
        knee = dancer.position + bellows.Vec3(0.0, 1.0, 0.0)
        self.check(close(animator.bone_position("Top"), knee, 1e-3), "bone_position follows the pose")
        self.check(close(animator.bone_rotation("Top").z, 90.0, 0.05), "bone_rotation follows the pose")
        before = animator.time
        yield from self.wait_frames(3)
        self.check(close(animator.time, before), "a paused clip doesn't move")
        animator.resume()
        yield from self.wait_frames(3)
        self.check(animator.playing and animator.time > before, "resume lets it go on")

        # Playing the clip that's playing changes nothing; a new clip fades in.
        time_now = animator.time
        animator.play("Bend")
        self.check(close(animator.time, time_now), "playing the current clip changes nothing")
        animator.stop()
        self.check(not animator.playing and animator.clip == "Bend", "stop holds the pose")

        # A clip that doesn't loop ends once and says so.
        self.events.clear()
        animator.loop = False
        animator.play("Wave", 0.0)
        animator.time = 0.9
        yield from self.wait_until(lambda: not animator.playing, 2.0)
        yield from self.wait_frames(1)
        self.check(not animator.playing and close(animator.time, 1.0), "a clip that doesn't loop stops at its end")
        self.check([values for name, values in self.events if name == "animation finished"] == [(dancer, "Wave")],
                   "'animation finished' reaches on_event once, with the entity and the clip")
        animator.loop = True
        animator.play("Wave", 0.5)

    def test_field_changed(self):
        """The Water Bottle's Spinner has the Inspector field speed (60 in the scene)."""
        bottle = bellows.find_entity("Water Bottle")
        spinner = bottle.get_component(bellows.ScriptComponent).instance
        self.events.clear()
        spinner.speed = 75.0
        spinner.speed = 75.0
        spinner.axis = bellows.Vec3(0.0, 1.0, 0.0)
        spinner.not_a_field = 1
        yield from self.wait_frames(1)
        changes = [values for name, values in self.events if name == "field changed" and values[0] == bottle]
        self.check(changes == [(bottle, "speed", 75.0, 60.0)],
                   f"'field changed' once, with the entity, field, new and old values; not for the same value or other attributes ({changes})")
        spinner.speed = 60.0

    def test_nav_agents(self):
        """The Guard, a NavAgent carrying a kinematic capsule, walks the open ground in front of the camera (z = 7)."""
        guard = bellows.find_entity("Guard")
        agent = guard.get_component(bellows.NavAgent)
        # FeatureTest.forge halted it on its way: it comes to rest, and stays.
        yield from self.wait_until(lambda: agent.velocity.length() < 0.01, 2.0)
        self.check(not agent.moving and agent.destination is None and agent.velocity.length() < 0.01, "the Guard stands until it's sent")
        start = guard.position
        self.events.clear()
        self.check(agent.set_destination(bellows.Vec3(3.0, 0.0, 7.0)), "set_destination on open ground")
        self.check(agent.moving and close(agent.destination, bellows.Vec3(3.0, 0.0, 7.0), 0.2), f"destination ({agent.destination})")
        yield from self.wait_frames(20)
        self.check(agent.velocity.length() > 1.0 and agent.velocity.length() <= agent.speed * 1.05, f"it walks at its speed ({agent.velocity})")
        yield from self.wait_until(lambda: not agent.moving, 8.0)
        self.check(not agent.moving and close(guard.position, bellows.Vec3(3.0, 0.9, 7.0), 0.15),
                   f"the Guard walks there, its capsule on the ground ({guard.position})")
        reached = [values for name, values in self.events if name == "destination reached"]
        self.check(reached == [(guard, True)], f"'destination reached' with the entity and True ({reached})")
        self.check(not agent.set_destination(bellows.Vec3(0.0, 0.0, 500.0)) and agent.destination is None,
                   "set_destination refuses a point off the navmesh, and changes nothing")
        self.check(raises(lambda: agent.set_destination(bellows.Vec3(float("nan"), 0.0, 0.0)), ValueError), "set_destination refuses NaN")

        # Sent back, then stopped on the way: it slows to a stop and says nothing.
        self.events.clear()
        agent.set_destination(start)
        yield from self.wait_frames(20)
        agent.stop()
        self.check(not agent.moving and agent.destination is None, "stop")
        yield from self.wait_frames(60)
        self.check(agent.velocity.length() < 0.05 and not any(name == "destination reached" for name, _ in self.events),
                   "stopped, it comes to rest and sends no 'destination reached'")

    def test_navigation(self):
        """The scene's navmesh is baked from its static colliders; the pedestals (0.6 m, above a 0.4 m step) block the
        straight line from (0, 0, -5) to (0, 0, -1)."""
        start, end = bellows.Vec3(0.0, 0.0, -5.0), bellows.Vec3(0.0, 0.0, -1.0)
        path = bellows.find_path(start, end)
        self.check(len(path) >= 3 and close(path[0], start, 0.2) and close(path[-1], end, 0.2), f"find_path around a pedestal ({path})")
        length = sum((path[i] - path[i - 1]).length() for i in range(1, len(path)))
        self.check(length > 4.1, f"the path bends around the pedestal ({length:.2f} m)")
        self.check(all(bellows.is_navigable(corner) for corner in path), "every corner of the path is navigable")
        self.check(bellows.is_navigable(bellows.Vec3(3.0, 0.0, 3.0)) and not bellows.is_navigable(bellows.Vec3(0.0, 0.0, -3.0)),
                   "is_navigable: open ground, and not inside a pedestal")
        nearest = bellows.nearest_navigable_point(bellows.Vec3(0.0, 0.0, -3.0))
        self.check(nearest is not None and (nearest - bellows.Vec3(0.0, 0.0, -3.0)).length() > 0.8, f"nearest_navigable_point ({nearest})")
        self.check(bellows.nearest_navigable_point(bellows.Vec3(0.0, 0.0, 500.0)) is None, "nearest_navigable_point finds nothing far off the navmesh")
        self.check(raises(lambda: bellows.nearest_navigable_point(start, -1.0), ValueError)
                   and raises(lambda: bellows.nearest_navigable_point(start, float("nan")), ValueError), "nearest_navigable_point's search limits")
        self.check(bellows.find_path(start, bellows.Vec3(0.0, 0.0, 900.0)) == [], "find_path to nowhere gives an empty list")
        bellows.bake_navigation()
        self.check(len(bellows.find_path(start, end)) >= 3, "bake_navigation rebuilds the navmesh while the game runs")
