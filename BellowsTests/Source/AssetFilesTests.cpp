#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "AssetFiles.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"

#include <filesystem>
#include <string>

using namespace Bellows;

namespace {

	// A project's assets: a model with its import settings, a script, and an empty folder.
	struct TestAssets
	{
		std::filesystem::path Root;
		std::filesystem::path Model, Settings, Script, Folder;
		std::string PreviousDataHome;
		std::filesystem::path Trash;

		explicit TestAssets(const char* name)
		{
			const std::filesystem::path base = BellowsTests::GetTestOutputDirectory() / name;
			std::filesystem::remove_all(base);
			Root = base / "Assets";
			Model = Root / "Models" / "Crate.glb";
			Settings = Root / "Models" / "Crate.glb.bimport";
			Script = Root / "Scripts" / "Player.py";
			Folder = Root / "Props";
			REQUIRE(FileSystem::WriteTextFile(Model, "model"));
			REQUIRE(FileSystem::WriteTextFile(Settings, "{ \"FrontFaces\": \"+Z\" }"));
			REQUIRE(FileSystem::WriteTextFile(Script, "print('hi')"));
			REQUIRE(FileSystem::CreateDirectories(Folder));
			AssetManager::SetAssetDirectory(Root);

			// The Trash goes in the test's own folder (freedesktop.org's $XDG_DATA_HOME/Trash).
			PreviousDataHome = Platform::GetEnvVar("XDG_DATA_HOME");
			REQUIRE(Platform::SetEnvVar("XDG_DATA_HOME", (base / "DataHome").string()));
			Trash = base / "DataHome" / "Trash";
		}

		~TestAssets()
		{
			Platform::SetEnvVar("XDG_DATA_HOME", PreviousDataHome);
			AssetManager::SetAssetDirectory({});
		}
	};

}

TEST_CASE("A model's import settings are its companion and stay out of the Content panel")
{
	TestAssets assets("AssetFilesCompanions");
	REQUIRE(AssetFiles::GetCompanionFiles(assets.Model).size() == 1);
	CHECK(AssetFiles::GetCompanionFiles(assets.Model)[0] == assets.Settings);
	CHECK(AssetFiles::GetCompanionFiles(assets.Script).empty());
	CHECK(AssetFiles::GetCompanionFiles(assets.Folder).empty());
	CHECK(AssetFiles::IsCompanionFile(assets.Settings));
	CHECK_FALSE(AssetFiles::IsCompanionFile(assets.Model));
}

TEST_CASE("Renaming and moving a model takes its import settings with it")
{
	TestAssets assets("AssetFilesMove");
	std::string error;

	REQUIRE(AssetFiles::Rename(assets.Model, "Barrel.glb", error));
	const std::filesystem::path renamed = assets.Root / "Models" / "Barrel.glb";
	CHECK(FileSystem::Exists(renamed));
	CHECK(FileSystem::Exists(assets.Root / "Models" / "Barrel.glb.bimport"));
	CHECK_FALSE(FileSystem::Exists(assets.Model));
	CHECK_FALSE(FileSystem::Exists(assets.Settings));

	REQUIRE(AssetFiles::Move(renamed, assets.Folder, error));
	CHECK(FileSystem::Exists(assets.Folder / "Barrel.glb"));
	CHECK(FileSystem::Exists(assets.Folder / "Barrel.glb.bimport"));
	CHECK(FileSystem::ReadTextFile(assets.Folder / "Barrel.glb.bimport") == "{ \"FrontFaces\": \"+Z\" }");

	// A folder moves with everything in it, but never into itself.
	REQUIRE(AssetFiles::Move(assets.Folder, assets.Root / "Scripts", error));
	CHECK(FileSystem::Exists(assets.Root / "Scripts" / "Props" / "Barrel.glb.bimport"));
	CHECK_FALSE(AssetFiles::Move(assets.Root / "Scripts", assets.Root / "Scripts" / "Props", error));
	CHECK(error.find("inside itself") != std::string::npos);
}

TEST_CASE("Renames stay in their folder, and nothing moves out of the assets or over another file")
{
	TestAssets assets("AssetFilesRefused");
	std::string error;
	for (const char* name : { "", ".", "..", "../Crate.glb", "Models/Crate2.glb", "..\\Crate.glb", "C:\\Crate.glb", "/tmp/Crate.glb", "Crate?.glb", "Crate.glb." })
	{
		CAPTURE(name);
		CHECK(AssetFiles::CheckNewName(name));
		CHECK_FALSE(AssetFiles::Rename(assets.Model, name, error));
	}
	CHECK_FALSE(AssetFiles::CheckNewName("Crate (old).glb"));
	CHECK(FileSystem::Exists(assets.Model));
	CHECK(FileSystem::Exists(assets.Settings));

	// Outside the asset folder: moving there, and moving or deleting from there.
	CHECK_FALSE(AssetFiles::Move(assets.Model, assets.Root.parent_path(), error));
	CHECK(error.find("asset folder") != std::string::npos);
	const std::filesystem::path outside = assets.Root.parent_path() / "Outside.txt";
	REQUIRE(FileSystem::WriteTextFile(outside, "not an asset"));
	CHECK_FALSE(AssetFiles::Move(outside, assets.Folder, error));
	CHECK_FALSE(AssetFiles::Delete(outside, error));
	CHECK_FALSE(AssetFiles::Delete(assets.Root, error));
	CHECK(FileSystem::Exists(outside));
	CHECK(FileSystem::Exists(assets.Root));

	// A name already taken, by the asset or by its settings: nothing changes.
	REQUIRE(FileSystem::WriteTextFile(assets.Root / "Models" / "Taken.glb", "other"));
	CHECK_FALSE(AssetFiles::Rename(assets.Model, "Taken.glb", error));
	CHECK(error.find("already exists") != std::string::npos);
	REQUIRE(FileSystem::WriteTextFile(assets.Root / "Models" / "Free.glb.bimport", "stray settings"));
	CHECK_FALSE(AssetFiles::Rename(assets.Model, "Free.glb", error));
	CHECK(FileSystem::Exists(assets.Model));
	CHECK(FileSystem::Exists(assets.Settings));
	CHECK_FALSE(FileSystem::Exists(assets.Root / "Models" / "Free.glb"));
}

TEST_CASE("Deleting an asset moves it and its import settings to the Trash, where they can be restored")
{
	TestAssets assets("AssetFilesDelete");
	std::string error;
	REQUIRE(AssetFiles::Delete(assets.Model, error));
	CHECK(error.empty());
	CHECK_FALSE(FileSystem::Exists(assets.Model));
	CHECK_FALSE(FileSystem::Exists(assets.Settings));

#if !defined(BW_PLATFORM_WINDOWS)
	// freedesktop.org's layout: the file in files/, a note of where it was in info/.
	CHECK(FileSystem::ReadTextFile(assets.Trash / "files" / "Crate.glb") == "model");
	CHECK(FileSystem::Exists(assets.Trash / "files" / "Crate.glb.bimport"));
	const std::optional<std::string> info = FileSystem::ReadTextFile(assets.Trash / "info" / "Crate.glb.trashinfo");
	REQUIRE(info);
	CHECK(info->rfind("[Trash Info]\nPath=/", 0) == 0);
	CHECK(info->find("/Models/Crate.glb\n") != std::string::npos);
	CHECK(info->find("DeletionDate=") != std::string::npos);

	// A second file of the same name gets its own place, and names are percent-encoded.
	REQUIRE(FileSystem::WriteTextFile(assets.Model, "second"));
	REQUIRE(AssetFiles::Delete(assets.Model, error));
	CHECK(FileSystem::ReadTextFile(assets.Trash / "files" / "Crate.glb.2") == "second");
	const std::filesystem::path spaced = assets.Root / "Big Crate.glb";
	REQUIRE(FileSystem::WriteTextFile(spaced, "spaced"));
	REQUIRE(AssetFiles::Delete(spaced, error));
	CHECK(FileSystem::ReadTextFile(assets.Trash / "info" / "Big Crate.glb.trashinfo")->find("/Big%20Crate.glb\n") != std::string::npos);
#endif

	// A folder goes whole.
	REQUIRE(FileSystem::WriteTextFile(assets.Folder / "Inside.txt", "inside"));
	REQUIRE(AssetFiles::Delete(assets.Folder, error));
	CHECK_FALSE(FileSystem::Exists(assets.Folder));
	CHECK_FALSE(Platform::MoveToTrash(assets.Folder, error));
	CHECK(error.find("doesn't exist") != std::string::npos);
}
