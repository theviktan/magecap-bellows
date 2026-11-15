add_executable(ForgeTests
	src/TestFramework.h
	src/TestMain.cpp
	src/LexerTests.cpp
	src/LanguageTests.cpp
	src/EmbeddingTests.cpp
	src/ScriptTests.cpp
)
target_link_libraries(ForgeTests PRIVATE Forge)
target_compile_definitions(ForgeTests PRIVATE FORGE_TEST_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts")
forge_set_warnings(ForgeTests)

add_test(NAME ForgeTests COMMAND ForgeTests)
