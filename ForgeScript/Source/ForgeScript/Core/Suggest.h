#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ForgeScript {

	// Finds the known name a misspelled one most likely meant, for error messages such as
	// "'helth' has not been given a value yet. Did you mean 'health'?". Beginners make typos far more
	// often than they forget to set a value, so naming the close match saves them a confusing hunt.
	//
	// The match ignores letter case and counts a swap of two neighboring letters as one edit
	// ("lenght" is one edit from "length"). Short names allow fewer edits than long ones, so "x" never
	// suggests "y". Returns an empty string when nothing is close enough.
	std::string ClosestName(std::string_view name, const std::vector<std::string_view>& candidates);

	// " Did you mean '<closest>'?" to append to an error message, or an empty string.
	std::string DidYouMean(std::string_view name, const std::vector<std::string_view>& candidates);

	// The teaching layer for names from other languages: what ForgeScript writes instead of 'True',
	// 'None', 'len' or 'str', as a sentence to append to "'len' has not been given a value yet.", like
	// " In ForgeScript, write: length(list)". Empty when the name isn't one of them.
	std::string OtherLanguageNameHint(std::string_view name);

	// The same for a method a value doesn't have ('append' on a list, 'toUpperCase' on text). 'typeName'
	// is "list", "text", "map", "number" or "vector".
	std::string OtherLanguageMethodHint(std::string_view typeName, std::string_view method);

}
