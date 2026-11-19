# Third-party dependencies for MageCap Bellows.
#
# Every dependency is a pinned git submodule inside this folder, so a fresh
# clone builds offline after `git submodule update --init --recursive`.
# Third-party code is compiled with its own warning settings and never with
# our warnings-as-errors flags.

set(CMAKE_FOLDER "Vendor")

# --- Vulkan -------------------------------------------------------------------
# Headers come from the pinned submodule; the loader library (vulkan-1.dll, libvulkan.so.1)
# comes with the graphics driver and is opened at run time.
add_subdirectory(Vulkan-Headers EXCLUDE_FROM_ALL)
# Nothing links against the loader: see NvrhiVulkanDispatch.cpp.
add_library(BellowsVulkan INTERFACE)
target_link_libraries(BellowsVulkan INTERFACE Vulkan::Headers)
target_compile_definitions(BellowsVulkan INTERFACE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
if(WIN32)
	target_compile_definitions(BellowsVulkan INTERFACE VK_USE_PLATFORM_WIN32_KHR)
endif()

# --- GLFW ---------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw EXCLUDE_FROM_ALL)

# --- NVRHI --------------------------------------------------------------------
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_RTXMU OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(nvrhi EXCLUDE_FROM_ALL)
target_sources(nvrhi_vk PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/NvrhiVulkanDispatch.cpp")

# --- GLM ----------------------------------------------------------------------
add_library(BellowsGLM INTERFACE)
target_include_directories(BellowsGLM SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/glm")
target_compile_definitions(BellowsGLM INTERFACE
	GLM_FORCE_DEPTH_ZERO_TO_ONE
	GLM_FORCE_RADIANS
	GLM_ENABLE_EXPERIMENTAL
	GLM_FORCE_SILENT_WARNINGS)

# --- EnTT ---------------------------------------------------------------------
add_library(BellowsEnTT INTERFACE)
target_include_directories(BellowsEnTT SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/entt/src")

# --- spdlog -------------------------------------------------------------------
set(SPDLOG_BUILD_PIC ON CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
add_subdirectory(spdlog EXCLUDE_FROM_ALL)

# --- Jolt Physics -------------------------------------------------------------
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
add_subdirectory(JoltPhysics/Build EXCLUDE_FROM_ALL)

# --- pybind11 (embedded CPython) ------------------------------------------------
set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)
add_subdirectory(pybind11 EXCLUDE_FROM_ALL)

# --- Dear ImGui (docking) + ImGuizmo -----------------------------------------
add_library(BellowsImGui STATIC
	imgui/imgui.cpp
	imgui/imgui_draw.cpp
	imgui/imgui_tables.cpp
	imgui/imgui_widgets.cpp
	imgui/imgui_demo.cpp
	imgui/backends/imgui_impl_glfw.cpp
	imgui/misc/cpp/imgui_stdlib.cpp
	ImGuizmo/src/ImGuizmo.cpp)
target_include_directories(BellowsImGui SYSTEM PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends"
	"${CMAKE_CURRENT_SOURCE_DIR}/imgui/misc/cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/ImGuizmo/src")
target_compile_definitions(BellowsImGui PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(BellowsImGui PUBLIC glfw)
target_compile_definitions(BellowsImGui PRIVATE GLFW_INCLUDE_NONE)

# --- Code editor widget (ImGuiColorTextEdit, MIT) ------------------------------
add_library(BellowsTextEditor STATIC ImGuiColorTextEdit/TextEditor.cpp)
target_include_directories(BellowsTextEditor SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ImGuiColorTextEdit")
target_link_libraries(BellowsTextEditor PUBLIC BellowsImGui)
target_compile_features(BellowsTextEditor PUBLIC cxx_std_17)

# --- Multi-channel signed distance fields for text (msdfgen core, MIT) ---------
# Only the core: glyph outlines come from stb_truetype, so FreeType, Skia and PNG support stay out.
file(GLOB BELLOWS_MSDFGEN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/msdfgen/core/*.cpp")
add_library(BellowsMsdfgen STATIC ${BELLOWS_MSDFGEN_SOURCES})
target_include_directories(BellowsMsdfgen SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/msdfgen")
target_compile_features(BellowsMsdfgen PUBLIC cxx_std_11)

# --- Single-header libraries --------------------------------------------------
add_library(BellowsHeaderLibs INTERFACE)
target_include_directories(BellowsHeaderLibs SYSTEM INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}/stb"
	"${CMAKE_CURRENT_SOURCE_DIR}/cgltf"
	"${CMAKE_CURRENT_SOURCE_DIR}/json/single_include"
	"${CMAKE_CURRENT_SOURCE_DIR}/miniaudio"
	"${CMAKE_CURRENT_SOURCE_DIR}/tinyexr")

add_library(BellowsFileDialogs INTERFACE)
target_include_directories(BellowsFileDialogs SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/portable-file-dialogs")

add_library(BellowsDoctest INTERFACE)
target_include_directories(BellowsDoctest SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/doctest")

unset(CMAKE_FOLDER)
