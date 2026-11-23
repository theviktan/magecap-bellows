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
from _bellows import Vec2 as _Vec2
from _bellows import Vec3 as _Vec3
from _bellows import Vec4 as _Vec4
from _bellows import _field_changed

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
    * ``on_event(self, name, *values)``   - an event sent with ``bellows.emit`` or ForgeScript's
      ``trigger``, by any script (this one included)
    * ``on_reload(self)``                 - after the script's file was saved during play and its new
      code took over (the object and its attribute values stay; only the methods change)

    When one of its fields changes after ``on_create``, every script gets the ``field changed`` event
    with the entity, the field's name, and its new and old values.

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

    def on_event(self, name, *values):
        pass

    def on_reload(self):
        pass

    # Fields that tell when they change -------------------------------------
    _FIELD_TYPES = (bool, int, float, str, _Vec2, _Vec3, _Vec4)

    @classmethod
    def _bellows_field_names(cls):
        """The class's Inspector fields: annotated attributes of the field types, inherited ones too."""
        names = set()
        for klass in reversed(cls.__mro__):
            for name, annotation in vars(klass).get("__annotations__", {}).items():
                if annotation in Script._FIELD_TYPES and not name.startswith("_"):
                    names.add(name)
        return frozenset(names)

    def _bellows_watch_fields(self):
        """Called by the engine after on_create: from now on a field's change sends 'field changed'."""
        object.__setattr__(self, "_bellows_watched", type(self)._bellows_field_names())

    def __setattr__(self, name, value):
        watched = self.__dict__.get("_bellows_watched")
        if watched is None or name not in watched:
            object.__setattr__(self, name, value)
            return
        before = getattr(self, name, None)
        object.__setattr__(self, name, value)
        if type(value) is not type(before) or value != before:
            _field_changed(self.entity, name, value, before)

    # Convenience accessors -------------------------------------------------

    @property
    def transform(self):
        """The entity's Transform component."""
        return self.entity.transform

    def get_component(self, component):
        """Shortcut for ``self.entity.get_component(component)``."""
        return self.entity.get_component(component)
