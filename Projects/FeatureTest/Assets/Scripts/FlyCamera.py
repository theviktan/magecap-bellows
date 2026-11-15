import bellows
from bellows import Key, MouseButton


class FlyCamera(bellows.Script):
    """Free-flying camera: hold the right mouse button to look, WASD/QE to move, Shift to go faster."""

    move_speed: float = 6.0
    look_sensitivity: float = 0.15

    def on_create(self):
        self.yaw = self.entity.rotation.y
        self.pitch = self.entity.rotation.x

    def on_update(self, dt: float):
        if bellows.is_mouse_down(MouseButton.Right):
            delta = bellows.mouse_delta()
            self.yaw -= delta.x * self.look_sensitivity
            self.pitch = bellows.clamp(self.pitch - delta.y * self.look_sensitivity, -89.0, 89.0)
            self.entity.rotation = bellows.Vec3(self.pitch, self.yaw, 0.0)

        direction = bellows.Vec3(0.0)
        if bellows.is_key_down(Key.W): direction += self.entity.forward
        if bellows.is_key_down(Key.S): direction -= self.entity.forward
        if bellows.is_key_down(Key.D): direction += self.entity.right
        if bellows.is_key_down(Key.A): direction -= self.entity.right
        if bellows.is_key_down(Key.E): direction += bellows.Vec3(0.0, 1.0, 0.0)
        if bellows.is_key_down(Key.Q): direction -= bellows.Vec3(0.0, 1.0, 0.0)
        if direction.length() > 0.0:
            speed = self.move_speed * (3.0 if bellows.is_key_down(Key.LeftShift) else 1.0)
            self.entity.translate(direction.normalized() * (speed * dt))
