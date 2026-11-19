# MageCap Bellows core engine (static library).

file(GLOB_RECURSE BELLOWS_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

add_library(Bellows STATIC ${BELLOWS_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Source" FILES ${BELLOWS_SOURCES})
# The build's commit (cmake/BellowsBuildInfo.cmake).
target_sources(Bellows PRIVATE "${PROJECT_BINARY_DIR}/Generated/BuildInfo.cpp")

target_include_directories(Bellows PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_precompile_headers(Bellows PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/bwpch.h")

find_package(Threads REQUIRED)

target_link_libraries(Bellows
	PUBLIC
		BellowsGLM
		BellowsEnTT
		BellowsImGui
		BellowsHeaderLibs
		BellowsVulkan
		nvrhi_vk
		nvrhi
		glfw
		spdlog::spdlog
		Jolt
		pybind11::embed
		ForgeScript
		BellowsMsdfgen
		Threads::Threads
		${CMAKE_DL_LIBS})

if(WIN32)
	target_link_libraries(Bellows PUBLIC ws2_32)
endif()

target_compile_definitions(Bellows PUBLIC
	BW_VERSION="${PROJECT_VERSION}"
	$<$<NOT:$<CONFIG:Dist>>:BW_ENABLE_ASSERTS>
	$<$<CONFIG:Dist>:BW_DIST>)

bellows_set_compile_options(Bellows)

# The Python runtime the engine was built against; used when no bundled runtime ships with the game.
execute_process(
	COMMAND "${Python_EXECUTABLE}" -c "import sys; print(sys.base_prefix)"
	OUTPUT_VARIABLE BELLOWS_PYTHON_HOME
	OUTPUT_STRIP_TRAILING_WHITESPACE)
file(TO_CMAKE_PATH "${BELLOWS_PYTHON_HOME}" BELLOWS_PYTHON_HOME)
target_compile_definitions(Bellows PRIVATE BELLOWS_PYTHON_HOME="${BELLOWS_PYTHON_HOME}")
set(BELLOWS_PYTHON_HOME "${BELLOWS_PYTHON_HOME}" CACHE INTERNAL "")

# Third-party implementation units are compiled without our strict warnings.
set_source_files_properties(
	"${CMAKE_CURRENT_SOURCE_DIR}/Source/Bellows/Vendor/VendorImplementations.cpp"
	PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>"
	SKIP_PRECOMPILE_HEADERS ON)

# --- Shaders ------------------------------------------------------------------
file(GLOB BELLOWS_SHADER_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.vert"
	"${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.frag"
	"${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.comp")

bellows_compile_shaders(BellowsShaders
	OUTPUT_DIR "${BELLOWS_OUTPUT_DIR}/Shaders"
	INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Include"
	SOURCES ${BELLOWS_SHADER_SOURCES})
add_dependencies(Bellows BellowsShaders)

# --- Resources ----------------------------------------------------------------
# Engine resources (fonts, icons, the Python package) are mirrored next to the executables.
add_custom_target(BellowsResources ALL
	COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Resources" "${BELLOWS_OUTPUT_DIR}/Resources"
	COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/Python" "${BELLOWS_OUTPUT_DIR}/Python"
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/ThirdPartyNotices.md" "${BELLOWS_OUTPUT_DIR}/ThirdPartyNotices.md"
	# forgescript --lsp looks for the engine API description next to the program.
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/Resources/forgescript-api.json" "${BELLOWS_OUTPUT_DIR}/forgescript-api.json"
	# ForgeScript's icon lives with the language; the editor shows it on .forge files.
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_SOURCE_DIR}/ForgeScript/Icons/ForgeScript.png" "${BELLOWS_OUTPUT_DIR}/Resources/Icons/ForgeScript.png"
	COMMENT "Copying engine resources")
set_target_properties(BellowsResources PROPERTIES FOLDER "Shaders")
add_dependencies(Bellows BellowsResources)
