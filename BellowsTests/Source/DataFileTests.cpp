#include <doctest/doctest.h>

#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scripting/DataFiles.h"

#include <chrono>
#include <fstream>
#include <string>

using namespace Bellows;
using namespace Bellows::Scripting;

namespace {

	// A temporary Assets folder for the test, put back afterwards.
	struct DataFolder
	{
		std::filesystem::path Root = BellowsTests::GetTestOutputDirectory() / "DataFiles";
		std::filesystem::path Assets = Root / "Assets";
		std::filesystem::path Previous = AssetManager::GetAssetDirectory();

		DataFolder()
		{
			std::error_code error;
			std::filesystem::remove_all(Root, error);
			std::filesystem::create_directories(Assets / "Data");
			AssetManager::SetAssetDirectory(Assets);
			ClearDataFileCache();
		}

		~DataFolder()
		{
			AssetManager::SetAssetDirectory(Previous);
			ClearDataFileCache();
		}

		void Write(const std::string& relative, const std::string& text)
		{
			REQUIRE(FileSystem::WriteTextFile(Assets / relative, text));
		}
	};

	std::string ErrorOf(const std::function<void()>& action)
	{
		try
		{
			action();
		}
		catch (const ScriptValueError& error)
		{
			return error.what();
		}
		return "";
	}

}

TEST_CASE("Data files are read from the Assets folder as JSON or text")
{
	DataFolder folder;
	folder.Write("Data/items.json", R"({ "sword": { "damage": 7, "tags": ["sharp", "metal"] }, "potion": null, "count": 2, "rare": true })");
	folder.Write("Data/intro.txt", "\xEF\xBB\xBFOnce upon a time\nThe end\n");

	const ScriptValue items = LoadDataJson("Data/items.json");
	const ScriptValueMap* map = std::get_if<ScriptValueMap>(&items.Data);
	REQUIRE(map);
	REQUIRE(map->size() == 4);
	CHECK((*map)[0].first == "sword");
	CHECK((*map)[1].first == "potion");
	CHECK((*map)[1].second.IsNothing());
	CHECK((*map)[2].second == ScriptValue(2.0));
	CHECK((*map)[3].second == ScriptValue(true));
	const ScriptValueMap& sword = std::get<ScriptValueMap>((*map)[0].second.Data);
	CHECK(sword[0].second == ScriptValue(7.0));
	CHECK(sword[1].second == ScriptValue(ScriptValueList { std::string("sharp"), std::string("metal") }));

	// Text loses its byte order mark; \ works as a separator too.
	CHECK(LoadDataText("Data/intro.txt") == "Once upon a time\nThe end\n");
	CHECK(LoadDataText("Data\\intro.txt") == "Once upon a time\nThe end\n");
}

TEST_CASE("A data file is read again only when it changes")
{
	DataFolder folder;
	folder.Write("Data/level.json", "[1, 2]");
	CHECK(LoadDataJson("Data/level.json") == ScriptValue(ScriptValueList { 1.0, 2.0 }));
	folder.Write("Data/level.json", "[3, 4, 5]");
	// Make sure the change shows even on a file system with coarse timestamps.
	std::filesystem::last_write_time(folder.Assets / "Data/level.json", std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
	CHECK(LoadDataJson("Data/level.json") == ScriptValue(ScriptValueList { 3.0, 4.0, 5.0 }));
}

TEST_CASE("Values read from JSON count toward the data file cache's size")
{
	DataFolder folder;
	// About 1 MB of text that reads as 300,000 values, each far bigger than its 3 characters.
	std::string text = "[";
	for (int i = 0; i < 300000; i++)
		text += i == 0 ? "1" : ",1 ";
	text += "]";
	for (int i = 0; i < 6; i++)
		folder.Write("Data/big" + std::to_string(i) + ".json", text);

	CHECK(LoadDataText("Data/big0.json").size() == text.size());
	CHECK(GetDataFileCacheBytes() == text.size());
	const ScriptValue first = LoadDataJson("Data/big0.json");
	REQUIRE(std::get_if<ScriptValueList>(&first.Data));
	CHECK(std::get<ScriptValueList>(first.Data).size() == 300000);
	CHECK(GetDataFileCacheBytes() > text.size() + 300000 * sizeof(ScriptValue) - 1);

	// More of them than fit: the cache makes room and never grows past its limit.
	for (int round = 0; round < 2; round++)
	{
		for (int i = 0; i < 6; i++)
		{
			const ScriptValue value = LoadDataJson("Data/big" + std::to_string(i) + ".json");
			CHECK(std::get<ScriptValueList>(value.Data).size() == 300000);
			CHECK(GetDataFileCacheBytes() <= MaxDataFileCacheBytes);
		}
	}
	ClearDataFileCache();
	CHECK(GetDataFileCacheBytes() == 0);
}

TEST_CASE("A data file too big to keep doesn't push the others out of memory")
{
	DataFolder folder;
	folder.Write("Data/small.json", "[1, 2, 3]");
	// About 4 MB of text that reads as 2,000,000 values: more than the cache holds on its own.
	std::string text = "[";
	text.reserve(4'000'010);
	for (int i = 0; i < 2'000'000; i++)
		text += i == 0 ? "1" : ",1";
	text += "]";
	folder.Write("Data/huge.json", text);

	CHECK(LoadDataJson("Data/small.json") == ScriptValue(ScriptValueList { 1.0, 2.0, 3.0 }));
	const uintmax_t withSmall = GetDataFileCacheBytes();
	for (int read = 0; read < 2; read++)
	{
		const ScriptValue huge = LoadDataJson("Data/huge.json");
		CHECK(std::get<ScriptValueList>(huge.Data).size() == 2'000'000);
		// The small file is still kept (with the huge file's text, which fits), and the cap holds.
		CHECK(GetDataFileCacheBytes() == withSmall + text.size());
		CHECK(GetDataFileCacheBytes() <= MaxDataFileCacheBytes);
	}
}

TEST_CASE("Data files outside the Assets folder, too big or broken are refused with the reason")
{
	DataFolder folder;
	FileSystem::WriteTextFile(folder.Root / "secret.txt", "outside");

	CHECK(ErrorOf([] { LoadDataText(""); }).find("needs a path inside the Assets folder") != std::string::npos);
	CHECK(ErrorOf([&] { LoadDataText((folder.Root / "secret.txt").string()); }).find("is a full path") != std::string::npos);
	CHECK(ErrorOf([] { LoadDataText("../secret.txt"); }).find("leaves the Assets folder ('..')") != std::string::npos);
	CHECK(ErrorOf([] { LoadDataText("Data/../../secret.txt"); }).find("leaves the Assets folder") != std::string::npos);
	CHECK(ErrorOf([] { LoadDataText("..\\secret.txt"); }).find("leaves the Assets folder") != std::string::npos);
	CHECK(ErrorOf([] { LoadDataText("Data/missing.json"); }) == "There's no data file 'Data/missing.json' in the Assets folder");
	CHECK(ErrorOf([] { LoadDataText("Data"); }) == "'Data' is a folder, not a data file");

	// A link inside Assets that leads out is refused (where the system lets the test make one).
	std::error_code linkError;
	std::filesystem::create_symlink(folder.Root / "secret.txt", folder.Assets / "Data/link.txt", linkError);
	if (!linkError)
		CHECK(ErrorOf([] { LoadDataText("Data/link.txt"); }).find("leads outside the Assets folder") != std::string::npos);

	folder.Write("Data/broken.json", "{\n  \"a\": 1,\n  \"b\": }\n");
	const std::string broken = ErrorOf([] { LoadDataJson("Data/broken.json"); });
	CHECK(broken.find("The data file 'Data/broken.json' isn't valid JSON: line 3") != std::string::npos);

	folder.Write("Data/deep.json", std::string(300, '[') + std::string(300, ']'));
	CHECK(ErrorOf([] { LoadDataJson("Data/deep.json"); }).find("nested more than 256 deep") != std::string::npos);
	folder.Write("Data/fine.json", std::string(200, '[') + std::string(200, ']'));
	CHECK(ErrorOf([] { LoadDataJson("Data/fine.json"); }).empty());

	{
		std::ofstream big(folder.Assets / "Data/big.txt", std::ios::binary);
		big.seekp(static_cast<std::streamoff>(MaxDataFileBytes));
		big.put('x');
	}
	CHECK(ErrorOf([] { LoadDataText("Data/big.txt"); }).find("data files can be at most 16 MB") != std::string::npos);
}
