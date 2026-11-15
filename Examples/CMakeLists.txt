# A small stand-in engine that shows how to embed Forge. Run it to watch two scripted entities.
add_executable(ForgeEngineExample EngineIntegration/Main.cpp)
target_link_libraries(ForgeEngineExample PRIVATE Forge)
forge_set_warnings(ForgeEngineExample)

if(FORGE_BUILD_TESTS)
	add_test(NAME ForgeEngineExample COMMAND ForgeEngineExample)
endif()
