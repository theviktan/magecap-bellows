# A small stand-in engine that shows how to embed ForgeScript. Run it to watch two scripted entities.
add_executable(ForgeScriptEngineExample EngineIntegration/Main.cpp)
target_link_libraries(ForgeScriptEngineExample PRIVATE ForgeScript)
forge_set_warnings(ForgeScriptEngineExample)

if(FORGESCRIPT_BUILD_TESTS)
	add_test(NAME ForgeScriptEngineExample COMMAND ForgeScriptEngineExample)
endif()
