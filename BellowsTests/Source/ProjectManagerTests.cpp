#include <doctest/doctest.h>

#include "EditorSettings.h"
#include "ProjectList.h"
#include "TestUtilities.h"

#include "Bellows/Core/BuildInfo.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Core/Platform.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <algorithm>

using namespace Bellows;

namespace {

	std::filesystem::path MakeFolder(const std::string& name)
	{
		const std::filesystem::path folder = BellowsTests::GetTestOutputDirectory() / "ProjectManager" / name;
		std::error_code error;
		std::filesystem::remove_all(folder, error);
		std::filesystem::create_directories(folder);
		return folder;
	}

	ProjectListItem MakeItem(const std::string& name, const std::string& path, int64_t lastOpened, bool favorite = false)
	{
		ProjectListItem item;
		item.Name = name;
		item.Entry.Path = path;
		item.Entry.LastOpened = lastOpened;
		item.Entry.Favorite = favorite;
		return item;
	}

	// Points the editor's configuration folder somewhere else for the duration of a test.
	class ScopedConfigDirectory
	{
	public:
		explicit ScopedConfigDirectory(const std::filesystem::path& directory)
			: m_Previous(Platform::GetEnvVar("BELLOWS_CONFIG_DIR"))
		{
			Platform::SetEnvVar("BELLOWS_CONFIG_DIR", directory.string());
		}
		~ScopedConfigDirectory() { Platform::SetEnvVar("BELLOWS_CONFIG_DIR", m_Previous); }
	private:
		std::string m_Previous;
	};

}

TEST_CASE("Last opened times read like Godot's project list")
{
	const int64_t now = 1'800'000'000;
	CHECK(FormatTimeAgo(0, now) == "never");
	CHECK(FormatTimeAgo(now - 20, now) == "just now");
	CHECK(FormatTimeAgo(now + 50, now) == "just now");	// A clock that moved backwards
	CHECK(FormatTimeAgo(now - 60, now) == "1 minute ago");
	CHECK(FormatTimeAgo(now - 5 * 60, now) == "5 minutes ago");
	CHECK(FormatTimeAgo(now - 3 * 3600, now) == "3 hours ago");
	CHECK(FormatTimeAgo(now - 30 * 3600, now) == "yesterday");
	CHECK(FormatTimeAgo(now - 3 * 86400, now) == "3 days ago");
	CHECK(FormatTimeAgo(now - 14 * 86400, now) == "2 weeks ago");
	CHECK(FormatTimeAgo(now - 65 * 86400, now) == "2 months ago");
	CHECK(FormatTimeAgo(now - 800 * 86400, now) == "2 years ago");
}

TEST_CASE("The project list puts favorites first, then sorts and filters")
{
	const std::vector<ProjectListItem> items = {
		MakeItem("Zeta", "/games/zeta/Zeta.bproject", 100),
		MakeItem("alpha", "/games/alpha/alpha.bproject", 300),
		MakeItem("Middle", "/work/middle/Middle.bproject", 200, true),
		MakeItem("Never", "/games/never/Never.bproject", 0),
	};

	const auto byTime = SortProjects(items, ProjectSort::LastOpened, "");
	REQUIRE(byTime.size() == 4);
	CHECK(byTime[0].Name == "Middle");
	CHECK(byTime[1].Name == "alpha");
	CHECK(byTime[2].Name == "Zeta");
	CHECK(byTime[3].Name == "Never");

	const auto byName = SortProjects(items, ProjectSort::Name, "");
	CHECK(byName[0].Name == "Middle");
	CHECK(byName[1].Name == "alpha");	// Case-insensitive
	CHECK(byName[2].Name == "Never");
	CHECK(byName[3].Name == "Zeta");

	const auto byPath = SortProjects(items, ProjectSort::Path, "");
	CHECK(byPath[1].Name == "alpha");
	CHECK(byPath[3].Name == "Zeta");

	const auto filtered = SortProjects(items, ProjectSort::Name, "GAMES/Z");
	REQUIRE(filtered.size() == 1);
	CHECK(filtered[0].Name == "Zeta");
	CHECK(SortProjects(items, ProjectSort::Name, "alp").size() == 1);
	CHECK(SortProjects(items, ProjectSort::Name, "nothing like this").empty());
}

TEST_CASE("Project names come from the project file")
{
	const std::filesystem::path folder = MakeFolder("Names");
	REQUIRE(FileSystem::WriteTextFile(folder / "space_game.bproject", R"({ "Name": "Space Game" })"));
	REQUIRE(FileSystem::WriteTextFile(folder / "broken.bproject", "not json"));

	CHECK(ReadProjectName(folder / "space_game.bproject") == "Space Game");
	CHECK(ReadProjectName(folder / "broken.bproject") == "broken");
	CHECK(ReadProjectName(folder / "missing.bproject").empty());
}

TEST_CASE("Scanning finds projects in subfolders but not inside project data")
{
	const std::filesystem::path root = MakeFolder("Scan");
	REQUIRE(FileSystem::WriteTextFile(root / "One" / "One.bproject", "{}"));
	REQUIRE(FileSystem::WriteTextFile(root / "Group" / "Two" / "Two.bproject", "{}"));
	REQUIRE(FileSystem::WriteTextFile(root / "One" / "Assets" / "Copy" / "Copy.bproject", "{}"));
	REQUIRE(FileSystem::WriteTextFile(root / ".hidden" / "Hidden.bproject", "{}"));
	REQUIRE(FileSystem::WriteTextFile(root / "a" / "b" / "c" / "d" / "e" / "Deep.bproject", "{}"));
	REQUIRE(FileSystem::WriteTextFile(root / "One" / "notes.txt", "hello"));

	const std::vector<std::filesystem::path> found = FindProjects(root);
	std::vector<std::string> names;
	for (const std::filesystem::path& path : found)
		names.push_back(path.filename().string());
	std::sort(names.begin(), names.end());
	REQUIRE(names.size() == 2);
	CHECK(names[0] == "One.bproject");
	CHECK(names[1] == "Two.bproject");

	CHECK(FindProjects(root, 0).empty());
	CHECK(FindProjects(root / "does-not-exist").empty());
}

TEST_CASE("The New Project dialog explains where the project will go")
{
	const std::filesystem::path root = std::filesystem::absolute(MakeFolder("NewProject"));

	SUBCASE("A name and a full path are required")
	{
		CHECK(CheckNewProjectLocation("", root, true).Status == NewProjectCheck::Level::Error);
		CHECK(CheckNewProjectLocation("   ", root, true).Status == NewProjectCheck::Level::Error);
		CHECK(CheckNewProjectLocation("Game", "", true).Status == NewProjectCheck::Level::Error);
		CHECK(CheckNewProjectLocation("Game", "relative/folder", true).Status == NewProjectCheck::Level::Error);
	}

	SUBCASE("Windows' device names are refused")
	{
		for (const char* name : { "CON", "con", "Nul", "COM1", "lpt9", "AUX.game", "PRN " })
		{
			const NewProjectCheck check = CheckNewProjectLocation(name, root, true);
			CHECK_MESSAGE(check.Status == NewProjectCheck::Level::Error, name);
			CHECK(check.Message.find("device name") != std::string::npos);
		}
		CHECK(CheckNewProjectLocation("Console", root, true).Status == NewProjectCheck::Level::Ok);
		CHECK(CheckNewProjectLocation("COM10", root, true).Status == NewProjectCheck::Level::Ok);
	}

	SUBCASE("A new folder is created for the project")
	{
		const NewProjectCheck check = CheckNewProjectLocation("  My Game  ", root, true);
		CHECK(check.Status == NewProjectCheck::Level::Ok);
		CHECK(check.ProjectFolder.parent_path() == root);
		CHECK_FALSE(check.ProjectFolder.filename().string().empty());
		CHECK(check.ProjectFolder.filename().string().find(' ') != 0);
	}

	SUBCASE("An empty folder is fine, a full one is a warning, a project folder is refused")
	{
		std::filesystem::create_directories(root / "Empty");
		CHECK(CheckNewProjectLocation("Game", root / "Empty", false).Status == NewProjectCheck::Level::Ok);

		REQUIRE(FileSystem::WriteTextFile(root / "Full" / "readme.txt", "hi"));
		const NewProjectCheck full = CheckNewProjectLocation("Game", root / "Full", false);
		CHECK(full.Status == NewProjectCheck::Level::Warning);
		CHECK(full.ProjectFolder == root / "Full");

		REQUIRE(FileSystem::WriteTextFile(root / "Taken" / "Taken.bproject", "{}"));
		CHECK(CheckNewProjectLocation("Game", root / "Taken", false).Status == NewProjectCheck::Level::Error);

		REQUIRE(FileSystem::WriteTextFile(root / "AFile", "x"));
		CHECK(CheckNewProjectLocation("Game", root / "AFile", false).Status == NewProjectCheck::Level::Error);
	}
}

TEST_CASE("Editor settings remember projects, favorites and panels")
{
	SUBCASE("Recent projects are ordered by when they were opened")
	{
		EditorSettings settings;
		settings.AddRecentProject("/a.bproject", 10);
		settings.AddRecentProject("/b.bproject", 20);
		settings.AddRecentProject("/a.bproject", 30);	// Opened again: moves up, no duplicate
		ProjectEntry scanned;
		scanned.Path = "/scanned.bproject";				// Found by a scan, never opened
		settings.Projects.push_back(scanned);

		CHECK(settings.Projects.size() == 3);
		const std::vector<std::string> recent = settings.GetRecentProjects(12);
		REQUIRE(recent.size() == 2);
		CHECK(recent[0] == "/a.bproject");
		CHECK(recent[1] == "/b.bproject");
		CHECK(settings.GetRecentProjects(1).size() == 1);

		CHECK(settings.RemoveProject("/b.bproject"));
		CHECK_FALSE(settings.RemoveProject("/b.bproject"));
		CHECK(settings.FindProject("/b.bproject") == nullptr);
	}

	SUBCASE("The list is capped, and favorites are never dropped")
	{
		EditorSettings settings;
		settings.AddRecentProject("/favorite.bproject", 1);
		settings.FindProject("/favorite.bproject")->Favorite = true;
		for (size_t i = 0; i < EditorSettings::MaxProjects + 5; i++)
			settings.AddRecentProject("/p" + std::to_string(i) + ".bproject", 100 + static_cast<int64_t>(i));
		CHECK(settings.Projects.size() == EditorSettings::MaxProjects);
		CHECK(settings.FindProject("/favorite.bproject") != nullptr);
		CHECK(settings.FindProject("/p0.bproject") == nullptr);
	}

	SUBCASE("Settings survive a save and load")
	{
		const std::filesystem::path folder = MakeFolder("Settings");
		ScopedConfigDirectory config(folder);

		EditorSettings settings;
		settings.AddRecentProject("/games/one/One.bproject", 500);
		settings.FindProject("/games/one/One.bproject")->Favorite = true;
		settings.Panels.Console = false;
		settings.ShowContentFolderTree = false;
		settings.LastProjectLocation = "/games";
		CHECK(settings.PlayInGameWindow);	// Play opens a game window unless the user picks the viewport
		settings.PlayInGameWindow = false;
		CHECK(settings.ShowUI);
		settings.ShowUI = false;
		settings.UIView = true;
		settings.UIViewWidth = 1280;
		settings.UIViewHeight = 99999;
		CHECK_FALSE(settings.Snap);
		CHECK(settings.SelectionGlow);
		settings.Snap = true;
		settings.TranslateSnap = 0.25f;
		settings.RotateSnap = 0.0f;			// Held to its smallest step when loaded
		settings.ScaleSnap = 0.5f;
		settings.SelectionGlow = false;
		REQUIRE(settings.Save());

		const EditorSettings loaded = EditorSettings::Load();
		REQUIRE(loaded.Projects.size() == 1);
		CHECK(loaded.Projects[0].Path == "/games/one/One.bproject");
		CHECK(loaded.Projects[0].LastOpened == 500);
		CHECK(loaded.Projects[0].Favorite);
		CHECK_FALSE(loaded.Panels.Console);
		CHECK(loaded.Panels.Hierarchy);
		CHECK_FALSE(loaded.ShowContentFolderTree);
		CHECK(loaded.LastProjectLocation == "/games");
		CHECK_FALSE(loaded.PlayInGameWindow);
		CHECK_FALSE(loaded.ShowUI);
		CHECK(loaded.UIView);
		CHECK(loaded.UIViewWidth == 1280);
		CHECK(loaded.UIViewHeight == 8192);	// Kept to a size the renderer can make
		CHECK(loaded.Snap);
		CHECK(loaded.TranslateSnap == 0.25f);
		CHECK(loaded.RotateSnap == 0.1f);
		CHECK(loaded.ScaleSnap == 0.5f);
		CHECK_FALSE(loaded.SelectionGlow);
	}

	SUBCASE("Settings from before the Project Manager keep their recent projects")
	{
		const std::filesystem::path folder = MakeFolder("OldSettings");
		ScopedConfigDirectory config(folder);
		REQUIRE(FileSystem::WriteTextFile(folder / "EditorSettings.json", R"({ "RecentProjects": ["/new.bproject", "/old.bproject"], "ShowGrid": false })"));

		const EditorSettings loaded = EditorSettings::Load();
		const std::vector<std::string> recent = loaded.GetRecentProjects(12);
		REQUIRE(recent.size() == 2);
		CHECK(recent[0] == "/new.bproject");
		CHECK(recent[1] == "/old.bproject");
		CHECK_FALSE(loaded.ShowGrid);
	}
}

TEST_CASE("Install.json tells other tools where this editor is")
{
	const std::filesystem::path config = MakeFolder("InstallInfo");
	ScopedConfigDirectory scoped(config);
	const std::filesystem::path bin = MakeFolder("InstallBin");
	const std::string extension = Platform::GetExecutableExtension();
	REQUIRE(FileSystem::WriteTextFile(bin / ("forgescript" + extension), "tool"));
	REQUIRE(FileSystem::WriteTextFile(bin / "Docs" / "ScriptingAPI.md", "# API"));

	REQUIRE(EditorSettings::WriteInstallInfo(bin));
	const std::optional<std::string> text = FileSystem::ReadTextFile(config / "Install.json");
	REQUIRE(text);
	const Json json = Json::parse(*text);
	const std::filesystem::path absoluteBin = std::filesystem::absolute(bin).lexically_normal();
	CHECK(json["EngineVersion"] == BW_VERSION);
	CHECK(json["EngineCommit"] == BuildInfo::GetCommit());
	CHECK(json["ForgeScriptVersion"] == FORGESCRIPT_VERSION);
	CHECK(json["EditorPath"] == (absoluteBin / ("BellowsEditor" + extension)).string());
	CHECK(json["ForgeScriptPath"] == (absoluteBin / ("forgescript" + extension)).string());
	CHECK(json["DocsDirectory"] == (absoluteBin / "Docs").string());

	// Unchanged information is not rewritten.
	const auto writeTime = std::filesystem::last_write_time(config / "Install.json");
	REQUIRE(EditorSettings::WriteInstallInfo(bin));
	CHECK(std::filesystem::last_write_time(config / "Install.json") == writeTime);
}
