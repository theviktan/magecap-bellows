# ForgeScript - the natural-language scripting language of MageCap Bellows.
#
# The language core (compiler, virtual machine, standard library) has no engine dependencies:
# it only uses the C++ standard library. The engine binds to it from Bellows/Source/Bellows/Scripting.

file(GLOB_RECURSE FORGESCRIPT_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/ForgeScript/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/ForgeScript/*.h")

add_library(ForgeScript STATIC ${FORGESCRIPT_SOURCES})
target_include_directories(ForgeScript PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
# The version is set in ForgeScript.h; every target that uses the library sees the same one. CMake
# reads it when it configures, so a change to the header must make it configure again.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/ForgeScript/ForgeScript.h")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/Source/ForgeScript/ForgeScript.h" FORGESCRIPT_VERSION_LINE
	REGEX "#define FORGESCRIPT_VERSION \"[0-9]+\\.[0-9]+\\.[0-9]+\"")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" FORGESCRIPT_VERSION "${FORGESCRIPT_VERSION_LINE}")
if(NOT FORGESCRIPT_VERSION)
	message(FATAL_ERROR "Could not read FORGESCRIPT_VERSION from ForgeScript.h")
endif()
target_compile_definitions(ForgeScript PUBLIC FORGESCRIPT_VERSION="${FORGESCRIPT_VERSION}")
bellows_set_compile_options(ForgeScript)
set_target_properties(ForgeScript PROPERTIES FOLDER "ForgeScript")

# forgescript: run scripts, check them for mistakes, or try code in the interactive prompt.
add_executable(ForgeScriptCLI CLI/Source/Main.cpp)
set_target_properties(ForgeScriptCLI PROPERTIES OUTPUT_NAME forgescript FOLDER "ForgeScript")
target_link_libraries(ForgeScriptCLI PRIVATE ForgeScript)
bellows_set_compile_options(ForgeScriptCLI)

# A small stand-in engine that shows embedding without the rest of Bellows.
add_executable(ForgeScriptEngineExample Examples/EngineIntegration/Main.cpp)
set_target_properties(ForgeScriptEngineExample PROPERTIES FOLDER "ForgeScript")
target_link_libraries(ForgeScriptEngineExample PRIVATE ForgeScript)
bellows_set_compile_options(ForgeScriptEngineExample)

if(BELLOWS_BUILD_TESTS)
	file(GLOB FORGESCRIPT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Source/*.h")
	add_executable(ForgeScriptTests ${FORGESCRIPT_TEST_SOURCES})
	set_target_properties(ForgeScriptTests PROPERTIES FOLDER "ForgeScript")
	target_link_libraries(ForgeScriptTests PRIVATE ForgeScript BellowsDoctest)
	target_compile_definitions(ForgeScriptTests PRIVATE FORGESCRIPT_TEST_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Scripts")
	bellows_set_compile_options(ForgeScriptTests)

	add_test(NAME ForgeScriptTests COMMAND ForgeScriptTests)
	add_test(NAME ForgeScriptEngineExample COMMAND ForgeScriptEngineExample)
	add_test(NAME ForgeScriptCheckCommand
		COMMAND ${CMAKE_COMMAND} -DFORGESCRIPT=$<TARGET_FILE:ForgeScriptCLI> -DFOLDER=${CMAKE_CURRENT_SOURCE_DIR}/Tests/CLI
			-P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/CLI/CheckCommand.cmake)
	add_test(NAME ForgeScriptLspCommand
		COMMAND ${CMAKE_COMMAND} -DFORGESCRIPT=$<TARGET_FILE:ForgeScriptCLI> -DFOLDER=${CMAKE_CURRENT_SOURCE_DIR}/Tests/LSP
			-P ${CMAKE_CURRENT_SOURCE_DIR}/Tests/LSP/LspCommand.cmake)
endif()
