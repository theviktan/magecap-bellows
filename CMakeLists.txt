cmake_minimum_required(VERSION 3.24)

project(MageCapBellows
	VERSION 0.3.0
	DESCRIPTION "MageCap Bellows - a simple, production-grade 3D game engine"
	LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Global settings
# -----------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (Debug, Release, Dist)" FORCE)
endif()

# Dist is a Release build with every editor/debug facility stripped out.
set(CMAKE_CXX_FLAGS_DIST "${CMAKE_CXX_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DIST "${CMAKE_C_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DIST "${CMAKE_EXE_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_DIST "${CMAKE_STATIC_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DIST "${CMAKE_SHARED_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
# Imported targets have no Dist configuration: use their Release files, or their only files.
set(CMAKE_MAP_IMPORTED_CONFIG_DIST Release "")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Every executable lands in one folder so the runtime finds shaders and resources next to it.
set(BELLOWS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BELLOWS_OUTPUT_DIR}")
foreach(config Debug Release Dist RelWithDebInfo MinSizeRel)
	string(TOUPPER ${config} configUpper)
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configUpper} "${BELLOWS_OUTPUT_DIR}")
endforeach()

option(BELLOWS_BUILD_EDITOR "Build the MageCap Bellows editor" ON)
option(BELLOWS_BUILD_TESTS "Build the MageCap Bellows unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BellowsCompileOptions)
include(BellowsShaders)
include(BellowsBuildInfo)
include(BellowsAppIcon)

# -----------------------------------------------------------------------------
# Third-party libraries
# -----------------------------------------------------------------------------
add_subdirectory(vendor)

# -----------------------------------------------------------------------------
# Engine, tools and tests
# -----------------------------------------------------------------------------
# Testing is switched on before any folder adds tests (ForgeScript adds its own).
if(BELLOWS_BUILD_TESTS)
	enable_testing()
endif()

add_subdirectory(ForgeScript)
add_subdirectory(Bellows)
add_subdirectory(BellowsRuntime)

if(BELLOWS_BUILD_EDITOR)
	add_subdirectory(BellowsEditor)
endif()

if(BELLOWS_BUILD_TESTS)
	add_subdirectory(BellowsTests)
endif()
