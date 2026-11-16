"""MageCap Bellows scripting API.

Game logic lives in Python classes that derive from :class:`Script` and are attached to
entities with a Script component. A minimal script::

    import bellows

    class Spinner(bellows.Script):
        speed: float = 90.0          # Annotated class attributes appear in the editor

        def on_update(self, dt):
            self.entity.rotate((0.0, self.speed * dt, 0.0))

Everything else (entities, components, input, physics, audio) is exposed as module-level
functions and classes of this package; see ``docs/ScriptingAPI.md`` for the full reference.
"""

from _bellows import *  # noqa: F401,F403  (re-export the native API)
from _bellows import Entity as _Entity

__all__ = [name for name in dir() if not name.startswith("_")] + ["Script"]


class Script:
    """Base class for entity scripts.

    The engine sets ``self.entity`` before ``on_create`` runs. Override any of the callbacks:

    * ``on_create(self)``                 - once, after every script in the scene was created
    * ``on_update(self, dt)``             - every frame, ``dt`` in seconds
    * ``on_fixed_update(self, dt)``       - every physics step (fixed timestep)
    * ``on_destroy(self)``                - when the entity is destroyed or play stops
    * ``on_collision_enter(self, other)`` - started touching ``other`` (an Entity)
    * ``on_collision_exit(self, other)``
    * ``on_trigger_enter(self, other)``   - ``other`` entered this trigger (or this entered a trigger)
    * ``on_trigger_exit(self, other)``
    * ``on_reload(self)``                 - after the script's file was saved during play and its new
      code took over (the object and its attribute values stay; only the methods change)

    Class attributes with type annotations (bool, int, float, str, Vec2, Vec3, Vec4) are exposed
    as editable fields in the inspector and stored with the scene.
    """

    entity: _Entity

    def on_create(self):
        pass

    def on_update(self, dt):
        pass

    def on_fixed_update(self, dt):
        pass

    def on_destroy(self):
        pass

    def on_collision_enter(self, other):
        pass

    def on_collision_exit(self, other):
        pass

    def on_trigger_enter(self, other):
        pass

    def on_trigger_exit(self, other):
        pass

    def on_reload(self):
        pass

    # Convenience accessors -------------------------------------------------

    @property
    def transform(self):
        """The entity's Transform component."""
        return self.entity.transform

    def get_component(self, component):
        """Shortcut for ``self.entity.get_component(component)``."""
        return self.entity.get_component(component)
