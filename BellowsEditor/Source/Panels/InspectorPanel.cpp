#include "InspectorPanel.h"

#include "EditorLayer.h"
#include "EditorTheme.h"
#include "EditorUI.h"
#include "IconsLucide.h"
#include "Panels/ContentBrowserPanel.h"
#include "Panels/ScriptEditorPanel.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scripting/ForgeScriptRuntime.h"
#include "Bellows/UI/UILayout.h"
#include "Bellows/UI/UIWidgets.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <vector>

namespace Bellows {

	namespace {

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

		// Gives a rect a preset's anchors. It stays where it was on screen (its offsets are worked out
		// from where it was last laid out), or with 'moveRect' goes to the preset: filling stretching
		// axes, sitting on the point, pivoted there, along the others.
		void ApplyAnchorPreset(EditorLayer& editor, Entity entity, UIRectComponent& rect, UIAnchorPreset preset, bool moveRect)
		{
			glm::vec2 anchorMin, anchorMax;
			GetUIAnchorPreset(preset, anchorMin, anchorMax);
			const std::optional<Scene::UIRectPlacement> placement = editor.GetActiveScene()->GetUIRectPlacement(entity);
			if (placement)
			{
				const UIRectangle target = moveRect
					? GetUIAnchorPresetRect(preset, placement->Rect, placement->ParentArea, rect.Pivot, rect.Pivot)
					: placement->Rect;
				ComputeUIOffsetsFor(target, anchorMin, anchorMax, placement->ParentArea, placement->Scale, rect.OffsetMin, rect.OffsetMax);
			}
			rect.AnchorMin = anchorMin;
			rect.AnchorMax = anchorMax;
			editor.CommitChange(moveRect ? "Move UI rect to anchors" : "UI anchor preset");
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
		ImGui::PushStyleColor(ImGuiCol_Button, Theme::ToVec4(Theme::AccentMuted));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::ToVec4(IM_COL32(242, 112, 26, 90)));
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
					ApplyAnchorPreset(m_Editor, entity, c, static_cast<UIAnchorPreset>(preset), ImGui::GetIO().KeyShift);
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
			changed |= UI::PropertySlider("Metallic", properties.Metallic, 0.0f, 1.0f);
			changed |= UI::PropertySlider("Roughness", properties.Roughness, 0.0f, 1.0f);
			changed |= UI::PropertyColor("Emissive", properties.EmissiveColor);
			changed |= UI::Property("Emissive Intensity", properties.EmissiveIntensity, 0.05f, 0.0f, 1000.0f, "%.2f");
			changed |= UI::PropertySlider("Normal Strength", properties.NormalStrength, 0.0f, 2.0f);
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
		entry.operator()<CameraComponent>(ICON_CAMERA, "Camera", "Rendering");
		entry.operator()<DirectionalLightComponent>(ICON_SUN, "Directional Light", "Rendering");
		entry.operator()<PointLightComponent>(ICON_LIGHTBULB, "Point Light", "Rendering");
		entry.operator()<SpotLightComponent>(ICON_FLASHLIGHT, "Spot Light", "Rendering");
		entry.operator()<SkyLightComponent>(ICON_CLOUD, "Sky Light", "Rendering");
		entry.operator()<TextComponent>(ICON_TYPE, "Text", "Rendering");
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
