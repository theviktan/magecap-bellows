add_executable(ForgeScriptTests
	src/TestFramework.h
	src/TestMain.cpp
	src/LexerTests.cpp
	src/LanguageTests.cpp
	src/EmbeddingTests.cpp
	src/ScriptTests.cpp
)
target_link_libraries(ForgeScriptTests PRIVATE ForgeScript)
target_compile_definitions(ForgeScriptTests PRIVATE FORGESCRIPT_TEST_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts")
forge_set_warnings(ForgeScriptTests)

add_test(NAME ForgeScriptTests COMMAND ForgeScriptTests)
