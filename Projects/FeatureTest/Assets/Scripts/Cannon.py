import bellows


class Cannon(bellows.Script):
    """Fires a ball prefab along its forward axis at a fixed interval."""

    prefab: str = "Prefabs/Ball.bprefab"
    interval: float = 1.5
    launch_speed: float = 14.0
    lifetime: float = 8.0
    max_balls: int = 12

    def on_create(self):
        self.timer = 0.5
        self.balls = []                      # (entity, time spawned)
        self.fired = 0

    def on_update(self, dt: float):
        now = bellows.time()
        for ball, spawned in list(self.balls):
            if not ball.valid or now - spawned > self.lifetime:
                ball.destroy()
                self.balls.remove((ball, spawned))

        self.timer -= dt
        if self.timer <= 0.0 and len(self.balls) < self.max_balls:
            self.timer = self.interval
            self.fire()

    def fire(self):
        muzzle = self.entity.position + self.entity.forward * 1.2
        ball = bellows.instantiate(self.prefab, muzzle)
        body = ball.get_component(bellows.RigidBody)
        body.velocity = self.entity.forward * self.launch_speed
        self.balls.append((ball, bellows.time()))
        self.fired += 1
