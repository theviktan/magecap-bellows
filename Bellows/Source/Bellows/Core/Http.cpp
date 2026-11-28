#include "bwpch.h"
#include "Bellows/Core/Http.h"

#include <fstream>

#if defined(BW_PLATFORM_WINDOWS)
	#include <Windows.h>
	#include <winhttp.h>
#else
	#include <fcntl.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

namespace Bellows::Http {

	namespace {

		constexpr std::string_view c_FileScheme = "file://";
		constexpr std::string_view c_HttpsScheme = "https://";

		bool StartsWith(std::string_view text, std::string_view prefix)
		{
			if (text.size() < prefix.size())
				return false;
			for (size_t i = 0; i < prefix.size(); i++)
			{
				if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i])
					return false;
			}
			return true;
		}

		// Moves 'temporary' over 'destination', or removes it; a failed download leaves nothing behind.
		bool Finish(const std::filesystem::path& temporary, const std::filesystem::path& destination, bool succeeded, std::string& outError)
		{
			std::error_code error;
			if (!succeeded)
			{
				std::filesystem::remove(temporary, error);
				return false;
			}
			std::filesystem::rename(temporary, destination, error);
			if (error)
			{
				std::filesystem::remove(temporary, error);
				outError = "couldn't save the download as " + destination.string();
				return false;
			}
			return true;
		}

		Response ReadLocal(const std::filesystem::path& path, size_t maxBytes)
		{
			Response response;
			std::error_code error;
			const uint64_t size = std::filesystem::file_size(path, error);
			if (error)
			{
				response.Error = "'" + path.string() + "' can't be read";
				return response;
			}
			if (size > maxBytes)
			{
				response.Error = "it's larger than expected";
				return response;
			}
			std::ifstream file(path, std::ios::binary);
			response.Body.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
			response.Status = 200;
			return response;
		}

		bool CopyLocal(const std::filesystem::path& source, const std::filesystem::path& temporary, uint64_t maxBytes,
			const std::function<bool(uint64_t, uint64_t)>& progress, std::string& outError)
		{
			std::error_code error;
			const uint64_t size = std::filesystem::file_size(source, error);
			if (error)
			{
				outError = "'" + source.string() + "' can't be read";
				return false;
			}
			if (size > maxBytes)
			{
				outError = "the file is larger than expected";
				return false;
			}
			std::ifstream in(source, std::ios::binary);
			std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
			if (!in || !out)
			{
				outError = "couldn't copy '" + source.string() + "'";
				return false;
			}
			std::vector<char> chunk(1 << 16);
			uint64_t done = 0;
			while (in)
			{
				in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
				const std::streamsize read = in.gcount();
				if (read <= 0)
					break;
				out.write(chunk.data(), read);
				done += static_cast<uint64_t>(read);
				if (progress && !progress(done, size))
				{
					outError = "cancelled";
					return false;
				}
			}
			if (!out)
			{
				outError = "couldn't write the download (is the disk full?)";
				return false;
			}
			return true;
		}

#if defined(BW_PLATFORM_WINDOWS)
		std::wstring Widen(const std::string& text)
		{
			if (text.empty())
				return {};
			const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<size_t>(size), L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
			return wide;
		}

		// One HTTPS GET with WinHTTP, its body handed to 'sink' in pieces (false from it stops).
		struct Handles
		{
			HINTERNET Session = nullptr, Connection = nullptr, Request = nullptr;
			~Handles()
			{
				if (Request) ::WinHttpCloseHandle(Request);
				if (Connection) ::WinHttpCloseHandle(Connection);
				if (Session) ::WinHttpCloseHandle(Session);
			}
		};

		int Fetch(const std::string& url, std::chrono::seconds timeout, uint64_t maxBytes,
			const std::function<bool(const char*, size_t, uint64_t)>& sink, std::string& outError)
		{
			const std::wstring wideUrl = Widen(url);
			URL_COMPONENTS parts {};
			parts.dwStructSize = sizeof(parts);
			wchar_t host[256] = {};
			wchar_t path[2048] = {};
			wchar_t extra[2048] = {};
			parts.lpszHostName = host;
			parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
			parts.lpszUrlPath = path;
			parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
			parts.lpszExtraInfo = extra;
			parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
			if (!::WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
			{
				outError = "the address isn't an https:// address";
				return 0;
			}

			Handles handles;
			const std::wstring agent = L"MageCapBellows/" + Widen(BW_VERSION);
			handles.Session = ::WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
			if (!handles.Session)
			{
				outError = "Windows couldn't start a web request";
				return 0;
			}
			const int milliseconds = static_cast<int>(timeout.count() * 1000);
			::WinHttpSetTimeouts(handles.Session, milliseconds, milliseconds, milliseconds, milliseconds);
			handles.Connection = ::WinHttpConnect(handles.Session, host, parts.nPort, 0);
			const std::wstring object = std::wstring(path) + extra;
			if (handles.Connection)
				handles.Request = ::WinHttpOpenRequest(handles.Connection, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
					WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
			if (!handles.Request || !::WinHttpSendRequest(handles.Request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
				|| !::WinHttpReceiveResponse(handles.Request, nullptr))
			{
				outError = "couldn't reach the update server (check the internet connection)";
				return 0;
			}

			DWORD status = 0, size = sizeof(status);
			::WinHttpQueryHeaders(handles.Request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
			uint64_t total = 0;
			wchar_t lengthText[32] = {};
			DWORD lengthSize = sizeof(lengthText);
			if (::WinHttpQueryHeaders(handles.Request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize, WINHTTP_NO_HEADER_INDEX))
				total = std::wcstoull(lengthText, nullptr, 10);
			if (status < 200 || status >= 300)
			{
				outError = "the update server answered " + std::to_string(status);
				return static_cast<int>(status);
			}
			if (total > maxBytes)
			{
				outError = "the file is larger than expected";
				return static_cast<int>(status);
			}

			std::vector<char> chunk(1 << 16);
			uint64_t received = 0;
			for (;;)
			{
				DWORD read = 0;
				if (!::WinHttpReadData(handles.Request, chunk.data(), static_cast<DWORD>(chunk.size()), &read))
				{
					outError = "the download stopped part way (check the internet connection)";
					return static_cast<int>(status);
				}
				if (read == 0)
					break;
				received += read;
				if (received > maxBytes)
				{
					outError = "the file is larger than expected";
					return static_cast<int>(status);
				}
				if (!sink(chunk.data(), read, total))
				{
					outError = "cancelled";
					return static_cast<int>(status);
				}
			}
			return static_cast<int>(status);
		}
#else
		// curl, quietly, failing on HTTP errors, HTTPS only (redirects too), into 'output'.
		bool RunCurl(const std::string& url, const std::filesystem::path& output, uint64_t maxBytes, std::chrono::seconds timeout, std::string& outError)
		{
			const std::string maxSize = std::to_string(maxBytes);
			const std::string maxTime = std::to_string(timeout.count());
			const std::string outputPath = output.string();
			const pid_t pid = fork();
			if (pid < 0)
			{
				outError = "couldn't start curl";
				return false;
			}
			if (pid == 0)
			{
				const int devNull = open("/dev/null", O_WRONLY);
				if (devNull >= 0)
				{
					dup2(devNull, STDOUT_FILENO);
					dup2(devNull, STDERR_FILENO);
				}
				execlp("curl", "curl", "--silent", "--fail", "--location", "--proto", "=https", "--proto-redir", "=https",
					"--max-filesize", maxSize.c_str(), "--max-time", maxTime.c_str(), "--output", outputPath.c_str(), url.c_str(),
					static_cast<char*>(nullptr));
				_exit(127);
			}
			int status = 0;
			waitpid(pid, &status, 0);
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			{
				outError = WIFEXITED(status) && WEXITSTATUS(status) == 127 ? "curl isn't installed"
					: "couldn't download it (curl error " + std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ")";
				return false;
			}
			return true;
		}
#endif

	}

	std::filesystem::path FileUrlToPath(const std::string& url)
	{
		if (!StartsWith(url, c_FileScheme))
			return {};
		std::string path = url.substr(c_FileScheme.size());
		// file:///C:/folder/file on Windows: the third slash comes before the drive letter.
		if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':')
			path.erase(0, 1);
		return std::filesystem::path(path);
	}

	Response Get(const std::string& url, size_t maxBytes, std::chrono::seconds timeout)
	{
		if (StartsWith(url, c_FileScheme))
			return ReadLocal(FileUrlToPath(url), maxBytes);
		Response response;
		if (!StartsWith(url, c_HttpsScheme))
		{
			response.Error = "only https:// addresses are used";
			return response;
		}
#if defined(BW_PLATFORM_WINDOWS)
		response.Status = Fetch(url, timeout, maxBytes, [&](const char* data, size_t size, uint64_t)
		{
			response.Body.append(data, size);
			return true;
		}, response.Error);
#else
		// One file per request, so requests on several threads never share one.
		static std::atomic<uint32_t> s_RequestCount { 0 };
		const std::filesystem::path temporary = std::filesystem::temp_directory_path()
			/ ("bellows-get-" + std::to_string(getpid()) + "-" + std::to_string(s_RequestCount++) + ".tmp");
		if (RunCurl(url, temporary, maxBytes, timeout, response.Error))
		{
			response = ReadLocal(temporary, maxBytes);
		}
		std::error_code error;
		std::filesystem::remove(temporary, error);
#endif
		return response;
	}

	bool Download(const std::string& url, const std::filesystem::path& destination, uint64_t maxBytes,
		const std::function<bool(uint64_t, uint64_t)>& progress, std::string& outError)
	{
		std::filesystem::path temporary = destination;
		temporary += ".part";
		if (StartsWith(url, c_FileScheme))
			return Finish(temporary, destination, CopyLocal(FileUrlToPath(url), temporary, maxBytes, progress, outError), outError);
		if (!StartsWith(url, c_HttpsScheme))
		{
			outError = "only https:// addresses are used";
			return false;
		}
#if defined(BW_PLATFORM_WINDOWS)
		std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			outError = "couldn't write to " + temporary.string();
			return false;
		}
		uint64_t done = 0;
		const int status = Fetch(url, std::chrono::seconds(60), maxBytes, [&](const char* data, size_t size, uint64_t total)
		{
			out.write(data, static_cast<std::streamsize>(size));
			done += size;
			return out.good() && (!progress || progress(done, total));
		}, outError);
		out.close();
		const bool succeeded = outError.empty() && status >= 200 && status < 300 && !out.fail();
		if (outError.empty() && !succeeded)
			outError = "couldn't write the download (is the disk full?)";
		return Finish(temporary, destination, succeeded, outError);
#else
		const bool succeeded = RunCurl(url, temporary, maxBytes, std::chrono::seconds(600), outError);
		if (succeeded && progress)
		{
			std::error_code error;
			const uint64_t size = std::filesystem::file_size(temporary, error);
			progress(size, size);
		}
		return Finish(temporary, destination, succeeded, outError);
#endif
	}

}
