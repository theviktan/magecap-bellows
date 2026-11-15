#pragma once

// Internal helpers shared by the Python bindings (not part of the public engine API).

#include "Bellows/Core/UUID.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Scene/Scene.h"

#include <pybind11/pybind11.h>

#include <functional>
#include <string>

namespace Bellows::Scripting {

	// Python-side entity handle. Holds IDs instead of pointers so stale handles fail safely
	// (with a Python exception) after the entity or its scene is gone.
	struct PyEntity
	{
		uint64_t SceneID = 0;
		UUID ID = 0;

		PyEntity() = default;
		explicit PyEntity(Entity entity);

		Scene* GetScene() const;
		Entity TryResolve() const;
		// Throws a Python RuntimeError when the entity no longer exists.
		Entity Resolve() const;
		bool IsValid() const { return static_cast<bool>(TryResolve()); }
	};

	// The scene that module-level functions (create_entity, find_entity, ...) operate on.
	void SetContextScene(Scene* scene);
	Scene* GetContextScene();			// May be null
	Scene& RequireContextScene();		// Throws a Python RuntimeError when there is none

	// Host callbacks (the runtime/editor decide what quitting or loading a scene means).
	void SetQuitHandler(std::function<void(int)> handler);
	void SetLoadSceneHandler(std::function<bool(const std::string&)> handler);
	void RequestQuit(int exitCode);
	bool RequestLoadScene(const std::string& path);

	// Frame timing published to scripts.
	void SetFrameTiming(float deltaTime, float time, uint64_t frame);

	// Defines every type and function of the '_bellows' module.
	void RegisterBindings(pybind11::module_& module);
	// Forces the binding translation unit into the link (static library).
	void LinkBindings();

}
