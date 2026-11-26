#include "TestFramework.h"

#include "ForgeScript/Compiler/Lexer.h"
#include "ForgeScript/Tools/Help.h"

// Editors show this help; these tests keep it complete as the language grows.

using namespace ForgeScript;

namespace {

	std::string PatternText(const PhrasePattern& pattern)
	{
		std::string text;
		size_t slot = 0;
		for (const std::string& part : pattern.Parts)
		{
			if (!text.empty())
				text += " ";
			text += part.empty() ? "(" + pattern.SlotNames[slot++] + ")" : part;
		}
		return text;
	}

	void CheckMethods(const std::unordered_map<String*, NativeFunction*>& table, const std::string& owner)
	{
		for (const auto& [name, function] : table)
		{
			INFO(owner << "." << name->Text);
			CHECK(FindMethodHelp(owner, name->Text) != nullptr);
		}
	}

}

TEST_CASE("HelpCoversEveryKeyword")
{
	for (const std::string& word : Lexer::GetKeywords())
	{
		INFO(word);
		CHECK(FindKeywordHelp(word) != nullptr);
	}
}

TEST_CASE("HelpCoversEveryBuiltInFunctionPhraseAndMethod")
{
	VM vm;
	for (const std::string& name : vm.GetSharedGlobalNames())
	{
		// Phrases are stored under names with spaces; they are checked by pattern below.
		if (name.find(' ') != std::string::npos)
			continue;
		INFO(name);
		CHECK(FindFunctionHelp(name) != nullptr);
	}
	for (const PhrasePattern& pattern : vm.GetPhraseTable().GetPatterns())
	{
		INFO(PatternText(pattern));
		CHECK(FindFunctionHelp(PatternText(pattern)) != nullptr);
	}
	CheckMethods(vm.TextMethods, "text");
	CheckMethods(vm.ListMethods, "list");
	CheckMethods(vm.MapMethods, "map");
	CheckMethods(vm.VectorMethods, "vector");
}

TEST_CASE("HelpOnlyDescribesThingsThatExist")
{
	VM vm;
	const std::vector<std::string> keywords = Lexer::GetKeywords();
	const std::vector<std::string> globals = vm.GetSharedGlobalNames();
	std::vector<std::string> phrases;
	for (const PhrasePattern& pattern : vm.GetPhraseTable().GetPatterns())
		phrases.push_back(PatternText(pattern));

	auto hasMethod = [&vm](const std::string& owner, const std::string& name)
	{
		const std::unordered_map<String*, NativeFunction*>* table = owner == "text" ? &vm.TextMethods
			: owner == "list" ? &vm.ListMethods : owner == "map" ? &vm.MapMethods : &vm.VectorMethods;
		return std::any_of(table->begin(), table->end(), [&name](const auto& entry) { return entry.first->Text == name; });
	};
	auto contains = [](const std::vector<std::string>& names, const std::string& name)
	{
		return std::find(names.begin(), names.end(), name) != names.end();
	};

	for (const HelpEntry& entry : GetLanguageHelp())
	{
		INFO(entry.Owner << " " << entry.Name);
		CHECK_FALSE(entry.Doc.empty());
		CHECK_FALSE(entry.Signature.empty());
		switch (entry.EntryKind)
		{
			// 'match', 'every', 'after', 'choices', 'property' and 'keep' are not reserved, but they start
			// statements of their own (see MatchOpensBlock, TimerOpensBlock, IsChoicesAt and the kind body),
			// so they have their own help.
			case HelpEntry::Kind::Keyword:
				CHECK((contains(keywords, entry.Name) || entry.Name == "match" || entry.Name == "every" || entry.Name == "after" ||
					entry.Name == "choices" || entry.Name == "shared" || entry.Name == "test" || entry.Name == "expect" ||
					entry.Name == "property" || entry.Name == "keep"));
				break;
			case HelpEntry::Kind::Function: CHECK(contains(globals, entry.Name)); break;
			case HelpEntry::Kind::Phrase: CHECK(contains(phrases, entry.Name)); break;
			case HelpEntry::Kind::Method:
				// Properties (length, first, x) are read by the VM directly, not through a method table.
				if (!entry.IsProperty)
					CHECK(hasMethod(entry.Owner, entry.Name));
				break;
		}
	}
}
