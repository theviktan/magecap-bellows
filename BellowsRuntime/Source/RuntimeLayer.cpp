#include "RuntimeLayer.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Application.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Bellows {

	RuntimeLayer::RuntimeLayer(Ref<Project> project, RuntimeOptions options)
		: Layer("RuntimeLayer"), m_Project(std::move(project)), m_Options(std::move(options))
	{
	}

	void RuntimeLayer::OnAttach()
	{
		m_Renderer = CreateScope<SceneRenderer>();
		m_Blitter = CreateScope<Blitter>();
		m_CommandList = Renderer::GetDevice()->createCommandList();

		ScriptEngine::SetQuitHandler([](int exitCode) { Application::Get().Close(exitCode); });
		ScriptEngine::SetLoadSceneHandler([this](const std::string& path)
		{
			if (!AssetManager::Exists(path))
				return false;
			m_PendingScene = path;
			return true;
		});

		const std::string startScene = m_Options.SceneOverride.empty() ? m_Project->GetConfig().StartScene : m_Options.SceneOverride;
		if (!LoadScene(startScene))
		{
			m_Failed = true;
			Application::Get().Close(1);
		}
	}

	void RuntimeLayer::OnDetach()
	{
		if (m_Scene)
			m_Scene->OnRuntimeStop();
		m_Scene.reset();
		m_Renderer.reset();
		m_Blitter.reset();
		ScriptEngine::SetQuitHandler(nullptr);
		ScriptEngine::SetLoadSceneHandler(nullptr);
	}

	bool RuntimeLayer::LoadScene(const std::string& assetPath)
	{
		Ref<Scene> scene = CreateRef<Scene>();
		if (!SceneSerializer::Deserialize(*scene, AssetManager::GetAbsolutePath(assetPath)))
		{
			BW_CORE_ERROR("Runtime: could not load scene '{}'", assetPath);
			return false;
		}

		if (m_Scene)
			m_Scene->OnRuntimeStop();
		m_Scene = scene;

		RenderDevice& device = Application::Get().GetRenderDevice();
		m_Scene->OnViewportResize(device.GetBackBufferWidth(), device.GetBackBufferHeight());
		m_Scene->OnRuntimeStart();
		BW_CORE_INFO("Runtime: playing '{}'", assetPath);
		return true;
	}

	void RuntimeLayer::OnUpdate(Timestep timestep)
	{
		if (!m_Scene)
			return;

		if (m_PendingScene)
		{
			const std::string next = *m_PendingScene;
			m_PendingScene.reset();
			LoadScene(next);
		}

		Application& application = Application::Get();
		RenderDevice& device = application.GetRenderDevice();
		const uint32_t width = device.GetBackBufferWidth();
		const uint32_t height = device.GetBackBufferHeight();
		Input::SetViewportRect(glm::vec2(0.0f), glm::vec2(static_cast<float>(width), static_cast<float>(height)));
		m_Scene->OnViewportResize(width, height);
		m_Renderer->SetViewportSize(width, height);

		m_Scene->OnUpdateRuntime(timestep);

		RenderCamera camera;
		if (!m_Scene->GetPrimaryCamera(static_cast<float>(width) / static_cast<float>(std::max(height, 1u)), camera))
		{
			// No camera in the scene: look at the origin from a sensible default.
			camera.Position = glm::vec3(0.0f, 2.0f, 8.0f);
			camera.View = glm::lookAt(camera.Position, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
			camera.Projection = Math::PerspectiveProjection(glm::radians(60.0f), static_cast<float>(width) / static_cast<float>(std::max(height, 1u)), 0.1f, 1000.0f);
		}
		m_Renderer->GetSettings().ShowGrid = false;
		m_Renderer->GetSettings().ShowSelectionOutline = false;
		m_Scene->OnRender(*m_Renderer, camera);

		m_CommandList->open();
		m_Blitter->Blit(m_CommandList, m_Renderer->GetFinalImage(), device.GetCurrentFramebuffer());
		m_CommandList->close();
		Renderer::GetDevice()->executeCommandList(m_CommandList);

		if (m_Options.ScreenshotFrame > 0 && application.GetFrameCount() + 1 == m_Options.ScreenshotFrame)
		{
			const Image image = Renderer::ReadbackTexture(m_Renderer->GetFinalImage());
			if (image.SavePNG(m_Options.ScreenshotPath))
				BW_CORE_INFO("Runtime: screenshot saved to '{}'", m_Options.ScreenshotPath.string());
		}
	}

	void RuntimeLayer::OnEvent(Event& event)
	{
	}

}
