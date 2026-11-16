#pragma once

#include "Bellows/Core/Layer.h"
#include "Bellows/Project/Project.h"
#include "Bellows/Renderer/Blitter.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Scene.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Bellows {

	struct RuntimeOptions
	{
		std::string SceneOverride;				// Asset path of a scene to start instead of the project's start scene
		uint64_t ScreenshotFrame = 0;			// Saves a screenshot after this frame (0 = never)
		std::filesystem::path ScreenshotPath;
	};

	// Runs a project's scenes: play mode, rendering to the window, scene switching and quitting.
	class RuntimeLayer : public Layer
	{
	public:
		RuntimeLayer(Ref<Project> project, RuntimeOptions options);

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep timestep) override;
		void OnEvent(Event& event) override;

		bool HasFailed() const { return m_Failed; }
	private:
		bool LoadScene(const std::string& assetPath);
	private:
		Ref<Project> m_Project;
		RuntimeOptions m_Options;
		Ref<Scene> m_Scene;
		Scope<SceneRenderer> m_Renderer;
		Scope<Blitter> m_Blitter;
		nvrhi::CommandListHandle m_CommandList;
		std::optional<std::string> m_PendingScene;
		bool m_HasRenderedFrame = false;
		bool m_Failed = false;
	};

}
