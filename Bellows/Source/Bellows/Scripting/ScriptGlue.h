#pragma once

// Internal helpers of the Python bindings (not part of the public engine API).
// The language-neutral pieces (ScriptEntity, the active scene, frame timing) live in ScriptApi.h.

#include "Bellows/Scripting/ScriptApi.h"

#include <pybind11/pybind11.h>

namespace Bellows::Scripting {

	// Defines every type and function of the '_bellows' module.
	void RegisterBindings(pybind11::module_& module);
	// Forces the binding translation unit into the link (static library).
	void LinkBindings();

	// Script event values in Python: None, bool, float, str, Vec3, Entity, list and dict (text keys).
	pybind11::object ScriptValueToPython(const ScriptValue& value);
	// The reverse; int becomes a number, tuple a list. Throws ScriptValueError naming any other type.
	ScriptValue ScriptValueFromPython(const pybind11::handle& value);

}
