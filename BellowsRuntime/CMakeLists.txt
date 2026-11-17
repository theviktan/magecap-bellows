# MageCap Bellows runtime: runs an exported game without any editing features.

file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

add_executable(BellowsRuntime ${RUNTIME_SOURCES})
# Exported games are this program renamed, so they show the Bellows icon too.
bellows_add_app_icon(BellowsRuntime)
target_link_libraries(BellowsRuntime PRIVATE Bellows)
bellows_set_compile_options(BellowsRuntime)
bellows_copy_python_runtime(BellowsRuntime)

if(WIN32)
	set_target_properties(BellowsRuntime PROPERTIES WIN32_EXECUTABLE $<CONFIG:Dist>)
	target_link_options(BellowsRuntime PRIVATE $<$<CONFIG:Dist>:/ENTRY:mainCRTStartup>)
else()
	# Exported games ship their Python runtime next to the executable.
	set_target_properties(BellowsRuntime PROPERTIES BUILD_RPATH "\$ORIGIN;\$ORIGIN/PythonRuntime/lib" INSTALL_RPATH "\$ORIGIN;\$ORIGIN/PythonRuntime/lib")
endif()
