# MageCap Bellows editor.

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

add_executable(BellowsEditor ${EDITOR_SOURCES})
bellows_add_app_icon(BellowsEditor)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" FILES ${EDITOR_SOURCES})
target_include_directories(BellowsEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(BellowsEditor PRIVATE Bellows BellowsFileDialogs BellowsTextEditor ForgeScript)
bellows_set_compile_options(BellowsEditor)
bellows_copy_python_runtime(BellowsEditor)
add_dependencies(BellowsEditor BellowsRuntime)

if(WIN32)
	# A GUI program, so starting the editor never opens a console window; the log is in the Console
	# panel and Editor.log. Platform::ConnectConsole reconnects output when it's started from a command
	# prompt, and --console opens a console for debugging.
	set_target_properties(BellowsEditor PROPERTIES WIN32_EXECUTABLE ON)
	target_link_options(BellowsEditor PRIVATE /ENTRY:mainCRTStartup)
else()
	set_target_properties(BellowsEditor PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
