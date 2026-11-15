#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ForgeScript {

	// A phrase is a function you call with words instead of brackets, for example:
	//     move (thing) toward (target) by (amount)
	// Words must match exactly; each "(slot)" is filled by an expression.
	struct PhrasePattern
	{
		std::vector<std::string> Parts; // A word, or an empty string for a slot.
		std::vector<std::string> SlotNames;

		// The hidden variable name the phrase is stored under, for example "move _ toward _ by _".
		std::string MangledName() const;
		size_t SlotCount() const { return SlotNames.size(); }
	};

	// Parses "move (thing) toward (target) by (amount)" into a pattern.
	// Returns false and fills outError when the pattern is not valid.
	bool ParsePhrasePattern(const std::string& text, PhrasePattern& outPattern, std::string& outError);

	// All known phrases, stored as a tree of words so the compiler can match them one word at a time.
	class PhraseTable
	{
	public:
		struct Node
		{
			std::map<std::string, std::unique_ptr<Node>> Words;
			std::unique_ptr<Node> Slot;
			std::string Terminal; // Mangled name when a phrase ends at this node.
		};

		PhraseTable();
		PhraseTable(const PhraseTable& other);
		PhraseTable& operator=(const PhraseTable& other);

		// Returns false when the exact same pattern already exists.
		bool Add(const PhrasePattern& pattern);
		bool IsHeadWord(const std::string& word) const;
		const Node* GetRoot() const { return m_Root.get(); }
		const std::vector<PhrasePattern>& GetPatterns() const { return m_Patterns; }

	private:
		std::unique_ptr<Node> m_Root;
		std::vector<PhrasePattern> m_Patterns;
	};

}
