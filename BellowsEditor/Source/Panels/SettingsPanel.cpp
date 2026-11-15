#include "SettingsPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"

#include <imgui.h>

namespace Bellows {

	void SettingsPanel::OnImGuiRender()
	{
		if (!ImGui::IsAnyItemActive())
		{
			if (m_ProjectDirty && m_Editor.GetProject() && !m_Editor.GetProject()->Save())
				BW_ERROR("Could not save the project settings");
			if (m_SettingsDirty && !m_Editor.GetSettings().Save())
				BW_ERROR("Could not save the editor settings");
			m_ProjectDirty = false;
			m_SettingsDirty = false;
		}

		if (!ImGui::Begin("Settings"))
		{
			ImGui::End();
			return;
		}
		// Section headers stay neutral here; the accent color is reserved for selection.
		ImGui::PushStyleColor(ImGuiCol_Header, Theme::ToVec4(Theme::PanelRaised));
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::ToVec4(Theme::FieldHovered));
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::ToVec4(Theme::FieldActive));
		if (ImGui::BeginTabBar("##SettingsTabs"))
		{
			if (ImGui::BeginTabItem(ICON_CLAPPERBOARD " Scene"))
			{
				DrawSceneSettings();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_PACKAGE " Project"))
			{
				DrawProjectSettings();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_SETTINGS " Editor"))
			{
				DrawEditorSettings();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::PopStyleColor(3);
		ImGui::End();
	}

	void SettingsPanel::DrawSceneSettings()
	{
		const Ref<Scene>& scene = m_Editor.GetActiveScene();
		if (!scene)
		{
			UI::CenteredText("No scene");
			return;
		}
		SceneSettings& settings = scene->GetSettings();
		SceneRendererSettings& rendering = settings.Rendering;

		ImGui::BeginChild("##SceneSettings");
		if (ImGui::CollapsingHeader(ICON_ATOM "  Physics", ImGuiTreeNodeFlags_DefaultOpen) && UI::BeginPropertyGrid("Physics"))
		{
			UI::PropertyVec3("Gravity", settings.Gravity, 0.0f, 0.05f, "Applied at play start");
			float rate = 1.0f / settings.FixedTimestep;
			if (UI::Property("Physics Rate", rate, 1.0f, 10.0f, 480.0f, "%.0f Hz", "Fixed updates per second"))
				settings.FixedTimestep = 1.0f / std::clamp(rate, 10.0f, 480.0f);
			UI::EndPropertyGrid();
		}

		if (ImGui::CollapsingHeader(ICON_SUN "  Lighting", ImGuiTreeNodeFlags_DefaultOpen) && UI::BeginPropertyGrid("Lighting"))
		{
			UI::PropertyColor("Ambient", settings.AmbientColor, false, "Extra flat ambient light, added to the sky's light");
			UI::Property("Exposure", rendering.Exposure, 0.01f, 0.01f, 20.0f, "%.2f");
			const char* tonemappers[] = { "ACES", "AgX", "Reinhard", "None" };
			int tonemapper = static_cast<int>(rendering.ToneMapper);
			if (UI::PropertyCombo("Tonemapper", tonemapper, tonemappers, 4))
				rendering.ToneMapper = static_cast<Tonemapper>(tonemapper);
			UI::PropertySlider("Vignette", rendering.Vignette, 0.0f, 1.0f);
			UI::Property("FXAA", rendering.FXAAEnabled, "Fast approximate anti-aliasing");
			UI::EndPropertyGrid();
		}

		if (ImGui::CollapsingHeader(ICON_MOON "  Shadows") && UI::BeginPropertyGrid("Shadows"))
		{
			UI::Property("Enabled", rendering.ShadowsEnabled);
			const char* sizes[] = { "1024", "2048", "4096" };
			int size = rendering.ShadowMapSize >= 4096 ? 2 : (rendering.ShadowMapSize >= 2048 ? 1 : 0);
			if (UI::PropertyCombo("Resolution", size, sizes, 3))
				rendering.ShadowMapSize = 1024u << size;
			UI::Property("Cascades", rendering.CascadeCount, 1u, 4u);
			UI::Property("Distance", rendering.ShadowDistance, 0.5f, 5.0f, 1000.0f, "%.0f m");
			UI::PropertySlider("Split Lambda", rendering.CascadeSplitLambda, 0.0f, 1.0f, "%.2f", "0 = even splits, 1 = logarithmic");
			UI::PropertySlider("Softness", rendering.ShadowSoftness, 0.0f, 0.2f, "%.3f", "Light source size; larger values give softer penumbrae");
			UI::Property("Samples", rendering.ShadowPCFSamples, 4u, 64u);
			UI::Property("Depth Bias", rendering.ShadowBias, 0.00001f, 0.0f, 0.01f, "%.5f");
			UI::Property("Normal Bias", rendering.ShadowNormalBias, 0.05f, 0.0f, 10.0f, "%.2f");
			UI::EndPropertyGrid();
		}

		if (ImGui::CollapsingHeader(ICON_CIRCLE_DOT "  Ambient Occlusion") && UI::BeginPropertyGrid("SSAO"))
		{
			UI::Property("Enabled", rendering.SSAOEnabled);
			UI::Property("Half Resolution", rendering.SSAOHalfResolution, "Faster, slightly softer");
			UI::Property("Radius", rendering.SSAORadius, 0.01f, 0.05f, 5.0f, "%.2f m");
			UI::PropertySlider("Strength", rendering.SSAOStrength, 0.0f, 2.0f);
			UI::PropertySlider("Power", rendering.SSAOPower, 0.5f, 4.0f);
			UI::Property("Bias", rendering.SSAOBias, 0.001f, 0.0f, 0.2f, "%.3f");
			UI::Property("Samples", rendering.SSAOSamples, 4u, 64u);
			UI::EndPropertyGrid();
		}

		if (ImGui::CollapsingHeader(ICON_SPARKLES "  Bloom") && UI::BeginPropertyGrid("Bloom"))
		{
			UI::Property("Enabled", rendering.BloomEnabled);
			UI::PropertySlider("Intensity", rendering.BloomIntensity, 0.0f, 1.0f, "%.3f");
			UI::Property("Threshold", rendering.BloomThreshold, 0.01f, 0.0f, 20.0f, "%.2f");
			UI::PropertySlider("Knee", rendering.BloomKnee, 0.0f, 1.0f);
			UI::PropertySlider("Radius", rendering.BloomRadius, 0.1f, 3.0f);
			UI::EndPropertyGrid();
		}
		ImGui::EndChild();
	}

	void SettingsPanel::DrawProjectSettings()
	{
		Ref<Project> project = m_Editor.GetProject();
		if (!project)
			return;
		ProjectConfig& config = project->GetConfig();
		bool changed = false;
		if (UI::BeginPropertyGrid("Project"))
		{
			changed |= UI::Property("Name", config.Name);
			changed |= UI::PropertyAsset("Start Scene", config.StartScene, AssetType::Scene, "The scene exported games open first");
			UI::EndPropertyGrid();
		}
		UI::SectionHeader("Exported game window");
		if (UI::BeginPropertyGrid("ProjectWindow"))
		{
			changed |= UI::Property("Width", config.WindowWidth, 320u, 7680u);
			changed |= UI::Property("Height", config.WindowHeight, 240u, 4320u);
			changed |= UI::Property("Fullscreen", config.Fullscreen);
			changed |= UI::Property("VSync", config.VSync);
			UI::EndPropertyGrid();
		}
		m_ProjectDirty |= changed;
		ImGui::Spacing();
		UI::TextMuted(project->GetProjectFilePath().string().c_str());
	}

	void SettingsPanel::DrawEditorSettings()
	{
		EditorSettings& settings = m_Editor.GetSettings();
		bool changed = false;
		if (UI::BeginPropertyGrid("Editor"))
		{
			float speed = m_Editor.GetCamera().GetFlySpeed();
			if (UI::Property("Camera Speed", speed, 0.1f, 0.1f, 500.0f, "%.1f m/s", "Mouse wheel while flying (right mouse button) also changes it"))
				m_Editor.GetCamera().SetFlySpeed(speed);
			changed |= UI::Property("Show Grid", settings.ShowGrid);
			changed |= UI::Property("UI Scale", settings.UIScale, 0.01f, 0.0f, 3.0f, settings.UIScale <= 0.0f ? "Auto" : "%.2f", "Takes effect after a restart. 0 follows the monitor.");
			UI::EndPropertyGrid();
		}

		UI::SectionHeader("Agent automation");
		if (UI::BeginPropertyGrid("Agent"))
		{
			changed |= UI::Property("Agent Server", settings.AgentServerEnabled, "Lets AI agents and the bellows CLI control the editor (localhost only). Takes effect after a restart.");
			int port = settings.AgentServerPort;
			if (UI::Property("Port", port, 1024, 65535))
			{
				settings.AgentServerPort = static_cast<uint16_t>(port);
				changed = true;
			}
			AgentServer& server = m_Editor.GetAgentServer();
			UI::PropertyText("Status", server.IsRunning() ? fmt::format("Listening on 127.0.0.1:{}", server.GetPort()).c_str() : "Stopped");
			UI::PropertyText("Requests", std::to_string(server.GetRequestCount()).c_str());
			UI::EndPropertyGrid();
		}
		m_SettingsDirty |= changed;
	}

}
