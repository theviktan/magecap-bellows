import math

import bellows


class Orbit(bellows.Script):
    """Moves the entity in a horizontal circle (used for the colored lights)."""

    radius: float = 3.0
    speed: float = 0.6                       # Radians per second
    phase: float = 0.0
    height: float = 1.5
    center: bellows.Vec3 = bellows.Vec3(0.0, 0.0, 0.0)

    def on_update(self, dt: float):
        angle = self.phase + bellows.time() * self.speed
        self.entity.position = self.center + bellows.Vec3(math.cos(angle) * self.radius, self.height, math.sin(angle) * self.radius)
