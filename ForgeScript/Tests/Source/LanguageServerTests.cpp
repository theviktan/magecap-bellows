#include "TestFramework.h"

#include "ForgeScript/Tools/LanguageServer.h"
#include "ForgeScript/Tools/ProjectPaths.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

// 'forgescript --lsp', driven the way an editor drives it.

using namespace ForgeScript;

namespace {

	const std::filesystem::path c_TestApi = std::filesystem::path(FORGESCRIPT_TEST_SCRIPTS_DIR).parent_path() / "LSP" / "test-api.json";

	// A folder of files for one test, removed again afterwards. Each run gets a new name, so a folder
	// left behind by an earlier run (on Windows, possibly still held open by another program) never
	// has to be deleted first.
	struct TempFolder
	{
		std::filesystem::path Path;

		explicit TempFolder(const std::string& name)
			: Path(std::filesystem::temp_directory_path() / ("forgescript-lsp-" + name + "-" + std::to_string(std::random_device()())))
		{
			std::filesystem::create_directories(Path);
		}

		~TempFolder()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}

		void Write(const std::string& relative, const std::string& text) const
		{
			const std::filesystem::path file = Path / relative;
			std::filesystem::create_directories(file.parent_path());
			std::ofstream(file, std::ios::binary) << text;
		}
	};

	// One editor session: sends requests and notifications, keeps every reply.
	struct Session
	{
		LanguageServer Server;
		int NextID = 1;

		explicit Session(bool withApi = true)
			: Server(LanguageServer::Options { withApi ? c_TestApi : std::filesystem::path() })
		{
			Json init;
			init.Set("rootUri", "file:///tmp");
			REQUIRE_FALSE(Request("initialize", init).Get("capabilities").IsNull());
			Notify("initialized", Json(Json::Object()));
		}

		std::vector<Json> Send(const std::string& method, const Json& params, bool request)
		{
			Json message;
			message.Set("jsonrpc", "2.0");
			if (request)
				message.Set("id", NextID++);
			message.Set("method", method).Set("params", params);
			return Server.Handle(message);
		}

		Json Request(const std::string& method, const Json& params)
		{
			const std::vector<Json> replies = Send(method, params, true);
			REQUIRE_EQ(replies.size(), static_cast<size_t>(1));
			return replies[0].Find("error") ? replies[0] : replies[0].Get("result");
		}

		std::vector<Json> Notify(const std::string& method, const Json& params)
		{
			return Send(method, params, false);
		}

		// Opens a document and gives back its diagnostics.
		Json Open(const std::string& uri, const std::string& text)
		{
			Json document;
			document.Set("uri", uri).Set("languageId", "forgescript").Set("version", 1).Set("text", text);
			Json params;
			params.Set("textDocument", document);
			return DiagnosticsFor(uri, Notify("textDocument/didOpen", params));
		}

		// Every edit publishes the edited document's diagnostics first, then those of open documents that
		// use it (kept in Dependents).
		Json DiagnosticsFor(const std::string& uri, const std::vector<Json>& replies)
		{
			REQUIRE_FALSE(replies.empty());
			Dependents.clear();
			for (size_t i = 1; i < replies.size(); i++)
				Dependents.push_back(replies[i]);
			CHECK_EQ(replies[0].GetString("method"), std::string("textDocument/publishDiagnostics"));
			CHECK_EQ(replies[0].Get("params").GetString("uri"), uri);
			return replies[0].Get("params").Get("diagnostics");
		}

		std::vector<Json> Dependents;

		Json Change(const std::string& uri, const std::string& text, int version)
		{
			Json document;
			document.Set("uri", uri).Set("version", version);
			Json change;
			change.Set("text", text);
			Json params;
			params.Set("textDocument", document).Set("contentChanges", Json(Json::Array { change }));
			return DiagnosticsFor(uri, Notify("textDocument/didChange", params));
		}

		Json At(const std::string& method, const std::string& uri, int line, int character)
		{
			Json document;
			document.Set("uri", uri);
			Json position;
			position.Set("line", line).Set("character", character);
			Json params;
			params.Set("textDocument", document).Set("position", position);
			return Request(method, params);
		}
	};

	std::vector<std::string> Labels(const Json& items)
	{
		std::vector<std::string> labels;
		for (const Json& item : items.AsArray())
			labels.push_back(item.GetString("label"));
		return labels;
	}

	bool Has(const std::vector<std::string>& labels, const std::string& label)
	{
		return std::find(labels.begin(), labels.end(), label) != labels.end();
	}

	std::string HoverText(const Json& hover)
	{
		return hover.Get("contents").GetString("value");
	}

	int RangeValue(const Json& diagnostic, const char* end, const char* field)
	{
		return static_cast<int>(diagnostic.Get("range").Get(end).Get(field).AsNumber());
	}

}

TEST_CASE("LanguageServerFollowsTheLifecycle")
{
	LanguageServer server(LanguageServer::Options {});
	auto send = [&server](const std::string& text)
	{
		Json message;
		std::string error;
		REQUIRE(Json::Parse(text, message, error));
		return server.Handle(message);
	};

	std::vector<Json> early = send(R"({"jsonrpc":"2.0","id":1,"method":"textDocument/hover","params":{}})");
	REQUIRE_EQ(early.size(), static_cast<size_t>(1));
	CHECK_EQ(early[0].Get("error").Get("code").AsNumber(), -32002.0);

	std::vector<Json> init = send(R"({"jsonrpc":"2.0","id":"first","method":"initialize","params":{"capabilities":{}}})");
	REQUIRE_EQ(init.size(), static_cast<size_t>(1));
	CHECK_EQ(init[0].GetString("id"), std::string("first"));
	const Json& capabilities = init[0].Get("result").Get("capabilities");
	CHECK_EQ(capabilities.Get("textDocumentSync").Get("change").AsNumber(), 1.0);
	CHECK(capabilities.Get("hoverProvider").AsBool());
	CHECK(capabilities.Get("documentSymbolProvider").AsBool());
	CHECK_EQ(capabilities.Get("signatureHelpProvider").Get("triggerCharacters").AsArray().size(), static_cast<size_t>(2));
	CHECK(capabilities.Get("definitionProvider").AsBool());
	CHECK_EQ(capabilities.Get("completionProvider").Get("triggerCharacters").AsArray()[0].AsString(), std::string("."));
	CHECK_EQ(init[0].Get("result").Get("serverInfo").GetString("name"), std::string("forgescript"));

	CHECK(send(R"({"jsonrpc":"2.0","method":"initialized","params":{}})").empty());
	CHECK(send(R"({"jsonrpc":"2.0","method":"$/cancelRequest","params":{"id":4}})").empty());
	std::vector<Json> unknown = send(R"({"jsonrpc":"2.0","id":2,"method":"workspace/symbol","params":{}})");
	CHECK_EQ(unknown[0].Get("error").Get("code").AsNumber(), -32601.0);

	std::vector<Json> shutdown = send(R"({"jsonrpc":"2.0","id":3,"method":"shutdown"})");
	CHECK(shutdown[0].Get("result").IsNull());
	CHECK_EQ(send(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/hover","params":{}})")[0].Get("error").Get("code").AsNumber(), -32600.0);
	CHECK_FALSE(server.ExitRequested());
	send(R"({"jsonrpc":"2.0","method":"exit"})");
	CHECK(server.ExitRequested());
	CHECK_EQ(server.ExitCode(), 0);

	// Exit without shutdown means something went wrong.
	LanguageServer abrupt(LanguageServer::Options {});
	Json exit;
	exit.Set("jsonrpc", "2.0").Set("method", "exit");
	abrupt.Handle(exit);
	CHECK_EQ(abrupt.ExitCode(), 1);
}

TEST_CASE("LanguageServerReadsAndWritesFramedMessages")
{
	std::string input;
	auto frame = [&input](const std::string& body) { input += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body; };
	frame(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
	frame("{not json");
	frame(R"({"jsonrpc":"2.0","id":2,"method":"shutdown"})");
	frame(R"({"jsonrpc":"2.0","method":"exit"})");
	frame(R"({"jsonrpc":"2.0","id":3,"method":"shutdown"})");

	std::istringstream in(input);
	std::ostringstream out;
	LanguageServer server(LanguageServer::Options {});
	CHECK_EQ(server.Run(in, out), 0);

	std::istringstream replies(out.str());
	std::vector<Json> messages;
	std::string body;
	while (LanguageServer::ReadMessage(replies, body))
	{
		Json message;
		std::string error;
		REQUIRE(Json::Parse(body, message, error));
		messages.push_back(message);
	}
	// initialize, the parse error, shutdown; nothing after exit.
	REQUIRE_EQ(messages.size(), static_cast<size_t>(3));
	CHECK_EQ(messages[1].Get("error").Get("code").AsNumber(), -32700.0);
	CHECK(messages[1].Get("id").IsNull());
	CHECK(messages[2].Get("result").IsNull());

	// A header without a length, or a body shorter than promised, ends the session.
	std::istringstream broken("Content-Type: x\r\n\r\n{}");
	CHECK_FALSE(LanguageServer::ReadMessage(broken, body));
	std::istringstream shortBody("Content-Length: 50\r\n\r\n{}");
	CHECK_FALSE(LanguageServer::ReadMessage(shortBody, body));
}

TEST_CASE("LanguageServerConvertsFileUris")
{
	std::filesystem::path path;
	REQUIRE(LanguageServer::UriToPath("file:///home/ron/My%20Game/Scripts/a.forge", path));
	CHECK_EQ(path.generic_string(), std::string("/home/ron/My Game/Scripts/a.forge"));
	REQUIRE(LanguageServer::UriToPath("file:///C:/Games/Hero%27s%20Quest/main.forge", path));
	CHECK_EQ(path.generic_string(), std::string("C:/Games/Hero's Quest/main.forge"));
	REQUIRE(LanguageServer::UriToPath("file:///c%3A/Games/main.forge", path));
	CHECK_EQ(path.generic_string(), std::string("c:/Games/main.forge"));
	CHECK_FALSE(LanguageServer::UriToPath("untitled:Untitled-1", path));

	CHECK_EQ(LanguageServer::PathToUri("/home/ron/My Game/a.forge"), std::string("file:///home/ron/My%20Game/a.forge"));
	CHECK_EQ(LanguageServer::PathToUri("C:/Games/Hero's Quest/main.forge"), std::string("file:///C:/Games/Hero%27s%20Quest/main.forge"));
	REQUIRE(LanguageServer::UriToPath(LanguageServer::PathToUri("/tmp/a b/%c.forge"), path));
	CHECK_EQ(path.generic_string(), std::string("/tmp/a b/%c.forge"));
}

TEST_CASE("LanguageServerKeepsPathsInUtf8")
{
	// Text in URIs and scripts is UTF-8 on every system, also where paths are not (Windows).
	const std::string name = "C:/Users/Jos\xC3\xA9/Games/main.forge";
	CHECK_EQ(GenericPathToUtf8(PathFromUtf8(name)), name);
	std::filesystem::path path;
	REQUIRE(LanguageServer::UriToPath("file:///C:/Users/Jos%C3%A9/Games/main.forge", path));
	CHECK_EQ(GenericPathToUtf8(path), name);
	CHECK_EQ(LanguageServer::PathToUri(PathFromUtf8(name)), std::string("file:///C:/Users/Jos%C3%A9/Games/main.forge"));
	CHECK_EQ(PathToUtf8(ResolveUsePath(PathFromUtf8("/games"), "Scripts/Caf\xC3\xA9").filename()), std::string("Caf\xC3\xA9.forge"));
}

TEST_CASE("LanguageServerPublishesDiagnosticsInUtf16")
{
	TempFolder folder("diagnostics");
	Session session;
	const std::string uri = LanguageServer::PathToUri(folder.Path / "game.forge");

	// The emoji is two UTF-16 units and four bytes, so byte and UTF-16 columns differ after it.
	const Json diagnostics = session.Open(uri, "say \"\xf0\x9f\x98\x80\"\nset to 5\nsay \"\xf0\x9f\x98\x80\" + * 2\n");
	REQUIRE_EQ(diagnostics.AsArray().size(), static_cast<size_t>(2));
	const Json& first = diagnostics.AsArray()[0];
	CHECK_EQ(RangeValue(first, "start", "line"), 1);
	CHECK_EQ(RangeValue(first, "start", "character"), 4);
	CHECK_EQ(RangeValue(first, "end", "character"), 8);
	CHECK_EQ(first.Get("severity").AsNumber(), 1.0);
	CHECK_EQ(first.GetString("source"), std::string("forgescript"));
	const Json& second = diagnostics.AsArray()[1];
	CHECK_EQ(RangeValue(second, "start", "line"), 2);
	// "say "😀" + " is 13 bytes but 11 UTF-16 units; the line is 16 bytes, 14 units.
	CHECK_EQ(RangeValue(second, "start", "character"), 11);
	CHECK_EQ(RangeValue(second, "end", "character"), 14);

	CHECK(session.Change(uri, "say \"fixed\"\n", 2).AsArray().empty());

	Json document;
	document.Set("uri", uri);
	Json params;
	params.Set("textDocument", document);
	const std::vector<Json> closed = session.Notify("textDocument/didClose", params);
	REQUIRE_EQ(closed.size(), static_cast<size_t>(1));
	CHECK(closed[0].Get("params").Get("diagnostics").AsArray().empty());
	CHECK(session.At("textDocument/hover", uri, 0, 1).IsNull());
}

TEST_CASE("LanguageServerKnowsEnginePhrasesFromTheApiFile")
{
	TempFolder folder("phrases");
	const std::string script = "set shot to save screenshot to \"bug.png\"\n";
	Session withApi;
	CHECK(withApi.Open(LanguageServer::PathToUri(folder.Path / "a.forge"), script).AsArray().empty());

	// A phrase that yields to calls compiles like the engine's: "hide(1, 2)" is a call to a function named
	// hide, as before the phrase existed, and "hide panel" is the phrase.
	CHECK(withApi.Open(LanguageServer::PathToUri(folder.Path / "b.forge"), "set panel to 1\nhide panel\nhide(1, 2)\n").AsArray().empty());

	// Without the file the phrase is unknown, the same as a checker without the engine.
	Session withoutApi(false);
	CHECK_FALSE(withoutApi.Open(LanguageServer::PathToUri(folder.Path / "a.forge"), script).AsArray().empty());

	// A broken API file is reported once, and the server still works.
	TempFolder broken("broken-api");
	broken.Write("api.json", "{ not json");
	LanguageServer server(LanguageServer::Options { broken.Path / "api.json" });
	Json init;
	init.Set("jsonrpc", "2.0").Set("id", 1).Set("method", "initialize").Set("params", Json(Json::Object()));
	const std::vector<Json> replies = server.Handle(init);
	REQUIRE_EQ(replies.size(), static_cast<size_t>(2));
	CHECK_EQ(replies[1].GetString("method"), std::string("window/logMessage"));
	CHECK(replies[1].Get("params").GetString("message").find("not valid JSON") != std::string::npos);
}

TEST_CASE("LanguageServerResolvesUseLikeTheEngine")
{
	TempFolder project("project");
	project.Write("Game.bproject", "{ \"Name\": \"Game\", \"AssetDirectory\": \"Content\" }");
	project.Write("Content/Scripts/Helpers.forge", "# Doubles a number.\nfunction double(n)\n\treturn n * 2\nend\n");
	Session session;
	const std::filesystem::path main = project.Path / "Content/Scripts/Main.forge";
	const std::string mainUri = LanguageServer::PathToUri(main);

	CHECK(session.Open(mainUri, "use \"Scripts/Helpers\"\nsay double(2)\n").AsArray().empty());

	// A missing script is a mistake on its 'use' line.
	const Json missing = session.Change(mainUri, "say 1\nuse \"Scripts/Nowhere\"\n", 2);
	REQUIRE_EQ(missing.AsArray().size(), static_cast<size_t>(1));
	CHECK_EQ(RangeValue(missing.AsArray()[0], "start", "line"), 1);
	CHECK_EQ(missing.AsArray()[0].GetString("message"), std::string("Could not find the script \"Scripts/Nowhere\" to use."));

	// An open helper's unsaved text wins over the file: its new phrase is known to Main.
	session.Open(LanguageServer::PathToUri(project.Path / "Content/Scripts/Helpers.forge"),
		"function double(n)\n\treturn n * 2\nend\nphrase cheer (who)\n\tsay who\nend\n");
	CHECK(session.Change(mainUri, "use \"Scripts/Helpers\"\ncheer \"team\"\n", 3).AsArray().empty());
}

TEST_CASE("LanguageServerShowsMistakesOfUsedScriptsOnTheUseLine")
{
	// The folder name is not ASCII, so this also checks that paths stay UTF-8 from URI to file.
	TempFolder project("used-mistakes-Jos\xC3\xA9");
	project.Write("Game.bproject", "{ \"Name\": \"Game\" }");
	project.Write("Assets/Scripts/Broken.forge", "function f()\n\tadd 1 to\nend\nsay 1 + * 2\n");
	project.Write("Assets/Scripts/Fine.forge", "function g()\n\treturn 1\nend\n");
	Session session;
	const std::string uri = LanguageServer::PathToUri(project.Path / "Assets/Scripts/Main.forge");

	const Json diagnostics = session.Open(uri, "use \"Scripts/Fine\"\nuse \"Scripts/Broken\"\nsay 1\n");
	REQUIRE_EQ(diagnostics.AsArray().size(), static_cast<size_t>(1));
	const Json& used = diagnostics.AsArray()[0];
	CHECK_EQ(RangeValue(used, "start", "line"), 1);
	CHECK_EQ(RangeValue(used, "start", "character"), 4);
	CHECK_EQ(RangeValue(used, "end", "character"), 20);
	const std::string message = used.GetString("message");
	CHECK_EQ(message.rfind("Broken.forge line 2: ", 0), static_cast<size_t>(0));
	CHECK(message.ends_with(" (and 1 more mistake)"));

	// Fixing the used script in the editor, before saving, clears the mistake in Main.
	const std::string brokenUri = LanguageServer::PathToUri(project.Path / "Assets/Scripts/Broken.forge");
	CHECK(session.Open(brokenUri, "function f()\n\treturn 1\nend\n").AsArray().empty());
	CHECK(session.Change(uri, "use \"Scripts/Fine\"\nuse \"Scripts/Broken\"\nsay 1\n", 2).AsArray().empty());

	// Breaking it again, unsaved, shows one mistake without the count.
	session.Change(brokenUri, "say 1 + * 2\n", 2);
	const Json again = session.Change(uri, "use \"Scripts/Broken\" as b\n", 3);
	REQUIRE_EQ(again.AsArray().size(), static_cast<size_t>(1));
	CHECK_EQ(again.AsArray()[0].GetString("message").rfind("Broken.forge line 1: ", 0), static_cast<size_t>(0));
	CHECK_EQ(again.AsArray()[0].GetString("message").find("more mistake"), std::string::npos);

	// A script that uses itself is not checked twice.
	CHECK(session.Change(brokenUri, "use \"Scripts/Broken\"\nsay 1\n", 3).AsArray().empty());
}

TEST_CASE("LanguageServerCompletesNames")
{
	TempFolder project("completion");
	project.Write("Game.bproject", "{ \"Name\": \"Game\" }");
	project.Write("Assets/Scripts/Helpers.forge", "function double(n)\n\treturn n * 2\nend\nconstant LIMIT is 3\n");
	Session session;
	const std::string uri = LanguageServer::PathToUri(project.Path / "Assets/Scripts/Main.forge");
	session.Open(uri,
		"use \"Scripts/Helpers\" as helpers\n"
		"# How fast the hero runs.\n"
		"set speed to 2.0\n"
		"kind Enemy\n\tset health to 10\n\tfunction hurt(amount)\n\t\ttake amount from my health\n\tend\nend\n"
		"phrase heal (target) by (amount)\n\tadd amount to target.health\nend\n"
		"when update with delta\n"
		"\tsay sp\n"
		"\tself.\n"
		"\tsay my \n"
		"\thelpers.\n"
		"\tsay \"x\".up\n"
		"end\n");

	const std::vector<std::string> general = Labels(session.At("textDocument/completion", uri, 13, 7));
	CHECK(Has(general, "speed"));
	CHECK(Has(general, "Enemy"));
	CHECK(Has(general, "heal (target) by (amount)"));
	CHECK(Has(general, "delta"));
	CHECK(Has(general, "helpers"));
	CHECK(Has(general, "when"));
	CHECK(Has(general, "print"));
	CHECK(Has(general, "random item from (items)"));
	CHECK(Has(general, "find_entity"));
	CHECK(Has(general, "Entity"));
	CHECK(Has(general, "save screenshot to (path)"));
	CHECK_FALSE(Has(general, "hurt"));  // a kind's function, offered after a '.'
	CHECK_FALSE(Has(general, "double")); // only through 'helpers.'

	// Phrases insert their words up to the first value.
	const Json completions = session.At("textDocument/completion", uri, 13, 7);
	for (const Json& item : completions.AsArray())
	{
		if (item.GetString("label") == "heal (target) by (amount)")
			CHECK_EQ(item.GetString("insertText"), std::string("heal "));
		if (item.GetString("label") == "speed")
			CHECK(item.Get("documentation").GetString("value").find("How fast the hero runs.") != std::string::npos);
	}

	// 'self' is the entity, so only the Entity's members.
	const std::vector<std::string> self = Labels(session.At("textDocument/completion", uri, 14, 6));
	REQUIRE_FALSE(self.empty());
	CHECK_EQ(self[0], std::string("position"));
	CHECK(Has(self, "translate"));
	CHECK_FALSE(Has(self, "upper"));
	CHECK_FALSE(Has(self, "hurt"));

	const std::vector<std::string> my = Labels(session.At("textDocument/completion", uri, 15, 8));
	CHECK(Has(my, "position"));
	CHECK_FALSE(Has(my, "upper"));

	const std::vector<std::string> alias = Labels(session.At("textDocument/completion", uri, 16, 9));
	CHECK_EQ(alias, std::vector<std::string>{ "double", "LIMIT" });

	CHECK(Has(Labels(session.At("textDocument/completion", uri, 17, 13)), "upper"));
}

TEST_CASE("LanguageServerNarrowsCompletionToWhatAValueIs")
{
	Session session;
	const std::string uri = "file:///tmp/narrow.forge";
	session.Open(uri,
		"kind Creature\n\tset health to 10\n\tfunction hurt(amount)\n\t\ttake amount from my health\n\tend\nend\n" // 1-6
		"kind Dragon extends Creature\n\tset fire to 5\n\tfunction roar()\n\t\tsay \"roar\"\n\tend\nend\n"           // 7-12
		"function check(thing)\n"                    // 13
		"\tset hero to find_entity(\"Hero\")\n"       // 14
		"\tset same to hero\n"                        // 15
		"\tset moved to my transform\n"               // 16
		"\tset turned to hero.transform\n"            // 17
		"\tset name to \"Rex\"\n"                     // 18
		"\tset bag to []\n"                           // 19
		"\tset found to find_entities()\n"            // 20
		"\tset beast to new Dragon()\n"               // 21
		"\tset count to 3\n"                          // 22
		"\tsay hero.\n"                               // 23
		"\tsay same.\n"                               // 24
		"\tsay moved.\n"                              // 25
		"\tsay turned.\n"                             // 26
		"\tsay name.\n"                               // 27
		"\tsay bag.\n"                                // 28
		"\tsay found.\n"                              // 29
		"\tsay beast.\n"                              // 30
		"\tsay count.\n"                              // 31
		"\tsay thing.\n"                              // 32
		"end\n");

	auto at = [&](int line, int character) { return Labels(session.At("textDocument/completion", uri, line - 1, character)); };
	const std::vector<std::string> entity { "position", "id", "translate", "transform", "tint" };
	// Set straight from an engine function that returns an Entity (or nothing).
	CHECK_EQ(at(23, 10), entity);
	// Set from another variable.
	CHECK_EQ(at(24, 10), entity);
	// Set from an engine property: 'my transform' and 'hero.transform'.
	CHECK_EQ(at(25, 11), (std::vector<std::string> { "scale", "rotation" }));
	CHECK_EQ(at(26, 12), (std::vector<std::string> { "scale", "rotation" }));
	// Built-in kinds of value get their own methods only.
	const std::vector<std::string> text = at(27, 10);
	CHECK(Has(text, "upper"));
	CHECK_FALSE(Has(text, "add"));
	CHECK_FALSE(Has(text, "position"));
	const std::vector<std::string> list = at(28, 9);
	CHECK(Has(list, "add"));
	CHECK_FALSE(Has(list, "upper"));
	CHECK(Has(at(29, 11), "add")); // list[Entity] is a list
	// A kind's fields and functions, and those of the kind it extends.
	const std::vector<std::string> beast = at(30, 11);
	CHECK(Has(beast, "fire"));
	CHECK(Has(beast, "roar"));
	CHECK(Has(beast, "health"));
	CHECK(Has(beast, "hurt"));
	CHECK_FALSE(Has(beast, "upper"));
	// Numbers have nothing after a '.'.
	CHECK(at(31, 11).empty());
	// A parameter could be anything, so everything is offered.
	const std::vector<std::string> thing = at(32, 11);
	CHECK(Has(thing, "position"));
	CHECK(Has(thing, "upper"));
	CHECK(Has(thing, "scale"));
}

TEST_CASE("LanguageServerShowsForgeScriptTypeNames")
{
	Session session;
	const std::string uri = "file:///tmp/types.forge";
	session.Open(uri, "say find_entities()\nset e to find_entity(\"a\")\nsay e.tint\nsay e.transform.rotation\n");
	CHECK_NE(HoverText(session.At("textDocument/hover", uri, 0, 6)).find("find_entities(tag: text or nothing = nothing) -> list of Entity"), std::string::npos);
	CHECK_NE(HoverText(session.At("textDocument/hover", uri, 2, 8)).find("Entity.tint: color or list of number"), std::string::npos);
	const Json items = session.At("textDocument/completion", uri, 3, 6);
	bool sawTranslate = false;
	for (const Json& item : items.AsArray())
	{
		if (item.GetString("label") == "translate")
		{
			sawTranslate = true;
			CHECK_EQ(item.GetString("detail"), std::string("Entity.translate(offset: vector) -> nothing"));
		}
		if (item.GetString("label") == "id")
			CHECK_EQ(item.GetString("detail"), std::string("Entity.id: text (read-only)"));
	}
	CHECK(sawTranslate);
}

TEST_CASE("LanguageServerUpdatesScriptsThatUseAChangedScript")
{
	TempFolder project("dependents");
	project.Write("Game.bproject", "{ \"Name\": \"Game\" }");
	project.Write("Assets/Scripts/Helpers.forge", "function f()\n\tadd 1 to\nend\n");
	Session session;
	const std::string mainUri = LanguageServer::PathToUri(project.Path / "Assets/Scripts/Main.forge");
	const std::string otherUri = LanguageServer::PathToUri(project.Path / "Assets/Scripts/Other.forge");
	const std::string helpersUri = LanguageServer::PathToUri(project.Path / "Assets/Scripts/Helpers.forge");
	REQUIRE_EQ(session.Open(mainUri, "use \"Scripts/Helpers\"\nsay 1\n").AsArray().size(), static_cast<size_t>(1));
	session.Open(otherUri, "say 2\n");

	// Fixing Helpers in its tab, unsaved, clears Main's use line right away; Other is left alone.
	CHECK(session.Open(helpersUri, "function f()\n\treturn 1\nend\n").AsArray().empty());
	REQUIRE_EQ(session.Dependents.size(), static_cast<size_t>(1));
	CHECK_EQ(session.Dependents[0].Get("params").GetString("uri"), mainUri);
	CHECK(session.Dependents[0].Get("params").Get("diagnostics").AsArray().empty());

	// Breaking it again marks Main again.
	session.Change(helpersUri, "say 1 + * 2\n", 2);
	REQUIRE_EQ(session.Dependents.size(), static_cast<size_t>(1));
	CHECK_EQ(session.Dependents[0].Get("params").Get("diagnostics").AsArray().size(), static_cast<size_t>(1));

	// Closing Helpers without saving: Main goes back to the file on disk, which still has its mistake.
	Json document;
	document.Set("uri", helpersUri);
	Json params;
	params.Set("textDocument", document);
	const std::vector<Json> closed = session.Notify("textDocument/didClose", params);
	REQUIRE_EQ(closed.size(), static_cast<size_t>(2));
	CHECK_EQ(closed[0].Get("params").GetString("uri"), helpersUri);
	CHECK_EQ(closed[1].Get("params").GetString("uri"), mainUri);
	CHECK_EQ(closed[1].Get("params").Get("diagnostics").AsArray().size(), static_cast<size_t>(1));

	// Editing Main itself publishes only Main.
	session.Change(mainUri, "use \"Scripts/Helpers\"\nsay 3\n", 2);
	CHECK(session.Dependents.empty());
}

TEST_CASE("LanguageServerShowsHoverHelp")
{
	TempFolder folder("hover");
	Session session;
	const std::string uri = LanguageServer::PathToUri(folder.Path / "hero.forge");
	session.Open(uri,
		"# Moves the hero.\n# Takes the distance in meters.\n"
		"function walk(distance)\n\tsay distance\nend\n"
		"walk(3)\n"
		"repeat 2 times\n\tsay sqrt(4)\nend\n"
		"set hero to find_entity(\"Hero\")\n"
		"say hero.position\n"
		"say \"a\".upper()\n"
		"say random item from [1, 2]\n"
		"set shot to save screenshot to \"a.png\"\n");

	const std::string walk = HoverText(session.At("textDocument/hover", uri, 5, 1));
	CHECK(walk.find("function walk(distance)") != std::string::npos);
	CHECK(walk.find("Moves the hero.\nTakes the distance in meters.") != std::string::npos);

	CHECK(HoverText(session.At("textDocument/hover", uri, 6, 2)).find("Runs a block a number of times") != std::string::npos);
	CHECK(HoverText(session.At("textDocument/hover", uri, 7, 6)).find("sqrt(x)") != std::string::npos);
	CHECK(HoverText(session.At("textDocument/hover", uri, 9, 13)).find("find_entity(name: text) -> Entity or nothing") != std::string::npos);
	CHECK(HoverText(session.At("textDocument/hover", uri, 10, 10)).find("Entity.position: vector") != std::string::npos);
	CHECK(HoverText(session.At("textDocument/hover", uri, 11, 9)).find("text.upper()") != std::string::npos);
	CHECK(HoverText(session.At("textDocument/hover", uri, 12, 5)).find("random item from (items)") != std::string::npos);
	CHECK(HoverText(session.At("textDocument/hover", uri, 13, 13)).find("save screenshot to (path) -> text") != std::string::npos);
	// Any of a phrase's own words explains it: "item" and "screenshot" here.
	CHECK(HoverText(session.At("textDocument/hover", uri, 12, 12)).find("random item from (items)") != std::string::npos);
	CHECK(HoverText(session.At("textDocument/hover", uri, 13, 18)).find("save screenshot to (path) -> text") != std::string::npos);

	const Json hover = session.At("textDocument/hover", uri, 5, 2);
	CHECK_EQ(hover.Get("range").Get("start").Get("character").AsNumber(), 0.0);
	CHECK_EQ(hover.Get("range").Get("end").Get("character").AsNumber(), 4.0);
	CHECK(session.At("textDocument/hover", uri, 5, 4).IsNull() == false);
	CHECK(session.At("textDocument/hover", uri, 99, 0).IsNull());
}

TEST_CASE("LanguageServerGoesToDefinitions")
{
	TempFolder project("definition");
	project.Write("Game.bproject", "{ \"Name\": \"Game\" }");
	project.Write("Assets/Scripts/Helpers.forge", "\nfunction double(n)\n\treturn n * 2\nend\n");
	Session session;
	const std::string uri = LanguageServer::PathToUri(project.Path / "Assets/Scripts/Main.forge");
	session.Open(uri,
		"use \"Scripts/Helpers\"\n"
		"use \"Scripts/Helpers\" as h\n"
		"function walk(steps)\n\tsay steps\nend\n"
		"walk(double(2))\n"
		"say h.double(1)\n");

	const Json local = session.At("textDocument/definition", uri, 5, 1);
	CHECK_EQ(local.GetString("uri"), uri);
	CHECK_EQ(local.Get("range").Get("start").Get("line").AsNumber(), 2.0);
	CHECK_EQ(local.Get("range").Get("start").Get("character").AsNumber(), 9.0);

	const Json parameter = session.At("textDocument/definition", uri, 3, 6);
	CHECK_EQ(parameter.Get("range").Get("start").Get("line").AsNumber(), 2.0);
	CHECK_EQ(parameter.Get("range").Get("start").Get("character").AsNumber(), 14.0);

	const std::string helpersUri = LanguageServer::PathToUri(project.Path / "Assets/Scripts/Helpers.forge");
	const Json used = session.At("textDocument/definition", uri, 5, 7);
	CHECK_EQ(used.GetString("uri"), helpersUri);
	CHECK_EQ(used.Get("range").Get("start").Get("line").AsNumber(), 1.0);
	CHECK_EQ(session.At("textDocument/definition", uri, 6, 7).GetString("uri"), helpersUri);
	CHECK(session.At("textDocument/definition", uri, 6, 1).IsNull());

	// On the path of a 'use' line: the start of that script, inside or after the quotes' first word.
	for (int character : { 5, 12, 15 })
	{
		const Json script = session.At("textDocument/definition", uri, 0, character);
		CHECK_EQ(script.GetString("uri"), helpersUri);
		CHECK_EQ(script.Get("range").Get("start").Get("line").AsNumber(), 0.0);
	}
	CHECK(session.At("textDocument/definition", uri, 0, 1).Get("uri").IsNull());
	session.Change(uri, "use \"Scripts/Nowhere\"\n", 2);
	CHECK(session.At("textDocument/definition", uri, 0, 8).IsNull());
}

TEST_CASE("FindScriptSymbolsSeesPastElseIf")
{
	const std::vector<LanguageServer::Symbol> symbols = FindScriptSymbols(
		"kind Light\n"
		"\tfunction next(n)\n\t\tif n then\n\t\t\treturn 1\n\t\telse if n then\n\t\t\treturn 2\n\t\tend\n\tend\n"
		"end\n"
		"function after()\nend\n");
	bool found = false;
	for (const LanguageServer::Symbol& symbol : symbols)
	{
		if (symbol.Name == "after")
		{
			found = true;
			CHECK(symbol.Owner.empty());
		}
	}
	CHECK(found);
}

TEST_CASE("FindScriptSymbolsListsDefinitions")
{
	const std::vector<LanguageServer::Symbol> symbols = FindScriptSymbols(
		"use \"helpers\" as h\n"
		"constant MAX is 5\n"
		"kind Door extends Thing\n\tset open to false\n\tfunction toggle()\n\t\tset my open to not my open\n\tend\nend\n"
		"phrase open (door) slowly\n\tsay door\nend\n"
		"for each item in [1]\n\tsay item\nend\n"
		"try\n\tfail \"x\"\ncatch problem\n\tsay problem\nend\n"
		"set broken to (\n");
	std::vector<std::string> described;
	for (const LanguageServer::Symbol& symbol : symbols)
		described.push_back(symbol.Signature + (symbol.Owner.empty() ? "" : " @" + symbol.Owner));
	CHECK_EQ(described, std::vector<std::string> {
		"use \"helpers\" as h @h", "constant MAX is 5", "kind Door extends Thing", "set open to false @Door",
		"function Door.toggle() @Door", "door", "phrase open (door) slowly", "item", "problem", "set broken to (" });
}

TEST_CASE("FindOutlineListsAScriptsDefinitions")
{
	const std::vector<OutlineItem> outline = FindOutline(
		"constant LIMIT is 3\n"                  // 1
		"set speed to 2.5\n"                     // 2
		"function grade(score)\n"                // 3
		"\tset label to \"F\"\n"                 // 4  (inside a function: not listed)
		"\tif score is above 90 then\n"          // 5
		"\t\tset label to \"A\"\n"               // 6
		"\telse if score is above 80 then\n"     // 7
		"\t\tset label to \"B\"\n"               // 8
		"\tend\n"                                // 9
		"\treturn label\n"                       // 10
		"end\n"                                  // 11
		"phrase deal (amount) damage\n"          // 12
		"\tsay amount\n"                         // 13
		"end\n"                                  // 14
		"kind Door extends Thing\n"              // 15
		"\tset open to no\n"                     // 16
		"\tfunction toggle()\n"                  // 17
		"\t\tset my open to not my open\n"       // 18
		"\tend\n"                                // 19
		"end\n"                                  // 20
		"when hit with damage\n"                 // 21
		"\tsay damage\n"                         // 22
		"end\n"                                  // 23
		"set speed to 3\n"                       // 24 (already defined)
		"function unfinished()\n"                // 25
		"\tsay 1\n");                            // 26 (no 'end')

	using Kind = OutlineItem::Kind;
	REQUIRE_EQ(outline.size(), static_cast<size_t>(7));
	CHECK(outline[0].ItemKind == Kind::Constant);
	CHECK_EQ(outline[0].Name, std::string("LIMIT"));
	CHECK(outline[1].ItemKind == Kind::Variable);
	CHECK_EQ(outline[1].Detail, std::string("set speed to 2.5"));

	const OutlineItem& grade = outline[2];
	CHECK(grade.ItemKind == Kind::Function);
	CHECK_EQ(grade.Detail, std::string("function grade(score)"));
	CHECK_EQ(grade.StartLine, 3);
	CHECK_EQ(grade.EndLine, 11); // 'else if' shares its 'if's end
	CHECK_EQ(grade.NameLine, 3);
	CHECK_EQ(grade.NameColumn, 10);
	CHECK_EQ(grade.NameLength, 5);
	CHECK(grade.Children.empty());

	CHECK(outline[3].ItemKind == Kind::Phrase);
	CHECK_EQ(outline[3].Name, std::string("deal (amount) damage"));
	CHECK_EQ(outline[3].EndLine, 14);

	const OutlineItem& door = outline[4];
	CHECK(door.ItemKind == Kind::KindType);
	CHECK_EQ(door.Detail, std::string("kind Door extends Thing"));
	CHECK_EQ(door.StartLine, 15);
	CHECK_EQ(door.EndLine, 20);
	REQUIRE_EQ(door.Children.size(), static_cast<size_t>(2));
	CHECK(door.Children[0].ItemKind == Kind::Variable);
	CHECK_EQ(door.Children[0].Name, std::string("open"));
	CHECK(door.Children[1].ItemKind == Kind::Function);
	CHECK_EQ(door.Children[1].Name, std::string("toggle"));
	CHECK_EQ(door.Children[1].EndLine, 19);

	const OutlineItem& hit = outline[5];
	CHECK(hit.ItemKind == Kind::Handler);
	CHECK_EQ(hit.Name, std::string("when hit with damage"));
	CHECK_EQ(hit.NameColumn, 1);
	CHECK_EQ(hit.NameLength, 20);
	CHECK_EQ(hit.EndLine, 23);

	// A block left open runs to the end of the script.
	CHECK_EQ(outline[6].Name, std::string("unfinished"));
	CHECK_EQ(outline[6].EndLine, 26);

	CHECK(FindOutline("").empty());
	CHECK(FindOutline("end\nend\nfunction (\n").empty());
}

TEST_CASE("LanguageServerGivesAnOutline")
{
	Session session;
	const std::string uri = "file:///tmp/outline.forge";
	session.Open(uri, "set name to \"caf\xC3\xA9\"\nkind Door\n\tset open to no\n\tfunction toggle()\n\tend\nend\nwhen hit\n\tsay 1\nend\n");
	Json params;
	Json document;
	document.Set("uri", uri);
	params.Set("textDocument", document);
	const Json symbols = session.Request("textDocument/documentSymbol", params);
	REQUIRE_EQ(symbols.AsArray().size(), static_cast<size_t>(3));

	const Json& name = symbols.AsArray()[0];
	CHECK_EQ(name.GetString("name"), std::string("name"));
	CHECK_EQ(name.Get("kind").AsNumber(), 13.0);
	CHECK_EQ(name.Get("selectionRange").Get("start").Get("character").AsNumber(), 4.0);
	CHECK_EQ(name.Get("selectionRange").Get("end").Get("character").AsNumber(), 8.0);
	// The line 'set name to "café"' is 19 bytes but 18 UTF-16 units.
	CHECK_EQ(name.Get("range").Get("end").Get("character").AsNumber(), 18.0);

	const Json& door = symbols.AsArray()[1];
	CHECK_EQ(door.Get("kind").AsNumber(), 5.0);
	CHECK_EQ(door.Get("range").Get("start").Get("line").AsNumber(), 1.0);
	CHECK_EQ(door.Get("range").Get("end").Get("line").AsNumber(), 5.0);
	CHECK_EQ(door.GetString("detail"), std::string("kind Door"));
	const Json::Array& children = door.Get("children").AsArray();
	REQUIRE_EQ(children.size(), static_cast<size_t>(2));
	CHECK_EQ(children[0].Get("kind").AsNumber(), 8.0);  // a field
	CHECK_EQ(children[1].Get("kind").AsNumber(), 6.0);  // a method
	CHECK_EQ(symbols.AsArray()[2].Get("kind").AsNumber(), 24.0); // an event handler

	Json missing;
	Json other;
	other.Set("uri", "file:///tmp/not-open.forge");
	missing.Set("textDocument", other);
	CHECK(session.Request("textDocument/documentSymbol", missing).IsNull());
}

TEST_CASE("LanguageServerHelpsWithSignatures")
{
	Session session;
	const std::string uri = "file:///tmp/signatures.forge";
	session.Open(uri,
		"function walk(steps, speed)\n"                           // 0
		"end\n"                                                   // 1
		"kind Door\n\tfunction open(force)\n\tend\nend\n"         // 2-5
		"walk(3, \n"                                              // 6
		"save_screenshot(\"a.png\", \n"                          // 7
		"set hero to find_entity(\"Hero\")\n"                     // 8
		"hero.translate(\n"                                       // 9
		"say round(walk(1, 2), \n"                                // 10
		"set d to new Door()\n"                                   // 11
		"d.open(\n"                                               // 12
		"say \"walk(\"\n"                                         // 13
		"walk(1, 2)\n"                                            // 14
		"set name to \"Rex\"\n"                                   // 15
		"say name.split(\n"                                       // 16
		"say [walk(1, [2, 3], \n");                               // 17

	auto at = [&](int line, int character) { return session.At("textDocument/signatureHelp", uri, line, character); };
	auto label = [](const Json& help) { return help.Get("signatures").AsArray().at(0).GetString("label"); };

	// The script's own function, on its second parameter.
	Json help = at(6, 8);
	REQUIRE_FALSE(help.IsNull());
	CHECK_EQ(label(help), std::string("walk(steps, speed)"));
	CHECK_EQ(help.Get("activeParameter").AsNumber(), 1.0);
	const Json::Array& ranges = help.Get("signatures").AsArray()[0].Get("parameters").AsArray();
	REQUIRE_EQ(ranges.size(), static_cast<size_t>(2));
	CHECK_EQ(ranges[1].Get("label").AsArray()[0].AsNumber(), 12.0);
	CHECK_EQ(ranges[1].Get("label").AsArray()[1].AsNumber(), 17.0);

	// An engine function, with ForgeScript type names, on its second parameter.
	help = at(7, 25);
	REQUIRE_FALSE(help.IsNull());
	CHECK_EQ(label(help), std::string("save_screenshot(path: text = \"\", max_width: number = 1920) -> text"));
	CHECK_EQ(help.Get("activeParameter").AsNumber(), 1.0);
	CHECK_EQ(help.Get("signatures").AsArray()[0].Get("parameters").AsArray().size(), static_cast<size_t>(2));
	CHECK_EQ(help.Get("signatures").AsArray()[0].Get("documentation").GetString("value"), std::string("Saves what the player sees."));

	// A method of an engine type the script shows.
	help = at(9, 15);
	REQUIRE_FALSE(help.IsNull());
	CHECK_EQ(label(help), std::string("Entity.translate(offset: vector) -> nothing"));

	// A built-in function around a call that is already closed.
	help = at(10, 22);
	REQUIRE_FALSE(help.IsNull());
	CHECK_EQ(label(help), std::string("round(x, digits)"));
	CHECK_EQ(help.Get("activeParameter").AsNumber(), 1.0);
	// Inside the inner call, the inner function.
	CHECK_EQ(label(at(10, 16)), std::string("walk(steps, speed)"));

	// A kind's function, and a text method.
	CHECK_EQ(label(at(12, 7)), std::string("open(force)"));
	CHECK_EQ(label(at(16, 15)), std::string("text.split(separator)"));

	// Commas inside a list don't count, and the list's call is still found.
	help = at(17, 21);
	REQUIRE_FALSE(help.IsNull());
	CHECK_EQ(label(help), std::string("walk(steps, speed)"));
	CHECK_EQ(help.Get("activeParameter").AsNumber(), 2.0);

	// No help inside text, after a call is closed, or for something unknown.
	CHECK(at(13, 10).IsNull());
	CHECK(at(14, 10).IsNull());
	CHECK(at(1, 2).IsNull());
	CHECK(at(99, 0).IsNull());
}

TEST_CASE("LanguageServerShowsDefaultValues")
{
	// A default value shows in the signature as written, and names inside it aren't parameters.
	const std::vector<LanguageServer::Symbol> symbols = FindScriptSymbols(
		"function attack(target, damage is max(base, 2), label is \"hit {target}\")\nend\n");
	std::vector<std::string> described;
	for (const LanguageServer::Symbol& symbol : symbols)
		described.push_back(symbol.Signature);
	CHECK_EQ(described, std::vector<std::string> {
		"target", "damage", "label", "function attack(target, damage is max(base, 2), label is \"hit {target}\")" });

	Session session;
	const std::string uri = "file:///tmp/defaults.forge";
	session.Open(uri, "function walk(steps, speed is 2)\nend\nwalk(3, \n");
	const Json help = session.At("textDocument/signatureHelp", uri, 2, 8);
	REQUIRE_FALSE(help.IsNull());
	CHECK_EQ(help.Get("signatures").AsArray().at(0).GetString("label"), std::string("walk(steps, speed is 2)"));
	CHECK_EQ(help.Get("activeParameter").AsNumber(), 1.0);

	// A named value is for the parameter with that name, wherever it is in the list.
	session.Open("file:///tmp/named.forge", "function walk(steps, speed is 2, style is \"slow\")\nend\nwalk(3, style: \n");
	const Json named = session.At("textDocument/signatureHelp", "file:///tmp/named.forge", 2, 15);
	REQUIRE_FALSE(named.IsNull());
	CHECK_EQ(named.Get("activeParameter").AsNumber(), 2.0);
}

TEST_CASE("LanguageServerKnowsNamesSetByTakingApart")
{
	const std::vector<LanguageServer::Symbol> symbols = FindScriptSymbols("set name, score to [\"Ron\", 9]\nsay name\n");
	std::vector<std::string> names;
	for (const LanguageServer::Symbol& symbol : symbols)
		names.push_back(symbol.Name);
	CHECK_EQ(names, std::vector<std::string> { "name", "score" });
}
