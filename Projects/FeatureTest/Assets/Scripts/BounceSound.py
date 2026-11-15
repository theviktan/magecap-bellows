import bellows


class BounceSound(bellows.Script):
    """Plays the entity's AudioSource on impact and remembers what it hit (read by the feature test)."""

    def on_create(self):
        self.hits = []

    def on_collision_enter(self, other):
        self.hits.append(other.name)
        source = self.entity.get_component(bellows.AudioSource)
        if source is not None:
            source.play()

    def on_collision_exit(self, other):
        pass
