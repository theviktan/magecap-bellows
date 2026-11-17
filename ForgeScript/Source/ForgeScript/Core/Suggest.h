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

}
