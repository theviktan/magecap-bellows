import bellows


class TriggerZone(bellows.Script):
    """Glows while something is inside the trigger volume."""

    idle_color: bellows.Vec4 = bellows.Vec4(0.2, 0.6, 1.0, 0.35)
    active_color: bellows.Vec4 = bellows.Vec4(1.0, 0.45, 0.1, 0.55)

    def on_create(self):
        self.inside = set()
        self.entered = []
        self.exited = []
        self.renderer = self.entity.get_component(bellows.MeshRenderer)
        self.material = bellows.Material("TriggerZone", self.idle_color, 0.0, 0.3)
        self.material.alpha_mode = "Blend"
        self.material.emissive = bellows.Vec3(self.idle_color.r, self.idle_color.g, self.idle_color.b)
        self.material.emissive_intensity = 0.5
        if self.renderer is not None:
            self.renderer.material = self.material

    def on_trigger_enter(self, other):
        self.inside.add(other.id)
        self.entered.append(other.name)
        self._refresh()

    def on_trigger_exit(self, other):
        self.inside.discard(other.id)
        self.exited.append(other.name)
        self._refresh()

    def _refresh(self):
        color = self.active_color if self.inside else self.idle_color
        self.material.color = color
        self.material.emissive = bellows.Vec3(color.r, color.g, color.b)
