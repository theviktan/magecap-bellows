#include <doctest/doctest.h>

#include "ScriptDocument.h"
#include "TestUtilities.h"

#include "Bellows/Asset/AssetManager.h"
#include "Bellows/Core/FileSystem.h"
#include "Bellows/Scripting/ForgeScriptBindings.h"
#include "Bellows/Serialization/JsonUtils.h"

#include <ForgeScript/VM/VM.h>

using namespace Bellows;

namespace {

	bool s_SetupCalled = false;

	void RegisterTestPhrase(ForgeScript::VM& vm)
	{
		s_SetupCalled = true;
		vm.RegisterPhrase("spin (thing) by (amount)", [](ForgeScript::CallContext&) { return ForgeScript::Value(); });
	}

}

TEST_CASE("Script languages come from the file extension")
{
	CHECK(GetScriptLanguage("Scripts/Player.forge") == ScriptLanguage::ForgeScript);
	CHECK(GetScriptLanguage("Scripts/Player.FORGE") == ScriptLanguage::ForgeScript);
	CHECK(GetScriptLanguage("Scripts/player.py") == ScriptLanguage::Python);
	CHECK(GetScriptLanguage("Docs/Guide.md") == ScriptLanguage::None);
	CHECK(GetScriptLanguage("Scripts/forge") == ScriptLanguage::None);

	CHECK(std::string(GetScriptExtension(ScriptLanguage::ForgeScript)) == ".forge");
	CHECK(std::string(GetScriptExtension(ScriptLanguage::Python)) == ".py");
	CHECK(std::string(GetScriptLanguageName(ScriptLanguage::ForgeScript)) == "ForgeScript");

	// Both languages are script assets, so they get the script icon and can be dropped on entities.
	CHECK(AssetManager::GetAssetType("Scripts/Player.forge") == AssetType::Script);
	CHECK(AssetManager::GetAssetType("Scripts/Player.py") == AssetType::Script);
}

TEST_CASE("Script class names are valid identifiers")
{
	CHECK(MakeScriptClassName("player") == "Player");
	CHECK(MakeScriptClassName("main camera 2") == "MainCamera2");
	CHECK(MakeScriptClassName("enemy-spawner_v2") == "EnemySpawnerV2");
	CHECK(MakeScriptClassName("2d hud") == "Script2dHud");
	CHECK(MakeScriptClassName("") == "Script");
	CHECK(MakeScriptClassName("!!!") == "Script");
}

TEST_CASE("New script templates compile in both languages")
{
	for (ScriptTemplate scriptTemplate : { ScriptTemplate::Default, ScriptTemplate::Empty })
	{
		const std::string forge = MakeScriptSource(ScriptLanguage::ForgeScript, scriptTemplate, "Player");
		CHECK(forge.find("when update with delta") != std::string::npos);
		if (scriptTemplate == ScriptTemplate::Default)
			CHECK(forge.find("set speed to 2.0") != std::string::npos);	// A decimal, so the Inspector field takes 2.5
		CHECK(CheckScript(ScriptLanguage::ForgeScript, forge, "Player.forge").empty());

		const std::string python = MakeScriptSource(ScriptLanguage::Python, scriptTemplate, "my player");
		CHECK(python.find("class MyPlayer(bellows.Script):") != std::string::npos);
		CHECK(CheckScript(ScriptLanguage::Python, python, "my_player.py").empty());
	}

	const std::string forge = MakeScriptSource(ScriptLanguage::ForgeScript, ScriptTemplate::Default, "Player");
	CHECK(forge.find("say \"Player is ready\"") != std::string::npos);

	// Braces and quotes would turn the name into code inside a ForgeScript text.
	const std::string tricky = MakeScriptSource(ScriptLanguage::ForgeScript, ScriptTemplate::Default, "{boss} \"one\"");
	CHECK(tricky.find("say \"boss one is ready\"") != std::string::npos);
	CHECK(CheckScript(ScriptLanguage::ForgeScript, tricky, "Boss.forge").empty());

	CHECK(MakeScriptSource(ScriptLanguage::None, ScriptTemplate::Default, "Notes").empty());
}

TEST_CASE("The ForgeScript outline lists functions, events, kinds and phrases")
{
	const std::string source =
		"# Player\n"
		"set speed to 2  # when this is not an event\n"
		"\n"
		"when update with delta\n"
		"\tsay \"function in text\"\n"
		"end\n"
		"\n"
		"when key pressed with key and shift\n"
		"end\n"
		"\n"
		"when hit by damage\n"
		"end\n"
		"\n"
		"#[\n"
		"function hidden()\n"
		"]#\n"
		"function jump(height)\r\n"
		"\treturn height\r\n"
		"end\n"
		"\n"
		"kind Point\n"
		"\tfunction length()\n"
		"\t\treturn 1\n"
		"\tend\n"
		"end\n"
		"\n"
		"phrase heal (thing) by (amount)\n"
		"end\n";

	const std::vector<ScriptOutlineItem> outline = ParseScriptOutline(ScriptLanguage::ForgeScript, source);
	REQUIRE(outline.size() == 7);

	CHECK(outline[0].ItemKind == ScriptOutlineItem::Kind::Event);
	CHECK(outline[0].Name == "update");
	CHECK(outline[0].Line == 4);
	CHECK(outline[1].Name == "key pressed");
	CHECK(outline[2].Name == "hit");

	CHECK(outline[3].ItemKind == ScriptOutlineItem::Kind::Function);
	CHECK(outline[3].Name == "jump");
	CHECK(outline[3].Line == 17);
	CHECK(outline[3].Depth == 0);

	CHECK(outline[4].ItemKind == ScriptOutlineItem::Kind::Type);
	CHECK(outline[4].Name == "Point");
	CHECK(outline[5].Name == "length");
	CHECK(outline[5].Depth == 1);

	CHECK(outline[6].ItemKind == ScriptOutlineItem::Kind::Phrase);
	CHECK(outline[6].Name == "heal (thing) by (amount)");
}

TEST_CASE("The Python outline lists classes and functions")
{
	const std::string source =
		"import bellows\n"
		"\n"
		"class Player(bellows.Script):\n"
		"    def on_create(self):\n"
		"        pass\n"
		"\n"
		"    async def load(self):\n"
		"        pass\n"
		"\n"
		"def helper():\n"
		"    return 1\n";

	const std::vector<ScriptOutlineItem> outline = ParseScriptOutline(ScriptLanguage::Python, source);
	REQUIRE(outline.size() == 4);
	CHECK(outline[0].ItemKind == ScriptOutlineItem::Kind::Type);
	CHECK(outline[0].Name == "Player");
	CHECK(outline[0].Line == 3);
	CHECK(outline[1].Name == "on_create");
	CHECK(outline[1].Depth == 1);
	CHECK(outline[2].Name == "load");
	CHECK(outline[3].Name == "helper");
	CHECK(outline[3].Depth == 0);

	CHECK(ParseScriptOutline(ScriptLanguage::None, source).empty());
	CHECK(ParseScriptOutline(ScriptLanguage::ForgeScript, "").empty());
}

TEST_CASE("Script checks report the line and column of the first mistake")
{
	SUBCASE("ForgeScript")
	{
		const std::vector<ScriptDiagnostic> problems = CheckScript(ScriptLanguage::ForgeScript, "when update with delta\n\tset x to\nend\n", "Bad.forge");
		REQUIRE(problems.size() == 1);
		CHECK(problems[0].Line == 2);
		CHECK(problems[0].Column > 0);
		CHECK_FALSE(problems[0].Message.empty());
	}

	SUBCASE("Every ForgeScript mistake, sorted by line")
	{
		const std::vector<ScriptDiagnostic> problems = CheckScript(ScriptLanguage::ForgeScript,
			"when update with delta\n\tset x to\nend\n\nwhen created\n\tset y to\nend\n", "Two.forge");
		REQUIRE(problems.size() == 2);
		CHECK(problems[0].Line == 2);
		CHECK(problems[1].Line == 6);
	}

	SUBCASE("Python")
	{
		const std::vector<ScriptDiagnostic> problems = CheckScript(ScriptLanguage::Python, "import bellows\n\ndef broken(:\n    pass\n", "bad.py");
		REQUIRE(problems.size() == 1);
		CHECK(problems[0].Line == 3);
		CHECK(problems[0].Column > 0);
		CHECK_FALSE(problems[0].Message.empty());
	}

	SUBCASE("Text files are not checked")
	{
		CHECK(CheckScript(ScriptLanguage::None, "def broken(:", "notes.md").empty());
	}
}

TEST_CASE("The ForgeScript check can learn the engine's phrases")
{
	const std::string source = "spin 1 by 3\n";
	SetForgeScriptCheckSetup(nullptr);
	CHECK_FALSE(CheckScript(ScriptLanguage::ForgeScript, source, "Spin.forge").empty());

	s_SetupCalled = false;
	SetForgeScriptCheckSetup(RegisterTestPhrase);
	CHECK(CheckScript(ScriptLanguage::ForgeScript, source, "Spin.forge").empty());
	CHECK(s_SetupCalled);

	SetForgeScriptCheckSetup(nullptr);
	CHECK_FALSE(CheckScript(ScriptLanguage::ForgeScript, source, "Spin.forge").empty());
}

TEST_CASE("The Markdown outline lists headings outside code blocks")
{
	const std::string source =
		"# ForgeScript Language Guide\n"
		"\n"
		"## 1. Your first script\r\n"
		"```forge\n"
		"# a comment, not a heading\n"
		"```\n"
		"### Waiting\n"
		"#hashtag is not a heading\n"
		"####### too deep\n";

	const std::vector<ScriptOutlineItem> outline = ParseMarkdownOutline(source);
	REQUIRE(outline.size() == 3);
	CHECK(outline[0].Name == "ForgeScript Language Guide");
	CHECK(outline[0].Depth == 0);
	CHECK(outline[0].ItemKind == ScriptOutlineItem::Kind::Heading);
	CHECK(outline[1].Name == "1. Your first script");
	CHECK(outline[1].Line == 3);
	CHECK(outline[1].Depth == 1);
	CHECK(outline[2].Name == "Waiting");
	CHECK(outline[2].Line == 7);
	CHECK(outline[2].Depth == 2);
}

TEST_CASE("Entity scripts using the engine API pass the Script Editor's check")
{
	SetForgeScriptCheckSetup(Scripting::RegisterForgeScriptApi);
	const std::string source =
		"set speed to 2\n"
		"\n"
		"when update with delta\n"
		"\tset box to create_entity(\"Box\")\n"
		"\tsay box.name\n"
		"end\n";
	CHECK(CheckScript(ScriptLanguage::ForgeScript, source, "Player.forge").empty());
	SetForgeScriptCheckSetup(nullptr);
}

TEST_CASE("Check results are formatted for tools like forgescript --check")
{
	ScriptDiagnostic diagnostic;
	diagnostic.Message = "Expected a value here, but the line ended.";
	diagnostic.Line = 2;
	diagnostic.Column = 8;
	CHECK(FormatScriptDiagnostic("Scripts/Bad.forge", diagnostic) == "Scripts/Bad.forge:2:8: Expected a value here, but the line ended.");

	const std::string source = "when created\r\n\tset x to\r\nend\n";
	const Json json = Json::parse(FormatScriptDiagnosticsJson("Scripts/Bad.forge", source, { diagnostic }));
	CHECK(json["file"] == "Scripts/Bad.forge");
	REQUIRE(json["diagnostics"].size() == 1);
	const Json& item = json["diagnostics"][0];
	CHECK(item["line"] == 2);
	CHECK(item["column"] == 8);
	CHECK(item["endColumn"] == 10);		// "\tset x to" is 9 characters; the line ending is not counted
	CHECK(item["severity"] == "error");
	CHECK(item["message"] == diagnostic.Message);

	const Json clean = Json::parse(FormatScriptDiagnosticsJson("Scripts/Good.forge", "say 1\n", {}));
	CHECK(clean["diagnostics"].empty());
}

TEST_CASE("The Script Editor's check reads scripts pulled in with use, from the asset folder")
{
	const std::filesystem::path assets = BellowsTests::GetTestOutputDirectory() / "CheckUse" / "Assets";
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Helpers.forge", "phrase spin (thing) by (amount)\n\treturn amount\nend\n"));
	AssetManager::SetAssetDirectory(assets);
	SetForgeScriptCheckSetup(SetUpEngineForgeScriptCheck);

	const std::string source = "use \"Scripts/Helpers\"\nspin 1 by 3\n";
	CHECK(CheckScript(ScriptLanguage::ForgeScript, source, "Main.forge").empty());

	// An edited helper is read again on the next check, not taken from a cache.
	REQUIRE(FileSystem::WriteTextFile(assets / "Scripts" / "Helpers.forge", "phrase turn (thing) by (amount)\n\treturn amount\nend\n"));
	CHECK_FALSE(CheckScript(ScriptLanguage::ForgeScript, source, "Main.forge").empty());

	const std::vector<ScriptDiagnostic> missing = CheckScript(ScriptLanguage::ForgeScript, "say 1\nuse \"Scripts/Missing\"\n", "Main.forge");
	REQUIRE(missing.size() == 1);
	CHECK(missing[0].Line == 2);

	SetForgeScriptCheckSetup(nullptr);
	AssetManager::SetAssetDirectory({});
}
