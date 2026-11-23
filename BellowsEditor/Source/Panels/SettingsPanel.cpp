#include "SettingsPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"

#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/InputNames.h"
#include "Bellows/Navigation/SceneNavigation.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace Bellows {

	void SettingsPanel::OnImGuiRender(bool* open)
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

		if (!UI::BeginPanel("Settings", open))
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

		if (ImGui::CollapsingHeader(ICON_ROUTE "  Navigation"))
			DrawNavigationSettings(*scene);

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

	void SettingsPanel::DrawNavigationSettings(Scene& scene)
	{
		NavMeshSettings& navigation = scene.GetSettings().Navigation;
		if (UI::BeginPropertyGrid("Navigation"))
		{
			UI::Property("Agent Radius", navigation.AgentRadius, 0.01f, NavMeshSettings::MinRadius, NavMeshSettings::MaxRadius, "%.2f m",
				"How far agents keep from walls");
			UI::Property("Agent Height", navigation.AgentHeight, 0.01f, NavMeshSettings::MinHeight, NavMeshSettings::MaxHeight, "%.2f m",
				"The headroom agents need: lower ceilings aren't walked under");
			UI::Property("Step Height", navigation.MaxClimb, 0.01f, 0.0f, NavMeshSettings::MaxClimbLimit, "%.2f m", "The highest step agents walk up");
			UI::PropertySlider("Max Slope", navigation.MaxSlope, 0.0f, NavMeshSettings::MaxSlopeLimit, "%.0f deg", "The steepest ground agents walk on");
			UI::EndPropertyGrid();
		}

		// Whether the navmesh still matches the scene, worked out at most twice a second (it reads
		// every static collider).
		const Ref<NavMesh>& navMesh = scene.GetNavMesh();
		const double now = ImGui::GetTime();
		if (navMesh && (now - m_NavigationCheckTime > 0.5 || m_NavigationCheckedMesh != navMesh.get()))
		{
			m_NavigationUpToDate = CollectNavigationGeometry(scene).ComputeHash(navigation) == navMesh->GetSourceHash();
			m_NavigationCheckTime = now;
			m_NavigationCheckedMesh = navMesh.get();
		}
		ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
		if (!navMesh)
			ImGui::TextDisabled("Not baked: agents have nothing to walk on yet.");
		else
		{
			ImGui::TextDisabled("%d tiles, %d polygons", navMesh->GetTileCount(), navMesh->GetPolygonCount());
			if (!m_NavigationUpToDate)
				ImGui::TextColored(Theme::ToVec4(Theme::Warning), ICON_TRIANGLE_ALERT "  Out of date: the static colliders or settings changed.");
		}
		ImGui::PopFont();
		if (ImGui::Button(ICON_ROUTE "  Bake Navigation", ImVec2(-FLT_MIN, 0.0f)))
		{
			std::string error;
			m_NavigationMessage = m_Editor.BakeNavigation(error) ? std::string() : error;
			m_NavigationCheckedMesh = nullptr;
		}
		UI::Tooltip("Builds what agents walk on from the static colliders, and saves it beside the scene");
		if (!m_NavigationMessage.empty())
			ImGui::TextWrapped("%s", m_NavigationMessage.c_str());
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
			static const char* const c_Fronts[] = { "-Z (as made)", "+Z (turn to face forward)" };
			int front = config.ModelFrontFaces == ModelFront::PositiveZ ? 1 : 0;
			if (UI::PropertyCombo("Model Front Faces", front, c_Fronts, 2,
				"Which way the front of a model faces in its file, for models with no Front Faces setting of their own "
				"(Content panel, right-click a model). Blender and most glTF models face +Z; Bellows' forward is -Z"))
			{
				config.ModelFrontFaces = front == 1 ? ModelFront::PositiveZ : ModelFront::NegativeZ;
				changed = true;
				project->ApplyImportSettings();
				m_Editor.ReloadPlacedMeshes();
			}
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
		UI::SectionHeader("Audio buses");
		if (UI::BeginPropertyGrid("ProjectAudio"))
		{
			for (const std::string& bus : AudioEngine::GetBusNames())
			{
				float& volume = config.BusVolumes[bus];
				if (UI::PropertySlider(bus.c_str(), volume, 0.0f, 1.0f, "%.2f", "Starting volume of this bus in the game"))
				{
					changed = true;
					// Heard right away in the editor, too.
					AudioEngine::SetBusVolume(bus, volume);
				}
			}
			UI::EndPropertyGrid();
		}
		UI::SectionHeader("Input actions");
		if (DrawInputActions(config.InputActions))
		{
			changed = true;
			// Play mode and scripts see the change right away.
			project->ApplyInputSettings();
		}
		m_ProjectDirty |= changed;
		ImGui::Spacing();
		UI::TextMuted(project->GetProjectFilePath().string().c_str());
	}

	bool SettingsPanel::DrawInputActions(InputActionMap& actions)
	{
		bool changed = false;
		ImGui::PushTextWrapPos(0.0f);
		UI::TextMuted("Scripts ask for actions by name, e.g. is_action_down(\"jump\").");
		ImGui::PopTextWrapPos();
		ImGui::Spacing();

		std::string removeAction;
		for (auto& [action, bindings] : actions)
		{
			ImGui::PushID(action.c_str());
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(action.c_str());
			ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight() * 2.0f - ImGui::GetStyle().ItemSpacing.x);
			if (UI::IconButton(ICON_PLUS, "Add an input to this action"))
			{
				m_NewBindingDevice = 0;
				m_NewBindingIndex = 0;
				ImGui::OpenPopup("AddInput");
			}
			ImGui::SameLine();
			if (UI::IconButton(ICON_TRASH_2, "Delete this action"))
				removeAction = action;

			// Each input is a chip; clicking it removes the input. Chips wrap at the panel's right edge.
			const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			int removeBinding = -1;
			for (size_t i = 0; i < bindings.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				const std::string label = bindings[i].ToString() + "  " ICON_X;
				const float width = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
				if (i > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width < rowRight)
					ImGui::SameLine();
				if (ImGui::SmallButton(label.c_str()))
					removeBinding = static_cast<int>(i);
				UI::Tooltip("Remove this input");
				ImGui::PopID();
			}
			if (bindings.empty())
				UI::TextMuted("No inputs yet: this action is never down.");
			if (removeBinding >= 0)
			{
				bindings.erase(bindings.begin() + removeBinding);
				changed = true;
			}

			if (ImGui::BeginPopup("AddInput"))
			{
				const char* devices[] = { "Key", "Mouse", "Gamepad" };
				std::vector<const char*> names;
				if (m_NewBindingDevice == 0)
					for (const KeyName& entry : GetKeyCodeNames()) names.push_back(entry.Name);
				else if (m_NewBindingDevice == 1)
					for (const MouseButtonName& entry : GetMouseButtonNames()) names.push_back(entry.Name);
				else
					for (const GamepadButtonName& entry : GetGamepadButtonNames()) names.push_back(entry.Name);
				m_NewBindingIndex = std::clamp(m_NewBindingIndex, 0, static_cast<int>(names.size()) - 1);

				ImGui::SetNextItemWidth(120.0f);
				if (ImGui::Combo("##Device", &m_NewBindingDevice, devices, 3))
					m_NewBindingIndex = 0;
				ImGui::SameLine();
				ImGui::SetNextItemWidth(160.0f);
				ImGui::Combo("##Input", &m_NewBindingIndex, names.data(), static_cast<int>(names.size()), 12);
				ImGui::SameLine();
				if (ImGui::Button("Add"))
				{
					const std::string text = std::string(devices[m_NewBindingDevice]) + ":" + names[m_NewBindingIndex];
					std::optional<InputBinding> binding = InputBinding::Parse(text);
					if (binding && std::find(bindings.begin(), bindings.end(), *binding) == bindings.end())
					{
						bindings.push_back(*binding);
						changed = true;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
			ImGui::Spacing();
			ImGui::PopID();
		}
		if (!removeAction.empty())
		{
			actions.erase(removeAction);
			changed = true;
		}

		ImGui::Separator();
		const char* addLabel = ICON_PLUS " Add action";
		const float addWidth = ImGui::CalcTextSize(addLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addWidth - ImGui::GetStyle().ItemSpacing.x);
		const bool submitted = ImGui::InputTextWithHint("##NewAction", "new action name", &m_NewActionName, ImGuiInputTextFlags_EnterReturnsTrue);
		const bool canAdd = !m_NewActionName.empty() && actions.find(m_NewActionName) == actions.end();
		ImGui::SameLine();
		ImGui::BeginDisabled(!canAdd);
		if ((ImGui::Button(addLabel) || submitted) && canAdd)
		{
			actions[m_NewActionName];
			m_NewActionName.clear();
			changed = true;
		}
		ImGui::EndDisabled();
		if (ImGui::Button("Add default actions"))
		{
			for (auto& [action, bindings] : InputMap::GetDefaultActions())
				changed |= actions.emplace(action, bindings).second;
		}
		UI::Tooltip("Adds move_left, move_right, move_forward, move_back, jump, interact and pause if they are missing");
		return changed;
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
			changed |= UI::Property("Show Game UI", settings.ShowUI, "Draw the game's UI canvases over the 3D view while editing (the game camera always shows them)");
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
