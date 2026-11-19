#include "bwpch.h"
#include "Bellows/UI/UITheme.h"

#include "Bellows/Serialization/JsonUtils.h"
#include "Bellows/UI/UIStyle.h"

#include <fstream>
#include <sstream>

namespace Bellows {

	namespace {

		constexpr const char* c_StateNames[UIWidgetStateCount] = { "Normal", "Hover", "Pressed", "Focused", "Disabled" };
		constexpr const char* c_WidgetNames[UIThemeWidgetCount] = { "Panel", "Button", "Toggle", "Slider", "Dropdown", "DropdownList", "TextInput" };

		void SetAllStates(UIWidgetStyle& style, const UIStateStyle& state)
		{
			for (UIStateStyle& each : style.States)
				each = state;
		}

		UITheme MakeDefaultTheme()
		{
			using namespace UIColors;
			const glm::vec4 disabled(0.165f, 0.180f, 0.325f, 0.45f);
			const glm::vec4 dimText(glm::vec3(OffWhite), 0.5f);
			// Edges that must be seen on navy (fields, check boxes, dropdowns) are lighter than slate: this one
			// is 4.1:1 on navy and 3:1 on navy-light (WCAG 2.2 asks 3:1 for the edges of controls).
			const glm::vec4 edge(0.439f, 0.463f, 0.682f, 1.0f);	// #7076AE
			UITheme theme;
			theme.TextColor = OffWhite;
			theme.Accent = Accent;
			theme.AccentLight = AccentLight;

			UIWidgetStyle& panel = theme.Get(UIThemeWidget::Panel);
			panel.CornerRadius = 12.0f;
			SetAllStates(panel, { Navy, Slate, 0.0f, OffWhite });

			// Buttons and dropdowns show their state through their background.
			for (UIThemeWidget widget : { UIThemeWidget::Button, UIThemeWidget::Dropdown })
			{
				UIWidgetStyle& style = theme.Get(widget);
				style.CornerRadius = widget == UIThemeWidget::Button ? 12.0f : 10.0f;
				SetAllStates(style, { NavyLight, Slate, 0.0f, OffWhite });
				style.Get(UIWidgetState::Hover).Background = Slate;
				style.Get(UIWidgetState::Pressed).Background = Accent;
				style.Get(UIWidgetState::Disabled).Background = disabled;
				style.Get(UIWidgetState::Disabled).Text = dimText;
			}
			UIWidgetStyle& dropdown = theme.Get(UIThemeWidget::Dropdown);
			for (UIStateStyle& state : dropdown.States)
			{
				state.Border = edge;
				state.BorderWidth = 2.0f;
			}

			UIWidgetStyle& toggle = theme.Get(UIThemeWidget::Toggle);
			toggle.CornerRadius = 5.0f;
			SetAllStates(toggle, { Navy, OffWhite, 2.0f, OffWhite });
			toggle.Get(UIWidgetState::Disabled).Border = Slate;
			toggle.Get(UIWidgetState::Disabled).Text = dimText;

			UIWidgetStyle& slider = theme.Get(UIThemeWidget::Slider);
			SetAllStates(slider, { Slate, Accent, 2.0f, OffWhite });
			slider.Get(UIWidgetState::Pressed).Text = AccentLight;
			slider.Get(UIWidgetState::Disabled).Text = Slate;

			UIWidgetStyle& list = theme.Get(UIThemeWidget::DropdownList);
			list.CornerRadius = 8.0f;
			SetAllStates(list, { Navy, Slate, 1.0f, OffWhite });
			list.Get(UIWidgetState::Hover).Background = Slate;

			UIWidgetStyle& input = theme.Get(UIThemeWidget::TextInput);
			input.CornerRadius = 10.0f;
			SetAllStates(input, { Navy, edge, 2.0f, OffWhite });
			input.Get(UIWidgetState::Focused).Border = Accent;
			input.Get(UIWidgetState::Disabled).Border = Slate;
			input.Get(UIWidgetState::Disabled).Text = dimText;

			// "primary": the accent as background, with navy text (off-white on orange is only 2.2:1).
			UIWidgetStyle primary = theme.Get(UIThemeWidget::Button);
			SetAllStates(primary, { Accent, Accent, 0.0f, Navy });
			primary.Get(UIWidgetState::Hover).Background = AccentLight;
			primary.Get(UIWidgetState::Pressed).Background = glm::vec4(0.851f, 0.439f, 0.165f, 1.0f);	// #D9702A
			primary.Get(UIWidgetState::Disabled) = theme.Get(UIThemeWidget::Button).Get(UIWidgetState::Disabled);
			theme.Classes["primary"][UIThemeWidget::Button] = primary;
			// "danger": a red that off-white text reads on (6:1 and more).
			UIWidgetStyle danger = theme.Get(UIThemeWidget::Button);
			SetAllStates(danger, { glm::vec4(0.659f, 0.196f, 0.196f, 1.0f), Slate, 0.0f, OffWhite });	// #A83232
			danger.Get(UIWidgetState::Hover).Background = glm::vec4(0.749f, 0.239f, 0.239f, 1.0f);		// #BF3D3D
			danger.Get(UIWidgetState::Pressed).Background = glm::vec4(0.549f, 0.157f, 0.157f, 1.0f);	// #8C2828
			danger.Get(UIWidgetState::Disabled) = theme.Get(UIThemeWidget::Button).Get(UIWidgetState::Disabled);
			theme.Classes["danger"][UIThemeWidget::Button] = danger;
			return theme;
		}

		bool ReadColor(const Json& value, glm::vec4& out)
		{
			if (value.is_string())
			{
				if (const std::optional<glm::vec4> color = ParseUIColor(value.get<std::string>()))
				{
					out = *color;
					return true;
				}
				return false;
			}
			if (value.is_array() && (value.size() == 3 || value.size() == 4))
			{
				glm::vec4 color(1.0f);
				for (size_t i = 0; i < value.size(); i++)
				{
					if (!value[i].is_number())
						return false;
					color[static_cast<glm::length_t>(i)] = std::clamp(value[i].get<float>(), 0.0f, 1.0f);
				}
				out = color;
				return true;
			}
			return false;
		}

		// Reads one value of 'object' when it is there; a value of the wrong kind is reported and skipped.
		struct Reader
		{
			std::string Warnings;

			void Warn(const std::string& where, const char* expected)
			{
				Warnings += where + " should be " + expected + "; the default is used.\n";
			}

			void Color(const Json& object, const char* key, glm::vec4& out, const std::string& where)
			{
				if (auto it = object.find(key); it != object.end() && !ReadColor(*it, out))
					Warn(where + "." + key, "a color like \"#F5873A\"");
			}

			void Number(const Json& object, const char* key, float& out, const std::string& where, float min)
			{
				if (auto it = object.find(key); it != object.end())
				{
					if (it->is_number() && it->get<float>() >= min)
						out = it->get<float>();
					else
						Warn(where + "." + key, min > 0.0f ? "a positive number" : "a number, 0 or more");
				}
			}

			// A widget style: CornerRadius, then the states. "Normal" changes every state first, so a
			// file can restyle a widget in one place and pick out the states that differ.
			void Widget(const Json& object, UIWidgetStyle& style, const std::string& where)
			{
				if (!object.is_object())
				{
					Warn(where, "an object");
					return;
				}
				Number(object, "CornerRadius", style.CornerRadius, where, 0.0f);
				const auto readState = [&](const Json& state, UIStateStyle& out, const std::string& at)
				{
					if (!state.is_object())
					{
						Warn(at, "an object");
						return;
					}
					Color(state, "Background", out.Background, at);
					Color(state, "Border", out.Border, at);
					Number(state, "BorderWidth", out.BorderWidth, at, 0.0f);
					Color(state, "Text", out.Text, at);
				};
				if (auto it = object.find("Normal"); it != object.end())
				{
					for (UIStateStyle& state : style.States)
						readState(*it, state, where + ".Normal");
				}
				for (size_t i = 1; i < UIWidgetStateCount; i++)
				{
					if (auto it = object.find(c_StateNames[i]); it != object.end())
						readState(*it, style.States[i], where + "." + c_StateNames[i]);
				}
			}

			void Widgets(const Json& object, UIWidgetStyle (&widgets)[UIThemeWidgetCount], const std::string& where,
				std::map<UIThemeWidget, UIWidgetStyle>* changed)
			{
				for (size_t i = 0; i < UIThemeWidgetCount; i++)
				{
					auto it = object.find(c_WidgetNames[i]);
					if (it == object.end())
						continue;
					UIWidgetStyle style = widgets[i];
					Widget(*it, style, where + c_WidgetNames[i]);
					if (changed)
						(*changed)[static_cast<UIThemeWidget>(i)] = style;
					else
						widgets[i] = style;
				}
			}
		};

		Json WriteState(const UIStateStyle& state)
		{
			return Json { { "Background", UIColorToString(state.Background) }, { "Border", UIColorToString(state.Border) },
				{ "BorderWidth", state.BorderWidth }, { "Text", UIColorToString(state.Text) } };
		}

		Json WriteWidget(const UIWidgetStyle& style)
		{
			Json json { { "CornerRadius", style.CornerRadius } };
			for (size_t i = 0; i < UIWidgetStateCount; i++)
				json[c_StateNames[i]] = WriteState(style.States[i]);
			return json;
		}

	}

	const UIWidgetStyle& UITheme::Get(UIThemeWidget widget, const std::string& styleClass) const
	{
		if (!styleClass.empty())
		{
			if (auto found = Classes.find(styleClass); found != Classes.end())
			{
				if (auto style = found->second.find(widget); style != found->second.end())
					return style->second;
			}
		}
		return Get(widget);
	}

	const UITheme& GetDefaultUITheme()
	{
		static const UITheme c_Theme = MakeDefaultTheme();
		return c_Theme;
	}

	const char* UIWidgetStateToString(UIWidgetState state) { return c_StateNames[static_cast<size_t>(state)]; }
	const char* UIThemeWidgetToString(UIThemeWidget widget) { return c_WidgetNames[static_cast<size_t>(widget)]; }

	std::optional<glm::vec4> ParseUIColor(const std::string& text)
	{
		if (text.size() != 7 && text.size() != 9)
			return std::nullopt;
		if (text[0] != '#' || !std::all_of(text.begin() + 1, text.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
			return std::nullopt;
		glm::vec4 color(1.0f);
		for (size_t i = 0; i * 2 + 1 < text.size(); i++)
			color[static_cast<glm::length_t>(i)] = static_cast<float>(std::stoi(text.substr(1 + i * 2, 2), nullptr, 16)) / 255.0f;
		return color;
	}

	std::string UIColorToString(const glm::vec4& color)
	{
		const auto byte = [](float value) { return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
		char text[10];
		if (byte(color.a) == 255)
			std::snprintf(text, sizeof(text), "#%02X%02X%02X", byte(color.r), byte(color.g), byte(color.b));
		else
			std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X", byte(color.r), byte(color.g), byte(color.b), byte(color.a));
		return text;
	}

	std::optional<UITheme> ParseUITheme(const std::string& text, std::string* outError, std::string* outWarnings)
	{
		Json json = Json::parse(text, nullptr, false);
		if (json.is_discarded() || !json.is_object())
		{
			if (outError)
				*outError = "A theme is a JSON object, like { \"Accent\": \"#F5873A\" }";
			return std::nullopt;
		}

		UITheme theme = GetDefaultUITheme();
		Reader read;
		if (auto it = json.find("Font"); it != json.end())
		{
			if (it->is_string())
				theme.Font = it->get<std::string>();
			else
				read.Warn("Font", "a font's asset path (\"\" for the built-in font)");
		}
		read.Number(json, "TextSize", theme.TextSize, "", 1.0f);
		read.Color(json, "TextColor", theme.TextColor, "");
		read.Color(json, "Accent", theme.Accent, "");
		read.Color(json, "AccentLight", theme.AccentLight, "");
		read.Number(json, "FocusRingWidth", theme.FocusRingWidth, "", 0.0f);
		read.Number(json, "FocusRingGap", theme.FocusRingGap, "", 0.0f);
		read.Widgets(json, theme.Widgets, "", nullptr);

		if (auto it = json.find("Classes"); it != json.end())
		{
			if (!it->is_object())
				read.Warn("Classes", "an object of named styles");
			else
			{
				for (const auto& [name, styles] : it->items())
				{
					if (styles.is_object())
						read.Widgets(styles, theme.Widgets, "Classes." + name + ".", &theme.Classes[name]);
					else
						read.Warn("Classes." + name, "an object");
				}
			}
		}

		// Keys' messages read "TextSize should be ..."; drop the leading '.' of top-level keys.
		std::string warnings;
		std::istringstream lines(read.Warnings);
		for (std::string line; std::getline(lines, line); )
			warnings += (line.rfind('.', 0) == 0 ? line.substr(1) : line) + "\n";
		if (outWarnings)
			*outWarnings = warnings;
		return theme;
	}

	std::optional<UITheme> LoadUITheme(const std::filesystem::path& path, std::string* outError)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			if (outError)
				*outError = "The theme file '" + path.string() + "' could not be opened";
			return std::nullopt;
		}
		std::stringstream text;
		text << file.rdbuf();
		std::string warnings;
		std::optional<UITheme> theme = ParseUITheme(text.str(), outError, &warnings);
		if (theme && !warnings.empty())
			BW_CORE_WARN("Theme '{}':\n{}", path.filename().string(), warnings);
		return theme;
	}

	std::string WriteUITheme(const UITheme& theme)
	{
		Json json;
		json["Font"] = theme.Font;
		json["TextSize"] = theme.TextSize;
		json["TextColor"] = UIColorToString(theme.TextColor);
		json["Accent"] = UIColorToString(theme.Accent);
		json["AccentLight"] = UIColorToString(theme.AccentLight);
		json["FocusRingWidth"] = theme.FocusRingWidth;
		json["FocusRingGap"] = theme.FocusRingGap;
		for (size_t i = 0; i < UIThemeWidgetCount; i++)
			json[c_WidgetNames[i]] = WriteWidget(theme.Widgets[i]);
		Json classes = Json::object();
		for (const auto& [name, styles] : theme.Classes)
		{
			Json entry = Json::object();
			for (const auto& [widget, style] : styles)
				entry[c_WidgetNames[static_cast<size_t>(widget)]] = WriteWidget(style);
			classes[name] = std::move(entry);
		}
		json["Classes"] = std::move(classes);
		return json.dump(1, '\t') + "\n";
	}

}
