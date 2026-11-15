import bellows


class Spinner(bellows.Script):
    """Spins the entity around an axis."""

    speed: float = 45.0                      # Degrees per second
    axis: bellows.Vec3 = bellows.Vec3(0.0, 1.0, 0.0)

    def on_update(self, dt: float):
        self.entity.rotate(self.axis * (self.speed * dt))
