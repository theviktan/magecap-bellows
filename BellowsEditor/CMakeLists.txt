# MageCap Bellows editor.

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

add_executable(BellowsEditor ${EDITOR_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" FILES ${EDITOR_SOURCES})
target_include_directories(BellowsEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_link_libraries(BellowsEditor PRIVATE Bellows BellowsFileDialogs BellowsTextEditor ForgeScript)
bellows_set_compile_options(BellowsEditor)
bellows_copy_python_runtime(BellowsEditor)
add_dependencies(BellowsEditor BellowsRuntime)

if(NOT WIN32)
	set_target_properties(BellowsEditor PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
endif()
