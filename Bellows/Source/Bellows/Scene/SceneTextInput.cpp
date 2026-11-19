#include "bwpch.h"
#include "Bellows/Scene/Scene.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Renderer/SceneRenderer.h"
#include "Bellows/Scene/Entity.h"
#include "Bellows/Text/Font.h"
#include "Bellows/Text/TextLayout.h"
#include "Bellows/UI/UIStyle.h"
#include "Bellows/UI/UITextEdit.h"
#include "Bellows/UI/UIWidgets.h"

namespace Bellows {

	namespace {

		// The caret blinks on and off this often, and shows steadily for this long after it moves.
		constexpr float c_CaretBlinkSeconds = 0.53f;

		// A text field as drawn this frame: its text (bullets for a password), laid out on one line, or
		// one line per '\n' when multi-line, at the top-left of its text area less its scroll.
		struct TextInputView
		{
			Ref<Font> TextFont;
			float Size = 0.0f;
			UIRectangle Area;
			std::string Display;
			TextLayout Layout;
			glm::vec2 Origin { 0.0f };					// Where the layout's (0, 0) lands on screen
			std::vector<size_t> LineFirstCharacter;		// Each display line's first character index
			// Each display line's byte range in Display (offsets, which stay right when the view is moved).
			std::vector<std::pair<size_t, size_t>> LineRanges;

			std::string_view Line(size_t index) const
			{
				return std::string_view(Display).substr(LineRanges[index].first, LineRanges[index].second - LineRanges[index].first);
			}
		};

		bool IsMultiLine(const UITextInputComponent& input) { return input.MultiLine && !input.Password; }

		std::optional<TextInputView> MakeTextInputView(Entity entity, const UIRectangle& rect, float scale)
		{
			const auto* input = entity.TryGetComponent<UITextInputComponent>();
			if (!input)
				return std::nullopt;
			const auto* text = entity.TryGetComponent<TextComponent>();
			TextInputView view;
			view.TextFont = AssetManager::GetFont(text ? text->FontPath : std::string());
			if (!view.TextFont)
				return std::nullopt;
			view.Size = (text ? text->Size : TextComponent::DefaultScreenSize) * scale;
			view.Area = ComputeUITextInputArea(rect, scale);
			const std::string& value = text ? text->Text : std::string();
			view.Display = input->Password ? MaskUIText(value) : value;
			if (!IsMultiLine(*input))
				std::replace(view.Display.begin(), view.Display.end(), '\n', ' ');

			TextLayoutSettings settings;
			settings.Size = view.Size;
			settings.Wrap = false;
			view.Layout = LayoutText(*view.TextFont, view.Display, settings);
			// One line sits in the middle of the field; several start at its top.
			const float lineHeight = view.TextFont->GetLineHeight() * view.Size;
			const float top = IsMultiLine(*input) ? 0.0f : (view.Area.GetSize().y - lineHeight) * 0.5f;
			view.Origin = view.Area.Min + glm::vec2(0.0f, top) - input->RuntimeScroll;

			size_t start = 0, character = 0;
			while (true)
			{
				const size_t end = std::min(view.Display.find('\n', start), view.Display.size());
				view.LineRanges.emplace_back(start, end);
				view.LineFirstCharacter.push_back(character);
				character += CountUICharacters(view.Line(view.LineRanges.size() - 1)) + 1;
				if (end >= view.Display.size())
					break;
				start = end + 1;
			}
			return view;
		}

		// The line a character index is on, and its stops.
		size_t LineOfCharacter(const TextInputView& view, size_t character)
		{
			size_t line = 0;
			while (line + 1 < view.LineFirstCharacter.size() && view.LineFirstCharacter[line + 1] <= character)
				line++;
			return line;
		}

		// The caret's place for a character index: x and the top of its line, on screen.
		glm::vec2 CaretPosition(const TextInputView& view, size_t character)
		{
			const size_t line = LineOfCharacter(view, character);
			const std::vector<float> stops = ComputeUICaretStops(*view.TextFont, view.Line(line), view.Size);
			const size_t index = std::min(character - view.LineFirstCharacter[line], stops.size() - 1);
			const float lineHeight = view.TextFont->GetLineHeight() * view.Size;
			return view.Origin + glm::vec2(stops[index], lineHeight * static_cast<float>(line));
		}

		size_t CharacterAt(const TextInputView& view, const glm::vec2& point)
		{
			const float lineHeight = view.TextFont->GetLineHeight() * view.Size;
			const float row = std::floor((point.y - view.Origin.y) / std::max(lineHeight, 1.0f));
			const size_t line = static_cast<size_t>(std::clamp(row, 0.0f, static_cast<float>(view.LineRanges.size() - 1)));
			const std::vector<float> stops = ComputeUICaretStops(*view.TextFont, view.Line(line), view.Size);
			return view.LineFirstCharacter[line] + NearestUICaretStop(stops, point.x - view.Origin.x);
		}

		// Keeps the selection on character boundaries of the current text, which scripts may have changed.
		void ClampSelection(UITextSelection& selection, const std::string& text)
		{
			selection.Caret = ClampUITextOffset(text, selection.Caret);
			selection.Anchor = ClampUITextOffset(text, selection.Anchor);
		}

	}

	void Scene::PlaceUITextCaret(const UIElement& element, const glm::vec2& point, bool extend)
	{
		Entity entity(element.Handle, this);
		const std::optional<TextInputView> view = MakeTextInputView(entity, element.Rect, element.Scale);
		auto& input = entity.GetComponent<UITextInputComponent>();
		const auto* text = entity.TryGetComponent<TextComponent>();
		if (!view || !text)
			return;
		const size_t offset = UICharacterOffset(text->Text, CharacterAt(*view, point));
		ClampSelection(input.RuntimeSelection, text->Text);
		if (extend)
			input.RuntimeSelection.Caret = offset;
		else
			input.RuntimeSelection.CollapseTo(offset);
		m_UICaretMoved = std::chrono::steady_clock::now();
		KeepUITextCaretVisible(element);
	}

	void Scene::KeepUITextCaretVisible(const UIElement& element)
	{
		Entity entity(element.Handle, this);
		const std::optional<TextInputView> view = MakeTextInputView(entity, element.Rect, element.Scale);
		const auto* text = entity.TryGetComponent<TextComponent>();
		if (!view || !text)
			return;
		auto& input = entity.GetComponent<UITextInputComponent>();
		const glm::vec2 caret = CaretPosition(*view, UICharacterIndex(text->Text, input.RuntimeSelection.Caret));
		const float lineHeight = view->TextFont->GetLineHeight() * view->Size;
		// Where the caret is in the text, before scrolling.
		const glm::vec2 inText = caret - view->Origin;
		const glm::vec2 area = view->Area.GetSize();
		const float caretWidth = 2.0f * element.Scale;
		if (inText.x + caretWidth - input.RuntimeScroll.x > area.x)
			input.RuntimeScroll.x = inText.x + caretWidth - area.x;
		if (inText.x < input.RuntimeScroll.x)
			input.RuntimeScroll.x = inText.x;
		// No space left of the text when it all fits.
		input.RuntimeScroll.x = std::clamp(input.RuntimeScroll.x, 0.0f, std::max(view->Layout.Size.x + caretWidth - area.x, 0.0f));
		if (IsMultiLine(input))
		{
			if (inText.y + lineHeight - input.RuntimeScroll.y > area.y)
				input.RuntimeScroll.y = inText.y + lineHeight - area.y;
			if (inText.y < input.RuntimeScroll.y)
				input.RuntimeScroll.y = inText.y;
			input.RuntimeScroll.y = std::max(input.RuntimeScroll.y, 0.0f);
		}
		else
		{
			input.RuntimeScroll.y = 0.0f;
		}
	}

	void Scene::UpdateUITextInput(const UIElement& element, std::vector<UIEvent>& events)
	{
		Entity entity(element.Handle, this);
		auto& input = entity.GetComponent<UITextInputComponent>();
		std::string& value = (entity.HasComponent<TextComponent>() ? entity.GetComponent<TextComponent>() : entity.AddComponent<TextComponent>()).Text;
		UITextSelection& selection = input.RuntimeSelection;
		ClampSelection(selection, value);
		const UITextSelection before = selection;
		const UUID id = entity.GetUUID();
		const bool multiLine = IsMultiLine(input);

		const auto typed = [](KeyCode key) { return Input::IsKeyTyped(key, InputAccess::Raw); };
		const auto down = [](KeyCode key) { return Input::IsKeyDown(key, InputAccess::Raw); };
		const bool ctrl = down(KeyCode::LeftControl) || down(KeyCode::RightControl);
		const bool shift = down(KeyCode::LeftShift) || down(KeyCode::RightShift);
		bool changed = false;

		// Moves the caret, extending the selection with Shift; without Shift a selection collapses to
		// the side the arrow points at instead of moving.
		const auto moveCaret = [&](size_t offset, bool collapseToStart, bool collapseToEnd)
		{
			if (!shift && !selection.IsEmpty() && (collapseToStart || collapseToEnd))
				offset = collapseToStart ? selection.Begin() : selection.End();
			if (shift)
				selection.Caret = offset;
			else
				selection.CollapseTo(offset);
		};

		// Shortcuts like Ctrl+C type no text, while AltGr (Ctrl+Alt on Windows) types characters like '@'.
		if (const std::string& text = Input::GetTypedText(InputAccess::Raw); !text.empty())
			changed |= InsertUIText(value, selection, text, multiLine, input.MaxLength);

		if (typed(KeyCode::Backspace))
			changed |= EraseUIText(value, selection, false, ctrl);
		if (typed(KeyCode::Delete))
			changed |= EraseUIText(value, selection, true, ctrl);
		if (typed(KeyCode::Left))
			moveCaret(ctrl ? PreviousUIWord(value, selection.Caret) : PreviousUICharacter(value, selection.Caret), !ctrl, false);
		if (typed(KeyCode::Right))
			moveCaret(ctrl ? NextUIWord(value, selection.Caret) : NextUICharacter(value, selection.Caret), false, !ctrl);
		if (typed(KeyCode::Home))
			moveCaret(ctrl || !multiLine ? 0 : UILineStart(value, selection.Caret), false, false);
		if (typed(KeyCode::End))
			moveCaret(ctrl || !multiLine ? value.size() : UILineEnd(value, selection.Caret), false, false);
		if (multiLine && (typed(KeyCode::Up) || typed(KeyCode::Down)))
		{
			// Up and down keep the caret's x, on the line above or below.
			if (const std::optional<TextInputView> view = MakeTextInputView(entity, element.Rect, element.Scale))
			{
				const float lineHeight = view->TextFont->GetLineHeight() * view->Size;
				const glm::vec2 caret = CaretPosition(*view, UICharacterIndex(value, selection.Caret));
				const float target = caret.y + (typed(KeyCode::Up) ? -0.5f : 1.5f) * lineHeight;
				const bool past = typed(KeyCode::Up) ? caret.y <= view->Origin.y : LineOfCharacter(*view, UICharacterIndex(value, selection.Caret)) + 1 >= view->LineRanges.size();
				const size_t offset = past ? (typed(KeyCode::Up) ? 0 : value.size()) : UICharacterOffset(value, CharacterAt(*view, glm::vec2(caret.x, target)));
				moveCaret(offset, false, false);
			}
		}

		if (ctrl && typed(KeyCode::A))
		{
			selection.Anchor = 0;
			selection.Caret = value.size();
		}
		// A password is never copied out of its field.
		if (ctrl && (typed(KeyCode::C) || typed(KeyCode::X)) && !selection.IsEmpty() && !input.Password)
		{
			Input::SetClipboardText(value.substr(selection.Begin(), selection.End() - selection.Begin()));
			if (typed(KeyCode::X))
				changed |= EraseUIText(value, selection, false, false);
		}
		if (ctrl && typed(KeyCode::V))
			changed |= InsertUIText(value, selection, Input::GetClipboardText(), multiLine, input.MaxLength);

		const bool enter = typed(KeyCode::Enter) || typed(KeyCode::KPEnter);
		bool submitted = false;
		if (enter && multiLine && !ctrl)
			changed |= InsertUIText(value, selection, "\n", true, input.MaxLength);
		else if (enter)
			submitted = true;

		if (changed)
			events.push_back({ "changed", id, value });
		if (submitted)
			events.push_back({ "submitted", id, value });
		if (changed || selection.Caret != before.Caret || selection.Anchor != before.Anchor)
		{
			m_UICaretMoved = std::chrono::steady_clock::now();
			KeepUITextCaretVisible(element);
		}
	}

	void Scene::SubmitUITextInput(SceneRenderer& renderer, const UIElement& element, const glm::vec4& clip)
	{
		Entity entity(element.Handle, this);
		const std::optional<TextInputView> view = MakeTextInputView(entity, element.Rect, element.Scale);
		if (!view)
			return;
		auto& input = entity.GetComponent<UITextInputComponent>();
		const auto* text = entity.TryGetComponent<TextComponent>();
		const std::string& value = text ? text->Text : std::string();
		const glm::vec4 color = text ? text->Color : UIColors::OffWhite;
		const uint32_t entityID = entity.GetRenderID();
		const UIRectangle areaClip = element.Clip.Intersect(view->Area);
		const glm::vec4 textClip(areaClip.Min, areaClip.Max);
		const float lineHeight = view->TextFont->GetLineHeight() * view->Size;
		const bool focused = m_UIFocused == entity.GetUUID() && element.Interactable;
		ClampSelection(input.RuntimeSelection, value);

		// The selection, one band per line it covers, under the text.
		if (focused && !input.RuntimeSelection.IsEmpty())
		{
			const size_t first = UICharacterIndex(value, input.RuntimeSelection.Begin());
			const size_t last = UICharacterIndex(value, input.RuntimeSelection.End());
			UIRectStyle band;
			band.Color = glm::vec4(glm::vec3(UIColors::Accent), 0.4f);
			for (size_t line = LineOfCharacter(*view, first); line <= LineOfCharacter(*view, last); line++)
			{
				const size_t lineStart = view->LineFirstCharacter[line];
				const size_t lineEnd = lineStart + CountUICharacters(view->Line(line));
				const glm::vec2 from = CaretPosition(*view, std::max(first, lineStart));
				const glm::vec2 to = CaretPosition(*view, std::min(last, lineEnd));
				// A line break inside the selection shows as a little extra width.
				const float breakWidth = last > lineEnd ? view->Size * 0.3f : 0.0f;
				renderer.SubmitUIRect(from, glm::vec2(to.x + breakWidth, from.y + lineHeight), band, nullptr, entityID, &textClip);
			}
		}

		if (!view->Display.empty())
		{
			TextStyle style;
			style.Color = color;
			renderer.SubmitUIText(view->TextFont, view->Layout, view->Origin, style, entityID, &textClip);
		}
		else if (!input.Placeholder.empty())
		{
			TextLayoutSettings settings;
			settings.Size = view->Size;
			settings.Wrap = false;
			TextStyle style;
			style.Color = glm::vec4(glm::vec3(color), color.a * 0.45f);
			renderer.SubmitUIText(view->TextFont, LayoutText(*view->TextFont, input.Placeholder, settings), view->Origin, style, entityID, &textClip);
		}

		// The caret: steady just after it moves, then blinking.
		if (focused)
		{
			const float since = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_UICaretMoved).count();
			if (std::fmod(since, 2.0f * c_CaretBlinkSeconds) < c_CaretBlinkSeconds)
			{
				const glm::vec2 caret = CaretPosition(*view, UICharacterIndex(value, input.RuntimeSelection.Caret));
				UIRectStyle bar;
				bar.Color = color;
				const float width = std::max(2.0f * element.Scale, 1.0f);
				renderer.SubmitUIRect(caret, caret + glm::vec2(width, lineHeight), bar, nullptr, entityID, &clip);
			}
		}
	}

}
