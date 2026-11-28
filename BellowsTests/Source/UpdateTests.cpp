#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Core/Http.h"
#include "Bellows/Core/Process.h"
#include "Bellows/Core/Sha256.h"

#include "UpdateChecker.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

using namespace Bellows;

namespace {

	void WriteBytes(const std::filesystem::path& path, const std::string& bytes)
	{
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file << bytes;
	}

	std::string FileUrl(const std::filesystem::path& path)
	{
		std::string text = std::filesystem::absolute(path).generic_string();
		return text.front() == '/' ? "file://" + text : "file:///" + text;
	}

	// Waits (a few seconds at most) for the checker's background work to end.
	UpdateChecker::State WaitFor(const UpdateChecker& checker)
	{
		for (int i = 0; i < 500 && checker.IsBusy(); i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		return checker.GetState();
	}

	std::string Manifest(const std::string& version, const std::string& installerUrl, uint64_t size, const std::string& sha256)
	{
		return "{ \"name\": \"MageCap Bellows\", \"version\": \"" + version + "\", \"published\": \"2026-12-04\", "
			"\"notes\": \"## New\\n- Faster\", \"release_url\": \"https://magecapgames.com/bellows\", "
			"\"installer\": { \"url\": \"" + installerUrl + "\", \"size\": " + std::to_string(size) + ", \"sha256\": \"" + sha256 + "\" } }";
	}

}

TEST_CASE("SHA-256 matches the standard test vectors")
{
	CHECK(Sha256::Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(Sha256::Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(Sha256::Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
		== "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

	// A million 'a's, fed in uneven pieces that cross the 64-byte blocks.
	Sha256 hasher;
	const std::string piece(997, 'a');
	size_t left = 1000000;
	while (left > 0)
	{
		const size_t take = std::min(left, piece.size());
		hasher.Update(piece.data(), take);
		left -= take;
	}
	CHECK(Sha256::ToHex(hasher.Finish()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

	// Lengths around the padding boundary (55, 56 and 64 bytes) agree with hashing a file of them.
	const std::filesystem::path path = BellowsTests::GetTestOutputDirectory() / "Updates" / "hash.bin";
	for (size_t length : { 55, 56, 63, 64, 65, 200000 })
	{
		const std::string data(length, 'x');
		WriteBytes(path, data);
		CHECK(Sha256::HashFile(path) == Sha256::Hex(data));
	}
	CHECK_FALSE(Sha256::HashFile(path.parent_path() / "missing.bin").has_value());
}

TEST_CASE("Update versions compare by number, with pre-releases before their release")
{
	using UpdateLogic::CompareVersions;
	CHECK(CompareVersions("0.7.0", "0.7.0") == 0);
	CHECK(CompareVersions("0.8.0", "0.7.9") == 1);
	CHECK(CompareVersions("0.7.10", "0.7.9") == 1);				// Numbers, not text
	CHECK(CompareVersions("1.0", "0.99.99") == 1);
	CHECK(CompareVersions("v0.7.0", "0.7.0") == 0);
	CHECK(CompareVersions("0.7.0-dev", "0.7.0") == -1);
	CHECK(CompareVersions("0.7.0-dev (3f8b735)", "0.7.0-dev (91ec5ff)") == 0);	// Commits don't count
	CHECK(CompareVersions("0.7.0-rc2", "0.7.0-rc1") == 1);
	CHECK_FALSE(CompareVersions("seven", "0.7.0").has_value());
	CHECK_FALSE(CompareVersions("0.7.0.1", "0.7.0").has_value());
	CHECK_FALSE(CompareVersions("0.7.", "0.7.0").has_value());
	CHECK_FALSE(CompareVersions("0.7.0-", "0.7.0").has_value());

	// A dev build of the next version is newer than the last release; the release replaces it.
	CHECK(UpdateLogic::IsNewer("0.7.0", "0.7.0-dev (04265ee)"));
	CHECK_FALSE(UpdateLogic::IsNewer("0.6.0", "0.7.0-dev (04265ee)"));
	CHECK_FALSE(UpdateLogic::IsNewer("0.7.0", "0.7.0"));
	CHECK_FALSE(UpdateLogic::IsNewer("garbage", "0.7.0"));
}

TEST_CASE("Update manifests are checked field by field")
{
	const std::string digest(64, 'a');
	std::string error;

	std::optional<UpdateManifest> manifest = UpdateLogic::ParseManifest(
		Manifest("0.8.0", "download/v0.8.0/MageCap-Bellows-0.8.0-Windows-Setup.exe", 1234, std::string(64, 'A')),
		"https://updates.magecapgames.com/bellows/update.json", error);
	REQUIRE_MESSAGE(manifest.has_value(), error);
	CHECK(manifest->Version == "0.8.0");
	CHECK(manifest->Published == "2026-12-04");
	CHECK(manifest->Notes == "## New\n- Faster");
	CHECK(manifest->ReleaseUrl == "https://magecapgames.com/bellows");
	CHECK(manifest->Installer.Url == "https://updates.magecapgames.com/bellows/download/v0.8.0/MageCap-Bellows-0.8.0-Windows-Setup.exe");
	CHECK(manifest->Installer.Size == 1234);
	CHECK(manifest->Installer.Sha256 == digest);				// Lowercased
	CHECK_FALSE(manifest->Portable.has_value());

	const std::string base = "https://updates.magecapgames.com/bellows/update.json";
	auto fails = [&](const std::string& text)
	{
		std::string reason;
		const bool failed = !UpdateLogic::ParseManifest(text, base, reason).has_value();
		CHECK_FALSE(reason.empty());
		return failed;
	};
	CHECK(fails("not json"));
	CHECK(fails("[1, 2]"));
	CHECK(fails(Manifest("soon", "a.exe", 10, digest)));
	CHECK(fails(Manifest("0.8.0", "", 10, digest)));
	CHECK(fails(Manifest("0.8.0", "http://example.com/a.exe", 10, digest)));		// Not https
	CHECK(fails(Manifest("0.8.0", "file:///C:/a.exe", 10, digest)));				// A web manifest can't point at a local file
	CHECK(fails(Manifest("0.8.0", "a.exe", 0, digest)));
	CHECK(fails(Manifest("0.8.0", "a.exe", 10, "abc")));
	CHECK(fails(Manifest("0.8.0", "a.exe", 10, std::string(64, 'g'))));
	CHECK(fails("{ \"version\": \"0.8.0\" }"));
	CHECK(fails("{ \"version\": \"0.8.0\", \"installer\": { \"url\": \"a.exe\", \"size\": -5, \"sha256\": \"" + digest + "\" } }"));
	CHECK(fails("{ \"version\": \"0.8.0\", \"installer\": { \"url\": \"a.exe\", \"size\": 10, \"sha256\": \"" + digest + "\" }, \"portable\": 5 }"));

	// A release page that isn't https is dropped rather than failing the whole manifest.
	std::string text = Manifest("0.8.0", "a.exe", 10, digest);
	text.replace(text.find("https://magecapgames.com/bellows"), 32, "javascript:alert(1)");
	manifest = UpdateLogic::ParseManifest(text, base, error);
	REQUIRE(manifest.has_value());
	CHECK(manifest->ReleaseUrl.empty());
}

TEST_CASE("Update addresses resolve against the manifest's address")
{
	using UpdateLogic::ResolveUrl;
	const std::string base = "https://updates.magecapgames.com/bellows/update.json?x=1";
	CHECK(ResolveUrl(base, "https://example.com/a.exe") == "https://example.com/a.exe");
	CHECK(ResolveUrl(base, "download/a.exe") == "https://updates.magecapgames.com/bellows/download/a.exe");
	CHECK(ResolveUrl(base, "/other/a.exe") == "https://updates.magecapgames.com/other/a.exe");
	CHECK(ResolveUrl("https://updates.magecapgames.com", "a.exe") == "https://updates.magecapgames.com/a.exe");
	CHECK(ResolveUrl("file:///tmp/updates/update.json", "a.exe") == "file:///tmp/updates/a.exe");

	CHECK(UpdateLogic::FileNameFromUrl("https://x.com/d/v0.8.0/MageCap-Bellows-0.8.0-Windows-Setup.exe?t=1", "f.exe") == "MageCap-Bellows-0.8.0-Windows-Setup.exe");
	CHECK(UpdateLogic::FileNameFromUrl("https://x.com/d/..", "f.exe") == "f.exe");
	CHECK(UpdateLogic::FileNameFromUrl("https://x.com/d/", "f.exe") == "f.exe");
	CHECK(UpdateLogic::FileNameFromUrl("https://x.com/a%2F..%2Fb.exe", "f.exe") == "f.exe");
	CHECK(Http::FileUrlToPath("file:///C:/Updates/a.exe") == std::filesystem::path("C:/Updates/a.exe"));
	CHECK(Http::FileUrlToPath("file:///tmp/a.exe") == std::filesystem::path("/tmp/a.exe"));
	CHECK(Http::FileUrlToPath("https://x.com/a.exe").empty());
}

TEST_CASE("Automatic update checks run once a day and respect a skipped version")
{
	const int64_t now = 1'800'000'000;
	CHECK(UpdateLogic::MeansNoRelease(404));
	CHECK_FALSE(UpdateLogic::MeansNoRelease(500));
	CHECK_FALSE(UpdateLogic::MeansNoRelease(0));		// No answer at all is a failure
	CHECK(UpdateLogic::IsCheckDue(true, 0, now));
	CHECK_FALSE(UpdateLogic::IsCheckDue(false, 0, now));
	CHECK_FALSE(UpdateLogic::IsCheckDue(true, now - 3600, now));
	CHECK(UpdateLogic::IsCheckDue(true, now - UpdateLogic::c_CheckInterval, now));
	CHECK(UpdateLogic::IsCheckDue(true, now + 3600, now));			// The clock went back

	UpdateManifest manifest;
	manifest.Version = "0.8.0";
	CHECK(UpdateLogic::ShouldOffer(manifest, "0.7.0", "", false));
	CHECK_FALSE(UpdateLogic::ShouldOffer(manifest, "0.8.0", "", true));
	CHECK_FALSE(UpdateLogic::ShouldOffer(manifest, "0.7.0", "0.8.0", false));	// Skipped
	CHECK(UpdateLogic::ShouldOffer(manifest, "0.7.0", "0.8.0", true));			// Asked for
	CHECK(UpdateLogic::ShouldOffer(manifest, "0.7.0", "0.7.5", false));			// Newer than the skipped one

	const std::vector<std::string> arguments = UpdateLogic::InstallerArguments();
	CHECK(std::find(arguments.begin(), arguments.end(), "/SILENT") != arguments.end());
	CHECK(std::find(arguments.begin(), arguments.end(), "/UPDATE") != arguments.end());

	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "Updates" / "Installed";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	CHECK_FALSE(UpdateLogic::IsInstalledCopy(folder));
	WriteBytes(folder / "unins000.exe", "x");
	CHECK(UpdateLogic::IsInstalledCopy(folder));
}

TEST_CASE("The update checker finds, downloads and verifies an update from a folder")
{
	const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "Updates" / "Server (Ron's)";
	std::filesystem::remove_all(folder);
	const std::string installer(300000, 'S');
	WriteBytes(folder / "MageCap-Bellows-0.8.0-Windows-Setup.exe", installer);
	const std::string manifestUrl = FileUrl(folder / "update.json");

	SUBCASE("A newer version downloads and passes its check")
	{
		WriteBytes(folder / "update.json", Manifest("0.8.0", "MageCap-Bellows-0.8.0-Windows-Setup.exe", installer.size(), Sha256::Hex(installer)));
		UpdateChecker checker;
		CHECK(checker.GetState() == UpdateChecker::State::Idle);
		REQUIRE(checker.StartCheck(manifestUrl, "0.7.0", true));
		CHECK_FALSE(checker.StartCheck(manifestUrl, "0.7.0", true));		// One at a time
		REQUIRE(WaitFor(checker) == UpdateChecker::State::Available);
		CHECK(checker.IsManual());
		REQUIRE(checker.GetManifest().has_value());
		CHECK(checker.GetManifest()->Version == "0.8.0");
		CHECK_FALSE(checker.StartDownload(folder / "Downloads", true));	// No zip in this manifest

		REQUIRE(checker.StartDownload(folder / "Downloads", false));
		REQUIRE_MESSAGE(WaitFor(checker) == UpdateChecker::State::Downloaded, checker.GetError());
		const std::filesystem::path downloaded = checker.GetDownloadedFile();
		CHECK(downloaded.filename() == "MageCap-Bellows-0.8.0-Windows-Setup.exe");
		CHECK(Sha256::HashFile(downloaded) == Sha256::Hex(installer));
		CHECK(checker.GetProgress().first == installer.size());
		CHECK_FALSE(std::filesystem::exists(downloaded.string() + ".part"));
	}

	SUBCASE("The same version is up to date")
	{
		WriteBytes(folder / "update.json", Manifest("0.8.0", "MageCap-Bellows-0.8.0-Windows-Setup.exe", installer.size(), Sha256::Hex(installer)));
		UpdateChecker checker;
		REQUIRE(checker.StartCheck(manifestUrl, "0.8.0", false));
		CHECK(WaitFor(checker) == UpdateChecker::State::UpToDate);
		CHECK_FALSE(checker.StartDownload(folder / "Downloads", false));
	}

	SUBCASE("A file that doesn't match its digest is thrown away")
	{
		WriteBytes(folder / "update.json", Manifest("0.8.0", "MageCap-Bellows-0.8.0-Windows-Setup.exe", installer.size(), Sha256::Hex("something else")));
		UpdateChecker checker;
		REQUIRE(checker.StartCheck(manifestUrl, "0.7.0", false));
		REQUIRE(WaitFor(checker) == UpdateChecker::State::Available);
		REQUIRE(checker.StartDownload(folder / "Downloads", false));
		CHECK(WaitFor(checker) == UpdateChecker::State::Failed);
		CHECK(checker.GetError().find("SHA-256") != std::string::npos);
		CHECK_FALSE(std::filesystem::exists(folder / "Downloads" / "MageCap-Bellows-0.8.0-Windows-Setup.exe"));
		// It can be tried again from the failure.
		CHECK(checker.StartDownload(folder / "Downloads", false));
		WaitFor(checker);
	}

	SUBCASE("A file bigger than the manifest says is refused")
	{
		WriteBytes(folder / "update.json", Manifest("0.8.0", "MageCap-Bellows-0.8.0-Windows-Setup.exe", 1000, Sha256::Hex(installer)));
		UpdateChecker checker;
		REQUIRE(checker.StartCheck(manifestUrl, "0.7.0", false));
		REQUIRE(WaitFor(checker) == UpdateChecker::State::Available);
		REQUIRE(checker.StartDownload(folder / "Downloads", false));
		CHECK(WaitFor(checker) == UpdateChecker::State::Failed);
		CHECK_FALSE(std::filesystem::exists(folder / "Downloads" / "MageCap-Bellows-0.8.0-Windows-Setup.exe.part"));
	}

	SUBCASE("A missing or broken manifest fails with a reason")
	{
		UpdateChecker checker;
		REQUIRE(checker.StartCheck(FileUrl(folder / "missing.json"), "0.7.0", false));
		CHECK(WaitFor(checker) == UpdateChecker::State::Failed);
		CHECK_FALSE(checker.GetError().empty());

		WriteBytes(folder / "update.json", "{ broken");
		REQUIRE(checker.StartCheck(manifestUrl, "0.7.0", false));
		CHECK(WaitFor(checker) == UpdateChecker::State::Failed);
		CHECK(checker.GetError().find("JSON") != std::string::npos);
	}

	SUBCASE("Plain http and unknown schemes are refused")
	{
		CHECK_FALSE(Http::Get("http://updates.magecapgames.com/bellows/update.json", 100).Ok());
		CHECK_FALSE(Http::Get("ftp://updates.magecapgames.com/update.json", 100).Ok());
		std::string error;
		CHECK_FALSE(Http::Download("http://example.com/a.exe", folder / "a.exe", 100, nullptr, error));
		CHECK_FALSE(error.empty());
	}
}

TEST_CASE("A detached program keeps running and a missing one fails to start")
{
	CHECK_FALSE(Process::StartDetached({ BellowsTests::GetTestOutputDirectory() / "no-such-installer" }));
#if !defined(BW_PLATFORM_WINDOWS)
	// /bin/sh writes a file after this call returns, so it ran on its own.
	const std::filesystem::path marker = BellowsTests::GetTestOutputDirectory() / "Updates" / "detached.txt";
	std::filesystem::create_directories(marker.parent_path());
	std::filesystem::remove(marker);
	REQUIRE(Process::StartDetached({ "/bin/sh", { "-c", "sleep 0.2; echo ran > \"$0\"", marker.string() } }));
	for (int i = 0; i < 300 && !std::filesystem::exists(marker); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK(std::filesystem::exists(marker));
#endif
}
