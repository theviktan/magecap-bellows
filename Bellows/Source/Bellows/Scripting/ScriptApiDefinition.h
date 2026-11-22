#pragma once

// The engine API that scripts can use, described once for every scripting language.
//
// DefineScriptApi is instantiated by each language's binder (PythonApi in ScriptBindings.cpp,
// ForgeScriptApi in ForgeScriptBindings.cpp). A binder provides:
//
//   api.Function(name, function, doc, Arg(...)...)
//   auto c = api.template Class<T>(name, doc);
//   c.Property(name, getter, setter, doc)   c.ReadOnly(name, getter, doc)
//   c.Method(name, function, doc, Arg(...)...)
//   c.Constructor(function, Arg(...)...)    c.Static(name, function, doc, Arg(...)...)
//
// Functions only use the language-neutral types from ScriptApi.h, so a new engine feature added here
// appears in Python and ForgeScript at once. Language-specific extras (Python operators and
// __repr__, script objects) are added by the binders themselves.

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Audio/AudioEngine.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Core/InputMap.h"
#include "Bellows/Core/InputNames.h"
#include "Bellows/Math/Easing.h"
#include "Bellows/Math/Math.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/Scene/SceneSerializer.h"
#include "Bellows/Scene/Tweens.h"
#include "Bellows/Scripting/ScriptApi.h"
#include "Bellows/Scripting/TweenProperties.h"
#include "Bellows/Text/TextLayout.h"
#include "Bellows/UI/UILayout.h"
#include "Bellows/UI/UIWidgets.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Bellows::Scripting {

	namespace Detail {

		// Script calls that name an input action or gamepad button raise ValueError for unknown names.
		inline const std::string& RequireAction(const std::string& action)
		{
			if (!InputMap::HasAction(action))
				throw ScriptValueError("Unknown input action '" + action + "'. Actions are set in Project Settings > Input actions");
			return action;
		}

		inline GamepadButton RequireGamepadButton(const std::string& name)
		{
			if (std::optional<GamepadButton> button = FindGamepadButton(name))
				return *button;
			throw ScriptValueError("Unknown gamepad button '" + name + "'. Buttons: A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb, DPadUp, DPadRight, DPadDown, DPadLeft");
		}

		// Script calls that name an audio bus raise ValueError for unknown names.
		inline void RequireBus(const std::string& bus)
		{
			if (!AudioEngine::IsBus(bus))
				throw ScriptValueError("Unknown audio bus '" + bus + "'. Buses: Master, Music, Effects");
		}

		// Text settings named in scripts raise ValueError for unknown names.
		inline TextAlignment RequireTextAlignment(const std::string& name)
		{
			if (name != "left" && name != "center" && name != "right")
				throw ScriptValueError("Unknown text alignment '" + name + "'. Alignments: left, center, right");
			return TextAlignmentFromString(name);
		}

		inline TextVerticalAlignment RequireTextVerticalAlignment(const std::string& name)
		{
			if (name != "top" && name != "middle" && name != "bottom")
				throw ScriptValueError("Unknown vertical alignment '" + name + "'. Vertical alignments: top, middle, bottom");
			return TextVerticalAlignmentFromString(name);
		}

		inline TextOverflow RequireTextOverflow(const std::string& name)
		{
			if (name != "visible" && name != "clip" && name != "ellipsis")
				throw ScriptValueError("Unknown text overflow '" + name + "'. Overflows: visible, clip, ellipsis");
			return TextOverflowFromString(name);
		}

		inline UIScaleMode RequireUIScaleMode(const std::string& name)
		{
			if (!IsUIScaleModeName(name))
				throw ScriptValueError("Unknown UI scale mode '" + name + "'. Scale modes: fit_height, fit_width, expand, constant_pixels");
			return UIScaleModeFromString(name);
		}

		inline UILayoutDirection RequireUILayoutDirection(const std::string& name)
		{
			if (!IsUILayoutDirectionName(name))
				throw ScriptValueError("Unknown layout direction '" + name + "'. Directions: vertical, horizontal, grid");
			return UILayoutDirectionFromString(name);
		}

		inline UIAlignment RequireUIAlignment(const std::string& name)
		{
			if (!IsUIAlignmentName(name))
				throw ScriptValueError("Unknown alignment '" + name + "'. Alignments: start, center, end");
			return UIAlignmentFromString(name);
		}

		inline UISizing RequireUISizing(const std::string& name)
		{
			if (!IsUISizingName(name))
				throw ScriptValueError("Unknown sizing '" + name + "'. Sizings: fixed, fill");
			return UISizingFromString(name);
		}

		// Four edges (left, top, right, bottom) as a list of four numbers, none below 0.
		inline glm::vec4 RequireEdges(const std::vector<float>& values, const char* what)
		{
			if (values.size() != 4)
				throw ScriptValueError(std::string(what) + " needs 4 numbers (left, top, right, bottom), but got " + std::to_string(values.size()));
			return glm::max(glm::vec4(values[0], values[1], values[2], values[3]), glm::vec4(0.0f));
		}

		inline std::vector<float> ToList(const glm::vec4& value)
		{
			return { value.x, value.y, value.z, value.w };
		}

		// The everyday UI properties on Entity (text, visible, interactable, focus) work on the component
		// that carries them, and say which entity lacks it otherwise.
		template<typename T>
		T& RequireUIComponent(const ScriptEntity& handle, const char* property, const char* needs)
		{
			Entity entity = handle.Resolve();
			T* component = entity.TryGetComponent<T>();
			if (!component)
				throw std::runtime_error("Entity '" + entity.GetName() + "' has no " + needs + " for '" + property + "'");
			return *component;
		}

		// A UI entity's own visibility: its UIRect's, or its UICanvas's when it is a canvas.
		inline bool& RequireUIVisible(const ScriptEntity& handle)
		{
			Entity entity = handle.Resolve();
			if (auto* rect = entity.TryGetComponent<UIRectComponent>())
				return rect->Visible;
			if (auto* canvas = entity.TryGetComponent<UICanvasComponent>())
				return canvas->Visible;
			throw std::runtime_error("Entity '" + entity.GetName() + "' has no UIRect or UICanvas for 'visible'");
		}

		inline int RequireDropdownIndex(const UIDropdownComponent& dropdown, double index)
		{
			if (index != std::floor(index) || index < -1.0 || index >= static_cast<double>(dropdown.Options.size()))
				throw ScriptValueError("A dropdown with " + std::to_string(dropdown.Options.size()) + " options takes an index from 0 to "
					+ std::to_string(static_cast<int>(dropdown.Options.size()) - 1) + " (or -1 for none), not " + std::to_string(index));
			return static_cast<int>(index);
		}

		inline double RequireNumberValue(const ScriptValue& value, const char* widget)
		{
			if (const double* number = std::get_if<double>(&value.Data))
				return *number;
			throw ScriptValueError(std::string("The value of a ") + widget + " is a number");
		}

		// What a text field keeps of text set from a script: what typing it in would keep.
		inline std::string CleanTextInputValue(const UITextInputComponent& input, const std::string& text)
		{
			std::string value;
			UITextSelection selection;
			InsertUIText(value, selection, text, input.MultiLine && !input.Password, input.MaxLength);
			return value;
		}

		// Entity.value: a toggle's on/off, a slider's value, a dropdown's selected index or a text field's text.
		inline ScriptValue GetWidgetValue(const ScriptEntity& handle)
		{
			Entity entity = handle.Resolve();
			if (entity.HasComponent<UITextInputComponent>())
			{
				const auto* text = entity.TryGetComponent<TextComponent>();
				return ScriptValue(text ? text->Text : std::string());
			}
			if (const auto* toggle = entity.TryGetComponent<UIToggleComponent>())
				return ScriptValue(toggle->On);
			if (const auto* slider = entity.TryGetComponent<UISliderComponent>())
				return ScriptValue(static_cast<double>(slider->Value));
			if (const auto* dropdown = entity.TryGetComponent<UIDropdownComponent>())
				return ScriptValue(static_cast<double>(dropdown->Selected));
			throw std::runtime_error("Entity '" + entity.GetName() + "' has no UIToggle, UISlider, UIDropdown or UITextInput for 'value'");
		}

		inline void SetWidgetValue(const ScriptEntity& handle, const ScriptValue& value)
		{
			Entity entity = handle.Resolve();
			if (const auto* input = entity.TryGetComponent<UITextInputComponent>())
			{
				const std::string* text = std::get_if<std::string>(&value.Data);
				if (!text)
					throw ScriptValueError("The value of a text input is text");
				const std::string clean = CleanTextInputValue(*input, *text);
				(entity.HasComponent<TextComponent>() ? entity.GetComponent<TextComponent>() : entity.AddComponent<TextComponent>()).Text = clean;
			}
			else if (auto* toggle = entity.TryGetComponent<UIToggleComponent>())
			{
				const bool* on = std::get_if<bool>(&value.Data);
				if (!on)
					throw ScriptValueError("The value of a toggle is true or false");
				toggle->On = *on;
			}
			else if (auto* slider = entity.TryGetComponent<UISliderComponent>())
			{
				slider->Value = SnapUISliderValue(static_cast<float>(RequireNumberValue(value, "slider")), slider->Min, slider->Max, slider->Step);
			}
			else if (auto* dropdown = entity.TryGetComponent<UIDropdownComponent>())
			{
				dropdown->Selected = RequireDropdownIndex(*dropdown, RequireNumberValue(value, "dropdown"));
			}
			else
			{
				throw std::runtime_error("Entity '" + entity.GetName() + "' has no UIToggle, UISlider, UIDropdown or UITextInput for 'value'");
			}
		}

		// Script vectors are 3D; UI positions and sizes use x and y.
		inline glm::vec3 ToScriptVector(const glm::vec2& value) { return glm::vec3(value, 0.0f); }

		// Assigns (or clears) one texture slot of a material from an asset path.
		inline void SetMaterialTexture(Material& material, const std::optional<std::string>& value, std::string& pathSlot,
			TextureColorSpace colorSpace, void (Material::*setter)(Ref<Texture2D>))
		{
			if (!value || value->empty())
			{
				pathSlot.clear();
				(material.*setter)(nullptr);
				return;
			}
			Ref<Texture2D> texture = AssetManager::GetTexture(*value, colorSpace);
			if (!texture)
				throw ScriptValueError("Texture '" + *value + "' could not be loaded");
			pathSlot = *value;
			(material.*setter)(std::move(texture));
		}

		inline glm::vec2 ScriptViewportSize(const Scene& scene)
		{
			return Input::GetViewportSize().x > 0 ? Input::GetViewportSize()
				: glm::vec2(static_cast<float>(scene.GetViewportWidth()), static_cast<float>(scene.GetViewportHeight()));
		}

		inline Entity ResolveOptional(const std::optional<ScriptEntity>& entity)
		{
			return entity ? entity->Resolve() : Entity();
		}

		// --- Tweens ----------------------------------------------------------

		inline double RequireTweenSeconds(double seconds, const char* what)
		{
			if (!std::isfinite(seconds) || seconds < 0.0 || seconds > SceneTweens::c_MaxSeconds)
				throw ScriptValueError(std::string("A tween's ") + what + " is a number of seconds from 0 to 1,000,000");
			return seconds;
		}

		inline Math::EaseCurve RequireEaseCurve(const std::string& style)
		{
			if (Math::EaseCurve curve = Math::FindEaseCurve(style))
				return curve;
			std::string names;
			for (const std::string& name : Math::GetEaseStyleNames())
				names += (names.empty() ? "\"" : ", \"") + name + "\"";
			throw ScriptValueError("'" + style + "' isn't an easing style. The styles are " + names);
		}

		// The value a script gave for a property, as the vec4 tweens work in: a number for a number,
		// and a vector or a list of numbers of the property's size for a vector (colors take 4).
		inline glm::vec4 ToTweenValue(const ScriptValue& value, const TweenProperty& property)
		{
			const int size = static_cast<int>(property.Type);
			if (const double* number = std::get_if<double>(&value.Data))
			{
				if (size == 1 && std::isfinite(*number))
					return glm::vec4(static_cast<float>(*number), 0.0f, 0.0f, 0.0f);
			}
			else if (const glm::vec3* vector = std::get_if<glm::vec3>(&value.Data))
			{
				if (size == 3 && std::isfinite(vector->x) && std::isfinite(vector->y) && std::isfinite(vector->z))
					return glm::vec4(*vector, 0.0f);
			}
			else if (const ScriptValueList* list = std::get_if<ScriptValueList>(&value.Data))
			{
				glm::vec4 result(0.0f);
				bool numbers = static_cast<int>(list->size()) == size && size > 1;
				for (size_t i = 0; numbers && i < list->size(); i++)
				{
					const double* item = std::get_if<double>(&(*list)[i].Data);
					numbers = item && std::isfinite(*item);
					if (numbers)
						result[static_cast<int>(i)] = static_cast<float>(*item);
				}
				if (numbers)
					return result;
			}
			static constexpr const char* c_Kinds[] = { "", "a number", "a vector of 2 numbers", "a vector of 3 numbers", "4 numbers (red, green, blue, alpha)" };
			throw ScriptValueError("Tweening '" + property.Name + "' needs " + c_Kinds[size] + ", in ordinary numbers");
		}

		inline ScriptTween StartTween(const ScriptEntity& target, const std::string& propertyName, const ScriptValue& value, double seconds,
			const std::string& ease, double delay, bool relative)
		{
			Entity entity = target.Resolve();
			SceneTweens* tweens = entity.GetScene()->GetTweens();
			if (!tweens)
				throw ScriptValueError("Tweens run while the game is playing");
			const TweenProperty& property = FindTweenProperty(propertyName);
			TweenSettings settings;
			settings.Property = &property;
			settings.Value = ToTweenValue(value, property);
			settings.Relative = relative;
			settings.Duration = RequireTweenSeconds(seconds, "length");
			settings.Delay = RequireTweenSeconds(delay, "delay");
			settings.Ease = RequireEaseCurve(ease);
			uint64_t id = 0;
			try
			{
				id = tweens->Start(target.ID, settings);
			}
			catch (const std::length_error& error)
			{
				throw ScriptValueError(error.what());
			}
			return ScriptTween { target.SceneID, id, target, property.Name };
		}

		// --- Physics -----------------------------------------------------------

		inline void AddImpulse(const RigidBodyRef& body, const glm::vec3& impulse)
		{
			Entity entity = body.GetEntity();
			body.Get();
			if (PhysicsWorld* physics = GetPhysics(entity))
				physics->AddImpulse(entity, impulse);
		}

		inline SceneTweens* FindTweens(const ScriptTween& tween)
		{
			Scene* scene = Scene::GetSceneByID(tween.SceneID);
			return scene ? scene->GetTweens() : nullptr;
		}
	}

	template<typename Api>
	void DefineScriptApi(Api& api)
	{
		// --- Input ------------------------------------------------------------
		api.Function("is_key_down", [](const KeyArg& k) { return Input::IsKeyDown(k.Code); }, "True while the key is held", Arg("key"));
		api.Function("is_key_pressed", [](const KeyArg& k) { return Input::IsKeyPressed(k.Code); }, "True on the frame the key went down", Arg("key"));
		api.Function("is_key_released", [](const KeyArg& k) { return Input::IsKeyReleased(k.Code); }, "True on the frame the key went up", Arg("key"));
		api.Function("is_mouse_down", [](const MouseButtonArg& b) { return Input::IsMouseButtonDown(b.Button); }, "", Arg("button", MouseButtonArg { MouseButton::Left }));
		api.Function("is_mouse_pressed", [](const MouseButtonArg& b) { return Input::IsMouseButtonPressed(b.Button); }, "", Arg("button", MouseButtonArg { MouseButton::Left }));
		api.Function("is_mouse_released", [](const MouseButtonArg& b) { return Input::IsMouseButtonReleased(b.Button); }, "", Arg("button", MouseButtonArg { MouseButton::Left }));
		api.Function("mouse_position", []() { return Input::GetMousePosition(); }, "Mouse position in viewport pixels (top-left origin)");
		api.Function("mouse_delta", []() { return Input::GetMouseDelta(); }, "Mouse movement this frame in pixels");
		api.Function("scroll_delta", []() { return Input::GetScrollDelta(); }, "Scroll wheel movement this frame");
		api.Function("viewport_size", []() { return Input::GetViewportSize(); }, "Size of the game viewport in pixels");
		api.Function("action_names", []()
			{
				std::vector<std::string> names;
				for (const auto& [name, bindings] : InputMap::GetActions())
					names.push_back(name);
				return names;
			}, "The project's input actions, sorted by name");
		api.Function("is_action_down", [](const std::string& action) { return InputMap::IsActionDown(Detail::RequireAction(action)); },
			"True while any input bound to the action is held", Arg("action"));
		api.Function("is_action_pressed", [](const std::string& action) { return InputMap::IsActionPressed(Detail::RequireAction(action)); },
			"True on the frame the action started", Arg("action"));
		api.Function("is_action_released", [](const std::string& action) { return InputMap::IsActionReleased(Detail::RequireAction(action)); },
			"True on the frame the action ended", Arg("action"));
		api.Function("gamepad_connected", []() { return Input::IsGamepadConnected(); }, "True while a gamepad is connected");
		api.Function("ui_wants_pointer", []() { return RequireContextScene().UIWantsPointer(); },
			"True while the game UI is using the pointer (over a panel or button, or a modal canvas is shown); the mouse buttons then read as up");
		api.Function("ui_wants_keyboard", []() { return RequireContextScene().UIWantsKeyboard(); },
			"True while the game UI is using the keyboard and gamepad (a modal canvas is shown); keys and buttons then read as up");
		api.Function("ui_clear_focus", []() { RequireContextScene().SetUIFocus(Entity()); }, "Takes focus away from the UI");
		api.Function("is_gamepad_down", [](const std::string& button) { return Input::IsGamepadButtonDown(Detail::RequireGamepadButton(button)); },
			"True while the gamepad button (\"A\", \"Start\", \"DPadLeft\", ...) is held", Arg("button"));
		api.Function("is_gamepad_pressed", [](const std::string& button) { return Input::IsGamepadButtonPressed(Detail::RequireGamepadButton(button)); },
			"True on the frame the gamepad button went down", Arg("button"));
		api.Function("is_gamepad_released", [](const std::string& button) { return Input::IsGamepadButtonReleased(Detail::RequireGamepadButton(button)); },
			"True on the frame the gamepad button went up", Arg("button"));

		// --- Time -------------------------------------------------------------
		api.Function("delta_time", []() { return GetDeltaTime(); }, "Seconds since the previous frame");
		api.Function("time", []() { return GetTime(); }, "Seconds since play started");
		// A signed count, so ForgeScript sees a number (its unsigned 64-bit values are IDs, shown as text).
		api.Function("frame", []() { return static_cast<int64_t>(GetFrame()); }, "Frames since play started");

		// --- Materials --------------------------------------------------------
		auto material = api.template Class<ScriptMaterial>("Material", "PBR material that can be assigned to a MeshRenderer");
		material.Constructor([](const std::string& name, const std::optional<ColorValue>& color, float metallic, float roughness)
			{
				ScriptMaterial result;
				result.Instance = CreateRef<Material>(name);
				MaterialProperties& properties = result.Instance->EditProperties();
				if (color)
					properties.AlbedoColor = color->Value;
				properties.Metallic = metallic;
				properties.Roughness = roughness;
				result.Path = RegisterRuntimeMaterial(result.Instance);
				return result;
			}, Arg("name", std::string("Material")), Arg("color", std::optional<ColorValue>()), Arg("metallic", 0.0f), Arg("roughness", 0.5f));
		material.Static("load", [](const std::string& path)
			{
				Ref<Material> loaded = AssetManager::GetMaterial(path);
				if (!loaded)
					throw ScriptValueError("Material '" + path + "' could not be loaded");
				return ScriptMaterial { loaded, path };
			}, "Loads a .bmat material from the project", Arg("path"));
		material.ReadOnly("path", [](const ScriptMaterial& m) { return m.Path; }, "");
		material.Property("name", [](const ScriptMaterial& m) { return m.Instance->GetName(); },
			[](const ScriptMaterial& m, const std::string& v) { m.Instance->SetName(v); }, "");
		material.Property("color", [](const ScriptMaterial& m) { return ColorValue { m.Instance->GetProperties().AlbedoColor }; },
			[](const ScriptMaterial& m, const ColorValue& v) { m.Instance->EditProperties().AlbedoColor = v.Value; }, "");
		material.Property("metallic", [](const ScriptMaterial& m) { return m.Instance->GetProperties().Metallic; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().Metallic = std::clamp(v, 0.0f, 1.0f); }, "");
		material.Property("roughness", [](const ScriptMaterial& m) { return m.Instance->GetProperties().Roughness; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().Roughness = std::clamp(v, 0.0f, 1.0f); }, "");
		material.Property("emissive", [](const ScriptMaterial& m) { return m.Instance->GetProperties().EmissiveColor; },
			[](const ScriptMaterial& m, const glm::vec3& v) { m.Instance->EditProperties().EmissiveColor = v; }, "");
		material.Property("emissive_intensity", [](const ScriptMaterial& m) { return m.Instance->GetProperties().EmissiveIntensity; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().EmissiveIntensity = std::max(v, 0.0f); }, "");
		material.Property("alpha_mode", [](const ScriptMaterial& m) { return std::string(AlphaModeToString(m.Instance->GetProperties().Mode)); },
			[](const ScriptMaterial& m, const std::string& v) { m.Instance->EditProperties().Mode = AlphaModeFromString(v); }, "");
		material.Property("double_sided", [](const ScriptMaterial& m) { return m.Instance->GetProperties().DoubleSided; },
			[](const ScriptMaterial& m, bool v) { m.Instance->EditProperties().DoubleSided = v; }, "");
		material.Property("normal_strength", [](const ScriptMaterial& m) { return m.Instance->GetProperties().NormalStrength; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().NormalStrength = std::max(v, 0.0f); }, "");
		material.Property("alpha_cutoff", [](const ScriptMaterial& m) { return m.Instance->GetProperties().AlphaCutoff; },
			[](const ScriptMaterial& m, float v) { m.Instance->EditProperties().AlphaCutoff = std::clamp(v, 0.0f, 1.0f); }, "");
		material.Property("uv_tiling", [](const ScriptMaterial& m) { return m.Instance->GetProperties().UVTiling; },
			[](const ScriptMaterial& m, const glm::vec2& v) { m.Instance->EditProperties().UVTiling = v; }, "");
		material.Property("uv_offset", [](const ScriptMaterial& m) { return m.Instance->GetProperties().UVOffset; },
			[](const ScriptMaterial& m, const glm::vec2& v) { m.Instance->EditProperties().UVOffset = v; }, "");
		material.Property("albedo_map", [](const ScriptMaterial& m) { return m.Instance->AlbedoMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->AlbedoMapPath, TextureColorSpace::SRGB, &Material::SetAlbedoMap); },
			"Albedo (base color) texture path, or None");
		material.Property("normal_map", [](const ScriptMaterial& m) { return m.Instance->NormalMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->NormalMapPath, TextureColorSpace::Linear, &Material::SetNormalMap); },
			"Tangent-space normal map path, or None");
		material.Property("roughness_map", [](const ScriptMaterial& m) { return m.Instance->RoughnessMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->RoughnessMapPath, TextureColorSpace::Linear, &Material::SetRoughnessMap); },
			"Grayscale roughness texture path (as downloaded texture sets have it), or None");
		material.Property("metallic_map", [](const ScriptMaterial& m) { return m.Instance->MetallicMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->MetallicMapPath, TextureColorSpace::Linear, &Material::SetMetallicMap); },
			"Grayscale metallic texture path, or None");
		material.Property("metallic_roughness_map", [](const ScriptMaterial& m) { return m.Instance->MetallicRoughnessMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->MetallicRoughnessMapPath, TextureColorSpace::Linear, &Material::SetMetallicRoughnessMap); },
			"glTF metallic (B) / roughness (G) texture path, or None");
		material.Property("occlusion_map", [](const ScriptMaterial& m) { return m.Instance->OcclusionMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->OcclusionMapPath, TextureColorSpace::Linear, &Material::SetOcclusionMap); },
			"Ambient occlusion (R) texture path, or None");
		material.Property("emissive_map", [](const ScriptMaterial& m) { return m.Instance->EmissiveMapPath; },
			[](const ScriptMaterial& m, const std::optional<std::string>& v) { Detail::SetMaterialTexture(*m.Instance, v, m.Instance->EmissiveMapPath, TextureColorSpace::SRGB, &Material::SetEmissiveMap); },
			"Emissive texture path, or None");
		material.Method("save", [](ScriptMaterial& m, const std::string& path)
			{
				if (!AssetManager::SaveMaterial(m.Instance, path))
					throw std::runtime_error("Could not save material to '" + path + "'");
				m.Path = path;
			}, "Writes the material to a .bmat file in the project", Arg("path"));

		// --- Components -------------------------------------------------------
		auto transform = api.template Class<TransformRef>("Transform", "Position, rotation and scale relative to the parent");
		transform.Property("translation", [](const TransformRef& r) { return r.Get().Translation; },
			[](const TransformRef& r, const glm::vec3& v) { r.Get().Translation = v; TeleportIfSimulated(r.GetEntity()); }, "");
		transform.Property("position", [](const TransformRef& r) { return r.Get().Translation; },
			[](const TransformRef& r, const glm::vec3& v) { r.Get().Translation = v; TeleportIfSimulated(r.GetEntity()); }, "Local position (same as translation)");
		transform.Property("rotation", [](const TransformRef& r) { return r.Get().GetRotationEuler(); },
			[](const TransformRef& r, const glm::vec3& v) { r.Get().SetRotationEuler(v); TeleportIfSimulated(r.GetEntity()); }, "Euler angles in radians");
		transform.Property("rotation_degrees", [](const TransformRef& r) { return glm::degrees(r.Get().GetRotationEuler()); },
			[](const TransformRef& r, const glm::vec3& v) { r.Get().SetRotationEuler(glm::radians(v)); TeleportIfSimulated(r.GetEntity()); }, "");
		transform.Property("quaternion", [](const TransformRef& r) { return r.Get().GetRotation(); },
			[](const TransformRef& r, const glm::quat& q) { r.Get().SetRotation(q); TeleportIfSimulated(r.GetEntity()); }, "");
		transform.Property("scale", [](const TransformRef& r) { return r.Get().Scale; }, [](const TransformRef& r, const glm::vec3& v) { r.Get().Scale = v; }, "");
		transform.Property("world_position", [](const TransformRef& r) { return GetWorldPosition(r.Owner); },
			[](const TransformRef& r, const glm::vec3& v) { SetWorldPosition(r.Owner, v); }, "");
		transform.ReadOnly("forward", [](const TransformRef& r) { return r.Get().GetForward(); }, "");
		transform.ReadOnly("right", [](const TransformRef& r) { return r.Get().GetRight(); }, "");
		transform.ReadOnly("up", [](const TransformRef& r) { return r.Get().GetUp(); }, "");

		auto meshRenderer = api.template Class<MeshRendererRef>("MeshRenderer", "Draws a mesh (glTF file or Builtin/Cube, Sphere, Plane, Capsule, Cylinder, Cone, Quad)");
		meshRenderer.Property("mesh", [](const MeshRendererRef& r) { return r.Get().MeshPath; }, [](const MeshRendererRef& r, const std::string& v) { r.Get().MeshPath = v; }, "");
		meshRenderer.Property("material", [](const MeshRendererRef& r) { return r.Get().MaterialPath; },
			[](const MeshRendererRef& r, const MaterialAssignment& v)
			{
				if (!v)
					r.Get().MaterialPath.clear();
				else if (const ScriptMaterial* m = std::get_if<ScriptMaterial>(&*v))
					r.Get().MaterialPath = m->Path;
				else
					r.Get().MaterialPath = std::get<std::string>(*v);
			}, "Material path (.bmat or runtime material); None uses the mesh's own materials");
		meshRenderer.Property("color", [](const MeshRendererRef& r)
			{
				MeshRendererComponent& c = r.Get();
				if (c.RuntimeMaterial) return ColorValue { c.RuntimeMaterial->GetProperties().AlbedoColor };
				if (c.RuntimeMesh && !c.RuntimeMesh->GetMaterials().empty()) return ColorValue { c.RuntimeMesh->GetMaterials()[0]->GetProperties().AlbedoColor };
				return ColorValue { glm::vec4(1.0f) };
			},
			[](const MeshRendererRef& r, const ColorValue& v)
			{
				// Gives this entity its own material instance so other entities are unaffected.
				MeshRendererComponent& c = r.Get();
				const std::string uniquePrefix = "Runtime/Entity_" + std::to_string(static_cast<uint64_t>(r.Owner.ID));
				if (c.MaterialPath != uniquePrefix)
				{
					Ref<Material> base = c.RuntimeMaterial;
					if (!base && c.RuntimeMesh && !c.RuntimeMesh->GetMaterials().empty())
						base = c.RuntimeMesh->GetMaterials()[0];
					Ref<Material> instance = base ? base->Clone() : CreateRef<Material>("Default");
					AssetManager::RegisterMaterial(uniquePrefix, instance);
					c.MaterialPath = uniquePrefix;
					c.RuntimeMaterial = instance;
					c.LoadedMaterialPath = uniquePrefix;
				}
				c.RuntimeMaterial->EditProperties().AlbedoColor = v.Value;
			}, "Albedo color; setting it gives the entity its own material");
		meshRenderer.Property("cast_shadows", [](const MeshRendererRef& r) { return r.Get().CastShadows; }, [](const MeshRendererRef& r, bool v) { r.Get().CastShadows = v; }, "");
		meshRenderer.Property("visible", [](const MeshRendererRef& r) { return r.Get().Visible; }, [](const MeshRendererRef& r, bool v) { r.Get().Visible = v; }, "");

		auto camera = api.template Class<CameraRef>("Camera", "");
		camera.Property("projection", [](const CameraRef& r) { return std::string(r.Get().Projection == ProjectionType::Orthographic ? "Orthographic" : "Perspective"); },
			[](const CameraRef& r, const std::string& v) { r.Get().Projection = v == "Orthographic" ? ProjectionType::Orthographic : ProjectionType::Perspective; }, "");
		camera.Property("fov", [](const CameraRef& r) { return glm::degrees(r.Get().FieldOfView); },
			[](const CameraRef& r, float v) { r.Get().FieldOfView = glm::radians(std::clamp(v, 1.0f, 179.0f)); }, "Vertical field of view in degrees");
		camera.Property("orthographic_size", [](const CameraRef& r) { return r.Get().OrthographicSize; }, [](const CameraRef& r, float v) { r.Get().OrthographicSize = std::max(v, 0.001f); }, "");
		camera.Property("near_clip", [](const CameraRef& r) { return r.Get().NearClip; }, [](const CameraRef& r, float v) { r.Get().NearClip = std::max(v, 0.0001f); }, "");
		camera.Property("far_clip", [](const CameraRef& r) { return r.Get().FarClip; }, [](const CameraRef& r, float v) { r.Get().FarClip = v; }, "");
		camera.Property("primary", [](const CameraRef& r) { return r.Get().Primary; }, [](const CameraRef& r, bool v) { r.Get().Primary = v; }, "");

		auto directionalLight = api.template Class<DirectionalLightRef>("DirectionalLight", "Sun-like light shining along the entity's forward (-Z)");
		directionalLight.Property("color", [](const DirectionalLightRef& r) { return r.Get().Color; }, [](const DirectionalLightRef& r, const glm::vec3& v) { r.Get().Color = v; }, "");
		directionalLight.Property("intensity", [](const DirectionalLightRef& r) { return r.Get().Intensity; }, [](const DirectionalLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); }, "");
		directionalLight.Property("cast_shadows", [](const DirectionalLightRef& r) { return r.Get().CastShadows; }, [](const DirectionalLightRef& r, bool v) { r.Get().CastShadows = v; }, "");

		auto pointLight = api.template Class<PointLightRef>("PointLight", "");
		pointLight.Property("color", [](const PointLightRef& r) { return r.Get().Color; }, [](const PointLightRef& r, const glm::vec3& v) { r.Get().Color = v; }, "");
		pointLight.Property("intensity", [](const PointLightRef& r) { return r.Get().Intensity; }, [](const PointLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); }, "");
		pointLight.Property("range", [](const PointLightRef& r) { return r.Get().Range; }, [](const PointLightRef& r, float v) { r.Get().Range = std::max(v, 0.01f); }, "");

		auto spotLight = api.template Class<SpotLightRef>("SpotLight", "Cone light shining along the entity's forward (-Z)");
		spotLight.Property("color", [](const SpotLightRef& r) { return r.Get().Color; }, [](const SpotLightRef& r, const glm::vec3& v) { r.Get().Color = v; }, "");
		spotLight.Property("intensity", [](const SpotLightRef& r) { return r.Get().Intensity; }, [](const SpotLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); }, "");
		spotLight.Property("range", [](const SpotLightRef& r) { return r.Get().Range; }, [](const SpotLightRef& r, float v) { r.Get().Range = std::max(v, 0.01f); }, "");
		spotLight.Property("inner_angle", [](const SpotLightRef& r) { return glm::degrees(r.Get().InnerAngle); }, [](const SpotLightRef& r, float v) { r.Get().InnerAngle = glm::radians(v); }, "Degrees");
		spotLight.Property("outer_angle", [](const SpotLightRef& r) { return glm::degrees(r.Get().OuterAngle); }, [](const SpotLightRef& r, float v) { r.Get().OuterAngle = glm::radians(v); }, "Degrees");

		auto skyLight = api.template Class<SkyLightRef>("SkyLight", "Environment lighting: an HDRI (.hdr) or the procedural sky");
		skyLight.Property("environment", [](const SkyLightRef& r) { return r.Get().EnvironmentPath; }, [](const SkyLightRef& r, const std::string& v) { r.Get().EnvironmentPath = v; }, "");
		skyLight.Property("intensity", [](const SkyLightRef& r) { return r.Get().Intensity; }, [](const SkyLightRef& r, float v) { r.Get().Intensity = std::max(v, 0.0f); }, "");
		skyLight.Property("rotation", [](const SkyLightRef& r) { return glm::degrees(r.Get().Rotation); }, [](const SkyLightRef& r, float v) { r.Get().Rotation = glm::radians(v); }, "Degrees");
		skyLight.Property("skybox_blur", [](const SkyLightRef& r) { return r.Get().SkyboxBlur; }, [](const SkyLightRef& r, float v) { r.Get().SkyboxBlur = std::max(v, 0.0f); }, "");

		auto rigidBody = api.template Class<RigidBodyRef>("RigidBody", "Physics body. Needs a collider component on the same entity.");
		rigidBody.Property("type", [](const RigidBodyRef& r) { return std::string(RigidBodyTypeToString(r.Get().Type)); },
			[](const RigidBodyRef& r, const std::string& v) { r.Get().Type = RigidBodyTypeFromString(v); }, "'Static', 'Dynamic' or 'Kinematic' (set before play)");
		rigidBody.Property("mass", [](const RigidBodyRef& r) { return r.Get().Mass; }, [](const RigidBodyRef& r, float v) { r.Get().Mass = std::max(v, 0.001f); }, "");
		rigidBody.Property("friction", [](const RigidBodyRef& r) { return r.Get().Friction; }, [](const RigidBodyRef& r, float v) { r.Get().Friction = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("restitution", [](const RigidBodyRef& r) { return r.Get().Restitution; }, [](const RigidBodyRef& r, float v) { r.Get().Restitution = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("linear_damping", [](const RigidBodyRef& r) { return r.Get().LinearDamping; }, [](const RigidBodyRef& r, float v) { r.Get().LinearDamping = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("angular_damping", [](const RigidBodyRef& r) { return r.Get().AngularDamping; }, [](const RigidBodyRef& r, float v) { r.Get().AngularDamping = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("gravity_scale", [](const RigidBodyRef& r) { return r.Get().GravityScale; }, [](const RigidBodyRef& r, float v) { r.Get().GravityScale = v; ApplyBodySettings(r.GetEntity()); }, "");
		rigidBody.Property("is_trigger", [](const RigidBodyRef& r) { return r.Get().IsTrigger; }, [](const RigidBodyRef& r, bool v) { r.Get().IsTrigger = v; }, "");
		rigidBody.Property("lock_rotation_x", [](const RigidBodyRef& r) { return r.Get().LockRotationX; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationX = v; }, "");
		rigidBody.Property("lock_rotation_y", [](const RigidBodyRef& r) { return r.Get().LockRotationY; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationY = v; }, "");
		rigidBody.Property("lock_rotation_z", [](const RigidBodyRef& r) { return r.Get().LockRotationZ; }, [](const RigidBodyRef& r, bool v) { r.Get().LockRotationZ = v; }, "");
		rigidBody.Property("continuous", [](const RigidBodyRef& r) { return r.Get().ContinuousCollision; }, [](const RigidBodyRef& r, bool v) { r.Get().ContinuousCollision = v; },
			"Continuous collision detection for fast bodies (set before play)");
		rigidBody.Property("velocity",
			[](const RigidBodyRef& r) { Entity e = r.GetEntity(); r.Get(); return GetPhysics(e) ? GetPhysics(e)->GetLinearVelocity(e) : glm::vec3(0.0f); },
			[](const RigidBodyRef& r, const glm::vec3& v) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->SetLinearVelocity(e, v); }, "Linear velocity (play mode)");
		rigidBody.Property("angular_velocity",
			[](const RigidBodyRef& r) { Entity e = r.GetEntity(); r.Get(); return GetPhysics(e) ? GetPhysics(e)->GetAngularVelocity(e) : glm::vec3(0.0f); },
			[](const RigidBodyRef& r, const glm::vec3& v) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->SetAngularVelocity(e, v); }, "");
		rigidBody.Method("add_force", [](const RigidBodyRef& r, const glm::vec3& f) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->AddForce(e, f); }, "", Arg("force"));
		rigidBody.Method("add_impulse", [](const RigidBodyRef& r, const glm::vec3& i) { Detail::AddImpulse(r, i); }, "", Arg("impulse"));
		rigidBody.Method("add_torque", [](const RigidBodyRef& r, const glm::vec3& t) { Entity e = r.GetEntity(); r.Get(); if (GetPhysics(e)) GetPhysics(e)->AddTorque(e, t); }, "", Arg("torque"));
		rigidBody.Method("teleport", [](const RigidBodyRef& r, const glm::vec3& position) { r.Get(); SetWorldPosition(r.Owner, position); }, "", Arg("position"));

		auto boxCollider = api.template Class<BoxColliderRef>("BoxCollider", "");
		boxCollider.Property("half_extents", [](const BoxColliderRef& r) { return r.Get().HalfExtents; }, [](const BoxColliderRef& r, const glm::vec3& v) { r.Get().HalfExtents = v; }, "");
		boxCollider.Property("offset", [](const BoxColliderRef& r) { return r.Get().Offset; }, [](const BoxColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; }, "");

		auto sphereCollider = api.template Class<SphereColliderRef>("SphereCollider", "");
		sphereCollider.Property("radius", [](const SphereColliderRef& r) { return r.Get().Radius; }, [](const SphereColliderRef& r, float v) { r.Get().Radius = v; }, "");
		sphereCollider.Property("offset", [](const SphereColliderRef& r) { return r.Get().Offset; }, [](const SphereColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; }, "");

		auto capsuleCollider = api.template Class<CapsuleColliderRef>("CapsuleCollider", "");
		capsuleCollider.Property("radius", [](const CapsuleColliderRef& r) { return r.Get().Radius; }, [](const CapsuleColliderRef& r, float v) { r.Get().Radius = v; }, "");
		capsuleCollider.Property("half_height", [](const CapsuleColliderRef& r) { return r.Get().HalfHeight; }, [](const CapsuleColliderRef& r, float v) { r.Get().HalfHeight = v; }, "");
		capsuleCollider.Property("offset", [](const CapsuleColliderRef& r) { return r.Get().Offset; }, [](const CapsuleColliderRef& r, const glm::vec3& v) { r.Get().Offset = v; }, "");

		auto meshCollider = api.template Class<MeshColliderRef>("MeshCollider", "Static triangle-mesh collider built from the MeshRenderer");
		meshCollider.Property("use_mesh_renderer", [](const MeshColliderRef& r) { return r.Get().UseMeshRenderer; }, [](const MeshColliderRef& r, bool v) { r.Get().UseMeshRenderer = v; }, "");

		auto script = api.template Class<ScriptRef>("ScriptComponent", "Attaches a script to the entity");
		script.Property("path", [](const ScriptRef& r) { return r.Get().ScriptPath; }, [](const ScriptRef& r, const std::string& v) { r.Get().ScriptPath = v; }, "");
		script.Property("class_name", [](const ScriptRef& r) { return r.Get().ClassName; }, [](const ScriptRef& r, const std::string& v) { r.Get().ClassName = v; }, "");
		script.Property("run_while_paused", [](const ScriptRef& r) { return r.Get().RunWhilePaused; }, [](const ScriptRef& r, bool v) { r.Get().RunWhilePaused = v; },
			"Keeps updating while the game is paused (pause menus); its delta time is the real frame time then");

		auto audioSource = api.template Class<AudioSourceRef>("AudioSource", "");
		audioSource.Property("clip", [](const AudioSourceRef& r) { return r.Get().ClipPath; }, [](const AudioSourceRef& r, const std::string& v) { r.Get().ClipPath = v; }, "");
		audioSource.Property("volume", [](const AudioSourceRef& r) { return r.Get().Volume; },
			[](const AudioSourceRef& r, float v) { r.Get().Volume = std::max(v, 0.0f); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); }, "");
		audioSource.Property("pitch", [](const AudioSourceRef& r) { return r.Get().Pitch; },
			[](const AudioSourceRef& r, float v) { r.Get().Pitch = std::max(v, 0.01f); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); }, "");
		audioSource.Property("loop", [](const AudioSourceRef& r) { return r.Get().Loop; },
			[](const AudioSourceRef& r, bool v) { r.Get().Loop = v; Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->ApplySettings(e); }, "");
		audioSource.Property("play_on_start", [](const AudioSourceRef& r) { return r.Get().PlayOnStart; }, [](const AudioSourceRef& r, bool v) { r.Get().PlayOnStart = v; }, "");
		audioSource.Property("spatial", [](const AudioSourceRef& r) { return r.Get().Spatial; }, [](const AudioSourceRef& r, bool v) { r.Get().Spatial = v; }, "");
		audioSource.Property("min_distance", [](const AudioSourceRef& r) { return r.Get().MinDistance; }, [](const AudioSourceRef& r, float v) { r.Get().MinDistance = v; }, "");
		audioSource.Property("max_distance", [](const AudioSourceRef& r) { return r.Get().MaxDistance; }, [](const AudioSourceRef& r, float v) { r.Get().MaxDistance = v; }, "");
		audioSource.Property("bus", [](const AudioSourceRef& r) { return r.Get().Bus; },
			[](const AudioSourceRef& r, const std::string& v)
			{
				Detail::RequireBus(v);
				r.Get().Bus = v;
			}, "The bus the source plays on: 'Master', 'Music' or 'Effects'");
		audioSource.Method("play", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); return GetAudio(e) ? GetAudio(e)->Play(e) : false; }, "");
		audioSource.Method("stop", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->Stop(e); }, "");
		audioSource.Method("pause", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); if (GetAudio(e)) GetAudio(e)->Pause(e); }, "");
		audioSource.ReadOnly("is_playing", [](const AudioSourceRef& r) { r.Get(); Entity e = r.GetEntity(); return GetAudio(e) ? GetAudio(e)->IsPlaying(e) : false; }, "");

		auto audioListener = api.template Class<AudioListenerRef>("AudioListener", "");
		audioListener.Property("active", [](const AudioListenerRef& r) { return r.Get().Active; }, [](const AudioListenerRef& r, bool v) { r.Get().Active = v; }, "");

		auto text = api.template Class<TextRef>("Text", "Text drawn crisp at any size: in the world (centered on its entity) or, under a UI canvas, on screen");
		text.Property("text", [](const TextRef& r) { return r.Get().Text; }, [](const TextRef& r, const std::string& v) { r.Get().Text = v; }, "");
		text.Property("font", [](const TextRef& r) { return r.Get().FontPath; }, [](const TextRef& r, const std::string& v) { r.Get().FontPath = v; },
			"A .ttf or .otf in the assets; empty for the built-in font (Inter)");
		text.Property("size", [](const TextRef& r) { return r.Get().Size; }, [](const TextRef& r, float v) { r.Get().Size = std::max(v, 0.0f); },
			"The height of one em: meters for world text, reference pixels for screen text under a UI canvas");
		text.Property("color", [](const TextRef& r) { return ColorValue { r.Get().Color }; }, [](const TextRef& r, const ColorValue& v) { r.Get().Color = v.Value; }, "");
		text.Property("alignment", [](const TextRef& r) { return std::string(TextAlignmentToString(r.Get().Alignment)); },
			[](const TextRef& r, const std::string& v) { r.Get().Alignment = Detail::RequireTextAlignment(v); }, "'left', 'center' or 'right'");
		text.Property("vertical_alignment", [](const TextRef& r) { return std::string(TextVerticalAlignmentToString(r.Get().VerticalAlignment)); },
			[](const TextRef& r, const std::string& v) { r.Get().VerticalAlignment = Detail::RequireTextVerticalAlignment(v); }, "'top', 'middle' or 'bottom'");
		text.Property("box_size", [](const TextRef& r) { return glm::vec3(r.Get().BoxSize, 0.0f); },
			[](const TextRef& r, const glm::vec3& v) { r.Get().BoxSize = glm::max(glm::vec2(v), glm::vec2(0.0f)); },
			"The box the text sits in (x width, y height; z unused); 0 sizes it to the text");
		text.Property("wrap", [](const TextRef& r) { return r.Get().Wrap; }, [](const TextRef& r, bool v) { r.Get().Wrap = v; }, "Wrap lines at the box's width");
		text.Property("overflow", [](const TextRef& r) { return std::string(TextOverflowToString(r.Get().Overflow)); },
			[](const TextRef& r, const std::string& v) { r.Get().Overflow = Detail::RequireTextOverflow(v); },
			"What text past the box does: 'visible', 'clip' or 'ellipsis'");
		text.Property("line_spacing", [](const TextRef& r) { return r.Get().LineSpacing; },
			[](const TextRef& r, float v) { r.Get().LineSpacing = std::max(v, 0.0f); }, "A multiple of the font's line height");
		text.Property("outline_width", [](const TextRef& r) { return r.Get().OutlineWidth; },
			[](const TextRef& r, float v) { r.Get().OutlineWidth = std::clamp(v, 0.0f, 0.1f); }, "In ems, 0 to 0.1");
		text.Property("outline_color", [](const TextRef& r) { return ColorValue { r.Get().OutlineColor }; },
			[](const TextRef& r, const ColorValue& v) { r.Get().OutlineColor = v.Value; }, "");
		text.Property("face_camera", [](const TextRef& r) { return r.Get().FaceCamera; }, [](const TextRef& r, bool v) { r.Get().FaceCamera = v; },
			"World text always turned toward the camera");
		text.Property("theme_color", [](const TextRef& r) { return r.Get().ThemeColor; }, [](const TextRef& r, bool v) { r.Get().ThemeColor = v; },
			"Screen text takes its color from its canvas's theme (for its widget's state) instead of color");
		text.Property("visible", [](const TextRef& r) { return r.Get().Visible; }, [](const TextRef& r, bool v) { r.Get().Visible = v; }, "");
		text.ReadOnly("measured_size", [](const TextRef& r) { return glm::vec3(Scene::MeasureTextSize(r.Get()), 0.0f); },
			"The size the text takes up (x width, y height; meters, or reference pixels on a UI), for fitting boxes and backgrounds to it");

		// --- UI -----------------------------------------------------------------
		auto canvas = api.template Class<UICanvasRef>("UICanvas",
			"The root of a screen UI: everything under it with a UIRect is laid out on screen, over the game");
		canvas.Property("reference_size", [](const UICanvasRef& r) { return Detail::ToScriptVector(r.Get().ReferenceSize); },
			[](const UICanvasRef& r, const glm::vec3& v) { r.Get().ReferenceSize = glm::max(glm::vec2(v), glm::vec2(1.0f)); },
			"The screen size the UI is designed for, in reference pixels (x width, y height; z unused)");
		canvas.Property("scale_mode", [](const UICanvasRef& r) { return std::string(UIScaleModeToString(r.Get().ScaleMode)); },
			[](const UICanvasRef& r, const std::string& v) { r.Get().ScaleMode = Detail::RequireUIScaleMode(v); },
			"'fit_height', 'fit_width', 'expand' (the whole reference area fits) or 'constant_pixels'");
		canvas.Property("sort_order", [](const UICanvasRef& r) { return r.Get().SortOrder; }, [](const UICanvasRef& r, int v) { r.Get().SortOrder = v; },
			"Canvases with a higher sort order draw on top");
		canvas.Property("visible", [](const UICanvasRef& r) { return r.Get().Visible; }, [](const UICanvasRef& r, bool v) { r.Get().Visible = v; }, "");
		canvas.Property("modal", [](const UICanvasRef& r) { return r.Get().Modal; }, [](const UICanvasRef& r, bool v) { r.Get().Modal = v; },
			"While shown, it takes every key, button and click: the game and other UI get nothing (pause menus, dialogs)");
		canvas.Property("theme", [](const UICanvasRef& r) { return r.Get().Theme; }, [](const UICanvasRef& r, const std::string& v) { r.Get().Theme = v; },
			"The .btheme asset its UI is drawn with; \"\" is the built-in one, Builtin/MageCap.btheme");
		canvas.ReadOnly("scale", [](const UICanvasRef& r)
			{
				const UICanvasComponent& c = r.Get();
				const Scene* scene = r.GetEntity().GetScene();
				const glm::vec2 screen(static_cast<float>(scene->GetViewportWidth()), static_cast<float>(scene->GetViewportHeight()));
				return ComputeUIScale(c.ReferenceSize, c.ScaleMode, screen);
			}, "Screen pixels per reference pixel at the current screen size");

		auto rect = api.template Class<UIRectRef>("UIRect",
			"Where a UI entity sits inside its parent: anchors are fractions of the parent ((0, 0) top-left, (1, 1) bottom-right), "
			"offsets are reference pixels from the anchored corners");
		rect.Property("anchor_min", [](const UIRectRef& r) { return Detail::ToScriptVector(r.Get().AnchorMin); },
			[](const UIRectRef& r, const glm::vec3& v) { r.Get().AnchorMin = glm::clamp(glm::vec2(v), 0.0f, 1.0f); },
			"The parent's point the top-left corner follows (z unused)");
		rect.Property("anchor_max", [](const UIRectRef& r) { return Detail::ToScriptVector(r.Get().AnchorMax); },
			[](const UIRectRef& r, const glm::vec3& v) { r.Get().AnchorMax = glm::clamp(glm::vec2(v), 0.0f, 1.0f); },
			"The parent's point the bottom-right corner follows; different anchors stretch with the parent (z unused)");
		rect.Property("pivot", [](const UIRectRef& r) { return Detail::ToScriptVector(r.Get().Pivot); },
			[](const UIRectRef& r, const glm::vec3& v) { r.Get().Pivot = glm::clamp(glm::vec2(v), 0.0f, 1.0f); },
			"The point of the rect that stays put when its size changes ((0, 0) top-left, (1, 1) bottom-right; z unused)");
		rect.Property("offset_min", [](const UIRectRef& r) { return Detail::ToScriptVector(r.Get().OffsetMin); },
			[](const UIRectRef& r, const glm::vec3& v) { r.Get().OffsetMin = glm::vec2(v); }, "The top-left corner's distance from anchor_min (z unused)");
		rect.Property("offset_max", [](const UIRectRef& r) { return Detail::ToScriptVector(r.Get().OffsetMax); },
			[](const UIRectRef& r, const glm::vec3& v) { r.Get().OffsetMax = glm::vec2(v); }, "The bottom-right corner's distance from anchor_max (z unused)");
		rect.Property("position", [](const UIRectRef& r) { const UIRectComponent& c = r.Get(); return Detail::ToScriptVector(GetUIRectPosition(c.Pivot, c.OffsetMin, c.OffsetMax)); },
			[](const UIRectRef& r, const glm::vec3& v) { UIRectComponent& c = r.Get(); SetUIRectPosition(c.Pivot, c.OffsetMin, c.OffsetMax, glm::vec2(v)); },
			"Where the pivot sits relative to the anchors, in reference pixels (moves both offsets; z unused)");
		rect.Property("size", [](const UIRectRef& r) { const UIRectComponent& c = r.Get(); return Detail::ToScriptVector(GetUIRectSize(c.OffsetMin, c.OffsetMax)); },
			[](const UIRectRef& r, const glm::vec3& v) { UIRectComponent& c = r.Get(); SetUIRectSize(c.Pivot, c.OffsetMin, c.OffsetMax, glm::vec2(v)); },
			"The size in reference pixels, keeping the pivot in place (with stretching anchors, the size beyond the stretched part; z unused)");
		rect.Property("visible", [](const UIRectRef& r) { return r.Get().Visible; }, [](const UIRectRef& r, bool v) { r.Get().Visible = v; },
			"Hidden rects hide everything under them too");
		rect.Property("interactable", [](const UIRectRef& r) { return r.Get().Interactable; }, [](const UIRectRef& r, bool v) { r.Get().Interactable = v; },
			"Whether the pointer and focus can reach it and everything under it");
		rect.Property("width_sizing", [](const UIRectRef& r) { return std::string(UISizingToString(r.Get().WidthSizing)); },
			[](const UIRectRef& r, const std::string& v) { r.Get().WidthSizing = Detail::RequireUISizing(v); },
			"Inside a UILayout: 'fixed' keeps its own width, 'fill' shares the space left over");
		rect.Property("height_sizing", [](const UIRectRef& r) { return std::string(UISizingToString(r.Get().HeightSizing)); },
			[](const UIRectRef& r, const std::string& v) { r.Get().HeightSizing = Detail::RequireUISizing(v); },
			"Inside a UILayout: 'fixed' keeps its own height, 'fill' shares the space left over");
		rect.Property("focusable", [](const UIRectRef& r) { return r.Get().Focusable; }, [](const UIRectRef& r, bool v) { r.Get().Focusable = v; },
			"Can take keyboard and gamepad focus (Tab, arrows, d-pad); it also stops the pointer");
		rect.Property("style_class", [](const UIRectRef& r) { return r.Get().StyleClass; }, [](const UIRectRef& r, const std::string& v) { r.Get().StyleClass = v; },
			"A named style of its canvas's theme (\"primary\", \"danger\"); \"\" for the theme's plain style");
		rect.ReadOnly("hovered", [](const UIRectRef& r) { Entity e = r.GetEntity(); r.Get(); return e.GetScene()->GetUIHovered() == e; },
			"True while the pointer is over it (and it is what the pointer would use)");
		rect.ReadOnly("pressed", [](const UIRectRef& r) { Entity e = r.GetEntity(); r.Get(); return e.GetScene()->GetUIPressed() == e; },
			"True from a press on it until the button is let go");
		rect.ReadOnly("focused", [](const UIRectRef& r) { Entity e = r.GetEntity(); r.Get(); return e.GetScene()->GetUIFocused() == e; },
			"True while it has keyboard and gamepad focus");
		rect.Method("focus", [](const UIRectRef& r) { Entity e = r.GetEntity(); r.Get(); return e.GetScene()->SetUIFocus(e); },
			"Gives it focus; False when it cannot take focus (not focusable, hidden, not interactable, or behind a modal canvas)");
		rect.ReadOnly("screen_position", [](const UIRectRef& r) { return Detail::ToScriptVector(r.Get().RuntimeRect.Min); },
			"The top-left corner on screen, in pixels from the screen's top-left, as of the last frame drawn (z unused)");
		rect.ReadOnly("screen_size", [](const UIRectRef& r) { return Detail::ToScriptVector(r.Get().RuntimeRect.GetSize()); },
			"The size on screen in pixels, as of the last frame drawn (z unused)");

		auto image = api.template Class<UIImageRef>("UIImage", "A filled rectangle with optional rounded corners, border and texture, sized by the entity's UIRect");
		image.Property("texture", [](const UIImageRef& r) { return r.Get().TexturePath; }, [](const UIImageRef& r, const std::string& v) { r.Get().TexturePath = v; },
			"An image in the assets stretched over the rect; empty draws a plain color");
		image.Property("color", [](const UIImageRef& r) { return ColorValue { r.Get().Color }; }, [](const UIImageRef& r, const ColorValue& v) { r.Get().Color = v.Value; },
			"Multiplies the texture");
		image.Property("corner_radius", [](const UIImageRef& r) { return r.Get().CornerRadius; },
			[](const UIImageRef& r, float v) { r.Get().CornerRadius = std::max(v, 0.0f); }, "In reference pixels");
		image.Property("border_width", [](const UIImageRef& r) { return r.Get().BorderWidth; },
			[](const UIImageRef& r, float v) { r.Get().BorderWidth = std::max(v, 0.0f); }, "In reference pixels, drawn inside the rect");
		image.Property("border_color", [](const UIImageRef& r) { return ColorValue { r.Get().BorderColor }; },
			[](const UIImageRef& r, const ColorValue& v) { r.Get().BorderColor = v.Value; }, "");
		image.Property("slice_border", [](const UIImageRef& r) { return Detail::ToList(r.Get().SliceBorder); },
			[](const UIImageRef& r, const std::vector<float>& v) { r.Get().SliceBorder = Detail::RequireEdges(v, "slice_border"); },
			"9-slice: the texture's edges [left, top, right, bottom] in texture pixels keep their size and only the middle stretches; 0 stretches all");
		image.Property("visible", [](const UIImageRef& r) { return r.Get().Visible; }, [](const UIImageRef& r, bool v) { r.Get().Visible = v; }, "");

		auto button = api.template Class<UIButtonRef>("UIButton",
			"Clickable: colors its UIImage by state, from its canvas's theme unless it sets its own; a click (or ui_accept while focused) raises 'clicked'");
		// Reading a color gives the one the button shows: its own, or its theme's; setting one makes it its own.
		const auto buttonColor = [](const UIButtonRef& r, UIWidgetState state)
		{
			Entity entity = r.GetEntity();
			const auto* rect = entity.TryGetComponent<UIRectComponent>();
			const Ref<const UITheme> theme = entity.GetScene()->GetUIThemeFor(entity);
			return ColorValue { r.Get().GetColor(state, theme->Get(UIThemeWidget::Button, rect ? rect->StyleClass : std::string())) };
		};
		button.Property("normal_color", [buttonColor](const UIButtonRef& r) { return buttonColor(r, UIWidgetState::Normal); },
			[](const UIButtonRef& r, const ColorValue& v) { r.Get().NormalColor = v.Value; }, "Its own once set; otherwise its theme's");
		button.Property("hover_color", [buttonColor](const UIButtonRef& r) { return buttonColor(r, UIWidgetState::Hover); },
			[](const UIButtonRef& r, const ColorValue& v) { r.Get().HoverColor = v.Value; }, "Under the pointer");
		button.Property("pressed_color", [buttonColor](const UIButtonRef& r) { return buttonColor(r, UIWidgetState::Pressed); },
			[](const UIButtonRef& r, const ColorValue& v) { r.Get().PressedColor = v.Value; }, "Held down");
		button.Property("disabled_color", [buttonColor](const UIButtonRef& r) { return buttonColor(r, UIWidgetState::Disabled); },
			[](const UIButtonRef& r, const ColorValue& v) { r.Get().DisabledColor = v.Value; }, "When not interactable");
		button.Method("clear_colors", [](const UIButtonRef& r) { r.Get() = UIButtonComponent(); }, "Goes back to its theme's colors for every state");

		auto toggle = api.template Class<UIToggleRef>("UIToggle", "A check box; clicking flips it and raises 'changed' (on, widget)");
		toggle.Property("on", [](const UIToggleRef& r) { return r.Get().On; }, [](const UIToggleRef& r, bool v) { r.Get().On = v; }, "");

		auto slider = api.template Class<UISliderRef>("UISlider",
			"A track with a knob; dragging, clicking or ui_left/ui_right while focused raise 'changed' (value, widget)");
		slider.Property("min", [](const UISliderRef& r) { return r.Get().Min; },
			[](const UISliderRef& r, float v) { UISliderComponent& c = r.Get(); c.Min = v; c.Value = SnapUISliderValue(c.Value, c.Min, c.Max, c.Step); }, "");
		slider.Property("max", [](const UISliderRef& r) { return r.Get().Max; },
			[](const UISliderRef& r, float v) { UISliderComponent& c = r.Get(); c.Max = v; c.Value = SnapUISliderValue(c.Value, c.Min, c.Max, c.Step); }, "");
		slider.Property("step", [](const UISliderRef& r) { return r.Get().Step; },
			[](const UISliderRef& r, float v) { UISliderComponent& c = r.Get(); c.Step = std::max(v, 0.0f); c.Value = SnapUISliderValue(c.Value, c.Min, c.Max, c.Step); },
			"Values snap to steps from min; 0 for any value");
		slider.Property("value", [](const UISliderRef& r) { return r.Get().Value; },
			[](const UISliderRef& r, float v) { UISliderComponent& c = r.Get(); c.Value = SnapUISliderValue(v, c.Min, c.Max, c.Step); },
			"Kept between min and max and snapped to step");

		auto dropdown = api.template Class<UIDropdownRef>("UIDropdown",
			"A button that opens a list of options; picking one raises 'changed' (index, widget), and its Text shows the selection");
		dropdown.Property("options", [](const UIDropdownRef& r) { return r.Get().Options; },
			[](const UIDropdownRef& r, const std::vector<std::string>& v)
			{
				UIDropdownComponent& c = r.Get();
				c.Options = v;
				c.Selected = std::clamp(c.Selected, c.Options.empty() ? -1 : 0, static_cast<int>(c.Options.size()) - 1);
			}, "The choices, in order");
		dropdown.Property("selected", [](const UIDropdownRef& r) { return r.Get().Selected; },
			[](const UIDropdownRef& r, int v) { UIDropdownComponent& c = r.Get(); c.Selected = Detail::RequireDropdownIndex(c, v); },
			"The selected option's index, or -1 for none");
		dropdown.ReadOnly("selected_text", [](const UIDropdownRef& r)
			{
				const UIDropdownComponent& c = r.Get();
				return c.Selected >= 0 && c.Selected < static_cast<int>(c.Options.size()) ? c.Options[static_cast<size_t>(c.Selected)] : std::string();
			}, "The selected option, or empty");

		auto textInput = api.template Class<UITextInputRef>("UITextInput",
			"A field the player types into; its text is the entity's Text. Raises 'changed' (text, widget) as it is edited and 'submitted' (text, widget) on Enter (Ctrl+Enter when multi-line)");
		textInput.Property("placeholder", [](const UITextInputRef& r) { return r.Get().Placeholder; },
			[](const UITextInputRef& r, const std::string& v) { r.Get().Placeholder = v; }, "Shown, dimmed, while the text is empty");
		textInput.Property("multi_line", [](const UITextInputRef& r) { return r.Get().MultiLine; },
			[](const UITextInputRef& r, bool v) { r.Get().MultiLine = v; }, "Enter starts a new line; Ctrl+Enter submits");
		textInput.Property("password", [](const UITextInputRef& r) { return r.Get().Password; },
			[](const UITextInputRef& r, bool v) { r.Get().Password = v; }, "Shows a bullet per character, on one line, and can't be copied");
		textInput.Property("max_length", [](const UITextInputRef& r) { return r.Get().MaxLength; },
			[](const UITextInputRef& r, int v)
			{
				if (v < 0)
					throw ScriptValueError("max_length is a number of characters (0 for no limit), not " + std::to_string(v));
				r.Get().MaxLength = v;
			}, "The most characters it takes; 0 for no limit");

		auto layout = api.template Class<UILayoutRef>("UILayout",
			"Places the entity's children that have a UIRect: one under another, side by side, or in a grid");
		layout.Property("direction", [](const UILayoutRef& r) { return std::string(UILayoutDirectionToString(r.Get().Direction)); },
			[](const UILayoutRef& r, const std::string& v) { r.Get().Direction = Detail::RequireUILayoutDirection(v); }, "'vertical', 'horizontal' or 'grid'");
		layout.Property("spacing", [](const UILayoutRef& r) { return r.Get().Spacing; }, [](const UILayoutRef& r, float v) { r.Get().Spacing = std::max(v, 0.0f); },
			"Reference pixels between children");
		layout.Property("padding", [](const UILayoutRef& r) { return Detail::ToList(r.Get().Padding); },
			[](const UILayoutRef& r, const std::vector<float>& v) { r.Get().Padding = Detail::RequireEdges(v, "padding"); },
			"Space inside the edges: [left, top, right, bottom] in reference pixels");
		layout.Property("main_alignment", [](const UILayoutRef& r) { return std::string(UIAlignmentToString(r.Get().MainAlignment)); },
			[](const UILayoutRef& r, const std::string& v) { r.Get().MainAlignment = Detail::RequireUIAlignment(v); },
			"Where the children sit along the direction when none fills: 'start', 'center' or 'end'");
		layout.Property("cross_alignment", [](const UILayoutRef& r) { return std::string(UIAlignmentToString(r.Get().CrossAlignment)); },
			[](const UILayoutRef& r, const std::string& v) { r.Get().CrossAlignment = Detail::RequireUIAlignment(v); },
			"Where a child that doesn't fill sits across the direction: 'start', 'center' or 'end'");
		layout.Property("columns", [](const UILayoutRef& r) { return r.Get().Columns; }, [](const UILayoutRef& r, int v) { r.Get().Columns = std::max(v, 1); },
			"Columns of a grid");

		auto scroll = api.template Class<UIScrollRef>("UIScroll",
			"Scrolls and clips the entity's children; the mouse wheel scrolls it, and focus scrolls hidden UI into view");
		scroll.Property("horizontal", [](const UIScrollRef& r) { return r.Get().Horizontal; }, [](const UIScrollRef& r, bool v) { r.Get().Horizontal = v; }, "");
		scroll.Property("vertical", [](const UIScrollRef& r) { return r.Get().Vertical; }, [](const UIScrollRef& r, bool v) { r.Get().Vertical = v; }, "");
		scroll.Property("scroll_position", [](const UIScrollRef& r) { return Detail::ToScriptVector(r.Get().ScrollPosition); },
			[](const UIScrollRef& r, const glm::vec3& v) { r.Get().ScrollPosition = glm::max(glm::vec2(v), glm::vec2(0.0f)); },
			"How far the content is scrolled, in reference pixels (kept in range when the UI is laid out; z unused)");
		scroll.Property("wheel_speed", [](const UIScrollRef& r) { return r.Get().WheelSpeed; },
			[](const UIScrollRef& r, float v) { r.Get().WheelSpeed = std::max(v, 0.0f); }, "Reference pixels per wheel notch");
		scroll.Property("show_scrollbars", [](const UIScrollRef& r) { return r.Get().ShowScrollbars; },
			[](const UIScrollRef& r, bool v) { r.Get().ShowScrollbars = v; }, "");
		scroll.ReadOnly("content_size", [](const UIScrollRef& r) { return Detail::ToScriptVector(r.Get().RuntimeContentSize); },
			"The size of what it holds, in reference pixels, as of the last frame drawn (z unused)");
		scroll.ReadOnly("viewport_size", [](const UIScrollRef& r) { return Detail::ToScriptVector(r.Get().RuntimeViewportSize); },
			"The size of the part in view, in reference pixels, as of the last frame drawn (z unused)");

		// --- Entity -----------------------------------------------------------
		auto entity = api.template Class<ScriptEntity>("Entity", "A handle to an entity in a scene");
		entity.ReadOnly("id", [](const ScriptEntity& e) { return static_cast<uint64_t>(e.ID); }, "");
		entity.ReadOnly("valid", [](const ScriptEntity& e) { return e.IsValid(); }, "False once the entity has been destroyed");
		entity.Property("name", [](const ScriptEntity& e) { return e.Resolve().GetName(); }, [](const ScriptEntity& e, const std::string& v) { e.Resolve().SetName(v); }, "");
		entity.Property("keep_across_scenes", [](const ScriptEntity& e) { return e.Resolve().GetComponent<TagComponent>().KeepAcrossScenes; },
			[](const ScriptEntity& e, bool v) { e.Resolve().GetComponent<TagComponent>().KeepAcrossScenes = v; },
			"The entity and its children stay when load_scene changes the scene, keeping their scripts, physics and sounds");
		entity.ReadOnly("transform", [](const ScriptEntity& e) { e.Resolve(); return TransformRef { e }; }, "");
		entity.Property("position", [](const ScriptEntity& e) { return GetWorldPosition(e); }, [](const ScriptEntity& e, const glm::vec3& v) { SetWorldPosition(e, v); }, "World-space position");
		entity.Property("local_position", [](const ScriptEntity& e) { return e.Resolve().GetComponent<TransformComponent>().Translation; },
			[](const ScriptEntity& e, const glm::vec3& v) { Entity resolved = e.Resolve(); resolved.GetComponent<TransformComponent>().Translation = v; TeleportIfSimulated(resolved); }, "");
		entity.Property("rotation", [](const ScriptEntity& e) { return glm::degrees(e.Resolve().GetComponent<TransformComponent>().GetRotationEuler()); },
			[](const ScriptEntity& e, const glm::vec3& v) { Entity resolved = e.Resolve(); resolved.GetComponent<TransformComponent>().SetRotationEuler(glm::radians(v)); TeleportIfSimulated(resolved); },
			"Local rotation as Euler angles in degrees");
		entity.Property("scale", [](const ScriptEntity& e) { return e.Resolve().GetComponent<TransformComponent>().Scale; },
			[](const ScriptEntity& e, const glm::vec3& v) { e.Resolve().GetComponent<TransformComponent>().Scale = v; }, "");
		entity.Property("text", [](const ScriptEntity& e) { return Detail::RequireUIComponent<TextComponent>(e, "text", "Text").Text; },
			[](const ScriptEntity& e, const std::string& v) { Detail::RequireUIComponent<TextComponent>(e, "text", "Text").Text = v; },
			"Its Text component's text (a label, a button's caption, world text)");
		entity.Property("visible", [](const ScriptEntity& e) { return Detail::RequireUIVisible(e); },
			[](const ScriptEntity& e, bool v) { Detail::RequireUIVisible(e) = v; },
			"Whether a UI entity (its UIRect, or its UICanvas) is shown; hiding it hides everything under it");
		entity.Property("interactable", [](const ScriptEntity& e) { return Detail::RequireUIComponent<UIRectComponent>(e, "interactable", "UIRect").Interactable; },
			[](const ScriptEntity& e, bool v) { Detail::RequireUIComponent<UIRectComponent>(e, "interactable", "UIRect").Interactable = v; },
			"Whether the pointer and focus can reach a UI entity and everything under it");
		entity.Property("value", [](const ScriptEntity& e) { return Detail::GetWidgetValue(e); },
			[](const ScriptEntity& e, const ScriptValue& v) { Detail::SetWidgetValue(e, v); },
			"A toggle's on/off (true or false), a slider's value, a dropdown's selected index, or a text input's text; setting it raises no 'changed'");
		entity.Method("focus", [](const ScriptEntity& e)
			{
				Detail::RequireUIComponent<UIRectComponent>(e, "focus", "UIRect");
				Entity resolved = e.Resolve();
				return resolved.GetScene()->SetUIFocus(resolved);
			}, "Gives a UI entity keyboard and gamepad focus; False when it can't take focus (not focusable, hidden, not interactable, or behind a modal canvas)");
		entity.ReadOnly("forward", [](const ScriptEntity& e) { Entity resolved = e.Resolve(); return glm::normalize(-glm::vec3(resolved.GetScene()->GetWorldTransform(resolved)[2])); }, "");
		entity.ReadOnly("right", [](const ScriptEntity& e) { Entity resolved = e.Resolve(); return glm::normalize(glm::vec3(resolved.GetScene()->GetWorldTransform(resolved)[0])); }, "");
		entity.ReadOnly("up", [](const ScriptEntity& e) { Entity resolved = e.Resolve(); return glm::normalize(glm::vec3(resolved.GetScene()->GetWorldTransform(resolved)[1])); }, "");
		entity.Property("parent",
			[](const ScriptEntity& e) { return EntityOrNone(e.Resolve().GetParent()); },
			[](const ScriptEntity& e, const std::optional<ScriptEntity>& parent)
			{
				Entity resolved = e.Resolve();
				if (!resolved.GetScene()->SetParent(resolved, Detail::ResolveOptional(parent), true))
					throw ScriptValueError("Cannot create a cycle in the hierarchy");
			}, "");
		entity.ReadOnly("children", [](const ScriptEntity& e)
			{
				std::vector<ScriptEntity> children;
				for (Entity child : e.Resolve().GetChildren())
					children.emplace_back(child);
				return children;
			}, "");
		entity.Method("find_child", [](const ScriptEntity& e, const std::string& name)
			{
				std::function<Entity(Entity)> search = [&](Entity current) -> Entity
				{
					for (Entity child : current.GetChildren())
					{
						if (child.GetName() == name)
							return child;
						if (Entity found = search(child))
							return found;
					}
					return {};
				};
				return EntityOrNone(search(e.Resolve()));
			}, "Finds a descendant by name", Arg("name"));
		entity.Method("has_component", [](const ScriptEntity& e, const ComponentName& component) { return FindComponentInfo(component.Name).Has(e.Resolve()); }, "", Arg("component"));
		entity.Method("get_component", [](const ScriptEntity& e, const ComponentName& component) -> std::optional<ComponentHandle>
			{
				const ComponentInfo& info = FindComponentInfo(component.Name);
				if (!info.Has(e.Resolve()))
					return std::nullopt;
				return info.MakeRef(e);
			}, "Returns the component or None, e.g. entity.get_component('RigidBody')", Arg("component"));
		entity.Method("add_component", [](const ScriptEntity& e, const ComponentName& component)
			{
				const ComponentInfo& info = FindComponentInfo(component.Name);
				info.Add(e.Resolve());
				return info.MakeRef(e);
			}, "Adds the component (if missing) and returns it", Arg("component"));
		entity.Method("remove_component", [](const ScriptEntity& e, const ComponentName& component)
			{
				const ComponentInfo& info = FindComponentInfo(component.Name);
				if (std::string(info.Name) == "Transform")
					throw ScriptValueError("The Transform component cannot be removed");
				Entity resolved = e.Resolve();
				if (std::string(info.Name) == "RigidBody" && GetPhysics(resolved))
					GetPhysics(resolved)->DestroyBody(resolved);
				info.Remove(resolved);
			}, "", Arg("component"));
		entity.ReadOnly("components", [](const ScriptEntity& e)
			{
				Entity resolved = e.Resolve();
				std::vector<std::string> names;
				for (const ComponentInfo& info : GetComponentInfos())
				{
					if (info.Has(resolved))
						names.emplace_back(info.Name);
				}
				return names;
			}, "Names of the components on this entity");
		entity.Method("destroy", [](const ScriptEntity& e) { if (Entity resolved = e.TryResolve()) resolved.GetScene()->DestroyEntity(resolved); },
			"Destroys the entity (at the end of the frame during play)");
		entity.Method("look_at", [](const ScriptEntity& e, const glm::vec3& target, const glm::vec3& up) { LookAt(e, target, up); },
			"Rotates the entity so -Z faces the target", Arg("target"), Arg("up", glm::vec3(0.0f, 1.0f, 0.0f)));
		entity.Method("translate", [](const ScriptEntity& e, const glm::vec3& delta) { SetWorldPosition(e, GetWorldPosition(e) + delta); }, "", Arg("delta"));
		entity.Method("rotate", [](const ScriptEntity& e, const glm::vec3& degrees)
			{
				Entity resolved = e.Resolve();
				TransformComponent& component = resolved.GetComponent<TransformComponent>();
				component.SetRotation(component.GetRotation() * glm::quat(glm::radians(degrees)));
				TeleportIfSimulated(resolved);
			}, "Rotates by Euler angles (degrees) in local space", Arg("degrees"));

		// --- Physics ----------------------------------------------------------
		auto raycastHit = api.template Class<RaycastHit>("RaycastHit", "");
		raycastHit.ReadOnly("entity", [](const RaycastHit& hit) -> std::optional<ScriptEntity>
			{
				Scene* scene = GetContextScene();
				return scene ? EntityOrNone(scene->TryGetEntityByUUID(hit.Entity)) : std::nullopt;
			}, "");
		raycastHit.ReadOnly("point", [](const RaycastHit& hit) { return hit.Point; }, "");
		raycastHit.ReadOnly("normal", [](const RaycastHit& hit) { return hit.Normal; }, "");
		raycastHit.ReadOnly("distance", [](const RaycastHit& hit) { return hit.Distance; }, "");

		api.Function("raycast", [](const glm::vec3& origin, const glm::vec3& direction, float maxDistance) -> std::optional<RaycastHit>
			{
				Scene& scene = RequireContextScene();
				if (!scene.GetPhysicsWorld())
					return std::nullopt;
				RaycastHit hit;
				if (!scene.GetPhysicsWorld()->Raycast(origin, direction, maxDistance, hit))
					return std::nullopt;
				return hit;
			}, "Casts a ray against physics colliders (play mode); returns RaycastHit or None",
			Arg("origin"), Arg("direction"), Arg("max_distance", 1000.0f));

		api.Function("get_gravity", []() { return RequireContextScene().GetSettings().Gravity; }, "");
		api.Function("set_gravity", [](const glm::vec3& gravity) { RequireContextScene().GetSettings().Gravity = gravity; }, "", Arg("gravity"));

		// --- Scene ------------------------------------------------------------
		api.Function("create_entity", [](const std::string& name, const std::optional<ScriptEntity>& parent)
			{
				Scene& scene = RequireContextScene();
				return ScriptEntity(scene.CreateChildEntity(Detail::ResolveOptional(parent), name));
			}, "Creates an entity in the active scene", Arg("name", std::string("Entity")), Arg("parent", std::optional<ScriptEntity>()));
		api.Function("destroy", [](const ScriptEntity& target) { if (Entity e = target.TryResolve()) e.GetScene()->DestroyEntity(e); }, "", Arg("entity"));

		// --- Tweens ------------------------------------------------------------
		auto tween = api.template Class<ScriptTween>("Tween",
			"A property moving smoothly over time (what tween and tween_by return); 'tween finished' is sent when it reaches its end");
		tween.ReadOnly("id", [](const ScriptTween& t) { return t.ID; }, "The ID sent with 'tween finished'");
		tween.ReadOnly("entity", [](const ScriptTween& t) { return t.Target; }, "");
		tween.ReadOnly("property", [](const ScriptTween& t) { return t.Property; }, "");
		tween.ReadOnly("running", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens && tweens->IsRunning(t.ID); },
			"True until it ends (paused or waiting for its delay counts as running)");
		tween.ReadOnly("finished", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens && tweens->HasFinished(t.ID); },
			"True once it reached its end; false when it was stopped or replaced");
		tween.ReadOnly("progress", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens ? tweens->GetProgress(t.ID) : 0.0; },
			"From 0 to 1 over every repeat (the current one when repeating forever)");
		tween.Property("repeat", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens ? tweens->GetRepeat(t.ID) : int64_t(0); },
			[](const ScriptTween& t, int64_t repeat)
			{
				if (repeat < -1 || repeat > SceneTweens::c_MaxRepeat)
					throw ScriptValueError("A tween's repeat is a whole number from -1 (forever) to 1,000,000");
				if (SceneTweens* tweens = Detail::FindTweens(t))
					tweens->SetRepeat(t.ID, repeat);
			}, "How many more times it runs after the first: 0, a count, or -1 for forever");
		tween.Property("yoyo", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens && tweens->GetYoyo(t.ID); },
			[](const ScriptTween& t, bool yoyo) { if (SceneTweens* tweens = Detail::FindTweens(t)) tweens->SetYoyo(t.ID, yoyo); },
			"Every other run goes back the way it came (needs repeat)");
		tween.Property("while_paused", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens && tweens->GetWhilePaused(t.ID); },
			[](const ScriptTween& t, bool whilePaused) { if (SceneTweens* tweens = Detail::FindTweens(t)) tweens->SetWhilePaused(t.ID, whilePaused); },
			"Keeps moving while the game is paused (set_paused), for pause menus");
		tween.Method("stop", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens && tweens->Stop(t.ID); },
			"Stops it where it is, sending no 'tween finished'; false when it had already ended");
		tween.Method("pause", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens && tweens->Pause(t.ID); }, "");
		tween.Method("resume", [](const ScriptTween& t) { SceneTweens* tweens = Detail::FindTweens(t); return tweens && tweens->Resume(t.ID); }, "");
		tween.Method("complete", [](const ScriptTween& t)
			{
				Scene* scene = Scene::GetSceneByID(t.SceneID);
				return scene && scene->GetTweens() && scene->GetTweens()->Complete(*scene, t.ID);
			}, "Jumps to the end, repeats included (the current run when repeating forever); 'tween finished' follows next frame");

		api.Function("tween", [](const ScriptEntity& target, const std::string& property, const ScriptValue& to, double seconds, const std::string& ease, double delay)
			{ return Detail::StartTween(target, property, to, seconds, ease, delay, false); },
			"Moves a property smoothly from where it is to 'to' over 'seconds': an entity's position, local_position, rotation or scale, "
			"or a component's number or vector property as \"Component.property\" (\"UIImage.color\", \"AudioSource.volume\"). "
			"A new tween of the same property replaces the running one",
			Arg("entity"), Arg("property"), Arg("to"), Arg("seconds"), Arg("ease", std::string("in out")), Arg("delay", 0.0));
		api.Function("tween_by", [](const ScriptEntity& target, const std::string& property, const ScriptValue& amount, double seconds, const std::string& ease, double delay)
			{ return Detail::StartTween(target, property, amount, seconds, ease, delay, true); },
			"Like tween, but adds 'amount' to the property; a rotation turns by the full amount, more than one turn too",
			Arg("entity"), Arg("property"), Arg("amount"), Arg("seconds"), Arg("ease", std::string("in out")), Arg("delay", 0.0));
		api.Function("stop_tweens", [](const ScriptEntity& target, const std::string& property)
			{
				Entity entity = target.Resolve();
				SceneTweens* tweens = entity.GetScene()->GetTweens();
				return static_cast<int64_t>(tweens ? tweens->StopAll(target.ID, property) : 0);
			}, "Stops an entity's tweens, or only those of one property, sending no 'tween finished'; returns how many it stopped",
			Arg("entity"), Arg("property", std::string()));
		// Defined after Entity, so their return type has its script name.
		api.Function("ui_focused", []() { return EntityOrNone(RequireContextScene().GetUIFocused()); },
			"The UI entity with keyboard and gamepad focus, or None");
		api.Function("ui_hovered", []() { return EntityOrNone(RequireContextScene().GetUIHovered()); },
			"The UI entity under the pointer that the pointer can use, or None");
		api.Function("find_entity", [](const std::string& name) { return EntityOrNone(RequireContextScene().FindEntityByName(name)); }, "", Arg("name"));
		api.Function("get_entity", [](uint64_t id) { return EntityOrNone(RequireContextScene().TryGetEntityByUUID(UUID(id))); }, "", Arg("id"));
		api.Function("get_entities", [](const std::optional<ComponentName>& component)
			{
				Scene& scene = RequireContextScene();
				std::vector<ScriptEntity> result;
				const ComponentInfo* info = component ? &FindComponentInfo(component->Name) : nullptr;
				for (Entity candidate : scene.GetAllEntities())
				{
					if (scene.IsPendingDestroy(candidate))
						continue;
					if (!info || info->Has(candidate))
						result.emplace_back(candidate);
				}
				return result;
			}, "All entities (optionally only those with a component), in hierarchy order", Arg("component", std::optional<ComponentName>()));
		api.Function("instantiate", [](const std::string& prefab, const std::optional<glm::vec3>& position, const std::optional<ScriptEntity>& parent)
			{
				Scene& scene = RequireContextScene();
				Entity root = SceneSerializer::InstantiatePrefab(scene, prefab, Detail::ResolveOptional(parent));
				if (!root)
					throw ScriptValueError("Prefab '" + prefab + "' could not be instantiated");
				if (position)
					root.GetComponent<TransformComponent>().Translation = *position;
				return ScriptEntity(root);
			}, "Spawns a .bprefab and returns its root entity",
			Arg("prefab"), Arg("position", std::optional<glm::vec3>()), Arg("parent", std::optional<ScriptEntity>()));
		api.Function("primary_camera", []() { return EntityOrNone(RequireContextScene().GetPrimaryCameraEntity()); }, "");
		api.Function("screen_to_world_ray", [](const glm::vec2& screenPosition) -> std::optional<std::pair<glm::vec3, glm::vec3>>
			{
				Scene& scene = RequireContextScene();
				const glm::vec2 viewport = Detail::ScriptViewportSize(scene);
				RenderCamera view;
				if (!scene.GetPrimaryCamera(viewport.x / std::max(viewport.y, 1.0f), view))
					return std::nullopt;
				glm::vec3 origin(0.0f), direction(0.0f);
				Math::ScreenPointToRay(screenPosition, viewport, glm::inverse(view.Projection * view.View), origin, direction);
				return std::make_pair(origin, direction);
			}, "Returns (origin, direction) of a ray through a viewport pixel, or None without a camera", Arg("screen_position"));
		api.Function("world_to_screen", [](const glm::vec3& worldPosition) -> std::optional<glm::vec2>
			{
				Scene& scene = RequireContextScene();
				const glm::vec2 viewport = Detail::ScriptViewportSize(scene);
				RenderCamera view;
				glm::vec2 result(0.0f);
				if (!scene.GetPrimaryCamera(viewport.x / std::max(viewport.y, 1.0f), view)
					|| !Math::WorldToScreenPoint(worldPosition, view.Projection * view.View, viewport, result))
					return std::nullopt;
				return result;
			}, "", Arg("world_position"));
		api.Function("scene_name", []() { return RequireContextScene().GetName(); }, "");
		api.Function("is_playing", []() { Scene* scene = GetContextScene(); return scene && scene->IsRunning(); }, "");
		api.Function("load_scene", [](const std::string& path) { return RequestLoadScene(path); }, "Switches to another scene at the end of the frame", Arg("path"));
		api.Function("save_screenshot", [](const std::string& path, int maxWidth) { return SaveScreenshot(path, maxWidth); },
			"Saves what the player sees as a PNG (or JPEG for .jpg) and returns its full path; \"\" names it Screenshots/<scene>-<date>-<time>.png and relative paths are inside user_folder()",
			Arg("path", std::string()), Arg("max_width", 1920));
		api.Function("log_folder", []() { return GetLogFolder().string(); },
			"user_folder()/Logs, where a game playing in its own window writes Game.log, and Game.previous.log from its last run (playing inside the editor logs to the editor's Console instead)");
		api.Function("user_folder", []() { return GetUserFolder().string(); },
			"The game's own writable folder for saves, screenshots and reports");

		// --- Saved data ---------------------------------------------------------
		api.Function("save_data", [](const std::string& name, const ScriptValue& value) { return SaveData(name, value); },
			"Saves a value (nothing, true/false, numbers, text, vectors, lists and maps) as user_folder()/Saves/<name>.json; false when it can't be written",
			Arg("name"), Arg("value"));
		api.Function("load_data", [](const std::string& name, const ScriptValue& fallback) { return LoadData(name, fallback); },
			"The value saved under the name, or default when there is none (a damaged save is kept as <name>.damaged.json)",
			Arg("name"), Arg("default", ScriptValue()));
		api.Function("has_data", [](const std::string& name) { return HasData(name); }, "True when a value is saved under the name", Arg("name"));
		api.Function("delete_data", [](const std::string& name) { return DeleteData(name); },
			"Deletes the value saved under the name; false when there was none", Arg("name"));
		api.Function("real_time", []() { return GetRealTime(); },
			"Real-world seconds since January 1, 1970 (UTC); keeps counting while the game is closed, unlike time()");
		api.Function("set_paused", [](bool paused) { RequireContextScene().SetGamePaused(paused); },
			"Pauses or resumes the game: physics, time() and scripts stop, except scripts marked Run While Paused; the UI still answers. A new scene starts unpaused",
			Arg("paused", true));
		api.Function("is_paused", []() { return RequireContextScene().IsGamePaused(); }, "True while the game is paused (set_paused)");
		api.Function("quit", [](int exitCode) { RequestQuit(exitCode); }, "Quits the game (stops play mode in the editor)", Arg("exit_code", 0));
		api.Function("started_from_editor", []() { return IsStartedFromEditor(); },
			"True while the game plays in the editor (its viewport, or a game window it started); false in an exported game");

		// --- Audio ------------------------------------------------------------
		api.Function("play_sound", [](const std::string& path, float volume, const std::string& bus)
			{
				Detail::RequireBus(bus);
				return AudioEngine::PlayOneShot(path, volume, bus);
			}, "Plays a one-shot 2D sound", Arg("path"), Arg("volume", 1.0f), Arg("bus", std::string("Effects")));
		api.Function("set_master_volume", [](float volume) { AudioEngine::SetMasterVolume(volume); }, "", Arg("volume"));
		api.Function("get_master_volume", []() { return AudioEngine::GetMasterVolume(); }, "");
		api.Function("bus_names", []() { return AudioEngine::GetBusNames(); }, "The audio buses: Master, Music, Effects");
		api.Function("set_bus_volume", [](const std::string& bus, float volume)
			{
				Detail::RequireBus(bus);
				AudioEngine::SetBusVolume(bus, volume);
			}, "Sets a bus's volume (1 = normal, 0 = silent)", Arg("bus"), Arg("volume"));
		api.Function("get_bus_volume", [](const std::string& bus)
			{
				Detail::RequireBus(bus);
				return AudioEngine::GetBusVolume(bus);
			}, "", Arg("bus"));
		api.Function("set_bus_muted", [](const std::string& bus, bool muted)
			{
				Detail::RequireBus(bus);
				AudioEngine::SetBusMuted(bus, muted);
			}, "Mutes or unmutes a bus; its volume is kept", Arg("bus"), Arg("muted", true));
		api.Function("is_bus_muted", [](const std::string& bus)
			{
				Detail::RequireBus(bus);
				return AudioEngine::IsBusMuted(bus);
			}, "", Arg("bus"));
	}

}
