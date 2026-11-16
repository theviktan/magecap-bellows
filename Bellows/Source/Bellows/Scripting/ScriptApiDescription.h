#pragma once

#include "Bellows/Serialization/JsonUtils.h"

#include <string>

namespace Bellows::Scripting {

	// A JSON description of the engine's script API, written from the same DefineScriptApi that
	// Python and ForgeScript are bound from, so it always matches what scripts can call.
	// `forgescript --lsp` reads it (as forgescript-api.json, next to the program) for completion,
	// hover and engine phrases. Shape (version 1):
	//
	//   { "version": 1,
	//     "functions": [ { "name", "params": [ { "name", "type", "default"? } ], "returns", "doc" } ],
	//     "types": [ { "name", "doc", "constructor"?: { "params" },
	//                  "members": [ { "name", "kind": "property" | "method" | "static",
	//                                 "type"? (properties), "readonly"? (properties),
	//                                 "params"? and "returns"? (methods and statics), "doc" } ] } ],
	//     "phrases": [] }
	//
	// Type names follow Python's stub (_bellows.pyi): float, int, bool, str, Vec3, Entity,
	// list[str], "Entity | None". A parameter with "default" may be left out (null means None).
	Json DescribeScriptApi();

	// DescribeScriptApi() as the text of forgescript-api.json (tab-indented, ending in a newline).
	std::string WriteScriptApiDescription();

}
