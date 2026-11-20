#include <doctest/doctest.h>

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/Input.h"
#include "Bellows/Text/Font.h"
#include "Bellows/Text/TextLayout.h"
#include "Bellows/UI/UITextEdit.h"

#include <string>
#include <vector>

using namespace Bellows;

TEST_CASE("Text editing moves by characters, words and lines in UTF-8")
{
	// "héllo wörld, ok\nnext" with two-byte é and ö.
	const std::string text = "h\xC3\xA9llo w\xC3\xB6rld, ok\nnext";
	CHECK(NextUICharacter(text, 0) == 1);
	CHECK(NextUICharacter(text, 1) == 3);		// Over é
	CHECK(PreviousUICharacter(text, 3) == 1);
	CHECK(ClampUITextOffset(text, 2) == 1);		// Inside é
	CHECK(ClampUITextOffset(text, 999) == text.size());
	CHECK(CountUICharacters(text) == 20);
	CHECK(UICharacterOffset(text, 2) == 3);
	CHECK(UICharacterIndex(text, 3) == 2);

	CHECK(NextUIWord(text, 0) == 6);				// The end of "héllo"
	CHECK(NextUIWord(text, 6) == 13);				// Past the space, to the end of "wörld"
	CHECK(NextUIWord(text, 13) == 14);				// "," is a word of its own
	CHECK(PreviousUIWord(text, 14) == 13);
	CHECK(PreviousUIWord(text, 13) == 7);
	CHECK(PreviousUIWord(text, 7) == 0);

	CHECK(UILineStart(text, 20) == 18);
	CHECK(UILineEnd(text, 2) == 17);
	CHECK(UILineEnd(text, 18) == text.size());
	CHECK(UILineStart(text, 17) == 0);			// The break belongs to the first line
}

TEST_CASE("Inserting and erasing text replace the selection and keep its limits")
{
	std::string text = "hello";
	UITextSelection selection;
	selection.CollapseTo(5);
	CHECK(InsertUIText(text, selection, " world", false, 0));
	CHECK(text == "hello world");
	CHECK(selection.Caret == 11);
	CHECK(selection.IsEmpty());

	// Typing over a selection replaces it; line breaks and control characters are kept out of one line.
	selection.Anchor = 0;
	selection.Caret = 5;
	CHECK(InsertUIText(text, selection, "bye\n\tthere\r", false, 0));
	CHECK(text == "byethere world");
	CHECK(selection.Caret == 8);

	// Multi-line keeps the break; maxLength cuts what is inserted, counting characters.
	std::string lines;
	UITextSelection caret;
	CHECK(InsertUIText(lines, caret, "a\nb", true, 0));
	CHECK(lines == "a\nb");
	std::string limited = "ab";
	caret.CollapseTo(2);
	CHECK(InsertUIText(limited, caret, "\xC3\xA9\xC3\xA9\xC3\xA9", false, 4));
	CHECK(limited == "ab\xC3\xA9\xC3\xA9");
	CHECK_FALSE(InsertUIText(limited, caret, "x", false, 4));

	// Backspace and Delete, by character and by word.
	std::string words = "one two three";
	UITextSelection at;
	at.CollapseTo(7);
	CHECK(EraseUIText(words, at, false, false));
	CHECK(words == "one tw three");
	CHECK(EraseUIText(words, at, false, true));
	CHECK(words == "one  three");
	CHECK(at.Caret == 4);
	CHECK(EraseUIText(words, at, true, true));
	CHECK(words == "one ");
	at.CollapseTo(0);
	CHECK_FALSE(EraseUIText(words, at, false, false));

	CHECK(MaskUIText("p\xC3\xA4ss") == "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
}

TEST_CASE("Caret stops follow the font's advances as LayoutText places them")
{
	const Ref<Font> font = AssetManager::GetFont("");
	REQUIRE(font);
	TextLayoutSettings settings;
	settings.Size = 32.0f;
	settings.Wrap = false;
	const std::string line = "AVA wave";
	const std::vector<float> stops = ComputeUICaretStops(*font, line, 32.0f);
	REQUIRE(stops.size() == CountUICharacters(line) + 1);
	CHECK(stops.front() == 0.0f);
	for (size_t i = 1; i < stops.size(); i++)
		CHECK(stops[i] > stops[i - 1]);
	// The end stop is where the line's layout ends (it has no trailing space, which a line's width leaves out).
	CHECK(stops.back() == doctest::Approx(LayoutText(*font, line, settings).Lines[0].Width));
	CHECK(NearestUICaretStop(stops, -5.0f) == 0);
	CHECK(NearestUICaretStop(stops, 9999.0f) == stops.size() - 1);
	CHECK(NearestUICaretStop(stops, (stops[2] + stops[3]) * 0.5f + 0.1f) == 3);
}

TEST_CASE("Input collects typed text and key repeats, which the UI can take")
{
	Input::ResetState();
	Input::BeginFrame();
	Input::InjectText("h\xC3\xA9\x01\n!");
	CHECK(Input::GetTypedText() == "h\xC3\xA9!");			// Control characters are keys, not text

	Input::InjectKey(KeyCode::Backspace, true);
	CHECK(Input::IsKeyTyped(KeyCode::Backspace));
	Input::BeginFrame();
	CHECK_FALSE(Input::IsKeyTyped(KeyCode::Backspace));
	CHECK(Input::GetTypedText().empty());
	Input::InjectKeyRepeat(KeyCode::Backspace);
	CHECK(Input::IsKeyTyped(KeyCode::Backspace));
	CHECK_FALSE(Input::IsKeyPressed(KeyCode::Backspace));	// A repeat is not a new press
	Input::InjectKeyRepeat(KeyCode::Delete);				// Not held: no repeat
	CHECK_FALSE(Input::IsKeyTyped(KeyCode::Delete));

	// A UI that takes the keyboard takes typing too.
	Input::InjectText("x");
	Input::SetCapture(InputCapture { false, true, false });
	CHECK(Input::GetTypedText().empty());
	CHECK_FALSE(Input::IsKeyTyped(KeyCode::Backspace));
	CHECK(Input::GetTypedText(InputAccess::Raw) == "x");
	Input::SetCapture({});

	Input::ResetState();
	Input::BeginFrame();
}

TEST_CASE("Without a window, the clipboard is kept inside the program")
{
	Input::SetClipboardHandlers(nullptr, nullptr);
	Input::SetClipboardText("copied \xC3\xA9");
	CHECK(Input::GetClipboardText() == "copied \xC3\xA9");

	std::string system;
	Input::SetClipboardHandlers([&system]() { return system; }, [&system](const std::string& text) { system = text; });
	Input::SetClipboardText("system");
	CHECK(system == "system");
	CHECK(Input::GetClipboardText() == "system");
	Input::SetClipboardHandlers(nullptr, nullptr);
	CHECK(Input::GetClipboardText() == "copied \xC3\xA9");
}

TEST_CASE("Multi-line fields wrap long lines at spaces, or inside a word that doesn't fit")
{
	// Every character 10 pixels wide.
	const auto stopsFor = [](std::string_view line)
	{
		std::vector<float> stops;
		for (size_t i = 0; i <= CountUICharacters(line); i++)
			stops.push_back(10.0f * static_cast<float>(i));
		return stops;
	};
	using Rows = std::vector<std::pair<size_t, size_t>>;
	const auto wrap = [&](std::string_view line, float width) { return WrapUILine(line, stopsFor(line), width); };

	CHECK(wrap("short", 100.0f) == Rows { { 0, 5 } });
	CHECK(wrap("", 100.0f) == Rows { { 0, 0 } });
	CHECK(wrap("exactly ten", 110.0f) == Rows { { 0, 11 } });
	// Breaks after spaces, which stay at a row's end: 10 characters a row, then 8.
	CHECK(wrap("the quick brown fox", 100.0f) == Rows { { 0, 10 }, { 10, 19 } });
	CHECK(wrap("the quick brown fox", 80.0f) == Rows { { 0, 4 }, { 4, 10 }, { 10, 16 }, { 16, 19 } });
	// A space right after the fit ends the row too.
	CHECK(wrap("aaaa bbbb", 40.0f) == Rows { { 0, 5 }, { 5, 9 } });
	// A word longer than a row is cut.
	CHECK(wrap("abcdefghijklmnop", 50.0f) == Rows { { 0, 5 }, { 5, 10 }, { 10, 15 }, { 15, 16 } });
	CHECK(wrap("go abcdefghij", 50.0f) == Rows { { 0, 3 }, { 3, 8 }, { 8, 13 } });
	// Never an empty row, even narrower than one character; characters, not bytes.
	CHECK(wrap("abc", 5.0f) == Rows { { 0, 1 }, { 1, 2 }, { 2, 3 } });
	CHECK(wrap("\xc3\xa9t\xc3\xa9 caf\xc3\xa9", 50.0f) == Rows { { 0, 4 }, { 4, 8 } });
	// No width: one row.
	CHECK(wrap("the quick brown fox", 0.0f) == Rows { { 0, 19 } });
	// The rows always cover the line.
	for (float width : { 15.0f, 35.0f, 75.0f, 130.0f })
	{
		const Rows rows = wrap("lorem ipsum dolor sit amet, consectetur adipiscing", width);
		REQUIRE_FALSE(rows.empty());
		CHECK(rows.front().first == 0);
		CHECK(rows.back().second == 50);
		for (size_t i = 1; i < rows.size(); i++)
			CHECK(rows[i].first == rows[i - 1].second);
	}
}
