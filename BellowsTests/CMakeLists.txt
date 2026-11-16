# MageCap Bellows unit and integration tests (doctest).

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

# Editor logic without UI (settings, the project list, script languages and checks) is compiled in and tested directly.
set(EDITOR_LOGIC_SOURCES
	"${PROJECT_SOURCE_DIR}/BellowsEditor/Source/EditorSettings.cpp"
	"${PROJECT_SOURCE_DIR}/BellowsEditor/Source/EditorSettings.h"
	"${PROJECT_SOURCE_DIR}/BellowsEditor/Source/ProjectList.cpp"
	"${PROJECT_SOURCE_DIR}/BellowsEditor/Source/ProjectList.h"
	"${PROJECT_SOURCE_DIR}/BellowsEditor/Source/ScriptDocument.cpp"
	"${PROJECT_SOURCE_DIR}/BellowsEditor/Source/ScriptDocument.h")

add_executable(BellowsTests ${TEST_SOURCES} ${EDITOR_LOGIC_SOURCES})
target_include_directories(BellowsTests PRIVATE "${PROJECT_SOURCE_DIR}/BellowsEditor/Source")
target_link_libraries(BellowsTests PRIVATE Bellows BellowsDoctest ForgeScript)
bellows_set_compile_options(BellowsTests)
bellows_copy_python_runtime(BellowsTests)
target_compile_definitions(BellowsTests PRIVATE BELLOWS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

add_test(NAME BellowsUnitTests COMMAND BellowsTests --test-case-exclude=*[gpu]* WORKING_DIRECTORY "${BELLOWS_OUTPUT_DIR}")
add_test(NAME BellowsGpuTests COMMAND BellowsTests --test-case=*[gpu]* WORKING_DIRECTORY "${BELLOWS_OUTPUT_DIR}")

# The editor's automation API, driven end to end by a Python script in a headless editor.
if(TARGET BellowsEditor)
	add_test(NAME BellowsEditorAutomationTests
		COMMAND BellowsEditor --headless --no-agent-server --width 1280 --height 720
			--run-script "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/EditorAutomationTest.py" --exit
		WORKING_DIRECTORY "${BELLOWS_OUTPUT_DIR}")
	set_tests_properties(BellowsEditorAutomationTests PROPERTIES
		ENVIRONMENT "BELLOWS_CONFIG_DIR=${CMAKE_BINARY_DIR}/TestEditorConfig"
		TIMEOUT 600)
endif()

# The complete scripting API, exercised by Projects/FeatureTest in the runtime and in editor play mode.
add_test(NAME BellowsFeatureTest
	COMMAND BellowsRuntime --project "${PROJECT_SOURCE_DIR}/Projects/FeatureTest" --headless --width 640 --height 360 --frames 5000
	WORKING_DIRECTORY "${BELLOWS_OUTPUT_DIR}")
# The pass message is required too: a scene without its test runner would otherwise just run out of frames.
set_tests_properties(BellowsFeatureTest PROPERTIES
	ENVIRONMENT "BELLOWS_FEATURE_TEST=1"
	PASS_REGULAR_EXPRESSION "FEATURE TEST PASSED"
	FAIL_REGULAR_EXPRESSION "FEATURE TEST FAILED;Traceback;script error"
	TIMEOUT 900)

if(TARGET BellowsEditor)
	add_test(NAME BellowsEditorFeatureTest
		COMMAND BellowsEditor --headless --no-agent-server --width 960 --height 540 --project "${PROJECT_SOURCE_DIR}/Projects/FeatureTest"
			--run-script "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/EditorFeatureTest.py" --exit
		WORKING_DIRECTORY "${BELLOWS_OUTPUT_DIR}")
	set_tests_properties(BellowsEditorFeatureTest PROPERTIES
		ENVIRONMENT "BELLOWS_FEATURE_TEST=1;BELLOWS_CONFIG_DIR=${CMAKE_BINARY_DIR}/TestEditorConfig"
		TIMEOUT 900)
endif()
