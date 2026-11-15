#include "ForgeScript/Compiler/Phrase.h"

#include <cctype>

namespace ForgeScript {

	std::string PhrasePattern::MangledName() const
	{
		std::string name;
		for (const std::string& part : Parts)
		{
			if (!name.empty())
				name += ' ';
			name += part.empty() ? "_" : part;
		}
		return name;
	}

	static bool IsWordText(const std::string& text)
	{
		if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_'))
			return false;
		for (char c : text)
		{
			if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
				return false;
		}
		return true;
	}

	bool ParsePhrasePattern(const std::string& text, PhrasePattern& outPattern, std::string& outError)
	{
		outPattern = PhrasePattern();
		size_t i = 0;
		while (i < text.size())
		{
			char c = text[i];
			if (std::isspace(static_cast<unsigned char>(c)))
			{
				i++;
				continue;
			}

			if (c == '(')
			{
				size_t close = text.find(')', i);
				if (close == std::string::npos)
				{
					outError = "Phrase slot is missing its ')'.";
					return false;
				}
				std::string name = text.substr(i + 1, close - i - 1);
				while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
					name.pop_back();
				while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
					name.erase(name.begin());
				if (!IsWordText(name))
				{
					outError = "Phrase slot '(" + name + ")' needs a simple name inside the brackets.";
					return false;
				}
				if (!outPattern.Parts.empty() && outPattern.Parts.back().empty())
				{
					outError = "Two slots in a row are not allowed; put a word between them.";
					return false;
				}
				outPattern.Parts.emplace_back();
				outPattern.SlotNames.push_back(name);
				i = close + 1;
				continue;
			}

			size_t start = i;
			while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '(')
				i++;
			std::string word = text.substr(start, i - start);
			if (!IsWordText(word))
			{
				outError = "'" + word + "' is not a valid word in a phrase.";
				return false;
			}
			outPattern.Parts.push_back(word);
		}

		if (outPattern.Parts.empty())
		{
			outError = "A phrase needs at least one word.";
			return false;
		}
		if (outPattern.Parts.front().empty())
		{
			outError = "A phrase must start with a word, not a slot.";
			return false;
		}
		return true;
	}

	PhraseTable::PhraseTable()
		: m_Root(std::make_unique<Node>())
	{
	}

	PhraseTable::PhraseTable(const PhraseTable& other)
		: m_Root(std::make_unique<Node>())
	{
		for (const PhrasePattern& pattern : other.m_Patterns)
			Add(pattern);
	}

	PhraseTable& PhraseTable::operator=(const PhraseTable& other)
	{
		if (this != &other)
		{
			m_Root = std::make_unique<Node>();
			m_Patterns.clear();
			for (const PhrasePattern& pattern : other.m_Patterns)
				Add(pattern);
		}
		return *this;
	}

	bool PhraseTable::Add(const PhrasePattern& pattern)
	{
		Node* node = m_Root.get();
		for (const std::string& part : pattern.Parts)
		{
			if (part.empty())
			{
				if (!node->Slot)
					node->Slot = std::make_unique<Node>();
				node = node->Slot.get();
			}
			else
			{
				std::unique_ptr<Node>& child = node->Words[part];
				if (!child)
					child = std::make_unique<Node>();
				node = child.get();
			}
		}

		if (!node->Terminal.empty())
			return false;
		node->Terminal = pattern.MangledName();
		m_Patterns.push_back(pattern);
		return true;
	}

	bool PhraseTable::IsHeadWord(const std::string& word) const
	{
		return m_Root->Words.find(word) != m_Root->Words.end();
	}

}
