#include "UITemplates.h"

#include "Bellows/Scene/Components.h"
#include "Bellows/Scene/Scene.h"
#include "Bellows/UI/UITheme.h"

#include <array>
#include <cctype>

namespace Bellows {

	namespace {

		constexpr glm::vec4 c_PanelColor { 0.09f, 0.09f, 0.16f, 0.9f };

		struct TemplateInfo
		{
			UITemplate Kind;
			const char* Name;
		};

		constexpr std::array<TemplateInfo, UITemplateCount> c_Templates = { {
			{ UITemplate::Canvas, "Canvas" },
			{ UITemplate::Panel, "Panel" },
			{ UITemplate::Text, "Text" },
			{ UITemplate::Button, "Button" },
			{ UITemplate::Toggle, "Toggle" },
			{ UITemplate::Slider, "Slider" },
			{ UITemplate::Dropdown, "Dropdown" },
			{ UITemplate::TextInput, "Text Input" },
			{ UITemplate::VerticalLayout, "Vertical Layout" },
			{ UITemplate::ScrollList, "Scroll List" },
			{ UITemplate::MainMenu, "Main Menu" },
			{ UITemplate::HUD, "HUD" },
		} };

		std::string ScriptName(std::string_view text)
		{
			std::string name;
			for (char c : text)
				name.push_back(c == ' ' || c == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			return name;
		}

		Entity CreateCanvas(Scene& scene, Entity parent, const std::string& name)
		{
			Entity canvas = scene.CreateChildEntity(parent, name);
			canvas.AddComponent<UICanvasComponent>();
			return canvas;
		}

		// A rect of 'size' reference pixels whose 'pivot' sits on one anchor point in its parent,
		// moved by 'offset' from there.
		UIRectComponent& Place(Entity entity, const glm::vec2& anchor, const glm::vec2& pivot, const glm::vec2& size,
			const glm::vec2& offset = glm::vec2(0.0f))
		{
			UIRectComponent& rect = entity.HasComponent<UIRectComponent>() ? entity.GetComponent<UIRectComponent>() : entity.AddComponent<UIRectComponent>();
			rect.AnchorMin = anchor;
			rect.AnchorMax = anchor;
			rect.Pivot = pivot;
			rect.OffsetMin = offset - pivot * size;
			rect.OffsetMax = rect.OffsetMin + size;
			return rect;
		}

		// A rect that fills its parent, inset by 'inset' reference pixels on every side.
		UIRectComponent& Fill(Entity entity, float inset = 0.0f)
		{
			UIRectComponent& rect = entity.HasComponent<UIRectComponent>() ? entity.GetComponent<UIRectComponent>() : entity.AddComponent<UIRectComponent>();
			rect.AnchorMin = glm::vec2(0.0f);
			rect.AnchorMax = glm::vec2(1.0f);
			rect.OffsetMin = glm::vec2(inset);
			rect.OffsetMax = glm::vec2(-inset);
			return rect;
		}

		Entity CreateUI(Scene& scene, Entity parent, const std::string& name, const glm::vec2& size)
		{
			Entity entity = scene.CreateChildEntity(parent, name);
			UIRectComponent& rect = entity.AddComponent<UIRectComponent>();
			SetUIRectSize(rect.Pivot, rect.OffsetMin, rect.OffsetMax, size);
			return entity;
		}

		TextComponent& AddLabel(Entity entity, const std::string& text, float size, TextAlignment alignment = TextAlignment::Center)
		{
			TextComponent& label = entity.AddComponent<TextComponent>();
			label.Text = text;
			label.Size = size;
			label.Alignment = alignment;
			label.ThemeColor = true;
			return label;
		}

		// A rounded image with the theme's look for that widget in its normal state.
		void AddThemedImage(Entity widget, UIThemeWidget kind)
		{
			const Ref<const UITheme> theme = widget.GetScene()->GetUIThemeFor(widget);
			const UIWidgetStyle& style = theme->Get(kind);
			const UIStateStyle& normal = style.Get(UIWidgetState::Normal);
			UIImageComponent& image = widget.AddComponent<UIImageComponent>();
			image.Color = normal.Background;
			image.CornerRadius = style.CornerRadius;
			image.BorderWidth = normal.BorderWidth;
			image.BorderColor = normal.Border;
		}

		UIImageComponent& AddPanelImage(Entity entity, const glm::vec4& color = c_PanelColor, float cornerRadius = 12.0f)
		{
			UIImageComponent& image = entity.AddComponent<UIImageComponent>();
			image.Color = color;
			image.CornerRadius = cornerRadius;
			return image;
		}

		// One widget inside 'parent', which is in a canvas.
		Entity CreateWidget(Scene& scene, Entity parent, UITemplate kind)
		{
			switch (kind)
			{
				case UITemplate::Panel:
				{
					Entity panel = CreateUI(scene, parent, "Panel", glm::vec2(400.0f, 300.0f));
					AddPanelImage(panel);
					return panel;
				}
				case UITemplate::Text:
				{
					Entity label = CreateUI(scene, parent, "Label", glm::vec2(300.0f, 60.0f));
					label.AddComponent<TextComponent>().Size = TextComponent::DefaultScreenSize;
					return label;
				}
				case UITemplate::Button:
				{
					Entity button = CreateUI(scene, parent, "Button", glm::vec2(260.0f, 64.0f));
					AddLabel(button, "Button", 28.0f);
					AddThemedImage(button, UIThemeWidget::Button);
					button.AddComponent<UIButtonComponent>();
					return button;
				}
				case UITemplate::Toggle:
				{
					Entity toggle = CreateUI(scene, parent, "Toggle", glm::vec2(320.0f, 48.0f));
					AddLabel(toggle, "Option", 28.0f, TextAlignment::Left);
					toggle.AddComponent<UIToggleComponent>();
					return toggle;
				}
				case UITemplate::Slider:
				{
					Entity slider = CreateUI(scene, parent, "Slider", glm::vec2(320.0f, 40.0f));
					slider.AddComponent<UISliderComponent>().Value = 0.5f;
					return slider;
				}
				case UITemplate::Dropdown:
				{
					Entity dropdown = CreateUI(scene, parent, "Dropdown", glm::vec2(320.0f, 56.0f));
					AddLabel(dropdown, "Option 1", 28.0f, TextAlignment::Left);
					AddThemedImage(dropdown, UIThemeWidget::Dropdown);
					dropdown.AddComponent<UIDropdownComponent>().Options = { "Option 1", "Option 2", "Option 3" };
					return dropdown;
				}
				case UITemplate::TextInput:
				{
					Entity field = CreateUI(scene, parent, "Text Input", glm::vec2(360.0f, 56.0f));
					AddLabel(field, "", 28.0f, TextAlignment::Left);
					AddThemedImage(field, UIThemeWidget::TextInput);
					field.AddComponent<UITextInputComponent>().Placeholder = "Type here";
					return field;
				}
				case UITemplate::VerticalLayout:
				{
					Entity group = CreateUI(scene, parent, "Layout", glm::vec2(400.0f, 300.0f));
					group.AddComponent<UILayoutComponent>();
					return group;
				}
				case UITemplate::ScrollList:
				{
					// A scrolling panel whose children stack and stretch across it.
					Entity list = CreateUI(scene, parent, "Scroll List", glm::vec2(400.0f, 300.0f));
					AddPanelImage(list);
					list.AddComponent<UILayoutComponent>().Padding = glm::vec4(12.0f);
					list.AddComponent<UIScrollComponent>();
					return list;
				}
				case UITemplate::Canvas:
				case UITemplate::MainMenu:
				case UITemplate::HUD:
					break;
			}
			return {};
		}

		// A title, three buttons stacked in the middle, and a version line, over a dark background.
		// The buttons are real Buttons, so a script only needs to answer their 'clicked'.
		Entity CreateMainMenu(Scene& scene, Entity parent)
		{
			Entity canvas = CreateCanvas(scene, parent, "Main Menu");

			Entity background = scene.CreateChildEntity(canvas, "Background");
			Fill(background);
			AddPanelImage(background, glm::vec4(0.05f, 0.06f, 0.1f, 1.0f), 0.0f);

			Entity title = scene.CreateChildEntity(canvas, "Title");
			Place(title, glm::vec2(0.5f, 0.22f), glm::vec2(0.5f), glm::vec2(1000.0f, 140.0f));
			AddLabel(title, "My Game", 88.0f).OutlineWidth = 0.04f;

			constexpr float buttonHeight = 64.0f, spacing = 16.0f;
			const char* buttons[] = { "Play", "Settings", "Quit" };
			Entity column = scene.CreateChildEntity(canvas, "Buttons");
			Place(column, glm::vec2(0.5f, 0.58f), glm::vec2(0.5f), glm::vec2(360.0f, 3.0f * buttonHeight + 2.0f * spacing));
			UILayoutComponent& layout = column.AddComponent<UILayoutComponent>();
			layout.Spacing = spacing;
			for (const char* name : buttons)
			{
				Entity button = CreateWidget(scene, column, UITemplate::Button);
				button.GetComponent<TagComponent>().Tag = name;
				button.GetComponent<TextComponent>().Text = name;
				button.GetComponent<UIRectComponent>().WidthSizing = UISizing::Fill;
			}

			Entity version = scene.CreateChildEntity(canvas, "Version");
			Place(version, glm::vec2(1.0f), glm::vec2(1.0f), glm::vec2(320.0f, 40.0f), glm::vec2(-24.0f));
			AddLabel(version, "Version 1.0", 22.0f, TextAlignment::Right);
			return canvas;
		}

		// A health bar at the top left (its Fill's anchor max x is the amount left, 0 to 1), a score at
		// the top right and a message line at the bottom. None of it takes the pointer or focus.
		Entity CreateHUD(Scene& scene, Entity parent)
		{
			Entity canvas = CreateCanvas(scene, parent, "HUD");

			Entity bar = scene.CreateChildEntity(canvas, "Health Bar");
			Place(bar, glm::vec2(0.0f), glm::vec2(0.0f), glm::vec2(360.0f, 32.0f), glm::vec2(32.0f));
			AddPanelImage(bar, glm::vec4(0.05f, 0.05f, 0.09f, 0.85f), 8.0f);
			Entity fill = scene.CreateChildEntity(bar, "Fill");
			UIRectComponent& fillRect = Fill(fill, 3.0f);
			fillRect.AnchorMax.x = 0.75f;
			AddPanelImage(fill, glm::vec4(0.86f, 0.27f, 0.32f, 1.0f), 6.0f);
			Entity label = scene.CreateChildEntity(bar, "Label");
			Fill(label);
			AddLabel(label, "Health", 20.0f);

			Entity score = scene.CreateChildEntity(canvas, "Score");
			Place(score, glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(360.0f, 48.0f), glm::vec2(-32.0f, 24.0f));
			AddLabel(score, "Score: 0", 34.0f, TextAlignment::Right).OutlineWidth = 0.05f;

			Entity message = scene.CreateChildEntity(canvas, "Message");
			Place(message, glm::vec2(0.5f, 1.0f), glm::vec2(0.5f, 1.0f), glm::vec2(1000.0f, 60.0f), glm::vec2(0.0f, -56.0f));
			AddLabel(message, "Press E to interact", 30.0f).OutlineWidth = 0.05f;

			for (Entity entity : { bar, fill, label, score, message })
				entity.GetComponent<UIRectComponent>().Interactable = false;
			return canvas;
		}

	}

	const char* UITemplateToString(UITemplate kind)
	{
		for (const TemplateInfo& info : c_Templates)
		{
			if (info.Kind == kind)
				return info.Name;
		}
		return "UI";
	}

	std::optional<UITemplate> UITemplateFromName(std::string_view name)
	{
		const std::string wanted = ScriptName(name);
		for (const TemplateInfo& info : c_Templates)
		{
			if (ScriptName(info.Name) == wanted)
				return info.Kind;
		}
		return std::nullopt;
	}

	std::string UITemplateScriptName(UITemplate kind)
	{
		return ScriptName(UITemplateToString(kind));
	}

	bool IsUIScreenTemplate(UITemplate kind)
	{
		return kind == UITemplate::MainMenu || kind == UITemplate::HUD;
	}

	std::optional<UIAnchorPreset> UIAnchorPresetFromName(std::string_view name)
	{
		const std::string wanted = ScriptName(name);
		for (int i = 0; i < UIAnchorPresetCount; i++)
		{
			const auto preset = static_cast<UIAnchorPreset>(i);
			if (ScriptName(UIAnchorPresetToString(preset)) == wanted)
				return preset;
		}
		return std::nullopt;
	}

	std::string UIAnchorPresetScriptName(UIAnchorPreset preset)
	{
		return ScriptName(UIAnchorPresetToString(preset));
	}

	bool ApplyUIAnchorPreset(Entity entity, UIAnchorPreset preset, bool moveRect)
	{
		if (!entity || !entity.HasComponent<UIRectComponent>())
			return false;
		UIRectComponent& rect = entity.GetComponent<UIRectComponent>();
		glm::vec2 anchorMin, anchorMax;
		GetUIAnchorPreset(preset, anchorMin, anchorMax);
		if (const std::optional<Scene::UIRectPlacement> placement = entity.GetScene()->GetUIRectPlacement(entity))
		{
			const UIRectangle target = moveRect
				? GetUIAnchorPresetRect(preset, placement->Rect, placement->ParentArea, rect.Pivot, rect.Pivot)
				: placement->Rect;
			ComputeUIOffsetsFor(target, anchorMin, anchorMax, placement->ParentArea, placement->Scale, rect.OffsetMin, rect.OffsetMax);
		}
		rect.AnchorMin = anchorMin;
		rect.AnchorMax = anchorMax;
		return true;
	}

	Entity CreateUITemplate(Scene& scene, Entity parent, UITemplate kind)
	{
		switch (kind)
		{
			case UITemplate::Canvas:	return CreateCanvas(scene, parent, "Canvas");
			case UITemplate::MainMenu:	return CreateMainMenu(scene, parent);
			case UITemplate::HUD:		return CreateHUD(scene, parent);
			default:					break;
		}
		// Widgets live under a canvas: inside the one 'parent' is in, or in a new one.
		Entity uiParent = parent && scene.IsInsideUICanvas(parent) ? parent : CreateCanvas(scene, parent, "Canvas");
		return CreateWidget(scene, uiParent, kind);
	}

}
