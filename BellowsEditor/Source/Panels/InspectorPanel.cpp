#include "InspectorPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ScriptEditorPanel.h"
#include "ScriptDocument.h"
#include "UITemplates.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scene/EntityTags.h"
#include "Bellows/Scene/ParticleSystem.h"
#include "Bellows/Scene/TrailSystem.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/UI/UILayout.h"
#include "Bellows/UI/UIWidgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <vector>

namespace Bellows {

	namespace {

		// A strip showing a curve over a particle's life (left born, right dies) with its keys.
		void DrawCurvePlot(const std::vector<ParticleCurveKey>& keys, float maximum)
		{
			const float scale = Theme::GetScale();
			const ImVec2 size(ImGui::GetContentRegionAvail().x, 56.0f * scale);
			const ImVec2 start = ImGui::GetCursorScreenPos();
			ImGui::Dummy(size);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), Theme::Field, 6.0f * scale);
			const float top = std::max(maximum, 1e-3f);
			auto point = [&](float t, float value)
			{
				return ImVec2(start.x + 4.0f * scale + t * (size.x - 8.0f * scale),
					start.y + size.y - 4.0f * scale - std::clamp(value / top, 0.0f, 1.0f) * (size.y - 8.0f * scale));
			};
			ImVec2 previous = point(0.0f, ParticleSystem::EvaluateCurve(keys, 0.0f, 1.0f));
			for (int i = 1; i <= 48; i++)
			{
				const float t = static_cast<float>(i) / 48.0f;
				const ImVec2 next = point(t, ParticleSystem::EvaluateCurve(keys, t, 1.0f));
				drawList->AddLine(previous, next, Theme::NeutralBright, 2.0f * scale);
				previous = next;
			}
			for (const ParticleCurveKey& key : keys)
				drawList->AddCircleFilled(point(key.Time, key.Value), 3.5f * scale, Theme::Accent);
		}

		// A strip showing a gradient over a particle's life over a checkerboard (so alpha shows).
		void DrawGradientBar(const std::vector<ParticleColorKey>& keys)
		{
			const float scale = Theme::GetScale();
			const ImVec2 size(ImGui::GetContentRegionAvail().x, 22.0f * scale);
			const ImVec2 start = ImGui::GetCursorScreenPos();
			ImGui::Dummy(size);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			const float cell = 6.0f * scale;
			for (float x = 0.0f; x < size.x; x += cell)
			{
				for (float y = 0.0f; y < size.y; y += cell)
				{
					const bool dark = (static_cast<int>(x / cell) + static_cast<int>(y / cell)) % 2 == 0;
					drawList->AddRectFilled(ImVec2(start.x + x, start.y + y), ImVec2(start.x + std::min(x + cell, size.x), start.y + std::min(y + cell, size.y)),
						dark ? IM_COL32(70, 70, 80, 255) : IM_COL32(110, 110, 120, 255));
				}
			}
			const int steps = 48;
			for (int i = 0; i < steps; i++)
			{
				const float t0 = static_cast<float>(i) / steps, t1 = static_cast<float>(i + 1) / steps;
				const auto colorAt = [&](float t)
				{
					const glm::vec4 color = ParticleSystem::EvaluateGradient(keys, t, glm::vec4(1.0f));
					return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
				};
				const ImU32 a = colorAt(t0), b = colorAt(t1);
				drawList->AddRectFilledMultiColor(ImVec2(start.x + t0 * size.x, start.y), ImVec2(start.x + t1 * size.x, start.y + size.y), a, b, b, a);
			}
			drawList->AddRect(start, ImVec2(start.x + size.x, start.y + size.y), Theme::Border);
		}

		// A row of small preset buttons that wraps to the next line instead of running off the panel.
		// Returns the index of the one clicked, or -1.
		template<typename Preset>
		int PresetButtons(const std::vector<Preset>& presets)
		{
			int clicked = -1;
			const ImGuiStyle& style = ImGui::GetStyle();
			const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
			for (size_t i = 0; i < presets.size(); i++)
			{
				const float width = ImGui::CalcTextSize(presets[i].Name).x + style.FramePadding.x * 2.0f;
				if (i > 0 && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
					ImGui::SameLine();
				if (ImGui::SmallButton(presets[i].Name))
					clicked = static_cast<int>(i);
			}
			return clicked;
		}

		// The time of key 'index' stays between its neighbors, so dragging never reorders the keys.
		template<typename Key>
		void KeyTimeRange(const std::vector<Key>& keys, size_t index, float& minimum, float& maximum)
		{
			minimum = index > 0 ? keys[index - 1].Time : 0.0f;
			maximum = index + 1 < keys.size() ? keys[index + 1].Time : 1.0f;
		}

		// What a curve or gradient editor needs to know about the setting it edits.
		struct CurveEditorText
		{
			const char* Id;				// Unique within the component
			const char* Label;			// "Size Over Life"
			const char* ModeTooltip;
			const char* TimeTooltip;	// What the 0 to 1 of a key's time means here
		};

		struct CurvePreset
		{
			const char* Name;
			std::vector<ParticleCurveKey> Keys;
		};

		struct GradientPreset
		{
			const char* Name;
			std::vector<ParticleColorKey> Keys;
		};

		// A size-like curve, multiplying 'start': its mode (a straight line to 'end', or the curve), and
		// the curve's presets, plot and keys. 'endLabel' names the line's end value when there's no curve.
		void DrawCurveEditor(const CurveEditorText& text, std::vector<ParticleCurveKey>& curve, float start, float& end,
			const char* endLabel, const char* endTooltip, const char* valueTooltip, const std::vector<CurvePreset>& presets)
		{
			using Limits = ParticleEmitterComponent;
			int mode = curve.empty() ? 0 : 1;
			static const char* const s_Modes[] = { "Start to End", "Curve" };
			if (UI::BeginPropertyGrid(text.Id))
			{
				if (UI::PropertyCombo(text.Label, mode, s_Modes, 2, text.ModeTooltip))
				{
					if (mode == 1)
						curve = { { 0.0f, 1.0f }, { 1.0f, start > 0.0f ? end / start : 0.0f } };
					else
						curve.clear();
				}
				if (curve.empty())
					UI::Property(endLabel, end, 0.01f, 0.0f, Limits::MaxSize, "%.2f m", endTooltip);
				UI::EndPropertyGrid();
			}
			if (curve.empty())
				return;
			ImGui::PushID(text.Id);
			if (const int preset = PresetButtons(presets); preset >= 0)
				curve = presets[static_cast<size_t>(preset)].Keys;
			float top = 1.0f;
			for (const ParticleCurveKey& key : curve)
				top = std::max(top, key.Value);
			DrawCurvePlot(curve, top);
			size_t remove = curve.size();
			for (size_t i = 0; i < curve.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				float minimum, maximum;
				KeyTimeRange(curve, i, minimum, maximum);
				const ImGuiStyle& style = ImGui::GetStyle();
				const float removeWidth = ImGui::CalcTextSize(ICON_X).x + style.FramePadding.x * 2.0f;
				const float width = (ImGui::GetContentRegionAvail().x - removeWidth - style.ItemSpacing.x * 2.0f) * 0.5f;
				ImGui::SetNextItemWidth(width);
				ImGui::SliderFloat("##Time", &curve[i].Time, minimum, maximum, "at %.2f");
				ImGui::SetItemTooltip("%s", text.TimeTooltip);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(width);
				ImGui::DragFloat("##Value", &curve[i].Value, 0.01f, 0.0f, Limits::MaxSize, "x %.2f");
				ImGui::SetItemTooltip("%s", valueTooltip);
				ImGui::SameLine();
				ImGui::BeginDisabled(curve.size() <= 1);
				if (ImGui::SmallButton(ICON_X))
					remove = i;
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			if (remove < curve.size())
				curve.erase(curve.begin() + static_cast<std::ptrdiff_t>(remove));
			ImGui::BeginDisabled(curve.size() >= Limits::MaxCurveKeys);
			if (ImGui::SmallButton(ICON_PLUS "  Add key"))
			{
				// Halfway along the widest gap, on the curve, so the shape doesn't change.
				size_t widest = 0;
				float gap = curve.front().Time;
				float time = gap * 0.5f;
				for (size_t i = 1; i <= curve.size(); i++)
				{
					const float from = curve[i - 1].Time, to = i < curve.size() ? curve[i].Time : 1.0f;
					if (to - from > gap)
					{
						gap = to - from;
						time = (from + to) * 0.5f;
						widest = i;
					}
				}
				curve.insert(curve.begin() + static_cast<std::ptrdiff_t>(widest), { time, ParticleSystem::EvaluateCurve(curve, time, 1.0f) });
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}

		// A color gradient: its mode (a straight blend from 'startColor' to 'endColor', or the gradient),
		// and the gradient's presets, bar and keys.
		void DrawGradientEditor(const CurveEditorText& text, std::vector<ParticleColorKey>& gradient, glm::vec4& startColor, glm::vec4& endColor,
			const std::vector<GradientPreset>& presets)
		{
			using Limits = ParticleEmitterComponent;
			int mode = gradient.empty() ? 0 : 1;
			static const char* const s_Modes[] = { "Start to End", "Gradient" };
			if (UI::BeginPropertyGrid(text.Id))
			{
				if (UI::PropertyCombo(text.Label, mode, s_Modes, 2, text.ModeTooltip))
				{
					if (mode == 1)
						gradient = { { 0.0f, startColor }, { 1.0f, endColor } };
					else
						gradient.clear();
				}
				if (gradient.empty())
				{
					UI::PropertyColor("Start Color", startColor);
					UI::PropertyColor("End Color", endColor, false, "Set its alpha to 0 to fade out");
				}
				UI::EndPropertyGrid();
			}
			if (gradient.empty())
				return;
			ImGui::PushID(text.Id);
			if (const int preset = PresetButtons(presets); preset >= 0)
				gradient = presets[static_cast<size_t>(preset)].Keys;
			DrawGradientBar(gradient);
			size_t remove = gradient.size();
			for (size_t i = 0; i < gradient.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				float minimum, maximum;
				KeyTimeRange(gradient, i, minimum, maximum);
				ImGui::ColorEdit4("##Color", &gradient[i].Color.r, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
				ImGui::SameLine();
				const ImGuiStyle& style = ImGui::GetStyle();
				const float removeWidth = ImGui::CalcTextSize(ICON_X).x + style.FramePadding.x * 2.0f;
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - removeWidth - style.ItemSpacing.x);
				ImGui::SliderFloat("##Time", &gradient[i].Time, minimum, maximum, "at %.2f");
				ImGui::SetItemTooltip("%s", text.TimeTooltip);
				ImGui::SameLine();
				ImGui::BeginDisabled(gradient.size() <= 1);
				if (ImGui::SmallButton(ICON_X))
					remove = i;
				ImGui::EndDisabled();
				ImGui::PopID();
			}
			if (remove < gradient.size())
				gradient.erase(gradient.begin() + static_cast<std::ptrdiff_t>(remove));
			ImGui::BeginDisabled(gradient.size() >= Limits::MaxCurveKeys);
			if (ImGui::SmallButton(ICON_PLUS "  Add key"))
			{
				size_t widest = 0;
				float gap = gradient.front().Time;
				float time = gap * 0.5f;
				for (size_t i = 1; i <= gradient.size(); i++)
				{
					const float from = gradient[i - 1].Time, to = i < gradient.size() ? gradient[i].Time : 1.0f;
					if (to - from > gap)
					{
						gap = to - from;
						time = (from + to) * 0.5f;
						widest = i;
					}
				}
				gradient.insert(gradient.begin() + static_cast<std::ptrdiff_t>(widest), { time, ParticleSystem::EvaluateGradient(gradient, time, glm::vec4(1.0f)) });
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}

		const std::vector<CurvePreset>& SizePresets()
		{
			static const std::vector<CurvePreset> s_Presets = {
				{ "Grow, shrink", { { 0.0f, 0.0f }, { 0.25f, 1.0f }, { 1.0f, 0.0f } } },
				{ "Pop", { { 0.0f, 0.2f }, { 0.08f, 1.3f }, { 0.2f, 1.0f }, { 1.0f, 0.8f } } },
				{ "Grow", { { 0.0f, 0.1f }, { 1.0f, 1.0f } } },
				{ "Shrink", { { 0.0f, 1.0f }, { 1.0f, 0.0f } } },
				{ "Steady", { { 0.0f, 1.0f }, { 1.0f, 1.0f } } },
			};
			return s_Presets;
		}

		const std::vector<CurvePreset>& TrailWidthPresets()
		{
			static const std::vector<CurvePreset> s_Presets = {
				{ "Taper", { { 0.0f, 1.0f }, { 1.0f, 0.0f } } },
				{ "Slash", { { 0.0f, 0.2f }, { 0.15f, 1.0f }, { 1.0f, 0.0f } } },
				{ "Comet", { { 0.0f, 1.0f }, { 0.1f, 0.6f }, { 1.0f, 0.0f } } },
				{ "Steady", { { 0.0f, 1.0f }, { 1.0f, 1.0f } } },
			};
			return s_Presets;
		}

		const std::vector<GradientPreset>& ColorPresets()
		{
			static const std::vector<GradientPreset> s_Presets = {
				{ "Fire", { { 0.0f, { 1.0f, 0.95f, 0.7f, 1.0f } }, { 0.25f, { 1.0f, 0.55f, 0.1f, 1.0f } }, { 0.7f, { 0.8f, 0.15f, 0.02f, 0.6f } }, { 1.0f, { 0.2f, 0.05f, 0.02f, 0.0f } } } },
				{ "Magic", { { 0.0f, { 1.0f, 1.0f, 1.0f, 1.0f } }, { 0.2f, { 0.45f, 0.7f, 1.0f, 1.0f } }, { 0.7f, { 0.5f, 0.2f, 1.0f, 0.7f } }, { 1.0f, { 0.3f, 0.1f, 0.8f, 0.0f } } } },
				{ "Poison", { { 0.0f, { 0.8f, 1.0f, 0.5f, 1.0f } }, { 0.5f, { 0.3f, 0.9f, 0.2f, 0.8f } }, { 1.0f, { 0.1f, 0.4f, 0.05f, 0.0f } } } },
				{ "Smoke", { { 0.0f, { 0.6f, 0.6f, 0.6f, 0.0f } }, { 0.15f, { 0.5f, 0.5f, 0.5f, 0.6f } }, { 1.0f, { 0.3f, 0.3f, 0.3f, 0.0f } } } },
				{ "Fade", { { 0.0f, { 1.0f, 1.0f, 1.0f, 1.0f } }, { 1.0f, { 1.0f, 1.0f, 1.0f, 0.0f } } } },
			};
			return s_Presets;
		}

		// Draws one component section when the entity has it. 'draw' receives the component.
		template<typename T, typename DrawFunction>
		void DrawComponent(EditorLayer& editor, Entity entity, const char* icon, const char* name, DrawFunction draw, bool canRemove = true)
		{
			if (!entity.HasComponent<T>())
				return;

			bool remove = false;
			bool reset = false;
			const bool open = UI::ComponentHeader(icon, name, canRemove, remove, reset);
			if (open)
			{
				ImGui::PushID(name);
				ImGui::Indent(4.0f * Theme::GetScale());
				draw(entity.GetComponent<T>());
				ImGui::Unindent(4.0f * Theme::GetScale());
				ImGui::PopID();
				ImGui::Spacing();
			}

			if (reset)
			{
				if constexpr (std::is_same_v<T, TransformComponent>)
					entity.GetComponent<T>() = TransformComponent();
				else
					entity.GetComponent<T>() = T();
				editor.CommitChange(std::string("Reset ") + name);
			}
			if (remove)
			{
				entity.RemoveComponent<T>();
				editor.CommitChange(std::string("Remove ") + name);
			}
		}

		bool DrawFieldValue(const char* name, ScriptFieldValue& value)
		{
			return std::visit([name](auto& v) -> bool
			{
				using V = std::decay_t<decltype(v)>;
				if constexpr (std::is_same_v<V, bool>)
					return UI::Property(name, v);
				else if constexpr (std::is_same_v<V, int64_t>)
				{
					int intValue = static_cast<int>(v);
					const bool changed = UI::Property(name, intValue);
					v = intValue;
					return changed;
				}
				else if constexpr (std::is_same_v<V, double>)
				{
					float floatValue = static_cast<float>(v);
					const bool changed = UI::Property(name, floatValue, 0.05f, 0.0f, 0.0f, "%.3f");
					v = floatValue;
					return changed;
				}
				else if constexpr (std::is_same_v<V, std::string>)
					return UI::Property(name, v);
				else if constexpr (std::is_same_v<V, glm::vec2>)
					return UI::PropertyVec2(name, v);
				else if constexpr (std::is_same_v<V, glm::vec3>)
					return UI::PropertyVec3(name, v);
				else
					return UI::PropertyColor(name, v);
			}, value);
		}

	}

	const ScriptClassInfo* InspectorPanel::GetScriptInfo(const std::string& scriptPath, const std::string& className)
	{
		const std::string key = scriptPath + "|" + className;
		std::error_code error;
		const auto writeTime = std::filesystem::last_write_time(AssetManager::GetAbsolutePath(scriptPath), error);
		if (error)
			return nullptr;

		CachedScriptInfo& cached = m_ScriptInfoCache[key];
		if (cached.WriteTime != writeTime || (!cached.Valid && cached.Error.empty()))
		{
			cached.WriteTime = writeTime;
			cached.Valid = ScriptEngine::GetClassInfo(scriptPath, className, cached.Info);
			cached.Error = cached.Valid ? std::string() : ScriptEngine::GetLastError();
		}
		return cached.Valid ? &cached.Info : nullptr;
	}

	void InspectorPanel::FlushMaterialSave()
	{
		if (m_PendingMaterialSave && !AssetManager::SaveMaterial(m_PendingMaterialSave, m_PendingMaterialPath))
			BW_ERROR("Could not save the material '{}'", m_PendingMaterialPath);
		m_PendingMaterialSave.reset();
		m_PendingMaterialPath.clear();
	}

	void InspectorPanel::OnImGuiRender(bool* open)
	{
		// Material files are saved as soon as the user lets go of the widget, like other asset changes.
		if (m_PendingMaterialSave && !ImGui::IsAnyItemActive())
			FlushMaterialSave();

		if (!UI::BeginPanel("Inspector", open))
		{
			ImGui::End();
			return;
		}

		Entity entity = m_Editor.GetPrimarySelection();
		if (entity)
		{
			DrawEntity(entity);
		}
		else if (const std::string asset = m_Editor.m_ContentBrowserPanel->GetSelectedAsset();
			!asset.empty() && AssetManager::GetAssetType(asset) == AssetType::Material)
		{
			ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
			ImGui::Text(ICON_PALETTE "  %s", asset.c_str());
			ImGui::PopFont();
			ImGui::Separator();
			if (Ref<Material> material = AssetManager::GetMaterial(asset))
				DrawMaterialEditor(material, asset);
		}
		else
		{
			UI::CenteredText("Select an entity to see its components.");
		}

		ImGui::End();
	}

	void InspectorPanel::DrawEntity(Entity entity)
	{
		const float scale = Theme::GetScale();

		// Header: name, ID and Add Component
		std::string name = entity.GetName();
		ImGui::PushFont(Theme::GetFonts().SemiBold, 0.0f);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 140.0f * scale);
		if (ImGui::InputText("##Name", &name) && !name.empty())
			entity.SetName(name);
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::PanelRaised));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(IM_COL32(44, 50, 80, 255)));
		if (ImGui::Button(ICON_PLUS "  Add Component", ImVec2(-FLT_MIN, 0.0f)))
		{
			m_ComponentFilter.clear();
			ImGui::OpenPopup("##AddComponent");
		}
		ImGui::PopStyleColor(2);
		if (ImGui::BeginPopup("##AddComponent"))
		{
			DrawAddComponentMenu(entity);
			ImGui::EndPopup();
		}
		ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
		ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(static_cast<uint64_t>(entity.GetUUID())));
		if (const auto* prefab = entity.TryGetComponent<PrefabComponent>())
		{
			ImGui::SameLine();
			ImGui::TextDisabled(ICON_PACKAGE " %s", prefab->PrefabPath.c_str());
		}
		ImGui::Checkbox("Keep Across Scenes", &entity.GetComponent<TagComponent>().KeepAcrossScenes);
		ImGui::SetItemTooltip("This entity and its children stay when the game changes scene (load_scene), keeping their "
			"scripts, physics and sounds. A scene that has it too doesn't add a second copy.");
		DrawTags(entity);
		ImGui::PopFont();
		ImGui::Spacing();

		ImGui::BeginChild("##Components");

		DrawComponent<TransformComponent>(m_Editor, entity, ICON_MOVE, "Transform", [](TransformComponent& c)
		{
			if (UI::BeginPropertyGrid("Transform"))
			{
				UI::PropertyVec3("Position", c.Translation);
				glm::vec3 rotation = glm::degrees(c.GetRotationEuler());
				if (UI::PropertyVec3("Rotation", rotation, 0.0f, 0.25f))
					c.SetRotationEuler(glm::radians(rotation));
				UI::PropertyVec3("Scale", c.Scale, 1.0f);
				UI::EndPropertyGrid();
			}
		}, false);

		DrawComponent<MeshRendererComponent>(m_Editor, entity, ICON_BOX, "Mesh Renderer", [&](MeshRendererComponent& c) { DrawMeshRenderer(entity, c); });
		DrawComponent<AnimatorComponent>(m_Editor, entity, ICON_PERSON_STANDING, "Animator", [&](AnimatorComponent& c) { DrawAnimator(entity, c); });

		DrawComponent<NavAgentComponent>(m_Editor, entity, ICON_FOOTPRINTS, "Nav Agent", [&](NavAgentComponent& c)
		{
			if (UI::BeginPropertyGrid("NavAgent"))
			{
				UI::Property("Speed", c.Speed, 0.05f, 0.0f, NavAgentComponent::MaxSpeed, "%.2f", "Top speed, meters per second");
				UI::Property("Acceleration", c.Acceleration, 0.1f, NavAgentComponent::MinAcceleration, NavAgentComponent::MaxAcceleration, "%.2f",
					"How fast it speeds up and slows down, meters per second per second");
				UI::Property("Radius", c.Radius, 0.01f, NavAgentComponent::MinRadius, NavAgentComponent::MaxRadius, "%.2f", "Meters it keeps from other agents");
				UI::Property("Height", c.Height, 0.01f, NavAgentComponent::MinHeight, NavAgentComponent::MaxHeight, "%.2f");
				UI::Property("Base Offset", c.BaseOffset, 0.01f, -NavAgentComponent::MaxBaseOffset, NavAgentComponent::MaxBaseOffset, "%.2f",
					"How far the entity's origin is above the ground (half a capsule's height, say)");
				UI::Property("Stopping Distance", c.StoppingDistance, 0.01f, NavAgentComponent::MinStoppingDistance, NavAgentComponent::MaxStoppingDistance, "%.2f",
					"It arrives this close to its destination, meters");
				UI::Property("Turn Speed", c.TurnSpeed, 1.0f, 0.0f, NavAgentComponent::MaxTurnSpeed, "%.0f",
					"Degrees per second it turns to face where it walks; 0 keeps it facing as it is");
				UI::Property("Avoid Others", c.AvoidOthers, "Steers around other agents");
				c.Clamp();
				UI::EndPropertyGrid();
			}
			const RigidBodyComponent* body = entity.TryGetComponent<RigidBodyComponent>();
			const char* warning = nullptr;
			if (body && body->Type == RigidBodyType::Dynamic)
				warning = "Physics moves a dynamic body, so this agent can't: make the Rigid Body Kinematic, or remove it.";
			else if (!entity.GetScene()->GetNavMesh())
				warning = "This scene has no navmesh yet: bake one in Scene Settings > Navigation.";
			if (warning)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Warning));
				ImGui::TextWrapped(ICON_TRIANGLE_ALERT "  %s", warning);
				ImGui::PopStyleColor();
			}
		});

		DrawComponent<CameraComponent>(m_Editor, entity, ICON_CAMERA, "Camera", [](CameraComponent& c)
		{
			if (UI::BeginPropertyGrid("Camera"))
			{
				const char* projections[] = { "Perspective", "Orthographic" };
				int projection = static_cast<int>(c.Projection);
				if (UI::PropertyCombo("Projection", projection, projections, 2))
					c.Projection = static_cast<ProjectionType>(projection);
				if (c.Projection == ProjectionType::Perspective)
					UI::PropertyAngle("Field of View", c.FieldOfView, 1.0f, 179.0f, "Vertical field of view");
				else
					UI::Property("Size", c.OrthographicSize, 0.1f, 0.01f, 10000.0f, "%.2f", "Visible height in world units");
				UI::Property("Near Clip", c.NearClip, 0.01f, 0.001f, 100.0f);
				UI::Property("Far Clip", c.FarClip, 1.0f, 1.0f, 100000.0f, "%.1f");
				UI::Property("Primary", c.Primary, "The primary camera renders the game");
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<DirectionalLightComponent>(m_Editor, entity, ICON_SUN, "Directional Light", [](DirectionalLightComponent& c)
		{
			if (UI::BeginPropertyGrid("DirectionalLight"))
			{
				UI::PropertyColor("Color", c.Color);
				UI::Property("Intensity", c.Intensity, 0.02f, 0.0f, 100.0f, "%.2f");
				UI::Property("Cast Shadows", c.CastShadows);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<PointLightComponent>(m_Editor, entity, ICON_LIGHTBULB, "Point Light", [](PointLightComponent& c)
		{
			if (UI::BeginPropertyGrid("PointLight"))
			{
				UI::PropertyColor("Color", c.Color);
				UI::Property("Intensity", c.Intensity, 0.1f, 0.0f, 10000.0f, "%.1f");
				UI::Property("Range", c.Range, 0.05f, 0.01f, 1000.0f, "%.2f");
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<SpotLightComponent>(m_Editor, entity, ICON_FLASHLIGHT, "Spot Light", [](SpotLightComponent& c)
		{
			if (UI::BeginPropertyGrid("SpotLight"))
			{
				UI::PropertyColor("Color", c.Color);
				UI::Property("Intensity", c.Intensity, 0.1f, 0.0f, 10000.0f, "%.1f");
				UI::Property("Range", c.Range, 0.05f, 0.01f, 1000.0f, "%.2f");
				UI::PropertyAngle("Inner Angle", c.InnerAngle, 0.0f, 89.0f);
				UI::PropertyAngle("Outer Angle", c.OuterAngle, 0.1f, 89.0f);
				c.InnerAngle = std::min(c.InnerAngle, c.OuterAngle);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<SkyLightComponent>(m_Editor, entity, ICON_CLOUD, "Sky Light", [](SkyLightComponent& c)
		{
			if (UI::BeginPropertyGrid("SkyLight"))
			{
				UI::PropertyAsset("Environment", c.EnvironmentPath, AssetType::Environment,
					"An equirectangular .hdr image (e.g. from polyhaven.com). Empty uses the procedural sky.");
				UI::Property("Intensity", c.Intensity, 0.01f, 0.0f, 20.0f, "%.2f");
				UI::PropertyAngle("Rotation", c.Rotation, -180.0f, 180.0f);
				UI::PropertySlider("Background Blur", c.SkyboxBlur, 0.0f, 6.0f, "%.1f");
				if (c.EnvironmentPath.empty())
				{
					UI::PropertyColor("Zenith", c.Sky.ZenithColor);
					UI::PropertyColor("Horizon", c.Sky.HorizonColor);
					UI::PropertyColor("Ground", c.Sky.GroundColor);
					UI::Property("Sun Glow", c.Sky.SunIntensity, 0.5f, 0.0f, 500.0f, "%.0f");
				}
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<RigidBodyComponent>(m_Editor, entity, ICON_ATOM, "Rigid Body", [](RigidBodyComponent& c)
		{
			if (UI::BeginPropertyGrid("RigidBody"))
			{
				const char* types[] = { "Static", "Dynamic", "Kinematic" };
				int type = static_cast<int>(c.Type);
				if (UI::PropertyCombo("Type", type, types, 3, "Static never moves, Dynamic is simulated, Kinematic is moved by scripts"))
					c.Type = static_cast<RigidBodyType>(type);
				if (c.Type == RigidBodyType::Dynamic)
				{
					UI::Property("Mass", c.Mass, 0.05f, 0.001f, 100000.0f, "%.2f kg");
					UI::Property("Gravity Scale", c.GravityScale, 0.01f, -10.0f, 10.0f, "%.2f");
					UI::Property("Linear Damping", c.LinearDamping, 0.01f, 0.0f, 10.0f, "%.2f");
					UI::Property("Angular Damping", c.AngularDamping, 0.01f, 0.0f, 10.0f, "%.2f");
				}
				UI::PropertySlider("Friction", c.Friction, 0.0f, 2.0f);
				UI::PropertySlider("Bounciness", c.Restitution, 0.0f, 1.0f);
				UI::Property("Is Trigger", c.IsTrigger, "Reports overlaps (on_trigger_enter) without colliding");
				if (c.Type == RigidBodyType::Dynamic)
				{
					UI::Property("Lock Rotation X", c.LockRotationX);
					UI::Property("Lock Rotation Y", c.LockRotationY);
					UI::Property("Lock Rotation Z", c.LockRotationZ);
					UI::Property("Continuous", c.ContinuousCollision, "Prevents fast objects from tunneling through thin colliders");
				}
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<BoxColliderComponent>(m_Editor, entity, ICON_BOX, "Box Collider", [](BoxColliderComponent& c)
		{
			if (UI::BeginPropertyGrid("BoxCollider"))
			{
				UI::PropertyVec3("Half Extents", c.HalfExtents, 0.5f);
				UI::PropertyVec3("Offset", c.Offset);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<SphereColliderComponent>(m_Editor, entity, ICON_CIRCLE, "Sphere Collider", [](SphereColliderComponent& c)
		{
			if (UI::BeginPropertyGrid("SphereCollider"))
			{
				UI::Property("Radius", c.Radius, 0.01f, 0.001f, 10000.0f);
				UI::PropertyVec3("Offset", c.Offset);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<CapsuleColliderComponent>(m_Editor, entity, ICON_CIRCLE, "Capsule Collider", [](CapsuleColliderComponent& c)
		{
			if (UI::BeginPropertyGrid("CapsuleCollider"))
			{
				UI::Property("Radius", c.Radius, 0.01f, 0.001f, 10000.0f);
				UI::Property("Half Height", c.HalfHeight, 0.01f, 0.001f, 10000.0f, "%.3f", "Half the length of the cylinder part");
				UI::PropertyVec3("Offset", c.Offset);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<MeshColliderComponent>(m_Editor, entity, ICON_SHAPES, "Mesh Collider", [](MeshColliderComponent&)
		{
			UI::TextMuted("Uses the Mesh Renderer's triangles. Static bodies only.");
		});

		DrawComponent<ScriptComponent>(m_Editor, entity, ICON_FILE_CODE, "Script", [&](ScriptComponent& c) { DrawScriptComponent(entity, c); });

		DrawComponent<ParticleEmitterComponent>(m_Editor, entity, ICON_FLAME, "Particle Emitter", [&](ParticleEmitterComponent& c)
		{
			using Limits = ParticleEmitterComponent;
			if (UI::BeginPropertyGrid("ParticleEmitter"))
			{
				UI::Property("Play On Start", c.PlayOnStart, "Starts by itself when the game starts (and in the editor)");
				UI::Property("Looping", c.Looping, "Starts a new cycle when one ends; otherwise it stops after one");
				UI::Property("Duration", c.Duration, 0.05f, Limits::MinDuration, Limits::MaxDuration, "%.2f s", "Seconds one cycle lasts");
				UI::Property("Rate", c.Rate, 0.5f, 0.0f, Limits::MaxRate, "%.1f / s", "Particles per second while playing");
				UI::Property("Burst", c.Burst, 0u, Limits::MaxBurst, "Particles sent at once when each cycle starts");
				UI::Property("Max Particles", c.MaxParticles, 0u, Limits::MaxMaxParticles, "The most alive at once");
				UI::EndPropertyGrid();
			}
			UI::SectionHeader("Particles");
			if (UI::BeginPropertyGrid("ParticleEmitterMotion"))
			{
				UI::Property("Lifetime", c.Lifetime, 0.05f, Limits::MinLifetime, Limits::MaxLifetime, "%.2f s");
				UI::PropertySlider("Lifetime Variation", c.LifetimeVariation, 0.0f, 1.0f, "%.2f", "How much lifetimes vary either way (1 = from none to double)");
				UI::Property("Speed", c.Speed, 0.05f, 0.0f, Limits::MaxSpeed, "%.2f m/s");
				UI::PropertySlider("Speed Variation", c.SpeedVariation, 0.0f, 1.0f, "%.2f", "How much speeds vary either way");
				UI::PropertySlider("Spread", c.SpreadAngle, 0.0f, 180.0f, "%.0f deg", "Degrees from the entity's up (+Y) they may leave at: 0 a line, 180 every way");
				UI::Property("Outward", c.Outward, "Leave away from the center instead of up (bursts, rings that expand); Spread tilts them");
				static const char* const s_Shapes[] = { "Point", "Sphere", "Box", "Ring", "Line" };
				int shape = static_cast<int>(c.Shape);
				if (UI::PropertyCombo("Shape", shape, s_Shapes, 5, "Where particles are born: Ring is a circle around the entity's up, Line runs along its X"))
					c.Shape = static_cast<ParticleShape>(shape);
				if (c.Shape == ParticleShape::Sphere || c.Shape == ParticleShape::Ring)
					UI::Property("Radius", c.ShapeSize.x, 0.01f, 0.0f, Limits::MaxShapeSize, "%.2f m");
				else if (c.Shape == ParticleShape::Line)
					UI::Property("Length", c.ShapeSize.x, 0.01f, 0.0f, Limits::MaxShapeSize, "%.2f m", "Along the entity's X, centered on it");
				else if (c.Shape == ParticleShape::Box)
					UI::PropertyVec3("Half Size", c.ShapeSize, 0.5f, 0.01f, "Half the box's size on each axis, meters");
				UI::Property("Gravity Scale", c.GravityScale, 0.01f, -Limits::MaxGravityScale, Limits::MaxGravityScale, "%.2f",
					"How much the scene's gravity pulls them: 0 none, 1 full, below 0 they rise");
				UI::Property("Drag", c.Drag, 0.01f, 0.0f, Limits::MaxDrag, "%.2f", "Air resistance: how quickly they slow down");
				UI::Property("Noise", c.NoiseStrength, 0.05f, 0.0f, Limits::MaxNoise, "%.2f m/s²", "A swirling push: wisps, embers, curling smoke");
				if (c.NoiseStrength > 0.0f)
				{
					UI::Property("Noise Frequency", c.NoiseFrequency, 0.01f, 0.0f, Limits::MaxNoiseFrequency, "%.2f / m", "How tight the swirls are");
					UI::Property("Noise Speed", c.NoiseSpeed, 0.01f, 0.0f, Limits::MaxNoiseSpeed, "%.2f", "How fast the swirls change");
				}
				UI::Property("World Space", c.WorldSpace, "Born particles stay where they are when the entity moves; off, they move with it");
				UI::EndPropertyGrid();
			}
			UI::SectionHeader("Rotation");
			if (UI::BeginPropertyGrid("ParticleEmitterRotation"))
			{
				static const char* const s_Facings[] = { "Camera", "Stretched", "Flat" };
				int facing = static_cast<int>(c.Facing);
				if (UI::PropertyCombo("Facing", facing, s_Facings, 3,
					"Camera faces the camera; Stretched stretches along the way each moves (streaks, rain); Flat lies across the entity's up (ground rings, shockwaves)"))
					c.Facing = static_cast<ParticleFacing>(facing);
				if (c.Facing == ParticleFacing::Stretched)
				{
					UI::Property("Stretch", c.StretchAmount, 0.005f, 0.0f, Limits::MaxStretch, "%.3f s", "Seconds of motion its length shows: faster ones are longer");
				}
				else
				{
					UI::PropertySlider("Start Rotation", c.StartRotation, -360.0f, 360.0f, "%.0f deg");
					UI::PropertySlider("Rotation Variation", c.RotationVariation, 0.0f, 180.0f, "%.0f deg", "How much the start rotation varies either way (180: any angle)");
					UI::Property("Spin", c.SpinSpeed, 1.0f, -Limits::MaxSpin, Limits::MaxSpin, "%.0f deg/s", "Degrees per second each turns (below 0 the other way)");
					UI::Property("Spin Variation", c.SpinVariation, 1.0f, 0.0f, Limits::MaxSpin, "%.0f deg/s", "How much the spin varies either way");
				}
				UI::EndPropertyGrid();
			}
			UI::SectionHeader("Look");
			if (UI::BeginPropertyGrid("ParticleEmitterLook"))
			{
				UI::Property("Start Size", c.StartSize, 0.01f, 0.0f, Limits::MaxSize, "%.2f m", "Meters across when born (a size curve multiplies it)");
				UI::EndPropertyGrid();
			}
			DrawCurveEditor({ "ParticleSize", "Size Over Life", "A straight line from Start Size to End Size, or a curve of your own",
				"When, from 0 (born) to 1 (dies)" }, c.SizeCurve, c.StartSize, c.EndSize, "End Size", "Meters across when it dies",
				"The size then, times Start Size", SizePresets());
			DrawGradientEditor({ "ParticleColor", "Color Over Life", "A straight blend from Start Color to End Color, or a gradient of your own",
				"When, from 0 (born) to 1 (dies)" }, c.ColorGradient, c.StartColor, c.EndColor, ColorPresets());
			if (UI::BeginPropertyGrid("ParticleEmitterLookMore"))
			{
				UI::Property("Intensity", c.Intensity, 0.05f, 0.0f, Limits::MaxIntensity, "%.2f", "Brightness; above 1 they glow");
				static const char* const s_Blends[] = { "Additive", "Alpha" };
				int blend = static_cast<int>(c.Blend);
				if (UI::PropertyCombo("Blend", blend, s_Blends, 2, "Additive adds light (fire, sparks); Alpha covers what's behind (smoke, dust)"))
					c.Blend = static_cast<ParticleBlend>(blend);
				UI::PropertyAsset("Texture", c.TexturePath, AssetType::Texture, "The image each particle shows; empty draws a soft round dot");
				if (!c.TexturePath.empty())
				{
					UI::Property("Flipbook Columns", c.FlipbookColumns, 1u, Limits::MaxFlipbookFrames, "An animated sheet's columns of frames (left to right, then top to bottom)");
					UI::Property("Flipbook Rows", c.FlipbookRows, 1u, Limits::MaxFlipbookFrames, "An animated sheet's rows of frames");
					if (c.FlipbookColumns * c.FlipbookRows > 1)
					{
						UI::Property("Flipbook FPS", c.FlipbookFps, 0.5f, 0.0f, Limits::MaxFlipbookFps, "%.1f", "Frames per second; 0 plays the sheet once over each particle's life");
						UI::Property("Random Start Frame", c.FlipbookRandomStart, "Each particle starts on a random frame");
					}
				}
				c.Clamp();
				UI::EndPropertyGrid();
			}
			// The preview runs in the editor too; Restart replays it from the start.
			if (ImGui::Button(ICON_REFRESH_CW "  Restart"))
				ParticleSystem::Reset(c, entity.GetUUID());
			ImGui::SameLine();
			UI::TextMuted((std::to_string(c.RuntimeParticles.size()) + " alive").c_str());
		});

		DrawComponent<TrailComponent>(m_Editor, entity, ICON_SPLINE, "Trail", [&](TrailComponent& c)
		{
			using Limits = TrailComponent;
			if (UI::BeginPropertyGrid("Trail"))
			{
				UI::Property("Emitting", c.Emitting, "Leaves new points while the entity moves; off, the trail lets go and shrinks away");
				UI::Property("Lifetime", c.Lifetime, 0.01f, Limits::MinLifetime, Limits::MaxLifetime, "%.2f s", "Seconds each point lasts: how long the trail is, in time");
				UI::Property("Min Distance", c.MinDistance, 0.005f, Limits::MinMinDistance, Limits::MaxMinDistance, "%.3f m",
					"Meters the entity moves before another point is left (smaller is smoother)");
				UI::Property("Max Points", c.MaxPoints, 2u, Limits::MaxMaxPoints, "The most points it keeps; the oldest go first");
				UI::Property("Start Width", c.StartWidth, 0.01f, 0.0f, Limits::MaxWidth, "%.2f m", "Meters across at the entity (a width curve multiplies it)");
				UI::EndPropertyGrid();
			}
			DrawCurveEditor({ "TrailWidth", "Width Along Trail", "A straight line from Start Width to End Width, or a curve of your own",
				"Where, from 0 (at the entity) to 1 (the tail)" }, c.WidthCurve, c.StartWidth, c.EndWidth, "End Width", "Meters across at its tail",
				"The width there, times Start Width", TrailWidthPresets());
			DrawGradientEditor({ "TrailColor", "Color Along Trail", "A straight blend from Start Color to End Color, or a gradient of your own",
				"Where, from 0 (at the entity) to 1 (the tail)" }, c.ColorGradient, c.StartColor, c.EndColor, ColorPresets());
			if (UI::BeginPropertyGrid("TrailLook"))
			{
				UI::Property("Intensity", c.Intensity, 0.05f, 0.0f, ParticleEmitterComponent::MaxIntensity, "%.2f", "Brightness; above 1 it glows");
				static const char* const s_Blends[] = { "Additive", "Alpha" };
				int blend = static_cast<int>(c.Blend);
				if (UI::PropertyCombo("Blend", blend, s_Blends, 2, "Additive adds light (glows, magic); Alpha covers what's behind (smoke, tire marks)"))
					c.Blend = static_cast<ParticleBlend>(blend);
				UI::PropertyAsset("Texture", c.TexturePath, AssetType::Texture, "An image along the trail; empty is soft toward its edges");
				if (!c.TexturePath.empty())
				{
					static const char* const s_Modes[] = { "Stretch", "Tile" };
					int mode = static_cast<int>(c.TextureMode);
					if (UI::PropertyCombo("Texture Mode", mode, s_Modes, 2, "Stretch lays the image once along the whole trail; Tile repeats it every Tile Length"))
						c.TextureMode = static_cast<TrailTextureMode>(mode);
					if (c.TextureMode == TrailTextureMode::Tile)
						UI::Property("Tile Length", c.TileLength, 0.01f, Limits::MinTileLength, Limits::MaxTileLength, "%.2f m", "Meters one copy of the image covers");
				}
				c.Clamp();
				UI::EndPropertyGrid();
			}
			if (ImGui::Button(ICON_REFRESH_CW "  Clear"))
				TrailSystem::Clear(c);
			ImGui::SameLine();
			// Wrapped, so it fits a narrow Inspector.
			ImGui::PushTextWrapPos(0.0f);
			UI::TextMuted((std::to_string(c.RuntimePoints.size()) + " points. Move the entity to see it.").c_str());
			ImGui::PopTextWrapPos();
		});

		DrawComponent<DecalComponent>(m_Editor, entity, ICON_STICKER, "Decal", [&](DecalComponent& c)
		{
			if (UI::BeginPropertyGrid("Decal"))
			{
				UI::PropertyAsset("Texture", c.TexturePath, AssetType::Texture, "The image it projects; empty projects a plain rectangle of color");
				UI::PropertyColor("Color", c.Color, false, "Tint; alpha is the decal's opacity");
				UI::PropertyVec3("Size", c.Size, 1.0f, 0.01f, "Meters: X the image's width, Y how deep it reaches (down the entity's -Y), Z the image's height");
				UI::PropertySlider("Max Angle", c.MaxAngle, DecalComponent::MinMaxAngle, DecalComponent::MaxMaxAngle, "%.0f deg",
					"How far a surface may turn from the decal's up before the decal fades out on it (so it doesn't smear down walls)");
				UI::Property("Sort Order", c.SortOrder, -DecalComponent::MaxSortOrder, DecalComponent::MaxSortOrder, "Higher decals are drawn on top of lower ones");
				c.Clamp();
				UI::EndPropertyGrid();
			}
			// Wrapped, so it fits a narrow Inspector.
			ImGui::PushTextWrapPos(0.0f);
			UI::TextMuted("Projects onto opaque surfaces inside the green box, down the entity's -Y.");
			ImGui::PopTextWrapPos();
		});

		DrawComponent<AudioSourceComponent>(m_Editor, entity, ICON_VOLUME_2, "Audio Source", [&](AudioSourceComponent& c)
		{
			if (UI::BeginPropertyGrid("AudioSource"))
			{
				UI::PropertyAsset("Clip", c.ClipPath, AssetType::Audio);
				UI::PropertySlider("Volume", c.Volume, 0.0f, 2.0f);
				UI::PropertySlider("Pitch", c.Pitch, 0.1f, 3.0f);
				UI::Property("Loop", c.Loop);
				UI::Property("Play On Start", c.PlayOnStart);
				std::vector<const char*> buses;
				int bus = 0;
				for (const std::string& name : AudioEngine::GetBusNames())
				{
					if (name == c.Bus || (name == "Effects" && !AudioEngine::IsBus(c.Bus)))
						bus = static_cast<int>(buses.size());
					buses.push_back(name.c_str());
				}
				if (UI::PropertyCombo("Bus", bus, buses.data(), static_cast<int>(buses.size()), "Mixer bus: its volume is set in Project Settings or by scripts"))
					c.Bus = buses[bus];
				UI::Property("3D Sound", c.Spatial, "Volume and panning follow the listener");
				if (c.Spatial)
				{
					UI::Property("Min Distance", c.MinDistance, 0.05f, 0.01f, 1000.0f, "%.2f");
					UI::Property("Max Distance", c.MaxDistance, 0.1f, 0.01f, 10000.0f, "%.1f");
				}
				UI::EndPropertyGrid();
			}
		});

		// Text inside a UI canvas is screen text: sized in reference pixels and boxed by its UI rect.
		const bool screenText = entity.GetScene()->IsInsideUICanvas(entity);
		DrawComponent<TextComponent>(m_Editor, entity, ICON_TYPE, "Text", [screenText](TextComponent& c)
		{
			if (UI::BeginPropertyGrid("Text"))
			{
				UI::PropertyMultiline("Text", c.Text);
				UI::PropertyAsset("Font", c.FontPath, AssetType::Font, "A .ttf or .otf font; empty uses the built-in font (Inter)");
				if (screenText)
					UI::Property("Size", c.Size, 0.5f, 0.0f, 1000.0f, "%.1f", "The height of one em, in reference pixels (a line is about 1.2 em)");
				else
					UI::Property("Size", c.Size, 0.01f, 0.0f, 1000.0f, "%.2f", "The height of one em, in meters (a line is about 1.2 em)");
				UI::PropertyColor("Color", c.Color);
				static const char* const c_Alignments[] = { "Left", "Center", "Right" };
				int alignment = static_cast<int>(c.Alignment);
				if (UI::PropertyCombo("Alignment", alignment, c_Alignments, 3))
					c.Alignment = static_cast<TextAlignment>(alignment);
				static const char* const c_VerticalAlignments[] = { "Top", "Middle", "Bottom" };
				int verticalAlignment = static_cast<int>(c.VerticalAlignment);
				if (UI::PropertyCombo("Vertical Alignment", verticalAlignment, c_VerticalAlignments, 3))
					c.VerticalAlignment = static_cast<TextVerticalAlignment>(verticalAlignment);
				if (!screenText && UI::PropertyVec2("Box Size", c.BoxSize, 0.01f, "Width and height of the text's box, in meters; 0 sizes it to the text"))
					c.BoxSize = glm::max(c.BoxSize, glm::vec2(0.0f));
				UI::Property("Wrap", c.Wrap, screenText ? "Wrap lines at the UI rect's width" : "Wrap lines at the box's width");
				static const char* const c_Overflows[] = { "Visible", "Clip", "Ellipsis" };
				int overflow = static_cast<int>(c.Overflow);
				if (UI::PropertyCombo("Overflow", overflow, c_Overflows, 3, "Text past the box: drawn anyway, cut at the box, or ended with \"...\""))
					c.Overflow = static_cast<TextOverflow>(overflow);
				UI::Property("Line Spacing", c.LineSpacing, 0.01f, 0.0f, 10.0f, "%.2f");
				UI::PropertySlider("Outline Width", c.OutlineWidth, 0.0f, 0.1f, "%.3f", "In ems");
				if (c.OutlineWidth > 0.0f)
					UI::PropertyColor("Outline Color", c.OutlineColor);
				if (!screenText)
					UI::Property("Face Camera", c.FaceCamera, "Always turned toward the camera, like a name over a character");
				else
					UI::Property("Theme Color", c.ThemeColor, "Take the color from the canvas's theme (for its widget's state) instead of Color");
				UI::Property("Visible", c.Visible);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<UICanvasComponent>(m_Editor, entity, ICON_MONITOR, "UI Canvas", [](UICanvasComponent& c)
		{
			if (UI::BeginPropertyGrid("UICanvas"))
			{
				if (UI::PropertyVec2("Reference Size", c.ReferenceSize, 1.0f, "The screen size this UI is designed for, in reference pixels"))
					c.ReferenceSize = glm::max(c.ReferenceSize, glm::vec2(1.0f));
				static const char* const c_ScaleModes[] = { "Fit Height", "Fit Width", "Expand", "Constant Pixels" };
				int scaleMode = static_cast<int>(c.ScaleMode);
				if (UI::PropertyCombo("Scale Mode", scaleMode, c_ScaleModes, 4,
					"How the reference size meets the screen: fill its height, fill its width, fit all of it, or one pixel per pixel"))
					c.ScaleMode = static_cast<UIScaleMode>(scaleMode);
				UI::Property("Sort Order", c.SortOrder, 0, 0, "Canvases with a higher sort order draw on top");
				UI::Property("Visible", c.Visible);
				UI::Property("Modal", c.Modal, "While shown, it takes every key, button and click: the game and other UI get nothing (pause menus, dialogs)");
				UI::PropertyAsset("Theme", c.Theme, AssetType::Theme, "A .btheme file its widgets are drawn with; empty uses the built-in MageCap theme");
				UI::EndPropertyGrid();
			}
		});

		// Inside a layout container the rect's place comes from the layout, and only its sizing matters.
		const Entity parent = entity.GetParent();
		const bool inLayout = parent && parent.HasComponent<UILayoutComponent>();
		DrawComponent<UIRectComponent>(m_Editor, entity, ICON_SCAN, "UI Rect", [this, inLayout, entity](UIRectComponent& c)
		{
			if (UI::BeginPropertyGrid("UIRect"))
			{
				if (inLayout)
				{
					static const char* const c_Sizings[] = { "Fixed", "Fill" };
					int widthSizing = static_cast<int>(c.WidthSizing), heightSizing = static_cast<int>(c.HeightSizing);
					if (UI::PropertyCombo("Width", widthSizing, c_Sizings, 2, "Fixed: its own width (Size). Fill: a share of the space left over"))
						c.WidthSizing = static_cast<UISizing>(widthSizing);
					if (UI::PropertyCombo("Height", heightSizing, c_Sizings, 2, "Fixed: its own height (Size). Fill: a share of the space left over"))
						c.HeightSizing = static_cast<UISizing>(heightSizing);
				}
				glm::vec2 position = GetUIRectPosition(c.Pivot, c.OffsetMin, c.OffsetMax);
				if (UI::PropertyVec2("Position", position, 1.0f, "Where the pivot sits relative to the anchors, in reference pixels"))
					SetUIRectPosition(c.Pivot, c.OffsetMin, c.OffsetMax, position);
				glm::vec2 size = GetUIRectSize(c.OffsetMin, c.OffsetMax);
				if (UI::PropertyVec2("Size", size, 1.0f, "In reference pixels; with stretching anchors, added to the stretched size"))
					SetUIRectSize(c.Pivot, c.OffsetMin, c.OffsetMax, size);
				int preset = 0;
				if (!inLayout && UI::PropertyAnchorPreset("Anchors", c.AnchorMin, c.AnchorMax, preset,
					"Where in its parent the rect is attached: a point it keeps its distance from, or edges it stretches with"))
				{
					const bool moveRect = ImGui::GetIO().KeyShift;
					ApplyUIAnchorPreset(entity, static_cast<UIAnchorPreset>(preset), moveRect);
					m_Editor.CommitChange(moveRect ? "Move UI rect to anchors" : "UI anchor preset");
				}
				if (UI::PropertyVec2("Anchor Min", c.AnchorMin, 0.01f, "The parent's point the top-left corner follows: (0, 0) top-left, (1, 1) bottom-right"))
					c.AnchorMin = glm::clamp(c.AnchorMin, 0.0f, 1.0f);
				if (UI::PropertyVec2("Anchor Max", c.AnchorMax, 0.01f, "The parent's point the bottom-right corner follows; different anchors stretch with the parent"))
					c.AnchorMax = glm::clamp(c.AnchorMax, 0.0f, 1.0f);
				if (UI::PropertyVec2("Pivot", c.Pivot, 0.01f, "The point of the rect that stays put when its size changes: (0, 0) top-left, (1, 1) bottom-right"))
					c.Pivot = glm::clamp(c.Pivot, 0.0f, 1.0f);
				UI::PropertyVec2("Offset Min", c.OffsetMin, 1.0f, "The top-left corner's distance from Anchor Min, in reference pixels");
				UI::PropertyVec2("Offset Max", c.OffsetMax, 1.0f, "The bottom-right corner's distance from Anchor Max, in reference pixels");
				UI::Property("Visible", c.Visible, "Hidden rects hide everything under them too");
				UI::Property("Interactable", c.Interactable, "Whether the pointer and focus can reach it and what is under it");
				UI::Property("Focusable", c.Focusable, "Can take keyboard and gamepad focus (Tab, arrows, d-pad)");
				UI::Property("Style Class", c.StyleClass, "A named style of the canvas's theme, like primary or danger; empty for the plain style");
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<UIImageComponent>(m_Editor, entity, ICON_IMAGE, "UI Image", [](UIImageComponent& c)
		{
			if (UI::BeginPropertyGrid("UIImage"))
			{
				UI::PropertyAsset("Texture", c.TexturePath, AssetType::Texture, "An image stretched over the rect; empty draws a plain color");
				UI::PropertyColor("Color", c.Color, false, "Multiplies the texture");
				if (UI::Property("Corner Radius", c.CornerRadius, 0.5f, 0.0f, 10000.0f, "%.1f", "In reference pixels"))
					c.CornerRadius = std::max(c.CornerRadius, 0.0f);
				if (UI::Property("Border Width", c.BorderWidth, 0.25f, 0.0f, 10000.0f, "%.1f", "In reference pixels, drawn inside the rect"))
					c.BorderWidth = std::max(c.BorderWidth, 0.0f);
				if (c.BorderWidth > 0.0f)
					UI::PropertyColor("Border Color", c.BorderColor);
				if (!c.TexturePath.empty() && UI::PropertyVec4("9-Slice Border", c.SliceBorder, 0.25f,
					"Left, top, right and bottom edges of the texture, in texture pixels, that keep their size while the middle stretches"))
					c.SliceBorder = glm::max(c.SliceBorder, glm::vec4(0.0f));
				UI::Property("Visible", c.Visible);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<UIButtonComponent>(m_Editor, entity, ICON_MOUSE_POINTER_CLICK, "UI Button", [entity](UIButtonComponent& c)
		{
			// Each color shows what the button uses: its own, or its theme's until it is changed here.
			Entity owner = entity;
			const auto* rect = owner.TryGetComponent<UIRectComponent>();
			const Ref<const UITheme> theme = owner.GetScene()->GetUIThemeFor(owner);
			const UIWidgetStyle& style = theme->Get(UIThemeWidget::Button, rect ? rect->StyleClass : std::string());
			const auto colorProperty = [&](const char* label, std::optional<glm::vec4>& color, UIWidgetState state, const char* tooltip)
			{
				glm::vec4 value = c.GetColor(state, style);
				const std::string help = std::string(tooltip) + (color ? "" : ". From the theme until you change it");
				if (UI::PropertyColor(label, value, false, help.c_str()))
					color = value;
			};
			if (UI::BeginPropertyGrid("UIButton"))
			{
				colorProperty("Normal Color", c.NormalColor, UIWidgetState::Normal, "Its UI Image's color when nothing is happening");
				colorProperty("Hover Color", c.HoverColor, UIWidgetState::Hover, "Under the pointer");
				colorProperty("Pressed Color", c.PressedColor, UIWidgetState::Pressed, "Held down");
				colorProperty("Disabled Color", c.DisabledColor, UIWidgetState::Disabled, "When its UI Rect is not interactable");
				UI::EndPropertyGrid();
			}
			const bool themed = !c.NormalColor && !c.HoverColor && !c.PressedColor && !c.DisabledColor;
			ImGui::BeginDisabled(themed);
			if (ImGui::Button(ICON_PALETTE "  Use the Theme's Colors", ImVec2(-FLT_MIN, 0.0f)))
				c = UIButtonComponent();
			ImGui::EndDisabled();
		});

		DrawComponent<UIToggleComponent>(m_Editor, entity, ICON_SQUARE_CHECK, "UI Toggle", [](UIToggleComponent& c)
		{
			if (UI::BeginPropertyGrid("UIToggle"))
			{
				UI::Property("On", c.On);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<UISliderComponent>(m_Editor, entity, ICON_SLIDERS_HORIZONTAL, "UI Slider", [](UISliderComponent& c)
		{
			if (UI::BeginPropertyGrid("UISlider"))
			{
				bool changed = UI::Property("Min", c.Min, 0.01f, 0.0f, 0.0f, "%.3f");
				changed |= UI::Property("Max", c.Max, 0.01f, 0.0f, 0.0f, "%.3f");
				changed |= UI::Property("Step", c.Step, 0.01f, 0.0f, 0.0f, "%.3f", "Values snap to steps from Min; 0 for any value");
				changed |= UI::PropertySlider("Value", c.Value, std::min(c.Min, c.Max), std::max(c.Min, c.Max), "%.3f");
				if (changed)
				{
					c.Step = std::max(c.Step, 0.0f);
					c.Value = SnapUISliderValue(c.Value, c.Min, c.Max, c.Step);
				}
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<UIDropdownComponent>(m_Editor, entity, ICON_CHEVRONS_UP_DOWN, "UI Dropdown", [](UIDropdownComponent& c)
		{
			if (UI::BeginPropertyGrid("UIDropdown"))
			{
				std::string lines;
				for (size_t i = 0; i < c.Options.size(); i++)
					lines += (i > 0 ? "\n" : "") + c.Options[i];
				if (UI::PropertyMultiline("Options", lines, 4, "One option per line"))
				{
					c.Options.clear();
					size_t start = 0;
					while (start <= lines.size())
					{
						const size_t end = std::min(lines.find('\n', start), lines.size());
						c.Options.push_back(lines.substr(start, end - start));
						start = end + 1;
					}
					if (c.Options.size() == 1 && c.Options[0].empty())
						c.Options.clear();
				}
				const int last = static_cast<int>(c.Options.size()) - 1;
				if (UI::Property("Selected", c.Selected, -1, std::max(last, -1), "The selected option's index; -1 for none"))
					c.Selected = std::clamp(c.Selected, -1, last);
				c.Selected = std::min(c.Selected, last);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<UITextInputComponent>(m_Editor, entity, ICON_TEXT_CURSOR_INPUT, "UI Text Input", [](UITextInputComponent& c)
		{
			if (UI::BeginPropertyGrid("UITextInput"))
			{
				UI::Property("Placeholder", c.Placeholder, "Shown, dimmed, while the text is empty. The text itself is the Text component's");
				UI::Property("Multi Line", c.MultiLine, "Enter starts a new line; Ctrl+Enter submits");
				UI::Property("Password", c.Password, "Shows a bullet per character, on one line, and can't be copied");
				if (UI::Property("Max Length", c.MaxLength, 0, 100000, "The most characters it takes; 0 for no limit"))
					c.MaxLength = std::max(c.MaxLength, 0);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<UILayoutComponent>(m_Editor, entity, ICON_LAYOUT_GRID, "UI Layout", [](UILayoutComponent& c)
		{
			if (UI::BeginPropertyGrid("UILayout"))
			{
				static const char* const c_Directions[] = { "Vertical", "Horizontal", "Grid" };
				int direction = static_cast<int>(c.Direction);
				if (UI::PropertyCombo("Direction", direction, c_Directions, 3, "One under another, side by side, or in a grid filling rows left to right"))
					c.Direction = static_cast<UILayoutDirection>(direction);
				if (c.Direction == UILayoutDirection::Grid && UI::Property("Columns", c.Columns, 1, 64))
					c.Columns = std::max(c.Columns, 1);
				if (UI::Property("Spacing", c.Spacing, 0.5f, 0.0f, 10000.0f, "%.1f", "Reference pixels between children"))
					c.Spacing = std::max(c.Spacing, 0.0f);
				if (UI::PropertyVec4("Padding", c.Padding, 0.5f, "Left, top, right and bottom, in reference pixels"))
					c.Padding = glm::max(c.Padding, glm::vec4(0.0f));
				static const char* const c_Alignments[] = { "Start", "Center", "End" };
				int mainAlignment = static_cast<int>(c.MainAlignment), crossAlignment = static_cast<int>(c.CrossAlignment);
				if (UI::PropertyCombo("Main Alignment", mainAlignment, c_Alignments, 3, "Where the children sit along the direction when none fills"))
					c.MainAlignment = static_cast<UIAlignment>(mainAlignment);
				if (UI::PropertyCombo("Cross Alignment", crossAlignment, c_Alignments, 3, "Where a child that doesn't fill sits across the direction"))
					c.CrossAlignment = static_cast<UIAlignment>(crossAlignment);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<UIScrollComponent>(m_Editor, entity, ICON_LAYERS, "UI Scroll", [](UIScrollComponent& c)
		{
			if (UI::BeginPropertyGrid("UIScroll"))
			{
				UI::Property("Horizontal", c.Horizontal);
				UI::Property("Vertical", c.Vertical);
				if (UI::PropertyVec2("Scroll Position", c.ScrollPosition, 1.0f, "In reference pixels; kept in range"))
					c.ScrollPosition = glm::max(c.ScrollPosition, glm::vec2(0.0f));
				if (UI::Property("Wheel Speed", c.WheelSpeed, 1.0f, 0.0f, 10000.0f, "%.0f", "Reference pixels per wheel notch"))
					c.WheelSpeed = std::max(c.WheelSpeed, 0.0f);
				UI::Property("Show Scrollbars", c.ShowScrollbars);
				UI::EndPropertyGrid();
			}
		});

		DrawComponent<AudioListenerComponent>(m_Editor, entity, ICON_MUSIC, "Audio Listener", [](AudioListenerComponent& c)
		{
			if (UI::BeginPropertyGrid("AudioListener"))
			{
				UI::Property("Active", c.Active, "Without an active listener, the primary camera hears the scene");
				UI::EndPropertyGrid();
			}
		});

		ImGui::Spacing();
		ImGui::EndChild();
	}

	void InspectorPanel::DrawTags(Entity entity)
	{
		TagComponent& component = entity.GetComponent<TagComponent>();
		if (entity.GetUUID() != m_TagEntity)
		{
			m_TagEntity = entity.GetUUID();
			m_NewTag.clear();
			m_TagProblem.clear();
		}
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Tags");
		ImGui::SetItemTooltip("Groups this entity belongs to, such as \"enemy\" or \"pickup\". Scripts find them all with "
			"find_tagged(\"enemy\") (ForgeScript: everything tagged \"enemy\"). Click a tag to remove it.");

		// The tags flow along the line like words, then the field to add one. The cursor is at the start
		// of a line here, so the room it has is the line's whole width.
		const float lineEnd = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		const ImGuiStyle& style = ImGui::GetStyle();
		const auto sameLineIfItFits = [&](float width)
		{
			if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= lineEnd)
				ImGui::SameLine();
		};
		std::string removed;
		for (const std::string& tag : component.Tags)
		{
			const std::string label = tag + "  " ICON_X;
			sameLineIfItFits(ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f);
			ImGui::PushID(tag.c_str());
			if (ImGui::SmallButton(label.c_str()))
				removed = tag;
			ImGui::SetItemTooltip("Remove the tag \"%s\"", tag.c_str());
			ImGui::PopID();
		}
		if (!removed.empty())
		{
			EntityTags::RemoveTag(component, removed);
			m_Editor.CommitChange("Remove tag");
		}

		if (component.Tags.size() < EntityTags::MaxTagsPerEntity)
		{
			const float fieldWidth = 140.0f * Theme::GetScale();
			sameLineIfItFits(fieldWidth);
			ImGui::SetNextItemWidth(std::min(fieldWidth, ImGui::GetContentRegionAvail().x));
			if (ImGui::InputTextWithHint("##NewTag", "Add a tag", &m_NewTag, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				const std::string tag = EntityTags::Trim(m_NewTag);
				m_TagProblem = EntityTags::CheckTag(tag).value_or("");
				if (m_TagProblem.empty())
				{
					if (EntityTags::AddTag(component, tag))
						m_Editor.CommitChange("Add tag");
					m_NewTag.clear();
				}
				// Enter adds the tag and keeps typing in the field, for the next one.
				ImGui::SetKeyboardFocusHere(-1);
			}
			else if (ImGui::IsItemEdited())
			{
				m_TagProblem.clear();
			}
		}
		if (!m_TagProblem.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Error));
			ImGui::TextWrapped("%s.", m_TagProblem.c_str());
			ImGui::PopStyleColor();
		}
	}

	void InspectorPanel::DrawAnimator(Entity entity, AnimatorComponent& c)
	{
		const MeshRendererComponent* meshRenderer = entity.TryGetComponent<MeshRendererComponent>();
		const MeshSkin* skin = meshRenderer && meshRenderer->RuntimeMesh ? meshRenderer->RuntimeMesh->GetSkin() : nullptr;
		const bool hasClips = skin && !skin->Clips.empty();
		if (UI::BeginPropertyGrid("Animator"))
		{
			if (hasClips)
			{
				// "First Clip" is an empty Clip; a name the mesh doesn't have stays listed, marked missing.
				std::vector<std::string> names = { "First Clip" };
				int selected = 0;
				for (const Ref<AnimationClip>& clip : skin->Clips)
				{
					if (clip->GetName() == c.Clip)
						selected = static_cast<int>(names.size());
					names.push_back(clip->GetName());
				}
				if (selected == 0 && !c.Clip.empty())
				{
					selected = static_cast<int>(names.size());
					names.push_back(c.Clip + " (missing)");
				}
				std::vector<const char*> items;
				for (const std::string& name : names)
					items.push_back(name.c_str());
				if (UI::PropertyCombo("Clip", selected, items.data(), static_cast<int>(items.size()), "The clip played when the game starts"))
				{
					if (selected == 0)
						c.Clip.clear();
					else if (selected <= static_cast<int>(skin->Clips.size()))
						c.Clip = skin->Clips[static_cast<size_t>(selected - 1)]->GetName();
				}
			}
			else
				UI::Property("Clip", c.Clip, "The clip played when the game starts; empty plays the mesh's first");
			UI::Property("Play On Start", c.PlayOnStart, "Starts the clip when the game starts; otherwise scripts play one");
			UI::Property("Loop", c.Loop);
			UI::PropertySlider("Speed", c.Speed, -AnimationPlayer::MaxSpeed, AnimationPlayer::MaxSpeed, "%.2f", "Times as fast; negative plays backward");

			// The preview plays the clip here without changing the scene: nothing it does is saved or undone.
			if (hasClips)
			{
				UI::Property("Preview", c.RuntimePreview, "Plays the clip in the viewport while the game isn't running");
				AnimationPlayer& player = c.RuntimePlayer;
				if (c.RuntimePreview && player.GetClip())
				{
					float time = player.GetTime();
					if (UI::PropertySlider("Time", time, 0.0f, player.GetClip()->GetDuration(), "%.2f s", "Drag to look at any moment of the clip"))
					{
						player.Pause();
						player.SetTime(time);
					}
				}
			}
			UI::EndPropertyGrid();
		}

		if (!skin)
		{
			UI::TextMuted(meshRenderer ? "This entity's mesh has no skeleton, so there is nothing to animate. Use a glTF model with a skin."
				: "Add a Mesh Renderer with a skinned glTF model to animate.");
		}
		else if (!hasClips)
			UI::TextMuted("This model has a skeleton but no animations.");
		else if (c.RuntimePreview && c.RuntimePlayer.GetClip())
		{
			AnimationPlayer& player = c.RuntimePlayer;
			const bool moving = player.IsPlaying();
			if (ImGui::Button(moving ? ICON_PAUSE "  Pause Preview" : ICON_PLAY "  Play Preview", ImVec2(-FLT_MIN, 0.0f)))
			{
				if (moving)
					player.Pause();
				else if (player.IsPaused())
					player.Resume();
				else
					player.Play(player.GetClip(), 0.0f, c.Speed);
			}
		}
	}

	void InspectorPanel::DrawMeshRenderer(Entity entity, MeshRendererComponent& c)
	{
		if (UI::BeginPropertyGrid("MeshRenderer"))
		{
			UI::PropertyAsset("Mesh", c.MeshPath, AssetType::Mesh, "A glTF model or a built-in shape", true);
			UI::PropertyAsset("Material", c.MaterialPath, AssetType::Material, "Overrides every submesh's material");
			UI::Property("Cast Shadows", c.CastShadows);
			UI::Property("Visible", c.Visible);
			UI::EndPropertyGrid();
		}

		if (!c.MaterialPath.empty() && AssetManager::GetAssetType(c.MaterialPath) == AssetType::Material)
		{
			if (ImGui::TreeNodeEx("Material Properties", ImGuiTreeNodeFlags_SpanAvailWidth))
			{
				if (Ref<Material> material = AssetManager::GetMaterial(c.MaterialPath))
					DrawMaterialEditor(material, c.MaterialPath);
				ImGui::TreePop();
			}
		}
		else if (c.RuntimeMesh)
		{
			const auto& materials = c.RuntimeMesh->GetMaterials();
			ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
			ImGui::TextDisabled("%zu submesh(es), %zu material(s) from the mesh", c.RuntimeMesh->GetSubmeshes().size(), materials.size());
			ImGui::PopFont();
			if (ImGui::Button(ICON_PALETTE "  Create Material Override", ImVec2(-FLT_MIN, 0.0f)))
			{
				Ref<Material> material = materials.empty() ? CreateRef<Material>("Material") : materials.front()->Clone();
				material->SetName(entity.GetName());
				std::string path = "Materials/" + entity.GetName() + ".bmat";
				for (int suffix = 1; AssetManager::Exists(path); suffix++)
					path = "Materials/" + entity.GetName() + "_" + std::to_string(suffix) + ".bmat";
				if (AssetManager::SaveMaterial(material, path))
				{
					c.MaterialPath = path;
					m_Editor.CommitChange("Create material");
				}
			}
		}
	}

	bool InspectorPanel::DrawMaterialEditor(const Ref<Material>& material, const std::string& assetPath)
	{
		bool changed = false;
		MaterialProperties properties = material->GetProperties();
		if (UI::BeginPropertyGrid("Material"))
		{
			changed |= UI::PropertyColor("Albedo", properties.AlbedoColor);
			changed |= UI::Property("Unlit", properties.Unlit, "Shows the albedo (and emissive) as they are, with no lights, shade or shadows on it: "
				"for flat-shaded art and sprites. Metallic, Roughness and the normal map then do nothing. It still casts shadows.");
			ImGui::BeginDisabled(properties.Unlit);
			changed |= UI::PropertySlider("Metallic", properties.Metallic, 0.0f, 1.0f);
			changed |= UI::PropertySlider("Roughness", properties.Roughness, 0.0f, 1.0f);
			ImGui::EndDisabled();
			changed |= UI::PropertyColor("Emissive", properties.EmissiveColor);
			changed |= UI::Property("Emissive Intensity", properties.EmissiveIntensity, 0.05f, 0.0f, 1000.0f, "%.2f");
			ImGui::BeginDisabled(properties.Unlit);
			changed |= UI::PropertySlider("Normal Strength", properties.NormalStrength, 0.0f, 2.0f);
			ImGui::EndDisabled();
			const char* modes[] = { "Opaque", "Mask", "Blend" };
			int mode = static_cast<int>(properties.Mode);
			if (UI::PropertyCombo("Alpha Mode", mode, modes, 3))
			{
				properties.Mode = static_cast<AlphaMode>(mode);
				changed = true;
			}
			if (properties.Mode == AlphaMode::Mask)
				changed |= UI::PropertySlider("Alpha Cutoff", properties.AlphaCutoff, 0.0f, 1.0f);
			changed |= UI::Property("Double Sided", properties.DoubleSided);
			const char* filters[] = { "Smooth", "Pixelated" };
			int filter = static_cast<int>(properties.Filter);
			if (UI::PropertyCombo("Texture Filter", filter, filters, 2, "Pixelated keeps each texture pixel a sharp square, for pixel art and palette textures"))
			{
				properties.Filter = static_cast<TextureFilter>(filter);
				changed = true;
			}
			const char* wraps[] = { "Repeat", "Clamp", "Mirror" };
			int wrapU = static_cast<int>(properties.WrapU);
			if (UI::PropertyCombo("Wrap U", wrapU, wraps, 3, "What textures do past their left and right edges"))
			{
				properties.WrapU = static_cast<TextureWrap>(wrapU);
				changed = true;
			}
			int wrapV = static_cast<int>(properties.WrapV);
			if (UI::PropertyCombo("Wrap V", wrapV, wraps, 3, "What textures do past their top and bottom edges"))
			{
				properties.WrapV = static_cast<TextureWrap>(wrapV);
				changed = true;
			}
			changed |= UI::PropertyVec2("UV Tiling", properties.UVTiling);
			changed |= UI::PropertyVec2("UV Offset", properties.UVOffset);

			auto textureField = [&](const char* label, std::string& path, TextureColorSpace colorSpace, void (Material::*setter)(Ref<Texture2D>))
			{
				if (UI::PropertyAsset(label, path, AssetType::Texture))
				{
					(material.get()->*setter)(AssetManager::GetTexture(path, colorSpace));
					changed = true;
				}
			};
			textureField("Albedo Map", material->AlbedoMapPath, TextureColorSpace::SRGB, &Material::SetAlbedoMap);
			textureField("Normal Map", material->NormalMapPath, TextureColorSpace::Linear, &Material::SetNormalMap);
			textureField("Roughness Map", material->RoughnessMapPath, TextureColorSpace::Linear, &Material::SetRoughnessMap);
			textureField("Metallic Map", material->MetallicMapPath, TextureColorSpace::Linear, &Material::SetMetallicMap);
			textureField("Metal/Rough Map", material->MetallicRoughnessMapPath, TextureColorSpace::Linear, &Material::SetMetallicRoughnessMap);
			textureField("Occlusion Map", material->OcclusionMapPath, TextureColorSpace::Linear, &Material::SetOcclusionMap);
			textureField("Emissive Map", material->EmissiveMapPath, TextureColorSpace::SRGB, &Material::SetEmissiveMap);
			UI::EndPropertyGrid();
		}

		if (changed)
		{
			material->SetProperties(properties);
			if (m_PendingMaterialSave && m_PendingMaterialSave != material)
				FlushMaterialSave();
			m_PendingMaterialSave = material;
			m_PendingMaterialPath = assetPath;
		}
		return changed;
	}

	void InspectorPanel::DrawScriptComponent(Entity entity, ScriptComponent& c)
	{
		if (UI::BeginPropertyGrid("Script"))
		{
			UI::PropertyAsset("Script", c.ScriptPath, AssetType::Script);
			// A ForgeScript file is one script, so only Python files have a class to choose.
			if (!Scripting::ForgeScriptRuntime::IsForgeScript(c.ScriptPath))
				UI::Property("Class", c.ClassName, "Leave empty to use the first bellows.Script class in the file");
			UI::Property("Run While Paused", c.RunWhilePaused,
				"Keeps this script updating while the game is paused (set_paused), with real frame times: tick it for a pause "
				"menu that waits or animates. Unticked scripts still get UI events like clicked while paused");
			UI::EndPropertyGrid();
		}

		if (c.ScriptPath.empty())
		{
			UI::TextMuted("Choose a script (drag one from the Content panel).");
			return;
		}

		// Shown before the script loads, so a script with a mistake can still be opened and fixed.
		if (ImGui::Button(ICON_FILE_CODE "  Open in Script Editor", ImVec2(-FLT_MIN, 0.0f)))
			m_Editor.GetScriptEditor().OpenScript(c.ScriptPath);

		const ScriptClassInfo* info = GetScriptInfo(c.ScriptPath, c.ClassName);
		if (!info)
		{
			const auto& cached = m_ScriptInfoCache[c.ScriptPath + "|" + c.ClassName];
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Error));
			ImGui::TextWrapped(ICON_TRIANGLE_ALERT "  %s", cached.Error.empty() ? "The script could not be loaded." : cached.Error.c_str());
			ImGui::PopStyleColor();
			return;
		}

		// A pause menu whose buttons wait would freeze under the game's pause.
		if (const std::string warning = c.RunWhilePaused ? std::string() : FormatPauseWaitWarning(info->WaitingUIHandlers); !warning.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Theme::ToVec4(Theme::Warning));
			ImGui::TextWrapped(ICON_TRIANGLE_ALERT "  %s", warning.c_str());
			ImGui::PopStyleColor();
			if (ImGui::SmallButton("Tick Run While Paused"))
			{
				c.RunWhilePaused = true;
				m_Editor.CommitChange("Run while paused");
			}
		}

		if (info->Fields.empty())
		{
			ImGui::PushFont(Theme::GetFonts().Regular, 0.0f);
			ImGui::TextDisabled("%s has no exposed fields", info->ClassName.c_str());
			ImGui::PopFont();
			return;
		}

		UI::SectionHeader(info->ClassName.c_str());
		if (UI::BeginPropertyGrid("ScriptFields"))
		{
			for (const ScriptFieldInfo& field : info->Fields)
			{
				// Show an override set with another number type (10.0 on a whole-number field) as the field's type.
				auto it = c.Fields.find(field.Name);
				ScriptFieldValue value = field.DefaultValue;
				const bool overridden = it != c.Fields.end() && ConvertScriptFieldValue(it->second, field.DefaultValue, value);
				if (DrawFieldValue(field.Name.c_str(), value))
					c.Fields[field.Name] = value;
				if (overridden && ImGui::BeginPopupContextItem(field.Name.c_str()))
				{
					if (ImGui::MenuItem(ICON_REFRESH_CW "  Reset to default"))
					{
						c.Fields.erase(field.Name);
						m_Editor.CommitChange("Reset field");
					}
					ImGui::EndPopup();
				}
			}
			UI::EndPropertyGrid();
		}

		// Drop overrides for fields the script no longer declares.
		std::erase_if(c.Fields, [info](const auto& entry)
		{
			return std::none_of(info->Fields.begin(), info->Fields.end(), [&](const ScriptFieldInfo& field) { return field.Name == entry.first; });
		});
	}

	void InspectorPanel::DrawAddComponentMenu(Entity entity)
	{
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(220.0f * Theme::GetScale());
		ImGui::InputTextWithHint("##filter", ICON_SEARCH "  Search", &m_ComponentFilter);

		auto entry = [&]<typename T>(const char* icon, const char* name, const char* category)
		{
			if (entity.HasComponent<T>())
				return;
			std::string lowerName = name, lowerFilter = m_ComponentFilter;
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
			std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
			if (!lowerFilter.empty() && lowerName.find(lowerFilter) == std::string::npos)
				return;
			const std::string label = std::string(icon) + "  " + name;
			if (ImGui::Selectable(label.c_str()))
			{
				T& component = entity.AddComponent<T>();
				if constexpr (std::is_same_v<T, BoxColliderComponent>)
				{
					// Fit the collider to the mesh when there is one.
					if (auto* meshRenderer = entity.TryGetComponent<MeshRendererComponent>(); meshRenderer && meshRenderer->RuntimeMesh)
					{
						const AABB& bounds = meshRenderer->RuntimeMesh->GetBoundingBox();
						component.HalfExtents = glm::max(bounds.GetExtents(), glm::vec3(0.01f));
						component.Offset = bounds.GetCenter();
					}
				}
				if constexpr (std::is_same_v<T, TextComponent>)
				{
					// Screen text is sized in reference pixels, not meters.
					if (entity.GetScene()->IsInsideUICanvas(entity))
						component.Size = TextComponent::DefaultScreenSize;
				}
				m_Editor.CommitChange(std::string("Add ") + name);
				ImGui::CloseCurrentPopup();
			}
			(void)category;
		};

		if (m_ComponentFilter.empty())
			UI::SectionHeader("Rendering");
		entry.operator()<MeshRendererComponent>(ICON_BOX, "Mesh Renderer", "Rendering");
		entry.operator()<AnimatorComponent>(ICON_PERSON_STANDING, "Animator", "Rendering");
		entry.operator()<CameraComponent>(ICON_CAMERA, "Camera", "Rendering");
		entry.operator()<DirectionalLightComponent>(ICON_SUN, "Directional Light", "Rendering");
		entry.operator()<PointLightComponent>(ICON_LIGHTBULB, "Point Light", "Rendering");
		entry.operator()<SpotLightComponent>(ICON_FLASHLIGHT, "Spot Light", "Rendering");
		entry.operator()<SkyLightComponent>(ICON_CLOUD, "Sky Light", "Rendering");
		entry.operator()<TextComponent>(ICON_TYPE, "Text", "Rendering");
		entry.operator()<ParticleEmitterComponent>(ICON_FLAME, "Particle Emitter", "Rendering");
		entry.operator()<TrailComponent>(ICON_SPLINE, "Trail", "Rendering");
		entry.operator()<DecalComponent>(ICON_STICKER, "Decal", "Rendering");
		if (m_ComponentFilter.empty())
			UI::SectionHeader("Physics");
		entry.operator()<RigidBodyComponent>(ICON_ATOM, "Rigid Body", "Physics");
		entry.operator()<BoxColliderComponent>(ICON_BOX, "Box Collider", "Physics");
		entry.operator()<SphereColliderComponent>(ICON_CIRCLE, "Sphere Collider", "Physics");
		entry.operator()<CapsuleColliderComponent>(ICON_CIRCLE, "Capsule Collider", "Physics");
		entry.operator()<MeshColliderComponent>(ICON_SHAPES, "Mesh Collider", "Physics");
		if (m_ComponentFilter.empty())
			UI::SectionHeader("Behavior");
		entry.operator()<ScriptComponent>(ICON_FILE_CODE, "Script", "Behavior");
		entry.operator()<NavAgentComponent>(ICON_FOOTPRINTS, "Nav Agent", "Behavior");
		entry.operator()<AudioSourceComponent>(ICON_VOLUME_2, "Audio Source", "Audio");
		entry.operator()<AudioListenerComponent>(ICON_MUSIC, "Audio Listener", "Audio");
		if (m_ComponentFilter.empty())
			UI::SectionHeader("UI");
		entry.operator()<UICanvasComponent>(ICON_MONITOR, "UI Canvas", "UI");
		entry.operator()<UIRectComponent>(ICON_SCAN, "UI Rect", "UI");
		entry.operator()<UIImageComponent>(ICON_IMAGE, "UI Image", "UI");
		entry.operator()<UILayoutComponent>(ICON_LAYOUT_GRID, "UI Layout", "UI");
		entry.operator()<UIScrollComponent>(ICON_LAYERS, "UI Scroll", "UI");
		entry.operator()<UIButtonComponent>(ICON_MOUSE_POINTER_CLICK, "UI Button", "UI");
		entry.operator()<UIToggleComponent>(ICON_SQUARE_CHECK, "UI Toggle", "UI");
		entry.operator()<UISliderComponent>(ICON_SLIDERS_HORIZONTAL, "UI Slider", "UI");
		entry.operator()<UIDropdownComponent>(ICON_CHEVRONS_UP_DOWN, "UI Dropdown", "UI");
		entry.operator()<UITextInputComponent>(ICON_TEXT_CURSOR_INPUT, "UI Text Input", "UI");
	}

}
