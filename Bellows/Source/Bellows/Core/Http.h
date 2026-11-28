#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace Bellows {

	// Fetches files over HTTPS for the editor's update check. Only https:// and file:// addresses are
	// taken (file:// lets tests and offline setups point at a folder); plain http:// is refused, and a
	// redirect from HTTPS to HTTP isn't followed. On Windows this is WinHTTP, part of Windows; on other
	// systems it runs curl, if it's installed. Both block, so call them on a background thread.
	namespace Http {

		struct Response
		{
			int Status = 0;			// The HTTP status (200 for a file:// read that worked); 0 when nothing came back
			std::string Body;
			std::string Error;		// Why it failed, in words; empty when the request went through

			bool Ok() const { return Error.empty() && Status >= 200 && Status < 300; }
		};

		// A small document, such as an update manifest: at most 'maxBytes' of it, or it fails.
		Response Get(const std::string& url, size_t maxBytes, std::chrono::seconds timeout = std::chrono::seconds(20));

		// Writes the file at 'url' to 'destination' (replacing it), refusing anything over 'maxBytes'.
		// 'progress' gets the bytes so far and the total (0 when unknown) and returns false to cancel.
		// Leaves no partial file behind when it fails.
		bool Download(const std::string& url, const std::filesystem::path& destination, uint64_t maxBytes,
			const std::function<bool(uint64_t done, uint64_t total)>& progress, std::string& outError);

		// The path a file:// address names, or empty when 'url' isn't one.
		std::filesystem::path FileUrlToPath(const std::string& url);

	}

}
