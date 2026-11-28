#include "StatisticsPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"

#include "Bellows/Core/Application.h"
#include "Bellows/Renderer/RenderDevice.h"
#include "Bellows/Scripting/ScriptEngine.h"

#include <imgui.h>

namespace Bellows {

	void StatisticsPanel::OnImGuiRender(bool* open)
	{
		Application& application = Application::Get();
		const float frameTime = application.GetLastFrameTime() * 1000.0f;
		m_FrameTimes[m_FrameTimeOffset] = frameTime;
		m_FrameTimeOffset = (m_FrameTimeOffset + 1) % m_FrameTimes.size();

		ImGui::SetNextWindowSize(ImVec2(340.0f * Theme::GetScale(), 0.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Statistics", open))
		{
			ImGui::End();
			return;
		}

		float average = 0.0f, worst = 0.0f;
		for (float value : m_FrameTimes)
		{
			average += value;
			worst = std::max(worst, value);
		}
		average /= static_cast<float>(m_FrameTimes.size());

		const std::string overlay = fmt::format("{:.2f} ms  ({:.0f} FPS)", average, average > 0.0f ? 1000.0f / average : 0.0f);
		ImGui::PushStyleColor(ImGuiCol_PlotLines, Theme::ToVec4(Theme::Neutral));
		ImGui::PlotLines("##FrameTimes", m_FrameTimes.data(), static_cast<int>(m_FrameTimes.size()), static_cast<int>(m_FrameTimeOffset),
			overlay.c_str(), 0.0f, std::max(33.4f, worst * 1.1f), ImVec2(-FLT_MIN, 70.0f * Theme::GetScale()));
		ImGui::PopStyleColor();

		const SceneRendererStatistics& stats = m_Editor.GetViewportRenderer().GetStatistics();
		const Ref<Scene>& scene = m_Editor.GetActiveScene();
		if (UI::BeginPropertyGrid("Statistics", 0.5f))
		{
			UI::PropertyText("GPU", application.GetRenderDevice().GetAdapterName().c_str());
			UI::PropertyText("Viewport", fmt::format("{} x {}", m_Editor.GetViewportRenderer().GetViewportWidth(), m_Editor.GetViewportRenderer().GetViewportHeight()).c_str());
			UI::PropertyText("Worst Frame", fmt::format("{:.2f} ms", worst).c_str());
			UI::PropertyText("Draw Calls", std::to_string(stats.DrawCalls).c_str());
			UI::PropertyText("Shadow Draw Calls", std::to_string(stats.ShadowDrawCalls).c_str());
			UI::PropertyText("Triangles", std::to_string(stats.Triangles).c_str());
			UI::PropertyText("Meshes", std::to_string(stats.Meshes).c_str());
			UI::PropertyText("Point / Spot Lights", fmt::format("{} / {}", stats.PointLights, stats.SpotLights).c_str());
			UI::PropertyText("Particles / Decals", fmt::format("{} / {}", stats.Particles, stats.Decals).c_str());
			UI::PropertyText("Entities", std::to_string(scene ? scene->GetEntityCount() : 0).c_str());
			UI::PropertyText("Script Errors", std::to_string(ScriptEngine::GetErrorCount()).c_str());
			UI::EndPropertyGrid();
		}
		ImGui::End();
	}

}
