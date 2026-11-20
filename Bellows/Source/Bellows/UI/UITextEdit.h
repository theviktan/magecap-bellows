#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	class Font;

	// Editing UTF-8 text the way a text field does. Offsets are bytes at character boundaries; a
	// selection runs between its anchor (where it started) and the caret (where the cursor is).
	struct UITextSelection
	{
		size_t Caret = 0;
		size_t Anchor = 0;

		bool IsEmpty() const { return Caret == Anchor; }
		size_t Begin() const { return std::min(Caret, Anchor); }
		size_t End() const { return std::max(Caret, Anchor); }
		void CollapseTo(size_t offset) { Caret = Anchor = offset; }
	};

	size_t NextUICharacter(std::string_view text, size_t offset);
	size_t PreviousUICharacter(std::string_view text, size_t offset);
	// Ctrl+Right and Ctrl+Left: to the end of the next word, or the start of the previous one.
	size_t NextUIWord(std::string_view text, size_t offset);
	size_t PreviousUIWord(std::string_view text, size_t offset);
	size_t UILineStart(std::string_view text, size_t offset);
	size_t UILineEnd(std::string_view text, size_t offset);
	size_t CountUICharacters(std::string_view text);
	// The byte offset of character 'index' (the end for an index past it), and back.
	size_t UICharacterOffset(std::string_view text, size_t index);
	size_t UICharacterIndex(std::string_view text, size_t offset);
	// Moves 'offset' to a character boundary at or before it, inside the text.
	size_t ClampUITextOffset(std::string_view text, size_t offset);

	// Replaces the selection with 'insert' and puts the caret after it. Control characters are left
	// out, and so are line breaks unless 'multiLine'; with maxLength above 0 the text is cut to fit
	// that many characters. Returns whether the text changed.
	bool InsertUIText(std::string& text, UITextSelection& selection, std::string_view insert, bool multiLine, int maxLength);
	// Backspace (forward false) or Delete: the selection, or one character (a word with 'word') before
	// or after the caret. Returns whether the text changed.
	bool EraseUIText(std::string& text, UITextSelection& selection, bool forward, bool word);
	// What a password field shows: one bullet per character.
	std::string MaskUIText(std::string_view text);

	// Where a caret can stand on one line drawn with 'font' at 'size' pixels per em, matching
	// LayoutText: entry i is the left edge of character i, and the last entry is the line's end.
	std::vector<float> ComputeUICaretStops(const Font& font, std::string_view line, float size);
	// The character boundary nearest to x.
	size_t NearestUICaretStop(const std::vector<float>& stops, float x);
	// Word wrap for a multi-line field: splits one line (no '\n'), given its caret stops, into rows no
	// wider than 'width'. Each row is a [first, end) character range; together they cover the line.
	// A row breaks after a space (the space stays at the row's end, where it takes no room), or inside
	// a word too long for a row; every row has at least one character.
	std::vector<std::pair<size_t, size_t>> WrapUILine(std::string_view line, const std::vector<float>& stops, float width);

}
