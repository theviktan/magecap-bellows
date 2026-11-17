#include "TestFramework.h"

#include "ForgeScript/Tools/Json.h"

using namespace ForgeScript;

namespace {

	Json ParseOrFail(const std::string& text)
	{
		Json value;
		std::string error;
		INFO(text);
		REQUIRE(Json::Parse(text, value, error));
		return value;
	}

	std::string ParseError(const std::string& text)
	{
		Json value;
		std::string error;
		CHECK_FALSE(Json::Parse(text, value, error));
		return error;
	}

}

TEST_CASE("JsonReadsEveryKindOfValue")
{
	const Json value = ParseOrFail(R"( {"id": 7, "ok": true, "no": false, "none": null, "pi": -3.25e0,
		"list": [1, "two", [], {}], "text": "tab\there \"quoted\" back\\slash \/ \u00e9 \ud83d\ude00"} )");
	CHECK_EQ(value.Get("id").AsNumber(), 7.0);
	CHECK(value.Get("ok").AsBool());
	CHECK_FALSE(value.Get("no").AsBool(true));
	CHECK(value.Get("none").IsNull());
	CHECK_EQ(value.Get("pi").AsNumber(), -3.25);
	REQUIRE_EQ(value.Get("list").AsArray().size(), static_cast<size_t>(4));
	CHECK_EQ(value.Get("list").AsArray()[1].AsString(), std::string("two"));
	CHECK_EQ(value.GetString("text"), std::string("tab\there \"quoted\" back\\slash / \xc3\xa9 \xf0\x9f\x98\x80"));
	// Missing keys give empty values instead of crashing.
	CHECK(value.Get("missing").Get("deeper").IsNull());
	CHECK(value.GetString("missing").empty());
}

TEST_CASE("JsonWritesCompactTextThatReadsBack")
{
	Json message;
	message.Set("jsonrpc", "2.0").Set("id", 12).Set("result", Json());
	Json item;
	item.Set("label", "say \"hi\"\n").Set("kind", 3).Set("ratio", 0.5);
	message.Set("items", Json(Json::Array { item, Json(true) }));
	const std::string text = message.Dump();
	CHECK_EQ(text, std::string(R"({"jsonrpc":"2.0","id":12,"result":null,"items":[{"label":"say \"hi\"\n","kind":3,"ratio":0.5},true]})"));
	CHECK(ParseOrFail(text) == message);

	// Setting a key again replaces it instead of adding a second one.
	message.Set("id", 13);
	CHECK_EQ(message.Get("id").AsNumber(), 13.0);
	CHECK_EQ(message.AsObject().size(), static_cast<size_t>(4));
}

TEST_CASE("JsonRefusesBrokenText")
{
	CHECK(ParseError("").find("text ended") != std::string::npos);
	CHECK(ParseError("{\"a\" 1}").find("':'") != std::string::npos);
	CHECK(ParseError("[1, 2").find("','") != std::string::npos);
	CHECK(ParseError("\"open").find("closing quote") != std::string::npos);
	CHECK(ParseError("\"\\ud83d\"").find("other half") != std::string::npos);
	CHECK(ParseError("\"\\x\"").find("Unknown escape") != std::string::npos);
	CHECK(ParseError("{} extra").find("after the JSON value") != std::string::npos);
	CHECK(ParseError(std::string(1000, '[')).find("nested too deeply") != std::string::npos);
	CHECK(ParseError("01x").find("after the JSON value") != std::string::npos);
}
