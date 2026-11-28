#include "UpdateChecker.h"

#include "Bellows/Core/Http.h"
#include "Bellows/Core/Sha256.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <mutex>
#include <thread>

namespace Bellows {

	namespace {

		struct ParsedVersion
		{
			int Numbers[3] = { 0, 0, 0 };
			std::string PreRelease;		// After '-'; empty for a release
		};

		std::string_view Trim(std::string_view text)
		{
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
				text.remove_prefix(1);
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
				text.remove_suffix(1);
			return text;
		}

		std::optional<ParsedVersion> ParseVersion(std::string_view text)
		{
			text = Trim(text);
			// "0.7.0-dev (3f8b735)": the commit tells builds apart, not versions.
			if (const size_t paren = text.find(" ("); paren != std::string_view::npos)
				text = Trim(text.substr(0, paren));
			if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
				text.remove_prefix(1);
			ParsedVersion version;
			if (const size_t dash = text.find('-'); dash != std::string_view::npos)
			{
				version.PreRelease = std::string(text.substr(dash + 1));
				text = text.substr(0, dash);
				if (version.PreRelease.empty())
					return std::nullopt;
			}
			int part = 0;
			const char* cursor = text.data();
			const char* end = text.data() + text.size();
			while (true)
			{
				if (part == 3)
					return std::nullopt;
				const auto [next, error] = std::from_chars(cursor, end, version.Numbers[part]);
				if (error != std::errc() || next == cursor || version.Numbers[part] < 0)
					return std::nullopt;
				part++;
				cursor = next;
				if (cursor == end)
					break;
				if (*cursor != '.')
					return std::nullopt;
				cursor++;
			}
			// "1" and "1.2" are read as 1.0.0 and 1.2.0.
			return version;
		}

		bool IsHexDigest(const std::string& text)
		{
			return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
		}

		bool StartsWithText(std::string_view text, std::string_view prefix)
		{
			return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
		}

		// A file entry of the manifest: { "url": ..., "size": ..., "sha256": ... }.
		std::optional<UpdateFile> ReadFile(const Json& json, const char* key, const std::string& manifestUrl, std::string& outError)
		{
			const auto it = json.find(key);
			if (it == json.end() || !it->is_object())
			{
				outError = std::string("it has no \"") + key + "\" file";
				return std::nullopt;
			}
			UpdateFile file;
			std::string url;
			if (!it->contains("url") || !(*it)["url"].is_string() || (url = (*it)["url"].get<std::string>()).empty())
			{
				outError = std::string("the \"") + key + "\" file has no address";
				return std::nullopt;
			}
			file.Url = UpdateLogic::ResolveUrl(manifestUrl, url);
			// A manifest from the internet can only point at the internet; a local one (tests) may
			// also point at local files.
			const bool local = StartsWithText(manifestUrl, "file://");
			if (!StartsWithText(file.Url, "https://") && !(local && StartsWithText(file.Url, "file://")))
			{
				outError = std::string("the \"") + key + "\" file's address isn't https://";
				return std::nullopt;
			}
			if (!it->contains("size") || !(*it)["size"].is_number_unsigned() || (file.Size = (*it)["size"].get<uint64_t>()) == 0
				|| file.Size > UpdateLogic::c_MaxFileBytes)
			{
				outError = std::string("the \"") + key + "\" file's size is missing or wrong";
				return std::nullopt;
			}
			if (!it->contains("sha256") || !(*it)["sha256"].is_string())
			{
				outError = std::string("the \"") + key + "\" file has no SHA-256";
				return std::nullopt;
			}
			file.Sha256 = (*it)["sha256"].get<std::string>();
			std::transform(file.Sha256.begin(), file.Sha256.end(), file.Sha256.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			if (!IsHexDigest(file.Sha256))
			{
				outError = std::string("the \"") + key + "\" file's SHA-256 isn't 64 hexadecimal digits";
				return std::nullopt;
			}
			return file;
		}

		std::string ReadText(const Json& json, const char* key)
		{
			const auto it = json.find(key);
			return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
		}

	}

	namespace UpdateLogic {

		std::optional<int> CompareVersions(std::string_view a, std::string_view b)
		{
			const std::optional<ParsedVersion> left = ParseVersion(a);
			const std::optional<ParsedVersion> right = ParseVersion(b);
			if (!left || !right)
				return std::nullopt;
			for (int i = 0; i < 3; i++)
			{
				if (left->Numbers[i] != right->Numbers[i])
					return left->Numbers[i] < right->Numbers[i] ? -1 : 1;
			}
			if (left->PreRelease == right->PreRelease)
				return 0;
			if (left->PreRelease.empty())
				return 1;
			if (right->PreRelease.empty())
				return -1;
			return left->PreRelease < right->PreRelease ? -1 : 1;
		}

		bool IsNewer(std::string_view available, std::string_view running)
		{
			const std::optional<int> comparison = CompareVersions(available, running);
			return comparison && *comparison > 0;
		}

		std::string ResolveUrl(const std::string& base, const std::string& reference)
		{
			if (reference.find("://") != std::string::npos)
				return reference;
			const size_t schemeEnd = base.find("://");
			if (schemeEnd == std::string::npos)
				return reference;
			if (!reference.empty() && reference.front() == '/')
			{
				// Root-relative: the scheme and host of the base, then this path. file:// has no host.
				const size_t pathStart = base.find('/', schemeEnd + 3);
				return (pathStart == std::string::npos ? base : base.substr(0, pathStart)) + reference;
			}
			// Relative to the base's folder (the base without its query and last part).
			std::string folder = base.substr(0, base.find_first_of("?#"));
			const size_t lastSlash = folder.rfind('/');
			if (lastSlash != std::string::npos && lastSlash >= schemeEnd + 3)
				folder.erase(lastSlash + 1);
			else
				folder += '/';
			return folder + reference;
		}

		std::optional<UpdateManifest> ParseManifest(std::string_view text, const std::string& manifestUrl, std::string& outError)
		{
			const Json json = Json::parse(text.begin(), text.end(), nullptr, false);
			if (json.is_discarded() || !json.is_object())
			{
				outError = "the update information isn't valid JSON";
				return std::nullopt;
			}
			UpdateManifest manifest;
			manifest.Version = ReadText(json, "version");
			if (!ParseVersion(manifest.Version))
			{
				outError = "the update information has no version";
				return std::nullopt;
			}
			manifest.Published = ReadText(json, "published");
			manifest.Notes = ReadText(json, "notes");
			manifest.ReleaseUrl = ReadText(json, "release_url");
			if (!manifest.ReleaseUrl.empty() && !StartsWithText(manifest.ReleaseUrl, "https://"))
				manifest.ReleaseUrl.clear();
			std::optional<UpdateFile> installer = ReadFile(json, "installer", manifestUrl, outError);
			if (!installer)
				return std::nullopt;
			manifest.Installer = std::move(*installer);
			if (json.contains("portable"))
			{
				manifest.Portable = ReadFile(json, "portable", manifestUrl, outError);
				if (!manifest.Portable)
					return std::nullopt;
			}
			return manifest;
		}

		bool IsCheckDue(bool enabled, int64_t lastCheck, int64_t now)
		{
			if (!enabled)
				return false;
			return lastCheck <= 0 || lastCheck > now || now - lastCheck >= c_CheckInterval;
		}

		bool ShouldOffer(const UpdateManifest& manifest, std::string_view running, std::string_view skippedVersion, bool manual)
		{
			if (!IsNewer(manifest.Version, running))
				return false;
			if (manual || skippedVersion.empty())
				return true;
			const std::optional<int> comparison = CompareVersions(manifest.Version, skippedVersion);
			// A version newer than the skipped one is offered again.
			return !comparison || *comparison > 0;
		}

		bool IsInstalledCopy(const std::filesystem::path& executableDirectory)
		{
			std::error_code error;
			return std::filesystem::exists(executableDirectory / "unins000.exe", error);
		}

		std::string FileNameFromUrl(const std::string& url, const std::string& fallback)
		{
			std::string path = url.substr(0, url.find_first_of("?#"));
			const size_t slash = path.rfind('/');
			std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
			// Only a plain file name: nothing that could climb out of the downloads folder.
			const bool plain = !name.empty() && name != "." && name != ".."
				&& std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'; });
			return plain ? name : fallback;
		}

		std::vector<std::string> InstallerArguments()
		{
			return { "/SILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-", "/UPDATE" };
		}

		bool VerifyFile(const std::filesystem::path& path, const UpdateFile& expected, std::string& outError)
		{
			std::error_code error;
			const uint64_t size = std::filesystem::file_size(path, error);
			if (error || size != expected.Size)
			{
				outError = "the download is the wrong size";
				return false;
			}
			const std::optional<std::string> digest = Sha256::HashFile(path);
			if (!digest || *digest != expected.Sha256)
			{
				outError = "the download doesn't match its SHA-256, so it wasn't used";
				return false;
			}
			return true;
		}

	}

	// What the main thread and the worker share. The worker holds its own reference, so the checker
	// can go away while it runs.
	struct UpdateChecker::Shared
	{
		mutable std::mutex Mutex;
		State CurrentState = State::Idle;
		bool Manual = false;
		std::optional<UpdateManifest> Manifest;
		std::string Error;
		std::filesystem::path DownloadedFile;
		std::atomic<uint64_t> Done { 0 };
		std::atomic<uint64_t> Total { 0 };
		std::atomic<bool> Cancel { false };
	};

	UpdateChecker::UpdateChecker()
		: m_Shared(std::make_shared<Shared>())
	{
	}

	UpdateChecker::~UpdateChecker()
	{
		m_Shared->Cancel = true;
	}

	bool UpdateChecker::StartCheck(const std::string& manifestUrl, const std::string& runningVersion, bool manual)
	{
		{
			std::lock_guard lock(m_Shared->Mutex);
			if (m_Shared->CurrentState == State::Checking || m_Shared->CurrentState == State::Downloading)
				return false;
			m_Shared->CurrentState = State::Checking;
			m_Shared->Manual = manual;
			m_Shared->Error.clear();
			m_Shared->Manifest.reset();
			m_Shared->DownloadedFile.clear();
		}
		std::thread([shared = m_Shared, manifestUrl, runningVersion]()
		{
			const Http::Response response = Http::Get(manifestUrl, UpdateLogic::c_MaxManifestBytes);
			std::string error;
			std::optional<UpdateManifest> manifest;
			// The server answers 404 while no release has update information: nothing newer to offer.
			const bool nothingYet = UpdateLogic::MeansNoRelease(response.Status);
			if (nothingYet)
				error.clear();
			else if (!response.Ok())
				error = response.Error.empty() ? "the update server answered " + std::to_string(response.Status) : response.Error;
			else
				manifest = UpdateLogic::ParseManifest(response.Body, manifestUrl, error);

			std::lock_guard lock(shared->Mutex);
			if (nothingYet)
			{
				shared->CurrentState = State::UpToDate;
				return;
			}
			if (!manifest)
			{
				shared->CurrentState = State::Failed;
				shared->Error = error;
				return;
			}
			const bool newer = UpdateLogic::IsNewer(manifest->Version, runningVersion);
			shared->Manifest = std::move(manifest);
			shared->CurrentState = newer ? State::Available : State::UpToDate;
		}).detach();
		return true;
	}

	bool UpdateChecker::StartDownload(const std::filesystem::path& folder, bool portable)
	{
		UpdateFile file;
		{
			std::lock_guard lock(m_Shared->Mutex);
			if (m_Shared->CurrentState != State::Available && m_Shared->CurrentState != State::Failed)
				return false;
			if (!m_Shared->Manifest || (portable && !m_Shared->Manifest->Portable))
				return false;
			file = portable ? *m_Shared->Manifest->Portable : m_Shared->Manifest->Installer;
			m_Shared->CurrentState = State::Downloading;
			m_Shared->Error.clear();
			m_Shared->Done = 0;
			m_Shared->Total = file.Size;
			m_Shared->Cancel = false;
		}
		const std::string fallback = portable ? "MageCap-Bellows.zip" : "MageCap-Bellows-Setup.exe";
		const std::filesystem::path destination = folder / UpdateLogic::FileNameFromUrl(file.Url, fallback);
		std::thread([shared = m_Shared, file, folder, destination]()
		{
			std::string error;
			std::error_code ignored;
			std::filesystem::create_directories(folder, ignored);
			bool succeeded = Http::Download(file.Url, destination, file.Size, [&shared](uint64_t done, uint64_t)
			{
				shared->Done = done;
				return !shared->Cancel.load();
			}, error);
			if (succeeded && !UpdateLogic::VerifyFile(destination, file, error))
			{
				std::filesystem::remove(destination, ignored);
				succeeded = false;
			}

			std::lock_guard lock(shared->Mutex);
			if (succeeded)
			{
				shared->CurrentState = State::Downloaded;
				shared->DownloadedFile = destination;
			}
			else if (shared->Cancel)
			{
				shared->CurrentState = State::Available;
			}
			else
			{
				shared->CurrentState = State::Failed;
				shared->Error = error;
			}
		}).detach();
		return true;
	}

	void UpdateChecker::CancelDownload()
	{
		m_Shared->Cancel = true;
	}

	UpdateChecker::State UpdateChecker::GetState() const
	{
		std::lock_guard lock(m_Shared->Mutex);
		return m_Shared->CurrentState;
	}

	bool UpdateChecker::IsManual() const
	{
		std::lock_guard lock(m_Shared->Mutex);
		return m_Shared->Manual;
	}

	std::optional<UpdateManifest> UpdateChecker::GetManifest() const
	{
		std::lock_guard lock(m_Shared->Mutex);
		return m_Shared->Manifest;
	}

	std::string UpdateChecker::GetError() const
	{
		std::lock_guard lock(m_Shared->Mutex);
		return m_Shared->Error;
	}

	std::filesystem::path UpdateChecker::GetDownloadedFile() const
	{
		std::lock_guard lock(m_Shared->Mutex);
		return m_Shared->DownloadedFile;
	}

	std::pair<uint64_t, uint64_t> UpdateChecker::GetProgress() const
	{
		return { m_Shared->Done.load(), m_Shared->Total.load() };
	}

}
