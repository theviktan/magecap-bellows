#pragma once

#include <string>
#include <vector>

namespace ForgeScript {

	// Short help for everything built into the language, shown by editors (hover and completion in
	// 'forgescript --lsp'). Tests check that every keyword, built-in function, method and phrase has an
	// entry, so the help cannot fall behind the language.
	struct HelpEntry
	{
		enum class Kind
		{
			Keyword,
			Function,   // a built-in function or value (print, pi)
			Phrase,     // a built-in phrase (random item from (items))
			Method,     // a method or property of a built-in type
		};

		Kind EntryKind = Kind::Function;
		std::string Owner;      // for methods: "text", "list", "map" or "vector"
		std::string Name;       // the word looked up: "print", "random item from (items)", "upper"
		std::string Signature;  // how it is written: "print(values...)", "text.upper()"
		std::string Doc;        // one or two plain sentences
		bool IsProperty = false;
	};

	const std::vector<HelpEntry>& GetLanguageHelp();

	// nullptr when there is no entry.
	const HelpEntry* FindKeywordHelp(const std::string& word);
	const HelpEntry* FindFunctionHelp(const std::string& name);
	const HelpEntry* FindMethodHelp(const std::string& owner, const std::string& name);

}
