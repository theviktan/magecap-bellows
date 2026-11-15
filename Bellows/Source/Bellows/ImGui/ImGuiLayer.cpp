#include "bwpch.h"
#include "Bellows/ImGui/ImGuiLayer.h"

#include "Bellows/Core/Application.h"
#include "Bellows/ImGui/ImGuiRenderer.h"
#include "Bellows/Renderer/RenderDevice.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>

namespace Bellows {

	ImGuiLayer::ImGuiLayer()
		: Layer("ImGuiLayer")
	{
	}

	ImGuiLayer::~ImGuiLayer() = default;

	void ImGuiLayer::OnAttach()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
		io.IniFilename = nullptr;	// Applications choose where (and whether) layouts are saved.

		Application& application = Application::Get();
		if (Window* window = application.GetWindow())
		{
			ImGui_ImplGlfw_InitForVulkan(window->GetNativeWindow(), true);
			m_HasPlatformBackend = true;
		}
		else
		{
			io.BackendPlatformName = "Bellows Headless";
		}

		m_Renderer = CreateScope<ImGuiRenderer>();
	}

	void ImGuiLayer::OnDetach()
	{
		Application::Get().GetRenderDevice().WaitIdle();
		m_Renderer.reset();
		if (m_HasPlatformBackend)
			ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void ImGuiLayer::OnEvent(Event& event)
	{
		if (!m_BlockEvents)
			return;

		const ImGuiIO& io = ImGui::GetIO();
		switch (event.GetEventType())
		{
			case EventType::MouseButtonPressed:
			case EventType::MouseButtonReleased:
			case EventType::MouseScrolled:
			case EventType::MouseMoved:
				event.Handled |= io.WantCaptureMouse;
				break;
			case EventType::KeyPressed:
			case EventType::KeyReleased:
			case EventType::KeyTyped:
				event.Handled |= io.WantCaptureKeyboard;
				break;
			default:
				break;
		}
	}

	void ImGuiLayer::Begin()
	{
		Application& application = Application::Get();
		ImGuiIO& io = ImGui::GetIO();

		if (m_HasPlatformBackend)
		{
			ImGui_ImplGlfw_NewFrame();
		}
		else
		{
			const RenderDevice& device = application.GetRenderDevice();
			io.DisplaySize = ImVec2(static_cast<float>(device.GetBackBufferWidth()), static_cast<float>(device.GetBackBufferHeight()));
			io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
			io.DeltaTime = std::max(application.GetLastFrameTime(), 1.0f / 1000.0f);
		}

		ImGui::NewFrame();
		ImGuizmo::BeginFrame();
	}

	void ImGuiLayer::End()
	{
		ImGui::Render();
		RenderDevice& device = Application::Get().GetRenderDevice();
		m_Renderer->Render(ImGui::GetDrawData(), device.GetCurrentFramebuffer(), m_ClearBackBuffer);
	}

}
