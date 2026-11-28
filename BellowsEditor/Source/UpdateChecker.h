#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Bellows {

	// One downloadable file in an update manifest.
	struct UpdateFile
	{
		std::string Url;			// Absolute (relative ones are resolved against the manifest's address)
		uint64_t Size = 0;			// Bytes
		std::string Sha256;			// Lowercase hexadecimal
	};

	// What the update server says about the newest release (update.json, written by CI for each
	// release and served by the MageCap Games update server).
	struct UpdateManifest
	{
		std::string Version;		// "0.8.0"
		std::string Published;		// "2026-12-04", as written; may be empty
		std::string Notes;			// What's new, Markdown
		std::string ReleaseUrl;		// A page about the release; may be empty
		UpdateFile Installer;		// The Windows setup program
		std::optional<UpdateFile> Portable;	// The zip, for copies that weren't installed
	};

	// The parts of updating that don't need a window or a network, kept apart so they can be tested.
	namespace UpdateLogic {

		// Where the editor looks unless EditorSettings::UpdateUrl says otherwise.
		inline constexpr std::string_view c_DefaultUpdateUrl = "https://updates.magecapgames.com/bellows/update.json";
		// How long between automatic checks.
		inline constexpr int64_t c_CheckInterval = 24 * 60 * 60;
		// No manifest is this big; anything larger is not one.
		inline constexpr size_t c_MaxManifestBytes = 256 * 1024;
		// Nor is any installer.
		inline constexpr uint64_t c_MaxFileBytes = 2ull * 1024 * 1024 * 1024;

		// Compares "0.7.0", "0.7.0-dev" and "0.7.0-dev (3f8b735)": the numbers first, then a version
		// with a pre-release part ("-dev", "-rc1") before the same numbers without one. A commit in
		// parentheses is ignored. -1, 0 or 1; nothing when either isn't a version.
		std::optional<int> CompareVersions(std::string_view a, std::string_view b);
		// True when 'available' is a newer version than 'running'.
		bool IsNewer(std::string_view available, std::string_view running);

		// Reads update.json, checking every field; nothing (and 'outError') when it isn't a usable
		// manifest. 'manifestUrl' resolves relative file addresses and limits which schemes they may use
		// (https://, or file:// only for a manifest that is itself a file).
		std::optional<UpdateManifest> ParseManifest(std::string_view text, const std::string& manifestUrl, std::string& outError);
		// 'reference' resolved against 'base' (only absolute addresses, root-relative and plain relative paths).
		std::string ResolveUrl(const std::string& base, const std::string& reference);

		// The update server's answer (an HTTP status) when no release has update information yet, as
		// before the first release with an updater: not a failure, just nothing newer to offer.
		inline bool MeansNoRelease(int status) { return status == 404; }

		// An automatic check is due when it's on and the last one was a day ago (or in the future: the clock moved).
		bool IsCheckDue(bool enabled, int64_t lastCheck, int64_t now);
		// The startup check offers a version unless it was skipped; Help > Check for Updates always does.
		bool ShouldOffer(const UpdateManifest& manifest, std::string_view running, std::string_view skippedVersion, bool manual);

		// True for a copy the installer put in place (its uninstaller sits next to the editor), which
		// can update itself; a copy unzipped by hand downloads the zip instead.
		bool IsInstalledCopy(const std::filesystem::path& executableDirectory);
		// The installer's file name for a download, from the end of its address.
		std::string FileNameFromUrl(const std::string& url, const std::string& fallback);
		// The options a downloaded installer runs with: quiet, no questions, no restart, then it
		// waits for the editor to close and opens the new one.
		std::vector<std::string> InstallerArguments();
		// Checks a downloaded file against the manifest's size and digest.
		bool VerifyFile(const std::filesystem::path& path, const UpdateFile& expected, std::string& outError);

	}

	// Checks for and downloads updates on a background thread; the editor polls it once a frame. All
	// public functions are for the main thread. Destroying it during a check or download lets the
	// work finish on its own (or stop at the next piece) without waiting for it.
	class UpdateChecker
	{
	public:
		enum class State
		{
			Idle,			// Nothing asked yet
			Checking,
			UpToDate,		// The check found nothing newer (or no release to offer yet: no manifest)
			Available,		// A newer version is in GetManifest()
			Failed,			// The check or the download failed: GetError()
			Downloading,
			Downloaded,		// The installer (or zip) is at GetDownloadedFile(), checked
		};

		UpdateChecker();
		~UpdateChecker();
		UpdateChecker(const UpdateChecker&) = delete;
		UpdateChecker& operator=(const UpdateChecker&) = delete;

		// Starts a check unless one or a download is under way. 'manual' marks one the user asked for.
		bool StartCheck(const std::string& manifestUrl, const std::string& runningVersion, bool manual);
		// Downloads the manifest's installer (or 'portable', the zip) into 'folder' and checks it.
		bool StartDownload(const std::filesystem::path& folder, bool portable);
		// Stops a download at its next piece; the checker goes back to Available.
		void CancelDownload();

		State GetState() const;
		bool IsManual() const;
		bool IsBusy() const { State state = GetState(); return state == State::Checking || state == State::Downloading; }
		std::optional<UpdateManifest> GetManifest() const;
		std::string GetError() const;
		std::filesystem::path GetDownloadedFile() const;
		// Bytes so far and the total, while downloading.
		std::pair<uint64_t, uint64_t> GetProgress() const;
	private:
		struct Shared;
		std::shared_ptr<Shared> m_Shared;
	};

}
