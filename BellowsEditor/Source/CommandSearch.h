#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Bellows {

	// The title bar's search (Ctrl+K): what it can find and how a typed query ranks it. UI-free, so
	// it is unit tested; the editor gathers the items and runs the one picked.
	namespace CommandSearch {

		// In the order the results list them when they score the same.
		enum class ResultKind : uint8_t { Command, Entity, Scene, Script };
		const char* KindName(ResultKind kind);	// "command", "entity", "scene", "script"

		struct Item
		{
			ResultKind Kind = ResultKind::Command;
			std::string Name;			// What is matched and shown ("Save Scene", "Docking Bay A")
			std::string Detail;			// Shown muted beside it: a shortcut, a parent or a folder
			bool Enabled = true;		// A command that can't run now is listed dimmed and doesn't run
		};

		// How well 'query' matches 'text', or -1 when it doesn't. Any letter case; the query's letters
		// must appear in order. A whole-text prefix beats a word start beats letters in the middle, a
		// run of letters together beats scattered ones, and spaces in the query are ignored, so "sv sc"
		// finds "Save Scene". An empty query matches everything with 0.
		int Score(std::string_view text, std::string_view query);

		// The indexes of the items that match, best first: by score, then by kind (commands first),
		// then by name; at most 'limit' of them. An empty query lists only commands, in their order.
		std::vector<size_t> Rank(const std::vector<Item>& items, std::string_view query, size_t limit = 50);

	}

}
