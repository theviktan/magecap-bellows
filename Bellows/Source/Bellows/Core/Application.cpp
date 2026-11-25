#include "bwpch.h"
#include "Bellows/Core/Application.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Core/Profiler.h"
#include "Bellows/ImGui/ImGuiLayer.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Renderer/Renderer.h"
#include "Bellows/Scripting/ScriptEngine.h"

namespace Bellows {

	Application* Application::s_Instance = nullptr;

	bool ApplicationCommandLineArgs::Has(std::string_view flag) const
	{
		return std::find(Arguments.begin(), Arguments.end(), flag) != Arguments.end();
	}

	std::string ApplicationCommandLineArgs::GetValue(std::string_view flag, const std::string& fallback) const
	{
		for (size_t i = 0; i + 1 < Arguments.size(); i++)
		{
			if (Arguments[i] == flag)
				return Arguments[i + 1];
		}
		return fallback;
	}

	ApplicationCommandLineArgs ApplicationCommandLineArgs::FromArgv(int argc, char** argv)
	{
		ApplicationCommandLineArgs args;
		for (int i = 1; i < argc; i++)
			args.Arguments.emplace_back(argv[i]);
		return args;
	}

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		BW_CORE_VERIFY(!s_Instance, "Only one Application may exist at a time");
		s_Instance = this;

		if (!m_Specification.WorkingDirectory.empty())
			std::filesystem::current_path(m_Specification.WorkingDirectory);

		RenderDeviceSpecification deviceSpecification;
		deviceSpecification.EnableValidation = m_Specification.EnableValidation;
		deviceSpecification.VSync = m_Specification.VSync;
		deviceSpecification.HeadlessWidth = m_Specification.HeadlessWidth;
		deviceSpecification.HeadlessHeight = m_Specification.HeadlessHeight;

		if (!m_Specification.Headless)
		{
			if (!Window::InitializeGLFW())
				return;

			m_Window = CreateScope<Window>(m_Specification.Window);
			if (!m_Window->GetNativeWindow())
				return;
			m_Window->SetEventCallback(BW_BIND_EVENT_FN(OnEvent));
			deviceSpecification.TargetWindow = m_Window.get();
		}

		m_RenderDevice = RenderDevice::Create(deviceSpecification);
		if (!m_RenderDevice)
			return;

		if (!Renderer::Init(*m_RenderDevice))
			return;

		// Headless runs (tests, automation, servers) never open an audio device.
		if (m_Specification.EnableAudio && !AudioEngine::IsInitialized())
			m_OwnsAudio = AudioEngine::Init(m_Specification.Headless);

		if (m_Specification.EnableScripting && !ScriptEngine::IsInitialized())
		{
			m_OwnsScripting = ScriptEngine::Init();
			if (!m_OwnsScripting)
				BW_CORE_ERROR("Python scripting is unavailable; scripts will not run");
		}

		if (m_Specification.EnableImGui)
		{
			m_ImGuiLayer = new ImGuiLayer();
			PushOverlay(m_ImGuiLayer);
		}

		m_LastFrameStart = std::chrono::steady_clock::now();
		m_Valid = true;
	}

	Application::~Application()
	{
		if (m_RenderDevice)
			m_RenderDevice->WaitIdle();

		m_LayerStack.Clear();
		m_ImGuiLayer = nullptr;

		if (m_OwnsScripting)
			ScriptEngine::Shutdown();
		if (m_OwnsAudio)
			AudioEngine::Shutdown();
		// Cached assets own GPU resources and must go before the device.
		AssetManager::ClearCache();
		Renderer::Shutdown();
		m_RenderDevice.reset();
		m_Window.reset();
		Window::ShutdownGLFW();

		s_Instance = nullptr;
	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
	}

	void Application::PushOverlay(Layer* overlay)
	{
		m_LayerStack.PushOverlay(overlay);
	}

	void Application::Close(int exitCode)
	{
		m_ExitCode = exitCode;
		m_Running = false;
	}

	void Application::SubmitToMainThread(std::function<void()> function)
	{
		std::scoped_lock lock(m_MainThreadQueueMutex);
		m_MainThreadQueue.emplace_back(std::move(function));
	}

	void Application::RequestBackBufferCapture(std::function<void(const Image&)> callback)
	{
		m_BackBufferCaptures.push_back(std::move(callback));
	}

	void Application::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<WindowCloseEvent>(BW_BIND_EVENT_FN(OnWindowClose));

		// ImGui gets the first chance to consume input; game input is fed regardless
		// and filtered by Input::SetEnabled (the editor disables it outside the viewport).
		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (event.Handled)
				break;
			(*it)->OnEvent(event);
		}
		Input::OnEvent(event);
	}

	int Application::Run()
	{
		if (!m_Valid)
		{
			BW_CORE_CRITICAL("Application '{}' failed to initialize", m_Specification.Name);
			return 1;
		}

		while (m_Running)
			RunFrame();

		return m_ExitCode;
	}

	void Application::RunFrame()
	{
		const auto frameStart = std::chrono::steady_clock::now();
		float timestep = std::chrono::duration<float>(frameStart - m_LastFrameStart).count();
		m_LastFrameStart = frameStart;
		if (m_Specification.FixedTimestep > 0.0f)
			timestep = m_Specification.FixedTimestep;
		// Long stalls (debugger breaks, window drags) must not explode the simulation.
		timestep = std::min(timestep, 0.1f);
		m_LastFrameTime = timestep;
		m_Time += timestep;

		Profiler::BeginFrame(m_FrameCount);
		Input::BeginFrame();
		{
			BW_PROFILE_SCOPE("Events");
			if (m_Window)
				m_Window->PollEvents();
			ExecuteMainThreadQueue();
		}

		bool rendering = false;
		{
			// Waits for a swapchain image, so vsync and a busy GPU show here.
			BW_PROFILE_SCOPE("Acquire Frame");
			rendering = m_RenderDevice->BeginFrame();
		}
		if (rendering)
		{
			for (Layer* layer : m_LayerStack)
			{
				ProfileScope scope(Profiler::Intern(layer->GetName()));
				layer->OnUpdate(Timestep(timestep));
			}

			if (m_ImGuiLayer)
			{
				BW_PROFILE_SCOPE("Editor UI");
				m_ImGuiLayer->Begin();
				for (Layer* layer : m_LayerStack)
					layer->OnImGuiRender();
				m_ImGuiLayer->End();
			}

			if (!m_BackBufferCaptures.empty())
			{
				const Image image = Renderer::ReadbackTexture(m_RenderDevice->GetCurrentBackBuffer());
				std::vector<std::function<void(const Image&)>> captures;
				captures.swap(m_BackBufferCaptures);
				for (const auto& capture : captures)
					capture(image);
			}

			BW_PROFILE_SCOPE("Present");
			m_RenderDevice->Present();
		}
		else
		{
			// Minimized: keep the CPU idle without spinning.
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}

		Profiler::EndFrame();
		m_FrameCount++;
		if (m_Specification.MaxFrames > 0 && m_FrameCount >= m_Specification.MaxFrames)
			m_Running = false;
	}

	bool Application::OnWindowClose(WindowCloseEvent& event)
	{
		m_Running = false;
		return true;
	}

	void Application::ExecuteMainThreadQueue()
	{
		std::vector<std::function<void()>> queue;
		{
			std::scoped_lock lock(m_MainThreadQueueMutex);
			queue.swap(m_MainThreadQueue);
		}
		for (std::function<void()>& function : queue)
			function();
	}

}
