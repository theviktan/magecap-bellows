#pragma once

#include "Bellows/Core/Base.h"
#include "Bellows/Core/Events.h"
#include "Bellows/Core/LayerStack.h"
#include "Bellows/Core/Window.h"
#include "Bellows/Renderer/Image.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Bellows {

	class RenderDevice;
	class ImGuiLayer;

	struct ApplicationCommandLineArgs
	{
		std::vector<std::string> Arguments;

		bool Has(std::string_view flag) const;
		// Returns the value following 'flag' ("--project path"), or 'fallback' when missing.
		std::string GetValue(std::string_view flag, const std::string& fallback = {}) const;

		static ApplicationCommandLineArgs FromArgv(int argc, char** argv);
	};

	struct ApplicationSpecification
	{
		std::string Name = "MageCap Bellows";
		WindowSpecification Window;
		ApplicationCommandLineArgs CommandLineArgs;
		std::filesystem::path WorkingDirectory;

		// Headless applications have no window; they render into an offscreen back buffer.
		bool Headless = false;
		uint32_t HeadlessWidth = 1280;
		uint32_t HeadlessHeight = 720;

		bool EnableValidation = false;
		bool VSync = true;
		bool EnableImGui = true;
		bool EnableScripting = true;
		bool EnableAudio = true;

		// When > 0, every frame advances by exactly this many seconds (deterministic automation).
		float FixedTimestep = 0.0f;
		// Stops after this many frames when > 0.
		uint64_t MaxFrames = 0;
	};

	class Application
	{
	public:
		explicit Application(const ApplicationSpecification& specification);
		virtual ~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		// Runs the main loop until Close() is called or MaxFrames is reached.
		// Returns the process exit code.
		int Run();
		void Close(int exitCode = 0);

		// Advances exactly one frame. Used by Run() and by automation that needs frame-by-frame control.
		void RunFrame();

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);

		void OnEvent(Event& event);

		// Queues a function to run at the start of the next frame on the main thread. Thread-safe.
		void SubmitToMainThread(std::function<void()> function);

		// Reads back the finished frame (everything drawn, including UI) just before it is presented.
		void RequestBackBufferCapture(std::function<void(const Image&)> callback);

		bool IsValid() const { return m_Valid; }
		bool IsHeadless() const { return m_Specification.Headless; }
		Window* GetWindow() const { return m_Window.get(); }
		RenderDevice& GetRenderDevice() const { return *m_RenderDevice; }
		ImGuiLayer* GetImGuiLayer() const { return m_ImGuiLayer; }
		const ApplicationSpecification& GetSpecification() const { return m_Specification; }

		bool IsRunning() const { return m_Running; }
		uint64_t GetFrameCount() const { return m_FrameCount; }
		float GetTime() const { return m_Time; }
		float GetLastFrameTime() const { return m_LastFrameTime; }
		void SetFixedTimestep(float timestep) { m_Specification.FixedTimestep = timestep; }

		static Application& Get() { return *s_Instance; }
		static bool HasInstance() { return s_Instance != nullptr; }
	private:
		bool OnWindowClose(WindowCloseEvent& event);
		void ExecuteMainThreadQueue();
	private:
		ApplicationSpecification m_Specification;
		Scope<Window> m_Window;
		Scope<RenderDevice> m_RenderDevice;
		ImGuiLayer* m_ImGuiLayer = nullptr;
		LayerStack m_LayerStack;

		bool m_Valid = false;
		bool m_OwnsAudio = false;		// Subsystems started by this application (and therefore shut down by it)
		bool m_OwnsScripting = false;
		bool m_Running = true;
		int m_ExitCode = 0;
		uint64_t m_FrameCount = 0;
		float m_Time = 0.0f;
		float m_LastFrameTime = 0.0f;
		std::chrono::steady_clock::time_point m_LastFrameStart;

		std::mutex m_MainThreadQueueMutex;
		std::vector<std::function<void()>> m_MainThreadQueue;
		std::vector<std::function<void(const Image&)>> m_BackBufferCaptures;

		static Application* s_Instance;
	};

}
